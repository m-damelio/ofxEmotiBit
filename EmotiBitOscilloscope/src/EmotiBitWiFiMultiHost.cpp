#include "EmotiBitWiFiMultiHost.h"
#include "ofLog.h"
#include <chrono>
#include <sstream>

//----------------------------------AdvertisingChannelManager-------------------------------------
AdvertisingChannelManager::AdvertisingChannelManager(const WiFiHostSettings& s) 
	: settings(s), advertisingPort(EmotiBitComms::WIFI_ADVERTISING_PORT), stopFlag(false), discoveryActive(false) { }

AdvertisingChannelManager::~AdvertisingChannelManager() {
	stop();
}

void AdvertisingChannelManager::begin() {
	advertisingConnection.Create();
	advertisingConnection.SetNonBlocking(true);
	advertisingConnection.Bind(advertisingPort);
	advertisingConnection.SetReceiveBufferSize(pow(2, 15));

	//Creates a dummy sock/udp connection to figure out own ip
	int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		ofLogError() << "Unable to create dummy socket to figure out localIp";
		return;
	}
	sockaddr_in remoteAddr;
	std::memset(&remoteAddr, 0, sizeof(remoteAddr));
	remoteAddr.sin_family = AF_INET;
	remoteAddr.sin_port = htons(53);
	inet_pton(AF_INET, "8.8.8.8", &remoteAddr.sin_addr);
	int result = ::connect(sock, (sockaddr*)&remoteAddr, sizeof(remoteAddr));
	if (result < 0) {
		ofLogError() << "UDP connect() failed; maybe no network up?";
#ifdef _WIN32
		closesocket(sock);
#else
		::close(sock);
#endif
		return;
	}
	sockaddr_in localAddr;
	socklen_t addrLen = sizeof(localAddr);
	std::memset(&localAddr, 0, sizeof(localAddr));
	if (::getsockname(sock, (sockaddr*)&localAddr, &addrLen) < 0) {
		ofLogError() << "getsockname() failed";
	}
	else {
		char ipStr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(localAddr.sin_addr), ipStr, INET_ADDRSTRLEN);
		ofLogNotice() << "Local IP detected via dummy UDP socket: " << ipStr;
		localIp = ipStr;
	}

#ifdef _WIN32
	closesocket(sock);
#else
	::close(sock);
#endif

	stopFlag = false;
	advertisingThread = std::thread(&AdvertisingChannelManager::advertisingLoop, this);
}

void AdvertisingChannelManager::stop() {
	stopFlag = true;
	if (advertisingThread.joinable()) advertisingThread.join();
	advertisingConnection.Close();
}

void AdvertisingChannelManager::startDiscovery() {
	discoveryActive = true;
	lastDiscoveryBroadcast = 0;
}

void AdvertisingChannelManager::stopDiscovery() {
	discoveryActive = false;
}

std::unordered_map<std::string, EmotiBitInfo> AdvertisingChannelManager::getDiscoveredDevices() {
	std::lock_guard<std::mutex> lock(discoveredMutex);
	return discoveredDevices;
}

DeviceIdentifier AdvertisingChannelManager::resolveDeviceIdentifier(const std::string& rawDeviceId, const std::string& ip) {
	DeviceIdentifier identifier;
	identifier.ip = ip;

	if (!rawDeviceId.empty() && rawDeviceId != ip) {
		//We have a device id
		identifier.deviceId = rawDeviceId;
		identifier.fallbackId = rawDeviceId;
		//update ip mapping
		std::lock_guard<std::mutex> lock(discoveredMutex);
		ipToDeviceIdMap[ip] = rawDeviceId;
	} 
	else {
		//No device id use ip as fallback
		identifier.fallbackId = "IP_" + ip;

		//Check if we have seen this ip before with a device id
		std::lock_guard<std::mutex> lock(discoveredMutex);
		auto it = ipToDeviceIdMap.find(ip);
		if (it != ipToDeviceIdMap.end()) {
			identifier.deviceId = it->second;
			identifier.fallbackId = it->second;
		}
	}
	return identifier;
}

void AdvertisingChannelManager::initiateHandshake(const std::string& deviceId, const std::string& ip, int controlPort, int dataPort, HandshakeResponseCallback callback) {
	std::vector<std::string> payload = {
		EmotiBitPacket::PayloadLabel::CONTROL_PORT, std::to_string(controlPort),
		EmotiBitPacket::PayloadLabel::DATA_PORT, std::to_string(dataPort)
	};
	std::string packet = EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::EMOTIBIT_CONNECT, 0, payload);

	//Add to pending handshakes
	{
		std::lock_guard<std::mutex> lock(handshakeMutex);
		pendingHandshakes[deviceId] = {deviceId, ip, dataPort, controlPort, callback, ofGetElapsedTimeMillis(), 0};
	}
	//Queue handshake message
	{
		std::lock_guard<std::mutex> lock(messageQueueMutex);
		outgoingMessages.emplace(AdvertisingMessageType::HANDSHAKE_CONNECT, deviceId, ip, packet, callback);
	}

	ofLogNotice("AdvertisingChannelManager") << "Initiate handshake for " << deviceId << " at " << ip;
}

void AdvertisingChannelManager::sendPing(const std::string& deviceId, const std::string& ip, int dataPort, PongCallback callback) {
	std::vector<std::string> payload = {
		EmotiBitPacket::PayloadLabel::DATA_PORT, std::to_string(dataPort)
	};
	std::string packet = EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::PING, 0, payload);

	//Store ping callback
	{
		std::lock_guard<std::mutex> lock(pingCallbacksMutex);
		pingCallbacks[deviceId] = callback;
	}

	//Queue ping message
	{
		std::lock_guard<std::mutex> lock(messageQueueMutex);
		outgoingMessages.emplace(AdvertisingMessageType::PING, deviceId, ip, packet);
	}
}

void AdvertisingChannelManager::advertisingLoop() {
	while (!stopFlag) {
		//Handle discovery broadcasts
		if (discoveryActive && ofGetElapsedTimeMillis() - lastDiscoveryBroadcast >= settings.sendAdvertisingInterval) {
			lastDiscoveryBroadcast = ofGetElapsedTimeMillis();
			std::string packet = EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::HELLO_EMOTIBIT, discoveryPacketCounter++, "", 0);
			std::lock_guard<std::mutex> lock(messageQueueMutex);
			outgoingMessages.emplace(AdvertisingMessageType::DISCOVERY_HELLO, "", "255.255.255.255", packet);
		}

		//Process incoming messages
		processIncomingMessages();

		//Send queued outgoing messages
		sendQueuedMessages();

		//Handle handshake timeouts and retries
		{
			std::lock_guard<std::mutex> lock(handshakeMutex);
			auto it = pendingHandshakes.begin();
			while (it != pendingHandshakes.end()) {
				auto& handshake = it->second;
				uint64_t currentTime = ofGetElapsedTimeMillis();

				if (currentTime - handshake.startTime > settings.connectionTimeout) {
					//Timeout
					if (handshake.callback) { handshake.callback(handshake.deviceId, false); }
					it = pendingHandshakes.erase(it);
				}
				else if (currentTime - handshake.lastSendTime >= settings.handshakeRetryInterval) {
					//Resend handshake
					std::vector<std::string> payload = {
						EmotiBitPacket::PayloadLabel::CONTROL_PORT, std::to_string(handshake.controlPort),
						EmotiBitPacket::PayloadLabel::DATA_PORT, std::to_string(handshake.dataPort) 
					};
					std::string packet = EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::EMOTIBIT_CONNECT, 0, payload);

					{
						std::lock_guard<std::mutex> msgLock(messageQueueMutex);
						outgoingMessages.emplace(AdvertisingMessageType::HANDSHAKE_CONNECT, handshake.deviceId, handshake.ip, packet);
					}
					handshake.lastSendTime = currentTime;
					ofLogVerbose("AdvertisingChannelManager") << "Retrying handshake for " << handshake.deviceId;
					++it;
				} 
				else {
					++it;
				}
			}
		}
		cleanupTimedOutDevices();

		std::this_thread::sleep_for(std::chrono::milliseconds(settings.advertisingThreadSleep));
	}
}

void AdvertisingChannelManager::processIncomingMessages() {
	const int maxSize = 32768;
	static char buffer[maxSize];
	int sz = advertisingConnection.Receive(buffer, maxSize);

	if (sz > 0) {
		std::string message(buffer, sz);
		std::string senderIp;
		int senderPort;
		advertisingConnection.GetRemoteAddr(senderIp, senderPort);

		if (senderIp == localIp) return;
		ofLogVerbose("AdvertisingChannelManager") << "Received " << sz << " bytes from " << senderIp;
		std::vector<std::string> packets = ofSplitString(message, ofToString(EmotiBitPacket::PACKET_DELIMITER_CSV));
		for (const std::string& packet : packets) {
			EmotiBitPacket::Header header;
			int16_t dataStartChar = EmotiBitPacket::getHeader(packet, header);

			if (dataStartChar > 0) {
				if (header.typeTag == EmotiBitPacket::TypeTag::HELLO_HOST) {
					handleDiscoveryResponse(packet, senderIp);
				}
				else if (header.typeTag == EmotiBitPacket::TypeTag::PONG) {
					handlePongResponse(packet, senderIp);
				}
				else
				{
					ofLogWarning("AdvertisingChannelManager") << "Unhandled emotibit response on advertising channel from ip " << senderIp << " with header: " << header.typeTag;
				}
			}
		}
	}
}

void AdvertisingChannelManager::sendQueuedMessages() {
	std::lock_guard<std::mutex> lock(messageQueueMutex);

	while (!outgoingMessages.empty()) {
		auto& message = outgoingMessages.front();

		if (message.type == AdvertisingMessageType::DISCOVERY_HELLO) {
			advertisingConnection.SetEnableBroadcast(true);
		}
		else {
			advertisingConnection.SetEnableBroadcast(false);
		}

		advertisingConnection.Connect(message.targetIp.c_str(), advertisingPort);
		int sent = advertisingConnection.Send(message.packet.c_str(), message.packet.size());

		ofLogVerbose("AdvertisingChannelManager") << "Sent " << sent << " bytes to " << message.targetIp;

		outgoingMessages.pop();
	}
}

void AdvertisingChannelManager::handleDiscoveryResponse(const std::string& packet, const std::string& senderIp) {
	if (!discoveryActive) return;

	//Parse discovery response 
	std::string dataPortStr, rawDeviceId;
	int16_t dataPortPos = EmotiBitPacket::getPacketKeyedValue(packet, EmotiBitPacket::PayloadLabel::DATA_PORT, dataPortStr, 0);
	int16_t deviceIdPos = EmotiBitPacket::getPacketKeyedValue(packet, EmotiBitPacket::PayloadLabel::DEVICE_ID, rawDeviceId, 0);

	if (dataPortPos > -1)
	{
		int dataPortValue = ofToInt(dataPortStr);
		bool isConnectable = (dataPortValue == EmotiBitComms::EMOTIBIT_AVAILABLE);

		DeviceIdentifier identifier = resolveDeviceIdentifier(rawDeviceId, senderIp);
		std::string primaryId = identifier.getPrimaryId();
		{
			std::lock_guard<std::mutex> lock(discoveredMutex);
			auto it = discoveredDevices.find(primaryId);
			if (it != discoveredDevices.end()) {
				//Update existing device
				it->second.lastSeen = ofGetElapsedTimeMillis();
				it->second.isAvailable = isConnectable;
				it->second.identifier.ip = senderIp;
			}
			else {
				EmotiBitInfo info(identifier, isConnectable);
				discoveredDevices[primaryId] = info;
				ofLogNotice("AdvertisingChannelManager") << "Discovered new device: " << info.getDisplayName() << " (available: " << isConnectable << ")";
			}
		}
	}
}

void AdvertisingChannelManager::handlePongResponse(const std::string& packet, const std::string& senderIp) {
	std::string pongDataPortStr;
	int16_t pongDataPortPos = EmotiBitPacket::getPacketKeyedValue(packet, EmotiBitPacket::PayloadLabel::DATA_PORT, pongDataPortStr, 0);

	if (pongDataPortPos > -1) {
		int pongDataPort = ofToInt(pongDataPortStr);

		//Check if this is a handshake response
		{
			std::lock_guard<std::mutex> lock(handshakeMutex);
			for (auto it = pendingHandshakes.begin(); it != pendingHandshakes.end(); ++it) {
				if (it->second.ip == senderIp && it->second.dataPort == pongDataPort) {
					ofLogNotice("AdvertisingChannelManager") << "Handshake completed for " << it->second.deviceId;
					if (it->second.callback) {
						it->second.callback(it->second.deviceId, true);
					}
					pendingHandshakes.erase(it);
					return;
				}
			}
		}

		//Check if this is a ping response
		{
			std::lock_guard<std::mutex> lock(pingCallbacksMutex);
			//Find device by ip 
			std::string deviceId;
			{
				std::lock_guard<std::mutex> devLock(discoveredMutex);
				auto ipIT = ipToDeviceIdMap.find(senderIp);
				if (ipIT != ipToDeviceIdMap.end()) {
					deviceId = ipIT->second;
				}
				else {
					deviceId = "IP_" + senderIp;
				}
			}
			auto callbackIt = pingCallbacks.find(deviceId);
			if (callbackIt != pingCallbacks.end() && callbackIt->second) {
				callbackIt->second(deviceId);
				updateDeviceLastSeen(deviceId);
			}
		}
	}
}

void AdvertisingChannelManager::cleanupTimedOutDevices() {
	std::lock_guard<std::mutex> lock(discoveredMutex);
	auto it = discoveredDevices.begin();
	while (it != discoveredDevices.end()) {
		if (ofGetElapsedTimeMillis() - it->second.lastSeen > settings.deviceTimeoutMs){
			ofLogNotice("AdvertisingChannelManager") << "Device " << it->second.getDisplayName() << " timed out";
			it = discoveredDevices.erase(it);
		}
		else{
			++it;
		}
	}
}

void AdvertisingChannelManager::updateDeviceLastSeen(const std::string& deviceId) {
	std::lock_guard<std::mutex> lock(discoveredMutex);
	auto it = discoveredDevices.find(deviceId);
	if (it != discoveredDevices.end()) {
		it->second.lastSeen = ofGetElapsedTimeMillis();
	}
}
void AdvertisingChannelManager::cleanupDevice(const std::string& deviceId) {
	std::lock_guard<std::mutex> lock(discoveredMutex);
	discoveredDevices.erase(deviceId);

	for (auto it = ipToDeviceIdMap.begin(); it != ipToDeviceIdMap.end();) {
		if (it->second == deviceId) {
			it = ipToDeviceIdMap.erase(it);
		} else {
			++it;
		}
	}

	std::lock_guard<std::mutex> handshakeLock(handshakeMutex);
	pendingHandshakes.erase(deviceId);

	std::lock_guard<std::mutex> pingLock(pingCallbacksMutex);
	pingCallbacks.erase(deviceId);
}
//-------------------------------------------------EmotiBitSession----------------------------------------------------
EmotiBitSession::EmotiBitSession(const std::string& id, 
	const std::string& ipAddr,
	const WiFiHostSettings& s,
	int ctrlPort,
	int dtPort,
	UpdateLastSeenCallback callback = nullptr, 
	AdvertisingChannelManager* advManager = nullptr)
	: deviceId(id), ip(ipAddr), settings(s), controlPort(ctrlPort), dataPort(dtPort), stopFlag(false), connected(false), updateLastSeenCallback(callback), advertisingManager(advManager) {

	controlServer.setup(controlPort);
	controlServer.setMessageDelimiter(ofToString(EmotiBitPacket::PACKET_DELIMITER_CSV));
	ofLogNotice("EmotiBitSession") << "Created session for " << deviceId << " at " << ip << " - Control port: " << controlPort << " - Data port:" << dataPort;
}

EmotiBitSession::~EmotiBitSession() {
	stop();
}

void EmotiBitSession::start() {
	if (!dataCxn.Create()) { throw std::runtime_error("Failed to create data connection"); }
	dataCxn.SetNonBlocking(true);
	if (!dataCxn.Bind(dataPort)) { throw std::runtime_error("Failed to bind to port " + std::to_string(dataPort)); }
	dataCxn.SetReceiveBufferSize(pow(2, 15));

	stopFlag = false;
	dataThread = std::thread(&EmotiBitSession::rundDataLoop, this);
	controlThread = std::thread(&EmotiBitSession::controlLoop, this);

}

void EmotiBitSession::stop() {
	stopFlag = true;
	if (dataThread.joinable()) dataThread.join();
	if (controlThread.joinable()) controlThread.join();
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

	//PING variables 
	uint64_t lastPingTime = ofGetElapsedTimeMillis();
	const uint64_t pingInterval = settings.pingInterval;
	uint32_t pingPacketCounter = 0;

	while (!stopFlag) {
		//Handle control connections
		for (int i = 0; i < controlServer.getLastID();i++) {
			if (controlServer.isClientConnected(i)) {
				if (!connected) {
					ofLogNotice("EmotiBitSession") << "Control client " << i << " connected for " << deviceId;
					std::lock_guard<std::mutex> lock(stateMutex);
					connected = true;
				}

				std::string received = controlServer.receive(i);
				if (!received.empty())
				{
					ofLogVerbose("EmotiBitSession") << "Received control message from " << deviceId << ": " << received;

					std::vector<std::string> packets = ofSplitString(received, ofToString(EmotiBitPacket::PACKET_DELIMITER_CSV));
					for (const std::string& packet : packets) {
						EmotiBitPacket::Header h;
						int16_t dataStartChar = EmotiBitPacket::getHeader(packet, h);
						//Handle different controls from emotibit (are there any over TCP?)
					}
					std::lock_guard<std::mutex> lock(dataMutex);
					controlQueue.push_back(received);
				}
			}
		}
		//Send periodic ping if connected via UDP control connection
		if (connected && ofGetElapsedTimeMillis() - lastPingTime >= pingInterval)
		{
			lastPingTime = ofGetElapsedTimeMillis();

			if (advertisingManager)
			{
				auto pongCallback = [this](const std::string& devId) {
					ofLogVerbose("EmotiBitSession") << "Received PONG response for " << devId << " via AdvertisingChannelManager";
					if (updateLastSeenCallback) {
						updateLastSeenCallback(devId);
					}
				};

				advertisingManager->sendPing(deviceId, ip, dataPort, pongCallback);
				ofLogVerbose("EmotiBitSession") << "Sent PING to " << deviceId << " via AdvertisingChannelManager";
			}
			else {
				ofLogWarning("EmotiBitSession") << "AdvertisingChannelManager not available for ping to " << deviceId;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	ofLogNotice("EmotiBitSession") << "Control loop ended for " << deviceId;
}

//-----------------------------------EmotiBitWiFiHost----------------------------------
EmotiBitWiFiMultiHost::EmotiBitWiFiMultiHost()
	: settings(), baseControlPort(EmotiBitComms::WIFI_ADVERTISING_PORT+1), baseDataPort(EmotiBitComms::WIFI_ADVERTISING_PORT+2) {
	
	advertisingManager = std::make_unique<AdvertisingChannelManager>(settings);
}

EmotiBitWiFiMultiHost::~EmotiBitWiFiMultiHost() {
	stop();
}

int8_t EmotiBitWiFiMultiHost::begin() {
	advertisingManager->begin();
	advertisingManager->startDiscovery();
	return SUCCESS;
}

void EmotiBitWiFiMultiHost::stop() {
	advertisingManager->stopDiscovery();
	std::lock_guard<std::mutex> lock(sessionsMutex);
	for (auto& kv : sessions) kv.second->stop();
	sessions.clear();
	advertisingManager->stop();
}

std::unordered_map<std::string, EmotiBitInfo> EmotiBitWiFiMultiHost::getDiscoveredDevices() {
	return advertisingManager->getDiscoveredDevices();
}

EmotiBitInfo EmotiBitWiFiMultiHost::getDeviceInfo(const std::string& deviceId) {
	auto devices = getDiscoveredDevices();
	auto it = devices.find(deviceId);
	if (it != devices.end()) {
		return it->second;
	}
	return EmotiBitInfo(); //default if not found
}

int8_t EmotiBitWiFiMultiHost::connect(const std::string& deviceId) {
	{
		std::lock_guard<std::mutex> lock(sessionsMutex);
		auto it = sessions.find(deviceId);
		if (it != sessions.end()) {
			ofLogWarning("EmotiBitWiFiMultiHost") << "Found stale session for " << deviceId << ", cleaniing up";
			it->second->stop();
			sessions.erase(it);
		}
	}
	auto map = getDiscoveredDevices();
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
	int ctrlPort = getNextAvailablePortPair();
	int dataPort = ctrlPort +1;

	ofLogNotice("EmotibitWiFiMultiHost") << "Connecting to " << deviceId << " at " << it->second.identifier.ip << " - Control: " << ctrlPort << ", Data: " << dataPort;
	//Add call backs to update device info
	auto callback = [this](const std::string& devId) {
		this->updateDeviceLastSeen(devId);
	};

	try {
		auto session = std::make_unique<EmotiBitSession>(deviceId, it->second.identifier.ip, settings, ctrlPort, dataPort, callback, advertisingManager.get());
		auto handshakeCallback = [this, deviceId](const std::string& devId, bool success) {
			if (success) {
				ofLogNotice("EmotiBitWiFiMultiHost") << "Handshake completed successfully for " << devId;
			}
			else {
				ofLogError("EmotiBitWiFiMultiHost") << "Handshake failed for " << devId;
				std::lock_guard<std::mutex> lock(sessionsMutex);
				sessions.erase(devId);
			}
		};

		advertisingManager->initiateHandshake(deviceId, it->second.identifier.ip, ctrlPort, dataPort, handshakeCallback);

		session->start();
		sessions[deviceId] = std::move(session);

		return SUCCESS;
	}
	catch (const std::exception& e) {
		ofLogError("EmotiBitWiFiMultiHost") << "Failed to create session for " << deviceId << ": " << e.what();
		releasePortPair(ctrlPort);
		return FAIL;
	}
	
}

int8_t EmotiBitWiFiMultiHost::disconnect(const std::string& deviceId) {
	std::unique_ptr<EmotiBitSession> session;
	{
		std::lock_guard<std::mutex> lock(sessionsMutex);
		auto it = sessions.find(deviceId);
		if (it == sessions.end()) return FAIL;
		session = std::move(it->second);
		sessions.erase(it);
	}
	
	advertisingManager->cleanupDevice(deviceId);

	if (session) { session->stop(); }
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

void EmotiBitWiFiMultiHost::updateDeviceLastSeen(const std::string& deviceId)
{
	advertisingManager->updateDeviceLastSeen(deviceId);
}



