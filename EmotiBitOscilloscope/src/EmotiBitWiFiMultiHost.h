#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include "ofxNetwork.h"
#include "EmotiBitPacket.h"
#include "EmotiBitComms.h"

enum class PowerMode {
	HIBERNATE,
	WIRELESS_OFF,					// fully shutdown wireless
	MAX_LOW_POWER,	// data not sent, time-syncing accuracy low
	LOW_POWER,			// data not sent, time-syncing accuracy high
	NORMAL_POWER,				// data sending, time-syncing accuracy high
	length
};

struct DeviceIdentifier {
	std::string deviceId;
	std::string ip;
	std::string fallbackId;

	DeviceIdentifier() = default;
	DeviceIdentifier(const std::string& id, const std::string& ipAddr)
		: deviceId(id), ip(ipAddr), fallbackId(id.empty() ? ("IP_"+ipAddr): id) { }

	std::string getPrimaryId() const {
		return deviceId.empty() ? fallbackId : deviceId;
	}

	bool isValid() const {
		return !ip.empty() && (!deviceId.empty() || !fallbackId.empty());
	}
};

struct EmotiBitInfo {
	DeviceIdentifier identifier;
	bool isAvailable;
	uint64_t lastSeen = 0;
	uint64_t firstSeen = 0;

	int dataPort = -1;
	int controlPort = -1;
	int batteryPercent = -1;
	PowerMode powerMode = PowerMode::NORMAL_POWER;
	int clippingCount = 0;
	int overflowCount = 0;
	std::string currentSdFilename;

	//Connection tracking 
	std::string firmwareVersion;
	std::string hardwareVersion;
	int connectionAttempts = 0;
	bool hasConnectedBefore = false;

	EmotiBitInfo() = default;
	EmotiBitInfo(const DeviceIdentifier& id, bool avail = false)
		: identifier(id), isAvailable(avail), lastSeen(ofGetElapsedTimeMillis()), firstSeen(ofGetElapsedTimeMillis()) {
	}

	std::string getDisplayName() const {
		if (!identifier.deviceId.empty()) {
			return identifier.deviceId + " (" + identifier.ip + ")";
		}
		return identifier.fallbackId;
	}
};

struct WiFiHostSettings {
	int sendAdvertisingInterval = 1000;
	int checkAdvertisingInterval = 100;
	int advertisingThreadSleep = 100;
	bool enableBroadcast = true;
	bool enableUnicast = true;
	std::pair<int, int> unicastIpRange = { 2,254 };
	int nUnicastIpsPerLoop = 1;
	int unicastminLoopDelay = 3;
	int dataThreadSleep = 0;
	std::vector<std::string> networkIncludeList = { "*.*.*.*" };
	std::vector<std::string> networkExcludeList = { "" };
	int connectionTimeout = 50000;
	int handshakeRetryInterval = 1000;
	int pingInterval = 5000;
	int deviceTimeoutMs = 60000;
};



//Callback types
using UpdateLastSeenCallback = std::function<void(const std::string& deviceId)>;
using DiscoveryCallback = std::function<void(const std::string& deviceId, const EmotiBitInfo& info)>;
using HandshakeResponseCallback = std::function<void(const std::string& deviceId, bool success)>;
using PongCallback = std::function<void(const std::string& deviceId)>;



class AdvertisingChannelManager {
public:
	AdvertisingChannelManager(const WiFiHostSettings& settings);
	~AdvertisingChannelManager();

	void begin();
	void stop();

	//Discovery functions
	void startDiscovery();
	void stopDiscovery();
	std::unordered_map<std::string, EmotiBitInfo> getDiscoveredDevices();

	//Handshake functions
	void initiateHandshake(const std::string& deviceId, const std::string& ip, int controlPort, int dataPort, HandshakeResponseCallback callback);

	//Ping functions
	void sendPing(const std::string& deviceId, const std::string& ip, int dataPort, PongCallback callback);

	//Device management
	void updateDeviceLastSeen(const std::string& deviceId);
	DeviceIdentifier resolveDeviceIdentifier(const std::string& rawDeviceId, const std::string& ip);
	void cleanupDevice(const std::string& deviceId);

private:
	enum class AdvertisingMessageType {
		DISCOVERY_HELLO,
		HANDSHAKE_CONNECT,
		PING,
	};

	struct AdvertisingMessage {
		AdvertisingMessageType type;
		std::string deviceId;
		std::string targetIp;
		std::string packet;
		HandshakeResponseCallback callback;

		AdvertisingMessage(AdvertisingMessageType t, const std::string& id, const std::string& ip, const std::string& pkt)
			: type(t), deviceId(id), targetIp(ip), packet(pkt) {
		}
		AdvertisingMessage(AdvertisingMessageType t, const std::string& id, const std::string& ip, const std::string& pkt, HandshakeResponseCallback cb)
			: type(t), deviceId(id), targetIp(ip), packet(pkt), callback(cb) {
		}
	};
	struct PendingHandshake {
		std::string deviceId;
		std::string ip;
		int dataPort;
		int controlPort;
		HandshakeResponseCallback callback;
		uint64_t startTime;
		uint64_t lastSendTime;
	};

	void advertisingLoop();
	void processIncomingMessages();
	void sendQueuedMessages();
	void handleDiscoveryResponse(const std::string& packet, const std::string& senderIp);
	void handleHandshakeResponse(const std::string& packet, const std::string& senderIp);
	void handlePongResponse(const std::string& packet, const std::string& senderIp);
	void cleanupTimedOutDevices();

	WiFiHostSettings settings;
	ofxUDPManager advertisingConnection;
	int advertisingPort;
	std::string localIp;

	std::thread advertisingThread;
	std::atomic<bool> stopFlag;
	std::atomic<bool> discoveryActive;

	//Message queue for outgoing messages
	std::mutex messageQueueMutex;
	std::queue<AdvertisingMessage> outgoingMessages;

	//Discovery state
	std::mutex discoveredMutex;
	std::unordered_map<std::string, EmotiBitInfo> discoveredDevices;
	std::unordered_map<std::string, std::string> ipToDeviceIdMap; //IP -> Primary Device Id mapping
	DiscoveryCallback discoveryCallback;
	uint16_t discoveryPacketCounter = 0;
	uint16_t lastDiscoveryBroadcast = 0;

	//pending handshakes
	std::mutex handshakeMutex;
	std::unordered_map<std::string, PendingHandshake> pendingHandshakes;

	//Ping callbacks
	std::mutex pingCallbacksMutex;
	std::unordered_map<std::string, PongCallback> pingCallbacks;

};

class EmotiBitSession {
public:
	EmotiBitSession(const std::string& deviceId,
		const std::string& ip,
		const WiFiHostSettings& settings,
		int controlPort,
		int dataPort,
		UpdateLastSeenCallback callback,
		AdvertisingChannelManager* advManager);
	~EmotiBitSession();

	void start();
	void stop();
	bool isConnected();
	void sendControl(const std::string& packet);
	void readData(std::vector<std::string>& outPackets);
	void readControl(std::vector<std::string>& out);

	int getDataPort() const { return dataPort; }
	int getControlPort() const { return controlPort; }
	std::string getDeviceId() const { return deviceId; }
	std::string getIp() const { return ip; }

private:
	void rundDataLoop();
	void controlLoop();

	std::string deviceId;
	std::string ip;
	WiFiHostSettings settings;

	//ports and sockets
	int dataPort;
	int controlPort;
	ofxUDPManager dataCxn;
	ofxTCPServer controlServer;

	std::thread dataThread;
	std::thread controlThread;

	std::atomic<bool> stopFlag;
	std::atomic<bool> connected;

	std::mutex dataMutex;
	std::mutex stateMutex;

	std::vector<std::string> dataQueue;
	std::vector<std::string> controlQueue;

	UpdateLastSeenCallback updateLastSeenCallback;

	AdvertisingChannelManager* advertisingManager;

};

class EmotiBitWiFiMultiHost {
public:
	EmotiBitWiFiMultiHost();
	~EmotiBitWiFiMultiHost();

	int8_t begin(); //start discovery
	void stop(); //stops all

	//Device discovery and management 
	std::unordered_map<std::string, EmotiBitInfo> getDiscoveredDevices();
	EmotiBitInfo getDeviceInfo(const std::string& devideId);

	//Connection management
	int8_t connect(const std::string& deviceId);
	int8_t disconnect(const std::string& deviceId);
	bool isConnected(const std::string& devideId);

	//Communication
	void sendControl(const std::string& deviceId, const std::string& packet);
	void readData(const std::string& devideId, std::vector<std::string>& packets);
	void readControl(const std::string& deviceId, std::vector<std::string>& packets);

	//Session info
	int getSessionDataPort(const std::string& deviceId);
	int getSessionControlPort(const std::string& deviceId);
	bool isSessionConnected(const std::string& devideId);

	//Settings
	int getSettingsTimeout();
	WiFiHostSettings& getSettings() { return settings; }

	//Device management
	void updateDeviceLastSeen(const std::string& deviceId);

	//Port management
	int EmotiBitWiFiMultiHost::getNextAvailablePortPair() {
		int ctrlPort = baseControlPort;
		while (usedPorts.count(ctrlPort) || usedPorts.count(ctrlPort + 1)) {
			ctrlPort += 2;
		}
		usedPorts.insert(ctrlPort);
		usedPorts.insert(ctrlPort + 1);
		return ctrlPort;
	}
	void releasePortPair(int ctrlPort) {
		std::thread([this, ctrlPort]() {
			std::this_thread::sleep_for(std::chrono::seconds(5));
			usedPorts.erase(ctrlPort);
			usedPorts.erase(ctrlPort + 1);
			}).detach();
	}

	static const uint8_t SUCCESS = 0;
	static const uint8_t FAIL = -1;

private:
	void onHandshakeComplete(const std::string& deviceId, bool success);
	void onPongReceived(const std::string& deviceId);

	WiFiHostSettings settings;
	std::unique_ptr<AdvertisingChannelManager> advertisingManager;

	int baseControlPort;
	int baseDataPort;

	std::mutex sessionsMutex;
	std::unordered_map<std::string, std::unique_ptr<EmotiBitSession>> sessions;
	std::unordered_map<std::string, bool> pendingConnections;

	//Ping management
	std::mutex pingMutex;
	std::unordered_map<std::string, bool> activePings;
	std::thread pingThread;
	std::atomic<bool> pingThreadActive;
	void pingLoop();

	std::set<int> usedPorts;
};
