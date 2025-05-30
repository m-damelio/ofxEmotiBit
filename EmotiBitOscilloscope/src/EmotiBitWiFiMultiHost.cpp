#include "EmotiBitWiFiMultiHost.h"
#include "ofLog.h"
#include <chrono>
#include <sstream>

//---------------------------------------DiscoveryService-----------------------------------------

DiscoveryService::DiscoveryService(const WiFiHostSettings& s)
	: settings(s), advertisingPort(EmotiBitComms::WIFI_ADVERTISING_PORT), stopFlag(false) { }

DiscoveryService::~DiscoveryService()
{
	stop();
}

void DiscoveryService::begin() {
	advertisingCxn.Create();
	advertisingCxn.SetNonBlocking(true);
	advertisingCxn.SetReceiveBufferSize(pow(2,10));
	stopFlag = false;
	advThread = std::thread(&DiscoveryService::runAdvertisingLoop, this);
}

void DiscoveryService::stop() {
	stopFlag = true;
	if (advThread.joinable()) advThread.join();
}

std::unordered_map<std::string, EmotiBitInfo> DiscoveryService::getDiscovered() {
	std::lock_guard<std::mutex> lock(discoveredMutex);
	return discovered;
}

void DiscoveryService::runAdvertisingLoop() {
	uint64_t lastSend = ofGetElapsedTimeMillis();
	while (!stopFlag) {
		//send HELLO broadcast if interval elapsed
		if (ofGetElapsedTimeMillis() - lastSend >= settings.sendAdvertisingInterval) {
			lastSend = ofGetElapsedTimeMillis();
			std::string packet = EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::HELLO_EMOTIBIT, advertisingPacketCounter++, "", 0);
			advertisingCxn.SetEnableBroadcast(true);
			advertisingCxn.Connect("255.255.255.255", advertisingPort);
			advertisingCxn.Send(packet.c_str(), packet.size());
		}
		processIncoming();
		std::this_thread::sleep_for(std::chrono::milliseconds(settings.advertisingThreadSleep));
	}
}

void DiscoveryService::processIncoming() {
	const int maxSize = 32768;
	static char buffer[maxSize];
	int sz = advertisingCxn.Receive(buffer, maxSize);
	if (sz > 0) {
		std::string msg(buffer, sz);
		ofLogNotice("DiscoveryService") << "Received raw discovery packet: " << msg;
		int port; std::string ip;
		advertisingCxn.GetRemoteAddr(ip, port);
		//Parse one packet

		std::vector<std::string> packets = ofSplitString(msg, ofToString(EmotiBitPacket::PACKET_DELIMITER_CSV));
		for (const std::string& packet : packets)
		{
			EmotiBitPacket::Header h;
			int16_t dataStartChar = EmotiBitPacket::getHeader(packet, h);
			if (dataStartChar > 0 && h.typeTag == EmotiBitPacket::TypeTag::HELLO_HOST) {
				//extract data port and device id
				std::string dataPortStr, dev;
				int16_t dataPortPos = EmotiBitPacket::getPacketKeyedValue(packet, EmotiBitPacket::PayloadLabel::DATA_PORT, dataPortStr, dataStartChar);
				int16_t deviceIdPos = EmotiBitPacket::getPacketKeyedValue(packet, EmotiBitPacket::PayloadLabel::DEVICE_ID, dev, dataStartChar);
				if (discovered.find(dev) != discovered.end())
				{
					ofLogNotice("DiscoveryService") << "Device " << dev << " exists in dictionary already. Stopped processing the data port and device id.";
					return;
				}
				if (dataPortPos > -1)
				{
					int dataPortValue = ofToInt(dataPortStr);
					bool isConnectable = (dataPortValue == EmotiBitComms::EMOTIBIT_AVAILABLE);
					if (deviceIdPos > -1)
					{
						ofLogVerbose("DiscoveryService") << "EmotiBit DeviceId: " << dev;
					}
					else {
						dev = ip;
						ofLogVerbose("DiscoveryService") << "EmotiBit DeviceId: DeviceId not available. Using IP address as identifier";
					}
					ofLogNotice("DiscoveryService") << "Discovered device: " << dev << " at " << ip << " with data port value: " << dataPortValue << " (connectable: " << isConnectable << ")";
					std::lock_guard<std::mutex> lock(discoveredMutex);
					discovered[dev] = EmotiBitInfo(ip, isConnectable);
					
				}
			}
		}
	}
}
//-------------------------------------------------EmotiBitSession----------------------------------------------------
EmotiBitSession::EmotiBitSession(const std::string& id, 
	const std::string& ipAddr,
	const WiFiHostSettings& s,
	int advPort,
	int ctrlPort,
	int dtPort)
	: deviceId(id), ip(ipAddr), settings(s), controlPort(ctrlPort), dataPort(dtPort), stopFlag(false), connected(false), isStarting(false) {
	controlServer.setup(controlPort);
	controlServer.setMessageDelimiter(ofToString(EmotiBitPacket::PACKET_DELIMITER_CSV));
	ofLogNotice("EmotiBitSession") << "Created session for " << deviceId << " at " << ip << " - Control port: " << controlPort << " - Data port:" << dataPort;
}

EmotiBitSession::~EmotiBitSession() {
	stop();
}

void EmotiBitSession::start() {
	dataCxn.Create();
	dataCxn.SetNonBlocking(true);
	dataCxn.Bind(dataPort);
	dataCxn.SetReceiveBufferSize(pow(2, 15));

	stopFlag = false;
	dataThread = std::thread(&EmotiBitSession::rundDataLoop, this);
	controlThread = std::thread(&EmotiBitSession::controlLoop, this);
	handshakeThread = std::thread(&EmotiBitSession::handshakeLoop, this);
}

void EmotiBitSession::stop() {
	stopFlag = true;
	if (dataThread.joinable()) dataThread.join();
	if (controlThread.joinable()) controlThread.join();
	if (handshakeThread.joinable()) handshakeThread.join();
	dataCxn.Close();
	controlServer.close();
}

bool EmotiBitSession::isConnected() {
	std::lock_guard<std::mutex> lock(stateMutex);
	return connected;
}

void EmotiBitSession::sendControl(const std::string& packet) {
	for (unsigned int i = 0; i < (unsigned int)controlServer.getLastID(); i++)
	{
		if (!controlServer.isClientConnected(i)) continue;
		
		controlServer.send(i, packet);
		ofLogVerbose("EmotiBitSession") << "Sent control packet to client " << i << ": " <<packet;
	}
}

void EmotiBitSession::readData(std::vector<std::string>& out) {
	std::lock_guard<std::mutex> lock(dataMutex);
	out = dataQueue;
	dataQueue.clear();
}
void EmotiBitSession::readControl(std::vector<std::string>& out) {
	std::lock_guard<std::mutex> lock(dataMutex);
	out = controlQueue;
	controlQueue.clear();
}

void EmotiBitSession::rundDataLoop() {
	const int maxSize = 32768*2;
	char buf[maxSize];
	ofLogNotice("EmotiBitSession") << "Starting data loop for " << deviceId << " on port " << dataPort;
	while (!stopFlag) {
		int sz = dataCxn.Receive(buf, maxSize);
		if (sz > 0) {
			std::string pkt(buf, sz);
			std::lock_guard<std::mutex> lock(dataMutex);
			dataQueue.push_back(pkt);

			static int packetCount = 0;
			if (packetCount < 5)
			{
				ofLogNotice("EmotiBitSession") << "Received data packet " << packetCount << " for " << deviceId << ": " << pkt.substr(0, 100);
				packetCount++;
			}
		}
		std::this_thread::sleep_for(std::chrono::microseconds(settings.dataThreadSleep));
	}
	ofLogNotice("EmotiBitSession") << "Data loop ended for " << deviceId;
}

void EmotiBitSession::controlLoop() {
	ofLogNotice("EmotiBitSession") << "Started control loop for " << deviceId << " on port " << controlPort;
	while (!stopFlag) {
		for (int i = 0; i < controlServer.getLastID();i++) {
			if (controlServer.isClientConnected(i)) {
				if (!connected) {
					ofLogNotice("EmotiBitSession") << "Control client " << i << " connected for " << deviceId;
					std::lock_guard<std::mutex> lock(stateMutex);
					connected = true;
					isStarting = false;
				}

				std::string received = controlServer.receive(i);
				if (!received.empty())
				{
					ofLogVerbose("EmotiBitSession") << "Received control messag from " << deviceId << ": " << received;
					std::lock_guard<std::mutex> lock(dataMutex);
					controlQueue.push_back(received);
				}
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	ofLogNotice("EmotiBitSession") << "Control loop ended for " << deviceId;
}
void EmotiBitSession::handshakeLoop() {
	//Create seperate UDP connection for handshake communication
	ofxUDPManager handshakeCxn;
	handshakeCxn.Create();
	handshakeCxn.SetNonBlocking(true);
	handshakeCxn.SetReceiveBufferSize(pow(2,15));


	//Send EMOTIBIT_CONNECT packet
	std::vector<std::string> payload = {
		EmotiBitPacket::PayloadLabel::CONTROL_PORT, std::to_string(controlPort),
		EmotiBitPacket::PayloadLabel::DATA_PORT, std::to_string(dataPort)
	};
	std::string packet = EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::EMOTIBIT_CONNECT, 0, payload);
	std::string payloadStr;
	for (std::string load : payload)
	{
		payloadStr += load + " ";
	}
	ofLogNotice("EmotiBitSession") << "Sending EMOTIBIT_CONNECT to " << deviceId << " at " << ip << " with payload: " << payloadStr;

	

	auto start = std::chrono::steady_clock::now();
	isStarting = true;
	uint64_t lastSendTime = 0;
	const uint64_t resendInterval = 1000; //1 second
	const int maxSize = 32768*2;

	while (!stopFlag && isStarting) {
		uint64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		//Resend EMOTIBIT_CONNECT periodically
		if (currentTime - lastSendTime >= resendInterval)
		{
			handshakeCxn.Connect(ip.c_str(), EmotiBitComms::WIFI_ADVERTISING_PORT);
			handshakeCxn.SetEnableBroadcast(false);
			int sent = handshakeCxn.Send(packet.c_str(), packet.size());
			lastSendTime = currentTime;
			ofLogVerbose("EmotiBitSession") << "Resending EMOTIBIT_CONNECT to " << deviceId << " (sent " << sent << " bytes)";
		}

		//listen for PONG response on the handshake connection (advertising port)
		char buf[maxSize]; 
		int sz = handshakeCxn.Receive(buf, maxSize);
		if (sz > 0) {
			std::string response(buf, sz);
			ofLogNotice("EmotiBitSession") << "Received handshake response (" << sz << " bytes): " << response;

			//Parse response
			std::vector<std::string> packets = ofSplitString(response, ofToString(EmotiBitPacket::PACKET_DELIMITER_CSV));
			for (const std::string& responsePacket : packets)
			{
				EmotiBitPacket::Header h;
				int16_t dataStartChar = EmotiBitPacket::getHeader(responsePacket, h);

				if (dataStartChar > 0 && h.typeTag == EmotiBitPacket::TypeTag::PONG)
				{
					//Verify Pong is for our data port
					std::string pongDataPortStr;
					int16_t pongDataPortPos = EmotiBitPacket::getPacketKeyedValue(responsePacket, EmotiBitPacket::PayloadLabel::DATA_PORT, pongDataPortStr, dataStartChar);


					if (pongDataPortPos > -1)
					{
						int pongDataPort = ofToInt(pongDataPortStr);
						ofLogNotice("EmotiBitSession") << "PONG received - data port: " << pongDataPort << ", expected: " << dataPort;
						if (pongDataPort == dataPort)
						{
							std::lock_guard<std::mutex> lock(stateMutex);
							ofLogNotice("EmotiBitSession") << "Device " << deviceId << " connected successfully.";
							isStarting = false;
							handshakeCxn.Close();
							return;
						}
						
					}
				}
			}
		}
		if (currentTime > settings.connectionTimeout) {
			ofLogWarning("EmotiBitSession") << "Handshake connection to " << deviceId << " timed out after " << currentTime<<"ms";
			isStarting = false;
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	handshakeCxn.Close();
}

//-----------------------------------EmotiBitWiFiHost----------------------------------
EmotiBitWiFiMultiHost::EmotiBitWiFiMultiHost()
	: settings(), discovery(settings), advertisingPort(EmotiBitComms::WIFI_ADVERTISING_PORT), baseControlPort(EmotiBitComms::WIFI_ADVERTISING_PORT+1), baseDataPort(EmotiBitComms::WIFI_ADVERTISING_PORT+2) { }

EmotiBitWiFiMultiHost::~EmotiBitWiFiMultiHost() {
	stop();
}

int8_t EmotiBitWiFiMultiHost::begin() {
	discovery.begin();
	return SUCCESS;
}

void EmotiBitWiFiMultiHost::stop() {
	discovery.stop();
	std::lock_guard<std::mutex> lock(sessionsMutex);
	for (auto& kv : sessions) kv.second->stop();
	sessions.clear();
}

std::vector<std::string> EmotiBitWiFiMultiHost::listDevices() {
	auto map = discovery.getDiscovered();
	std::vector<std::string> ids;
	for (auto& kv : map) ids.push_back(kv.first);
	return ids;
}

std::unordered_map<std::string, EmotiBitInfo> EmotiBitWiFiMultiHost::getDiscoveredDevices() {
	return discovery.getDiscovered();
}

int8_t EmotiBitWiFiMultiHost::connect(const std::string& deviceId) {
	auto map = discovery.getDiscovered();
	auto it = map.find(deviceId);
	if (it == map.end()) {
		ofLogError("EmotiBitWiFiMultiHost") << "Device " << deviceId << " not found in discovery list";
		return FAIL;
	}

	if (!it->second.isAvailable)
	{
		ofLogWarning("EmotiBitWiFiMultihost") << "Device " << deviceId << " is not available for connection";
		return FAIL;
	}
	std::lock_guard<std::mutex> lock(sessionsMutex);
	if (sessions.count(deviceId)){
		ofLogNotice("EmotiBitWiFiMultiHost") << "Device " << deviceId << " already connected";
		return SUCCESS;
	}
	int sessionIndex = sessions.size();
	int ctrlPort = baseControlPort + sessionIndex * 2;
	int dataPort = baseDataPort + sessionIndex * 2;

	ofLogNotice("EmotibitWiFiMultiHost") << "Connecting to " << deviceId << " at " << it->second.ip << " - Control: " << ctrlPort << ", Data: " << dataPort;

	try {
		auto session = std::make_unique<EmotiBitSession>(deviceId, it->second.ip, settings, advertisingPort, ctrlPort, dataPort);
		session->start();
		sessions[deviceId] = std::move(session);
		return SUCCESS;
	}
	catch (const std::exception& e) {
		ofLogError("EmotiBitWiFiMultiHost") << "Failed to create session for " << deviceId << ": " << e.what();
		return FAIL;
	}
	
}

int8_t EmotiBitWiFiMultiHost::disconnect(const std::string& deviceId) {
	std::lock_guard<std::mutex> lock(sessionsMutex);
	auto it = sessions.find(deviceId);
	if (it == sessions.end()) return FAIL;
	it->second->stop();
	sessions.erase(it);
	return SUCCESS;
}

bool EmotiBitWiFiMultiHost::isConnected(const std::string& deviceId) {
	std::lock_guard<std::mutex> lock(sessionsMutex);
	auto it = sessions.find(deviceId);
	return it != sessions.end() && it->second->isConnected();
}

void EmotiBitWiFiMultiHost::sendControl(const std::string& deviceId, const std::string& packet) {
	std::lock_guard<std::mutex> lock(sessionsMutex);
	auto it = sessions.find(deviceId);
	if (it != sessions.end()) it->second->sendControl(packet);
}

void EmotiBitWiFiMultiHost::readData(const std::string& deviceId, std::vector<std::string>& packets) {
	std::lock_guard<std::mutex> lock(sessionsMutex);
	auto it = sessions.find(deviceId);
	if (it != sessions.end()) it->second->readData(packets);
}

int EmotiBitWiFiMultiHost::getSessionDataPort(const std::string& deviceId) {
	std::lock_guard<std::mutex> lock(sessionsMutex);
	auto it = sessions.find(deviceId);
	if (it == sessions.end()) return -1;
	return it->second->getDataPort();
}
int EmotiBitWiFiMultiHost::getSessionControlPort(const std::string& deviceId) {
	std::lock_guard<std::mutex> lock(sessionsMutex);
	auto it = sessions.find(deviceId);
	if (it == sessions.end()) return -1;
	return it->second->getControlPort();
}
bool EmotiBitWiFiMultiHost::isSessionConnected(const std::string& deviceId) {
	std::lock_guard<std::mutex> lock(sessionsMutex);
	auto it = sessions.find(deviceId);
	return it!=sessions.end() && it->second->isConnected();
}
int EmotiBitWiFiMultiHost::getSettingsTimeout() {
	return settings.connectionTimeout;
}
void EmotiBitWiFiMultiHost::readControl(const std::string& deviceId, std::vector<std::string>& packets)
{
	std::lock_guard<std::mutex> lock(sessionsMutex);
	auto it = sessions.find(deviceId);
	if (it != sessions.end())
	{
		it->second->readControl(packets);
	}
}
