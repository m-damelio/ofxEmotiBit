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

using UpdateLastSeenCallback = std::function<void(const std::string& deviceId)>;

enum class PowerMode {
	HIBERNATE,
	WIRELESS_OFF,					// fully shutdown wireless
	MAX_LOW_POWER,	// data not sent, time-syncing accuracy low
	LOW_POWER,			// data not sent, time-syncing accuracy high
	NORMAL_POWER,				// data sending, time-syncing accuracy high
	length
};

struct EmotiBitInfo {
	std::string ip;
	bool isAvailable;
	uint64_t lastSeen = 0;

	int dataPort = -1;
	int controlPort = -1;
	int batteryPercent = -1;
	PowerMode powerMode = PowerMode::NORMAL_POWER;
	int clippingCount = 0;
	int overflowCount = 0;
	std::string currentSdFilename;

	EmotiBitInfo() = default;
	EmotiBitInfo(const std::string &ip_, bool avail = false)
		: ip(ip_), isAvailable(avail), lastSeen(ofGetElapsedTimeMillis()) {}
};

struct WiFiHostSettings {
	int sendAdvertisingInterval = 1000;
	int checkAdvertisingInterval = 100;
	int advertisingThreadSleep = 0;
	bool enableBroadcast = true;
	bool enableUnicast = true;
	std::pair<int, int> unicastIpRange = { 2,254 };
	int nUnicastIpsPerLoop = 1;
	int unicastminLoopDelay = 3;
	int dataThreadSleep = 0;
	std::vector<std::string> networkIncludeList ={ "*.*.*.*" };
	std::vector<std::string> networkExcludeList = { "" };
	int connectionTimeout = 50000;
};

class DiscoveryService {
public:
	DiscoveryService(const WiFiHostSettings& settings);
	~DiscoveryService();
	void begin();
	void stop();
	std::unordered_map<std::string, EmotiBitInfo> getDiscovered();

	void updateDeviceLastSeen(const std::string& deviceId);

private:
	void runAdvertisingLoop();
	void processIncoming();

	WiFiHostSettings settings;
	ofxUDPManager advertisingCxn;
	int advertisingPort;
	std::thread advThread;
	bool stopFlag = false;
	uint16_t advertisingPacketCounter = 0;

	std::mutex discoveredMutex;
	std::unordered_map<std::string, EmotiBitInfo> discovered;
};

class EmotiBitSession {
public:
	EmotiBitSession(const std::string& deviceId,
		const std::string& ip,
		const WiFiHostSettings& settings,
		int advertisingPort,
		int controlPort,
		int dataPort,
		UpdateLastSeenCallback callback);
	~EmotiBitSession();

	void start();
	void stop();
	bool isConnected();
	void sendControl(const std::string& packet);
	void readData(std::vector<std::string>& outPackets);
	void readControl(std::vector<std::string>& out);

	int getDataPort() const { return dataPort; }
	int getControlPort() const { return controlPort; }

private:
	void rundDataLoop();
	void handshakeLoop();
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
	std::thread handshakeThread;
	std::thread controlThread;

	std::atomic<bool> stopFlag;
	std::atomic<bool> connected;
	std::atomic<bool> isStarting;

	std::mutex dataMutex;
	std::mutex stateMutex;

	uint16_t recvPacketNumber = 0;
	std::vector<std::string> dataQueue;
	std::vector<std::string> controlQueue;

	UpdateLastSeenCallback updateLastSeenCallback;
};

class EmotiBitWiFiMultiHost {
public:
	EmotiBitWiFiMultiHost();
	~EmotiBitWiFiMultiHost();

	int8_t begin(); //start discovery
	void stop(); //stops all

	std::vector<std::string> listDevices();
	int8_t connect(const std::string& deviceId);
	int8_t disconnect(const std::string& deviceId);
	bool isConnected(const std::string& devideId);
	void sendControl(const std::string& deviceId, const std::string& packet);
	void readData(const std::string& devideId, std::vector<std::string>& packets);
	void readControl(const std::string& deviceId, std::vector<std::string>& packets);
	std::unordered_map<std::string, EmotiBitInfo> getDiscoveredDevices();
	int getSessionDataPort(const std::string& deviceId);
	int getSessionControlPort(const std::string& deviceId);
	bool isSessionConnected(const std::string& devideId);
	int getSettingsTimeout();
	void updateDeviceLastSeen(const std::string& deviceId);

	static const uint8_t SUCCESS = 0;
	static const uint8_t FAIL = -1;

private:
	WiFiHostSettings settings;
	DiscoveryService discovery;
	int advertisingPort;
	int baseControlPort;
	int baseDataPort;

	std::mutex sessionsMutex;
	std::unordered_map<std::string, std::unique_ptr<EmotiBitSession>> sessions;

	
};
