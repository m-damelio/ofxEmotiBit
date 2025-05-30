#include "ofApp22.h"
#include "ofxBiquadFilter.h"
#include <algorithm>
#include "EmotiBitOfUtils.h"

#pragma region EmotiBitStuff



/*
void ofApp2::setup() {
	ofLogToConsole();
#ifdef TARGET_MAC_OS
    ofSetDataPathRoot("../Resources/");
    cout<<"Changed the data pathroot for macOS."<<endl;
#endif
	ofSetFrameRate(30);
	ofBackground(255, 255, 255);
	SoftwareVersionChecker::checkLatestVersion();
	ofSetLogLevel(OF_LOG_NOTICE);
	setTypeTagPlotAttributes();

	string commSettings = loadTextFile(commSettingsFile);
	emotiBitWiFi.parseCommSettings(commSettings);

	emotiBitWiFi.begin();	// Startup WiFi connectivity
	timeWindowOnSetup = 10;  // set timeWindow for setup (in seconds)
	setupGui();
	setupOscilloscopes();
	
	logData = false;
	logConsole = false;
	dataLogger.setFilename("dataLog.txt");
	if (logData)
	{
		dataLogger.startThread();
	}
	consoleLogger.setFilename("consoleLog.txt");
	if (logConsole)
	{
		consoleLogger.startThread();
	}

	// LSL marker setup
	if (!commSettings.empty())
	{
		emotibitLsl.addMarkerInput(commSettings);
	}

	// set log level to FATAL_ERROR to remove unrelated LSL error overflow in the console
	ofSetLogLevel(OF_LOG_FATAL_ERROR);
	//ofSetLogLevel(OF_LOG_VERBOSE);
}

//--------------------------------------------------------------
void ofApp2::update() {
	static uint64_t updateTimer = ofGetElapsedTimeMillis();
	ofLog(OF_LOG_VERBOSE) << "update(): " << ofGetElapsedTimeMillis() - updateTimer;
	updateTimer = ofGetElapsedTimeMillis();

	if (emotibitLsl.getNumMarkerInputs() > 0)
	{
		vector<string> packets = emotibitLsl.createMarkerInputPackets(emotiBitWiFi.controlPacketCounter);
		for (string packet : packets)
		{
			// cout << packet; // for debugging
			emotiBitWiFi.sendControl(packet);
		}
	}
	vector<string> dataPackets;
	emotiBitWiFi.readData(dataPackets);
	for (string packet : dataPackets)
	{
		processSlowResponseMessage(packet);
		if (logData)
		{
			dataLogger.push(packet + '\n');
		}
	}

	updateMenuButtons();
}
*/


// ToDo: This function  should be removed once we complete our move to xmlFileSettings
void ofApp2::setTypeTagPlotAttributes()
{
	// ToDo: Add attributes for all streams for refactor
	// Note:: THERM plot attributes only live in code. we should move it to the XML file some day
	{
		// Add plot attributes for THERMOPILE data
		typeTagPlotAttr attr;
		attr.plotName = "THERM";
		attr.plotColor = ofColor(239, 97, 82);
		// ToDo: someday, we can even consider a standard layout considering even plot number p -> {w,s,p}
		std::vector<int> sIdx = { 1, 3 };  // {window, scope}
		attr.scopeIdx = sIdx;
		typeTagPlotAttributes.emplace(EmotiBitPacket::TypeTag::THERMOPILE, attr);
	}

	{
		// Plot Attributes for TEMP1 data
		typeTagPlotAttr attr;
		attr.plotName = "TEMP1";
		attr.plotColor = ofColor(234, 174, 68);
		std::vector<int> sIdx = { 1, 3 };  // {window, scope}
		attr.scopeIdx = sIdx;
		typeTagPlotAttributes.emplace(EmotiBitPacket::TypeTag::TEMPERATURE_1, attr);
	}
}

void ofApp2::resetScopePlot(int w, int s)
{
	// store the last set timeWindow before resetting
	float lastTimeWindow = scopeWins.at(w).scopes.at(s).getTimeWindow();
	scopeWins.at(w).scopes.at(s).clearData();
	scopeWins.at(w).scopes.at(s).setup(timeWindowOnSetup, samplingFreqs.at(w).at(s), plotNames.at(w).at(s), plotColors.at(w).at(s),
		0, 1);
	// reset timeWindow to last set value
	scopeWins.at(w).scopes.at(s).setTimeWindow(lastTimeWindow);
}

void ofApp2::initMetaDataBuffers()
{
	bufferSizes = initBuffer(bufferSizes);
	dataCounts = initBuffer(dataCounts);
	dataFreqs = initBuffer(dataFreqs);
}

void ofApp2::resetIndexMapping()
{
	for (int w = 0; w < typeTags.size(); w++) {
		for (int s = 0; s < typeTags.at(w).size(); s++) {
			for (int p = 0; p < typeTags.at(w).at(s).size(); p++) {
				vector<int> indexes{ w, s, p };
				typeTagIndexes.emplace(typeTags.at(w).at(s).at(p), indexes);
			}
		}
	}
}

void ofApp2::addDataStream(std::string typetag)
{
	if (typeTagIndexes.find(typetag) == typeTagIndexes.end())
	{
		// ToDo: Find a more elegant way to solve default plot state.
		// If adding THERM, remove default TEMP1. 
		if (typetag == EmotiBitPacket::TypeTag::THERMOPILE)
		{
			// TEMP 1 was added at setup wihout confirmation that data is present. Therefore it is removed when
			// another data stream on the same plot is detected(to resolve autoscaling issues)
			// It will be added by separate add() call if Temp1 stream is found
			removeDataStream(EmotiBitPacket::TypeTag::TEMPERATURE_1);
		}
		auto scopeIdx = typeTagPlotAttributes[typetag].scopeIdx;
		int w = scopeIdx.at(0);
		int s = scopeIdx.at(1);
		// add plot attributes to class vairables
		plotNames.at(w).at(s).emplace_back(typeTagPlotAttributes[typetag].plotName);
		plotColors.at(w).at(s).emplace_back(typeTagPlotAttributes[typetag].plotColor);
		typeTags.at(w).at(s).emplace_back(typetag);
		// new plotIdx
		int p = plotNames.at(w).at(s).size() - 1; //  Size - 1 to make sure there is no out of bounds access.
		std::vector<int> plotIdx = { w, s, p };
		// update typetag Indexing
		typeTagIndexes.emplace(typetag, plotIdx);
		// re-init metadata buffers
		initMetaDataBuffers();
		// reset the scope
		resetScopePlot(w, s);
	}
}

void ofApp2::removeDataStream(std::string typetag)
{
	if (typeTagIndexes.find(typetag) != typeTagIndexes.end())
	{
		auto plotIdx = typeTagIndexes[typetag];
		// find the window, scope, plot for the stream
		int w = plotIdx.at(0);
		int s = plotIdx.at(1);
		int p = plotIdx.at(2);
		// erase the attributes from class variables
		plotNames.at(w).at(s).erase(plotNames.at(w).at(s).begin() + p);
		plotColors.at(w).at(s).erase(plotColors.at(w).at(s).begin() + p);
		typeTags.at(w).at(s).erase(typeTags.at(w).at(s).begin() + p);
		
		// recreate index mapping
		typeTagIndexes.clear();
		resetIndexMapping();

		// re-init metadata buffers
		initMetaDataBuffers();
		// reset the scope
		resetScopePlot(w, s);

		// ToDo: Find a more elegant way to handle "empty" plot buffer
		// if removing THERM, make sure TEMP1 is still in the plot buffer
		if (typetag == EmotiBitPacket::TypeTag::THERMOPILE)
		{
			addDataStream(EmotiBitPacket::TypeTag::TEMPERATURE_1);
		}
	}
}
#pragma endregion
//--------------------------------------------------------------
/*
void ofApp2::draw() {
	drawOscilloscopes();
	drawConsole();
}
*/

//--------------------------------------------------------------
void ofApp2::exit() {
	
	printf("exit()");
}

//--------------------------------------------------------------
void ofApp2::keyPressed(int key) {

	//string note = userNote.getParameter().toString();
	//if (note.compare(GUI_STRING_EMPTY_USER_NOTE) != 0) {
	//	// Don't process keystrokes if we're typing a note
	//	// ToDo: come up with a better check
	//	return;
	//}
	ofMouseEventArgs temp;
	/*
	if (!userNote.mouseReleased(temp))
	{
		// Increment the timeWindow
		if (key == OF_KEY_RIGHT) { // Right Arrow
			for (int w = 0; w < scopeWins.size(); w++) {
				scopeWins.at(w).incrementTimeWindow();
			}
		}

		// Decrement the timeWindow
		if (key == OF_KEY_LEFT) { // Left Arrow
			for (int w = 0; w < scopeWins.size(); w++) {
				scopeWins.at(w).decrementTimeWindow();
			}
		}
		if (DEBUGGING)
		{
			if (key == OF_KEY_UP) {
				drawYTranslate--;
				drawYScale = (drawYScale * 900.f + 1.f) / 900.f;
			}
			if (key == OF_KEY_DOWN) {
				drawYTranslate++;
				drawYScale = (drawYScale * 900.f - 1.f) / 900.f;
			}
		}
		if (ofGetElapsedTimef() < 5) {
			// Enter special modes if keys pressed in first few seconds
			if (key == 'T')
			{
				if (!_testingHelper.testingOn)
				{
					cout << "Entering Testing Mode" << endl;
					_testingHelper.setLogFilename("testingResults.txt");
					_testingHelper.testingOn = true;

					// Remove minYspans for testing
					for (int w = 0; w < plotNames.size(); w++) {
						for (int s = 0; s < plotNames.at(w).size(); s++) {
							if (yLims.at(w).at(s).at(0) == yLims.at(w).at(s).at(1)) {
								scopeWins.at(w).scopes.at(s).autoscaleY(true);
							}
						}
					}
				}
			}
		}
	}
	*/
}

//--------------------------------------------------------------
void ofApp2::keyReleased(int key) {
	//cout << "Key Released: " << (char)key << endl;

	//string note = userNote.getParameter().toString();
	//if (note.compare(GUI_STRING_EMPTY_USER_NOTE) != 0 ) {
	//	// Don't process keystrokes if we're typing a note
	//	// ToDo: come up with a better check
	//	return;
	//}
	ofMouseEventArgs temp;
	/*
	if (!userNote.mouseReleased(temp))
	{
		if (key == ' ') {
			//userNote.
			isPaused = !isPaused;
		}
		if (key == 'i') {
			drawDataInfo = !drawDataInfo;
		}
		if (key == OF_KEY_BACKSPACE || key == OF_KEY_DEL) {
			clearOscilloscopes(false);
		}
		if (key == ':')
		{
			logData = !logData;
			logConsole = !logConsole;
			cout << "Data logging: " << logData << endl;
			if (logData)
			{
				dataLogger.startThread();
			}
			else
			{
				dataLogger.stopThread();
			}
			if (logConsole)
			{
				consoleLogger.startThread();
			}
			else
			{
				consoleLogger.stopThread();
			}
		}
		if (key == 'D')
		{
			DEBUGGING = !DEBUGGING;
		}
		if (DEBUGGING) {
			if (key == 'l')
			{
				// ToDo: remove this hardcoded index. it will become harder to track once we move to xml settings.
				// use auto indexPtr = typeTagIndexes.find(packetHeader.typeTag); with some case structure here in the future.
				int w = 0;
				int s = 3;
				int p = 0;
				vector<int> indexes{ w, s, p };
				typeTagIndexes.erase(typeTags.at(w).at(s).at(p));
				typeTags.at(w).at(s).at(p) = EmotiBitPacket::TypeTag::EDL;
				typeTagIndexes.emplace(typeTags.at(w).at(s).at(p), indexes);
				plotNames.at(w).at(s).at(p) = "EDL";
				scopeWins.at(w).scopes.at(s).setVariableNames(plotNames.at(w).at(s));
				scopeWins.at(w).scopes.at(s).autoscaleY(true, 0.f);
			}
			if (key == 'r')
			{
				// ToDo: remove this hardcoded index. it will become harder to track once we move to xml settings.
				// use auto indexPtr = typeTagIndexes.find(packetHeader.typeTag); with some case structure here in the future.
				int w = 0;
				int s = 3;
				int p = 0;
				vector<int> indexes{ w, s, p };
				typeTagIndexes.erase(typeTags.at(w).at(s).at(p));
				typeTags.at(w).at(s).at(p) = EmotiBitPacket::TypeTag::EDR;
				typeTagIndexes.emplace(typeTags.at(w).at(s).at(p), indexes);
				plotNames.at(w).at(s).at(p) = "EDR";
				scopeWins.at(w).scopes.at(s).setVariableNames(plotNames.at(w).at(s));
				scopeWins.at(w).scopes.at(s).autoscaleY(true, 0.f);
			}
			if (key == 'a')
			{
				// ToDo: remove this hardcoded index. it will become harder to track once we move to xml settings.
				// use auto indexPtr = typeTagIndexes.find(packetHeader.typeTag); with some case structure here in the future.
				int w = 0;
				int s = 3;
				int p = 0;
				vector<int> indexes{ w, s, p };
				typeTagIndexes.erase(typeTags.at(w).at(s).at(p));
				typeTags.at(w).at(s).at(p) = EmotiBitPacket::TypeTag::EDA;
				typeTagIndexes.emplace(typeTags.at(w).at(s).at(p), indexes);
				plotNames.at(w).at(s).at(p) = "EDA";
				scopeWins.at(w).scopes.at(s).setVariableNames(plotNames.at(w).at(s));
				scopeWins.at(w).scopes.at(s).autoscaleY(true, 0.f);
			}
		}
		else if (_testingHelper.testingOn)
		{
			if (key == 'p')
			{
				_testingHelper.recordPpgResult();
			}
			if (key == 'l')
			{
				_testingHelper.pushEdlEdrResult();
			}
			if (key == 'L')
			{
				_testingHelper.popEdlEdrResult();
			}
			if (key == 'r')
			{
				_testingHelper.pushEdrP2pResult();
			}
			if (key == 'R')
			{
				_testingHelper.popEdrP2pResult();
			}
			if (key == 't')
			{
				_testingHelper.pushThermopileResult();
			}
			if (key == 'T')
			{
				_testingHelper.popThermopileResult();
			}
			if (key == 'c')
			{
				_testingHelper.clearAllResults();
			}
		}
		else
		{
			if (key == 'S')
			{
				ofxMultiScope::saveScopeSettings(scopeWins);
			}
			else if (key == 'L')
			{
				scopeWins = ofxMultiScope::loadScopeSettings();
				plotIdIndexes = ofxMultiScope::getPlotIdIndexes();
			}
			else if (key == 'P')
			{
				if (startOscOutput())
				{
					sendOsc = true;
				}
				else
				{
					sendOsc = false;
				}


			}
		}
	}
	*/
	ofLogNotice() << "Key Released: " << (char)key << endl;

}


//--------------------------------------------------------------
void ofApp2::mouseMoved(int x, int y) {

}

//--------------------------------------------------------------
void ofApp2::mouseDragged(int x, int y, int button) {

}

//--------------------------------------------------------------
void ofApp2::mousePressed(int x, int y, int button) {

}

//--------------------------------------------------------------
void ofApp2::mouseReleased(int x, int y, int button) {

}

//--------------------------------------------------------------
void ofApp2::windowResized(int w, int h) {

}

//--------------------------------------------------------------
void ofApp2::gotMessage(ofMessage msg) {

}

//--------------------------------------------------------------
void ofApp2::dragEvent(ofDragInfo dragInfo) {

}
#pragma region EmotiBitStuff

void ofApp2::recordButtonPressed(bool & recording) {
	if (recording) {
		string localTime = EmotiBit::ofGetTimestampString(EmotiBitPacket::TIMESTAMP_STRING_FORMAT);
		emotiBitWiFi.sendControl(EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::RECORD_BEGIN, emotiBitWiFi.controlPacketCounter++, localTime, 1));
	}
	else {
		string localTime = EmotiBit::ofGetTimestampString(EmotiBitPacket::TIMESTAMP_STRING_FORMAT);
		emotiBitWiFi.sendControl(EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::RECORD_END, emotiBitWiFi.controlPacketCounter++, localTime, 1));
	}
}

void ofApp2::sendExperimenterNoteButton() {
	string note = userNote.getParameter().toString();
	if (note.compare(GUI_STRING_EMPTY_USER_NOTE) != 0 && emotiBitWiFi.isConnected()) {
		vector<string> payload;
		payload.push_back(EmotiBit::ofGetTimestampString(EmotiBitPacket::TIMESTAMP_STRING_FORMAT));
		payload.push_back(note);
		emotiBitWiFi.sendControl(EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::USER_NOTE, emotiBitWiFi.controlPacketCounter++, payload));
		userNote.getParameter().fromString(GUI_STRING_EMPTY_USER_NOTE);
	}

	if (_testingHelper.testingOn)
	{
		_testingHelper.updateSerialNumber(note);
		//_testingHelper.updateTestStatus(note);
	}
}

template <class T>
vector<vector<vector<T>>> ofApp2::initBuffer(vector<vector<vector<T>>> buffer) {
	buffer.resize(typeTags.size());
	for (int w = 0; w < typeTags.size(); w++) {
		buffer.at(w).resize(typeTags.at(w).size());
		for (int s = 0; s < typeTags.at(w).size(); s++) {
			buffer.at(w).at(s).resize(typeTags.at(w).at(s).size());
			for (int p = 0; p < typeTags.at(w).at(s).size(); p++) {
				buffer.at(w).at(s).at(p) = 0;
			}
		}
	}
	return buffer;
}

float ofApp2::smoother(float smoothData, float newData, float newDataWeight) 
{
	smoothData = smoothData * (1 - newDataWeight) + newData * newDataWeight;
	return smoothData;
}

void ofApp2::updateDeviceList()
{
	// Update add any missing EmotiBits on network to the device list
	// ToDo: consider subtraction of EmotiBits that are stale
	auto discoveredEmotibits = emotiBitWiFi.getdiscoveredEmotibits();
	for (auto it = discoveredEmotibits.begin(); it != discoveredEmotibits.end(); it++)
	{
		string deviceId = it->first;
		bool available = it->second.isAvailable;
		bool found = false;
		// Search the GUI list to see if we're missing any EmotiBits
		for (auto device = deviceList.begin(); device != deviceList.end(); device++)
		{
			if (deviceId.compare(device->getName()) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			deviceList.emplace_back(deviceId, false);	// Add a new device (unchecked)
			//deviceList.at(deviceList.size() - 1).addListener(this, &ofApp2::deviceSelection);	// Attach a listener
			guiPanels.at(guiPanelDevice).getGroup(GUI_DEVICE_GROUP_NAME).add(deviceList.at(deviceList.size() - 1));
			if (discoveredEmotibits.size() == 1 && deviceList.size() == 1)  // This is the first device in the list
			{
				// There is one device on the network and it's the first device in the list
				// connect
				deviceList.at(deviceList.size() - 1).set(true);
			}
		}
	}

	// Update selected device
	if (emotiBitWiFi.isConnected())
	{
		// ToDo: Think about how to make the displayed text scalable to cover other info. about selected EmotiBit
		deviceSelected.setup(GUI_STRING_EMOTIBIT_SELECTED, emotiBitWiFi.connectedEmotibitIdentifier);
	}
	else
	{
		deviceSelected.setup(GUI_STRING_EMOTIBIT_SELECTED, GUI_STRING_NO_EMOTIBIT_SELECTED);
	}

	// Update deviceList to reflect availability and connection status
	for (auto device = deviceList.begin(); device != deviceList.end(); device++)
	{
		// Update availability color
		string deviceId = device->getName();
		bool available = false;
		try { available = discoveredEmotibits.at(deviceId).isAvailable; }
		catch (const std::out_of_range& oor) { oor; } // ignore exception
		ofColor textColor;
		if (available || deviceId.compare(emotiBitWiFi.connectedEmotibitIdentifier) == 0)
		{
			textColor = deviceAvailableColor;
		}
		else
		{
			textColor = notAvailableColor;
		}
		guiPanels.at(guiPanelDevice).getGroup(GUI_DEVICE_GROUP_NAME).getControl(deviceId)->setTextColor(textColor);

		// Update device connection status checkbox
		bool selected = device->get();
		if (deviceId.compare(emotiBitWiFi.connectedEmotibitIdentifier) == 0 && !selected)
		{
			// Connected to device -- checkbox needs to be checked
			ofRemoveListener(deviceGroup.parameterChangedE(), this, &ofApp2::deviceGroupSelection);
			device->set(true);
			ofAddListener(deviceGroup.parameterChangedE(), this, &ofApp2::deviceGroupSelection);
			if (sendLsl)
			{
				// Changing devices may require LSL stream setup
				string sourceId = emotiBitWiFi.connectedEmotibitIdentifier;
				if (!emotibitLsl.isDataStreamOutputSource(sourceId))
				{
					// A new sourceId needs to be added to LSL sender
					if (lslSettings.empty())
					{
						cout << "Loading LSL settings from: " << lslOutputSettingsFile << endl;
						emotibitLsl.clearDataStreamOutputs(); // clear the existing stream outputs when loading settings to avoid weird conflicts
						lslSettings = loadTextFile(lslOutputSettingsFile);
					}
					string sourceId = emotiBitWiFi.connectedEmotibitIdentifier;
					if (lslSettings.empty())
					{
						cout << "LSL settings not found: " << lslOutputSettingsFile << endl;
						// ToDo: consider a graceful way to unmark LSL in output list
						//sendDataList.at(j).set(false);
						//sendLsl = false;
					}
					else if (sourceId.empty())
					{
						cout << "Select an EmotiBit to setup LSL streaming" << endl;
					}
					else if (!emotibitLsl.addDataStreamOutputs(lslSettings, sourceId) == EmotiBitLsl::ReturnCode::SUCCESS)
					{
						cout << "LSL output setup failed: " << emotibitLsl.getlastErrMsg() << endl;
						// ToDo: consider a graceful way to unmark LSL in output list
						//sendDataList.at(j).set(false);
						//sendLsl = false;
					}
					else
					{
						cout << "Added LSL stream source: " << sourceId << endl;
						cout << "Starting LSL streaming from: " << sourceId << endl;
					}
				}
				else cout << "Starting LSL streaming from: " << sourceId << endl;
			}
		}
		else if (deviceId.compare(emotiBitWiFi.connectedEmotibitIdentifier) != 0 && selected)
		{
			// Not connected to device -- checkbox needs to be unchecked
			ofRemoveListener(deviceGroup.parameterChangedE(), this, &ofApp2::deviceGroupSelection);
			device->set(false);
			ofAddListener(deviceGroup.parameterChangedE(), this, &ofApp2::deviceGroupSelection);
			clearOscilloscopes(true);
		}
	}
}

void ofApp2::powerModeSelection(ofAbstractParameter& mode)
{
	// Remove listener during list management
	ofRemoveListener(powerModeGroup.parameterChangedE(), this, &ofApp2::powerModeSelection);

	bool selected = mode.cast<bool>().get();
	if (selected)
	{
		// Box checked

		// Unselect other options
		for (auto option = powerModeList.begin(); option != powerModeList.end(); option++)
		{
			if (option->getName().compare(mode.getName()) != 0)
			{
				option->set(false);
			}
		}

		string packet;
		string localTime = EmotiBit::ofGetTimestampString(EmotiBitPacket::TIMESTAMP_STRING_FORMAT);
		if (mode.getName().compare(GUI_STRING_NORMAL_POWER) == 0)
		{
			_powerMode = EmotibitInfo::PowerMode::NORMAL_POWER;
			packet = EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::MODE_NORMAL_POWER, 
				emotiBitWiFi.controlPacketCounter++, localTime, 1);
		}
		else if (mode.getName().compare(GUI_STRING_LOW_POWER) == 0)
		{
			_powerMode = EmotibitInfo::PowerMode::LOW_POWER;
			packet = EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::MODE_LOW_POWER, 
				emotiBitWiFi.controlPacketCounter++, localTime, 1);
		}
		else if (mode.getName().compare(GUI_STRING_WIRELESS_OFF) == 0)
		{
			_powerMode = EmotibitInfo::PowerMode::WIRELESS_OFF;
			packet = EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::MODE_WIRELESS_OFF, 
				emotiBitWiFi.controlPacketCounter++, localTime, 1);
		}
		else if (mode.getName().compare(GUI_STRING_HIBERNATE) == 0)
		{
			_powerMode = EmotibitInfo::PowerMode::HIBERNATE;
			packet = EmotiBitPacket::createPacket(EmotiBitPacket::TypeTag::MODE_HIBERNATE, 
				emotiBitWiFi.controlPacketCounter++, localTime, 1);
		}
		if (packet.length() > 0)
		{
			emotiBitWiFi.sendControl(packet);
		}
	}
	else
	{
		// Box unchecking not permitted on this list. Re-check box.
		mode.cast<bool>().set(true);
	}

	// Re-add the listener
	ofAddListener(powerModeGroup.parameterChangedE(), this, &ofApp2::powerModeSelection);
}

void ofApp2::deviceGroupSelection(ofAbstractParameter& device)
{
	string deviceId = device.getName();
	auto discoveredEmotibits = emotiBitWiFi.getdiscoveredEmotibits();
	bool selected = device.cast<bool>().get();
	if (selected)
	{
		// device selected
				bool available = false;
		try	{	available = discoveredEmotibits.at(deviceId).isAvailable;	}
		catch (const std::out_of_range& oor) { oor; } // ignore exception
		if (available)
		{
			// Only respond to available selections
			if (deviceId.compare(emotiBitWiFi.connectedEmotibitIdentifier) == 0)
			{
				// We're already connected to the selected device, so enjoy a cold beer
			}
			else
			{
				if (emotiBitWiFi.isConnected())
				{
					// If we're already connected, first disconnect
					emotiBitWiFi.disconnect();
					// ToDo: verify this is thread-safe
					vector<string> dataPackets;
					emotiBitWiFi.readData(dataPackets);
					emotiBitWiFi.readData(dataPackets);
					clearOscilloscopes(true);
				}
				// ToDo: consider if we need a delay here
				emotiBitWiFi.connect(deviceId);
				_powerMode = EmotibitInfo::PowerMode::LOW_POWER;
				clearOscilloscopes(true);
			}
		}
	}
	else	
	{
		// device unselected
		if (emotiBitWiFi.connectedEmotibitIdentifier.compare(deviceId) == 0)
		{
			// The device we're connected to has been unchecked... disconnect
			emotiBitWiFi.disconnect();
			// ToDo: verify this is thread-safe
			vector<string> dataPackets;
			emotiBitWiFi.readData(dataPackets);
			emotiBitWiFi.readData(dataPackets);
			clearOscilloscopes(true);
		}
	}
}

void ofApp2::sendDataSelection(ofAbstractParameter& output) {
	// Some outputs are disabled until code is written to support output channels

	string outputName = output.getName();
	bool selected = output.cast<bool>().get();
	for (int j = 0; j < sendDataList.size(); j++) {
		// if the Tx type is disabled, do nothing
		if (sendDataDisabled.at(j))
		{
			if (selected)
			{
				// set disabled outputs back to false
				sendDataList.at(j).set(false);
			}
		}
		else
		{
			if (outputName.compare(sendDataOptions.at(j)) == 0)
			{
				// ToDo: Generalize output management
				if (GUI_STRING_SEND_DATA_OSC.compare(sendDataOptions.at(j)) == 0)
				{
					if (selected)
					{
						if (startOscOutput())
						{
							sendOsc = true;
						}
						else
						{
							sendDataList.at(j).set(false);
							sendOsc = false;
						}		
					}
					else
					{
						sendOsc = false;
					}
				}
				if (GUI_STRING_SEND_DATA_UDP.compare(sendDataOptions.at(j)) == 0)
				{
					if (selected)
					{
						if (startUdpOutput())
						{
							sendUdp = true;
						}
						else
						{
							sendDataList.at(j).set(false);
							sendUdp = false;
						}
					}
					else
					{
						sendUdp = false;
					}
				}
				if (GUI_STRING_SEND_DATA_LSL.compare(sendDataOptions.at(j)) == 0)
				{
					if (selected)
					{
						// Clear and reload LSL settings whenever LSL output selected
						emotibitLsl.clearDataStreamOutputs();
						cout << "Loading LSL settings from: " << lslOutputSettingsFile << endl;
						lslSettings = loadTextFile(lslOutputSettingsFile);
						string sourceId = emotiBitWiFi.connectedEmotibitIdentifier;
						if (lslSettings.empty())
						{
							cout << "LSL settings not found: " << lslOutputSettingsFile << endl;
							// ToDo: consider whether auto-unchecking LSL box is the best UX
							sendDataList.at(j).set(false);
							sendLsl = false;
						}
						else if (sourceId.empty())
						{
							cout << "Select an EmotiBit to setup LSL streaming" << endl;
							sendLsl = true;
						}
						else if (!emotibitLsl.addDataStreamOutputs(lslSettings, sourceId) == EmotiBitLsl::ReturnCode::SUCCESS)
						{
							cout << "LSL output setup failed: " << emotibitLsl.getlastErrMsg() << endl;
							sendLsl = false;
						}
						else
						{
							sendLsl = true;
							cout << "Starting LSL streaming from: " << sourceId << endl;
						}
					}
					else
					{
						sendLsl = false;
					}

				}
			}
		}
	}
	bool isSending = false;
	// check if we are sending data out on any channel
	for (int i = 0; i < sendDataList.size(); i++)
	{
		if (sendDataList.at(i).get())
		{
			// we are sending data on some channel
			isSending = true;
			break;
		}
	}
	if (isSending)
	{
		sendOptionSelected.setup(GUI_STRING_SEND_DATA_VIA, "");
	}
	else
	{
		sendOptionSelected.setup(GUI_STRING_SEND_DATA_VIA, GUI_STRING_SEND_DATA_NONE);
	}
	return;  
#if (0)
	if (selected) {
		if (sendOptionSelected.get().compare(GUI_STRING_SEND_DATA_NONE) != 0) {	// If there is currently a selected IP address
			// Unselected it
			for (int j = 0; j < sendDataList.size(); j++) {
				if (sendOptionSelected.get().compare(sendDataList.at(j).getName()) == 0) {
					sendDataList.at(j).set(false);
				}
			}
		}

		// Updated the selected output
		for (int j = 0; j < sendDataList.size(); j++) {
			if (sendDataList.at(j).get()) {
				string output = sendDataList.at(j).getName();
				sendOptionSelected.set(output);
			}
		}
	}
	else {
		sendOptionSelected.set(GUI_STRING_SEND_DATA_NONE);
	}
#endif
}

void ofApp2::processAperiodicData(std::string signalId, std::vector<float> data)
{
	std::vector<float> periodizedData; // cleared before update inside every update call
	for (int i = 0; i < periodizerList.size(); i++)
	{
		// update() returns size of data which needs to be added into the plot buffers
		if (periodizerList.at(i).update(signalId, data, periodizedData))
		{
			auto indexPtr = typeTagIndexes.find(periodizerList.at(i).outputSignal);
			if (indexPtr != typeTagIndexes.end())
			{
				int w = indexPtr->second.at(0); // Scope window(multiscope)
				int s = indexPtr->second.at(1); // Scope
				int p = indexPtr->second.at(2); // Plot
				std::vector<std::vector<float>> plotData;
				plotData.resize(typeTags.at(w).at(s).size());
				plotData.at(p) = periodizedData;
				// Add data to oscilloscope
				scopeWins.at(w).scopes.at(s).updateData(plotData);
			}
		}
	}
}

void ofApp2::processSlowResponseMessage(string packet) {
	if (sendUdp) // Handle sending data to outputs
	{
		udpSender.Send(packet.c_str(), packet.length());
	}
	vector<string> splitPacket = ofSplitString(packet, ",");	// split data into separate value pairs
	processSlowResponseMessage(splitPacket);
}

void ofApp2::processSlowResponseMessage(vector<string> splitPacket) 
{

	EmotiBitPacket::Header packetHeader;
	if (EmotiBitPacket::getHeader(splitPacket, packetHeader)) 
	{
		if (packetHeader.dataLength >= MAX_BUFFER_LENGTH) 
		{
			bufferUnderruns++;
			cout << "**** POSSIBLE BUFFER UNDERRUN EVENT " << bufferUnderruns << ", " << packetHeader.dataLength << " ****" << endl;
		}

		if (_testingHelper.testingOn)
		{
			_testingHelper.update(splitPacket, packetHeader);
		}
		// ToDo: the second comparison is redundant with the called func. Added it here to skip a function call. Might want to change the order later.
		if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::THERMOPILE) == 0 && typeTagIndexes.find(EmotiBitPacket::TypeTag::THERMOPILE) == typeTagIndexes.end())
		{
			// Add stream to plot if data detected.
			addDataStream(EmotiBitPacket::TypeTag::THERMOPILE);
		}
		if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::TEMPERATURE_1) == 0 && typeTagIndexes.find(EmotiBitPacket::TypeTag::TEMPERATURE_1) == typeTagIndexes.end())
		{
			// Add stream to plot if data detected.
			addDataStream(EmotiBitPacket::TypeTag::TEMPERATURE_1);
		}
		auto indexPtr = typeTagIndexes.find(packetHeader.typeTag);	// Check whether we're plotting this typeTage
		if (indexPtr != typeTagIndexes.end()) 
		{	// We're plotting this packet's typeTag!
			vector<vector<float>> data;
			int w = indexPtr->second.at(0); // Scope window
			int s = indexPtr->second.at(1); // Scope
			int p = indexPtr->second.at(2); // Plot
			data.resize(typeTags.at(w).at(s).size());

			vector<string> oscAddresses;
			vector<ofxOscMessage> oscMessages;
			if (sendOsc) // Handle sending data to outputs
			{
				// ToDo: Refactor to handle data outputs in one place
				// ToDo: Make it possible to send data types that aren't being plotted (e.g. EL, ER)
				oscAddresses = oscPatchboard.patchcords[packetHeader.typeTag];
				oscMessages.resize(oscAddresses.size());
				for (int a = 0; a < oscAddresses.size(); a++)
				{
					oscMessages.at(a).setAddress(oscAddresses.at(a));
				}
			}

			for (int n = EmotiBitPacket::headerLength; n < splitPacket.size(); n++) 
			{
				data.at(p).emplace_back(ofToFloat(splitPacket.at(n))); 

				if (sendLsl)
				{
					vector<float> lslSample(1); // data is passed into addSample as a vector of 1 datapoint/channel
					lslSample.at(0) = data.at(p).back();
					emotibitLsl.addSample(lslSample, packetHeader.typeTag, emotiBitWiFi.connectedEmotibitIdentifier);
					//cout << packetHeader.typeTag << ":" << ofToString(data.at(p)) << ", ";
				}
				// Data for plotting in the oscilloscope

				if (sendOsc) // Handle sending data to outputs
				{
					for (int a = 0; a < oscMessages.size(); a++)
					{
						oscMessages.at(a).addFloatArg(data.at(p).back());
					}
				}
			}

			if (sendOsc)
			{
				for (int a = 0; a < oscMessages.size(); a++)
				{
					// ToDo: Consider using ofxOscBundle
					oscSender.sendMessage(oscMessages.at(a));
				}
			}

			if (!isPaused) {
				processAperiodicData(packetHeader.typeTag, data.at(p));
				// check if typetag is aperiodic
				bool isAperiodic = false;
				for (uint8_t i = 0; i < EmotiBitPacket::TypeTagGroups::NUM_APERIODIC; i++)
				{
					if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTagGroups::APERIODIC[i]) == 0)
					{
						// found
						isAperiodic = true;
						break;
					}
				}
				if (!isAperiodic)
				{
					// Add data to oscilloscope
					scopeWins.at(w).scopes.at(s).updateData(data);
				}
			}
			bufferSizes.at(w).at(s).at(p) = packetHeader.dataLength;
			dataCounts.at(w).at(s).at(p) += packetHeader.dataLength;

			// Sliding EDA minYspan 
			if (!DEBUGGING && packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::EDA) == 0 && data.at(p).size() > 0)
			{
				minYSpans.at(w).at(s) = 0.1f * pow(data.at(p).at(0), 1.5f);
				if (yLims.at(w).at(s).at(0) == yLims.at(w).at(s).at(1)) {
					scopeWins.at(w).scopes.at(s).autoscaleY(true, minYSpans.at(w).at(s));
				}
			}
		}
		else 
		{
			if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::BATTERY_VOLTAGE) == 0) 
			{
				deviceSelected.setup(GUI_STRING_EMOTIBIT_SELECTED, GUI_STRING_NO_EMOTIBIT_SELECTED);
				batteryStatus.setup(GUI_STRING_BATTERY_LEVEL, splitPacket.at(6) + "V");
			}
			else if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::BATTERY_PERCENT) == 0) 
			{
				batteryStatus.setup(GUI_STRING_BATTERY_LEVEL, splitPacket.at(6) + "%");
			}
			else if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::EMOTIBIT_MODE) == 0) 
			{
				processModePacket(splitPacket);
			}
			else if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::DATA_CLIPPING) == 0) 
			{
				for (int n = EmotiBitPacket::headerLength; n < splitPacket.size(); n++) {
					for (int w = 0; w < typeTags.size(); w++) {
						for (int s = 0; s < typeTags.at(w).size(); s++) {
							for (int p = 0; p < typeTags.at(w).at(s).size(); p++) {
								if (splitPacket.at(n).compare(typeTags.at(w).at(s).at(p)) == 0) {
									dataClippingCount++;
									guiPanels.at(guiPanelErrors).getControl(GUI_STRING_CLIPPING_EVENTS)->setBackgroundColor(ofColor(255, 0, 0));
								}
							}
						}
					}
				}
			}
			else if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::DATA_OVERFLOW) == 0) 
			{
				for (int n = EmotiBitPacket::headerLength; n < splitPacket.size(); n++) {
					for (int w = 0; w < typeTags.size(); w++) {
						for (int s = 0; s < typeTags.at(w).size(); s++) {
							for (int p = 0; p < typeTags.at(w).at(s).size(); p++) {
								if (splitPacket.at(n).compare(typeTags.at(w).at(s).at(p)) == 0) {
									dataOverflowCount++;
									guiPanels.at(guiPanelErrors).getControl(GUI_STRING_OVERFLOW_EVENTS)->setBackgroundColor(ofColor(255, 0, 0));
								}
							}
						}
					}
				}
			}
			else if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::RESET) == 0) 
			{
				//if (guiPanels.at(guiPanelMode).getControl(GUI_STRING_CONTROL_HIBERNATE) != NULL) {
				//	hibernateButton.set(GUI_STRING_CONTROL_HIBERNATE, false);
				//	guiPanels.at(guiPanelMode).getControl(GUI_STRING_CONTROL_HIBERNATE)->setBackgroundColor(ofColor(0, 0, 0));
				//	hibernateStatus.setBackgroundColor(ofColor(0, 0, 0));
				//	hibernateStatus.getParameter().fromString(GUI_STRING_MODE_ACTIVE);
				//}
				if (guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD) != NULL) {
					recordingButton.removeListener(this, &ofApp2::recordButtonPressed);
					recordingButton.set(false);
					recordingButton.addListener(this, &ofApp2::recordButtonPressed);
					recordingButton.set(GUI_STRING_CONTROL_RECORD, false);
					guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->setBackgroundColor(ofColor(0, 0, 0));
					recordingStatus.setBackgroundColor(ofColor(0, 0, 0));
					recordingStatus.getParameter().fromString(GUI_STRING_NOT_RECORDING);
				}
			}
		}
	}
}

void ofApp2::setupGui()
{
	ofSetWindowTitle("EmotiBit Oscilloscope (v" + ofxEmotiBitVersion + ")");

	string legendFontFilename = "verdanab.ttf";

	legendFont.load(ofToDataPath(legendFontFilename), 11, true, true);
	axesFont.load(ofToDataPath("verdana.ttf"), 10, true, true);
	subLegendFont.load(ofToDataPath("verdana.ttf"), 7, true, true);

	_consoleHeight = 21;


	recordingButton.addListener(this, &ofApp2::recordButtonPressed);
	sendUserNote.addListener(this, &ofApp2::sendExperimenterNoteButton);

	int sendDataWidth = 200;

	int guiXPos = 0;
	int guiYPos = 25;
	int guiWidth = 220;
	int guiPosInc = guiWidth + 1;
	
	//guiPanels.resize(6); // This fails in OF v0.11.2 with "attempting to reference a deleted function" error

	// Device Menu
	int p = 0;
	guiPanelDevice = p;
	guiPanels.at(guiPanelDevice).setDefaultWidth(guiWidth);
	guiPanels.at(guiPanelDevice).setDefaultHeight(guiYPos);
	if (legendFont.isLoaded())
	{
		// Check to see if legend font loaded before adding to gui panel to avoid blank text
		guiPanels.at(guiPanelDevice).loadFont(ofToDataPath(legendFontFilename), 10, true, true);
	}
	guiPanels.at(guiPanelDevice).setup("selectDevice", "junk.xml", guiXPos, -guiYPos);
	guiPanels.at(guiPanelDevice).add(deviceSelected.setup(GUI_STRING_EMOTIBIT_SELECTED, ":" + GUI_STRING_NO_EMOTIBIT_SELECTED));
	//deviceMenuGroup.setName(GUI_DEVICE_GROUP_MENU_NAME);
	deviceGroup.setName(GUI_DEVICE_GROUP_NAME);
	//deviceList.emplace_back("Message All Emotibits", true);
	//deviceGroup.add(deviceList.at(deviceList.size() - 1));
	//deviceMenuGroup.add(deviceGroup);
	guiPanels.at(guiPanelDevice).add(deviceGroup);
	//guiPanels.at(guiPanelDevice).getGroup(GUI_DEVICE_GROUP_MENU_NAME).getGroup(GUI_DEVICE_GROUP_NAME)
	ofAddListener(deviceGroup.parameterChangedE(), this, &ofApp2::deviceGroupSelection);


	// Power Status Menu
	p++;
	guiXPos += guiWidth + 1;
	guiWidth = 259;
	guiPanelPowerStatus = p;
	guiPanels.at(guiPanelPowerStatus).setDefaultWidth(guiWidth);
	guiPanels.at(guiPanelPowerStatus).setup("powerStatus", "junk.xml", guiXPos, -guiYPos);
	guiPanels.at(guiPanelPowerStatus).add(batteryStatus.setup(GUI_STRING_BATTERY_LEVEL, ":?", guiWidth, guiYPos));
	//powerStatusMenuGroup.setName(GUI_POWER_STATUS_MENU_NAME);
	//powerStatusMenuGroup.add(batteryStatus.set(GUI_STRING_BATTERY_LEVEL, "?"));
	powerModeGroup.setName(GUI_POWER_MODE_GROUP_NAME);
	//powerStatusMenuGroup.add(powerModeGroup);
	//guiPanels.at(guiPanelPowerStatus).add(powerStatusMenuGroup);
	guiPanels.at(guiPanelPowerStatus).add(powerModeGroup);
	powerModeOptions = {
		GUI_STRING_NORMAL_POWER,
		GUI_STRING_LOW_POWER,
		GUI_STRING_WIRELESS_OFF,
		GUI_STRING_HIBERNATE
	};
	for (int j = 0; j < powerModeOptions.size(); j++) {
		powerModeList.emplace_back(powerModeOptions.at(j), false);
		//sendDataList.at(sendDataList.size() - 1).addListener(this, &ofApp2::sendDataSelection);
		//sendDataGroup.add(sendDataList.at(sendDataList.size() - 1));
		guiPanels.at(guiPanelPowerStatus).getGroup(GUI_POWER_MODE_GROUP_NAME).add(powerModeList.at(powerModeList.size() - 1));
	}
	guiPanels.at(guiPanelPowerStatus).getGroup(GUI_POWER_MODE_GROUP_NAME).minimize();
	ofAddListener(powerModeGroup.parameterChangedE(), this, &ofApp2::powerModeSelection);

	// Recording Status
	p++;
	guiXPos += guiWidth + 1;
	guiWidth = 269;
	guiPanelRecord = p;
	guiPanels.at(guiPanelRecord).setDefaultWidth(guiWidth);
	guiPanels.at(guiPanelRecord).setup("startRecording", "junk.xml", guiXPos, -guiYPos);
	guiPanels.at(guiPanelRecord).add(recordingButton.set(GUI_STRING_CONTROL_RECORD, false));
	guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->setTextColor(recordControlColor); // color of label and x
	guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->setFillColor(recordControlColor); // fill color of checkbox
	//guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->loadFont(ofToDataPath("verdanab.ttf"), 11, true, true); // Seems to affect all guiPanels
	//guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->setUseTTF(true);
	//guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->setBackgroundColor(ofColor(0,0,255)); // background of whole control
	//guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->setHeaderBackgroundColor(ofColor(255,255,0)); // not cear what this does
	//guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->setBorderColor(ofColor(0,255,0)); // not clear what this does
	//guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->setSize(5, 10); // size of whole field
	guiPanels.at(guiPanelRecord).add(recordingStatus.setup("SD File", GUI_STRING_NOT_RECORDING));
	//guiPanels.at(0).getControl(GUI_STRING_CONTROL_RECORD)->setSize(guiWidth, guiYPos * 2);

	// Error Status
	p++;
	guiXPos += guiWidth + 1;
	guiWidth = 200;
	guiPanelErrors = p;
	guiPanels.at(guiPanelErrors).setDefaultWidth(guiWidth);
	guiPanels.at(guiPanelErrors).setup("errorStatus", "junk.xml", guiXPos, -guiYPos);
	guiPanels.at(guiPanelErrors).add(dataClippingCount.set(GUI_STRING_CLIPPING_EVENTS, 0, 0, 0));
	guiPanels.at(guiPanelErrors).add(dataOverflowCount.set(GUI_STRING_OVERFLOW_EVENTS, 0, 0, 0));
	p++;

	// User Note
	guiXPos += guiWidth + 1;
	guiWidth = 200;
	guiPanelUserNote = p;
	guiPanels.at(guiPanelUserNote).setDefaultWidth(ofGetWindowWidth() - guiXPos - sendDataWidth);
	guiPanels.at(guiPanelUserNote).setup("userNote", "junk.xml", guiXPos, -guiYPos);
	guiPanels.at(guiPanelUserNote).add(userNote.setup("Note:", GUI_STRING_EMPTY_USER_NOTE));
	guiPanels.at(guiPanelUserNote).add(sendUserNote.setup(GUI_STRING_NOTE_BUTTON));
	guiPanels.at(guiPanelUserNote).getControl(GUI_STRING_NOTE_BUTTON)->setTextColor(noteControlColor); // color of label and x
	guiPanels.at(guiPanelUserNote).getControl(GUI_STRING_NOTE_BUTTON)->setFillColor(noteControlColor); // fill color of checkbox

	// Send Data Menu
	p++;
	guiPanelSendData = p;
	guiWidth = sendDataWidth;
	guiXPos = ofGetWindowWidth() - guiWidth + 1;
	guiPanels.at(guiPanelSendData).setDefaultWidth(guiWidth);
	guiPanels.at(guiPanelSendData).setup("sendData", "junk.xml", guiXPos, -guiYPos);
	//sendDataMenuGroup.setName(GUI_SEND_DATA_MENU_NAME);
	guiPanels.at(guiPanelSendData).add(sendOptionSelected.setup(GUI_STRING_SEND_DATA_VIA, GUI_STRING_SEND_DATA_NONE));
	sendDataGroup.setName(GUI_OUTPUT_GROUP_NAME);
	//sendDataMenuGroup.add(sendDataGroup);
	guiPanels.at(guiPanelSendData).add(sendDataGroup);
	sendDataOptions = {
		GUI_STRING_SEND_DATA_OSC,
		GUI_STRING_SEND_DATA_LSL,
		GUI_STRING_SEND_DATA_TCP,
		GUI_STRING_SEND_DATA_UDP,
		GUI_STRING_SEND_DATA_MQTT
	};
	for (int j = 0; j < sendDataOptions.size(); j++) {
		sendDataList.emplace_back(sendDataOptions.at(j), false);
		//sendDataList.at(sendDataList.size() - 1).addListener(this, &ofApp2::sendDataSelection);
		//sendDataGroup.add(sendDataList.at(sendDataList.size() - 1));
		guiPanels.at(guiPanelSendData).getGroup(GUI_OUTPUT_GROUP_NAME).add(sendDataList.at(sendDataList.size() - 1));
		// Disable outputs until supporting code written
		if (GUI_STRING_SEND_DATA_OSC.compare(sendDataOptions.at(j)) == 0 
			|| GUI_STRING_SEND_DATA_UDP.compare(sendDataOptions.at(j)) == 0
			|| GUI_STRING_SEND_DATA_LSL.compare(sendDataOptions.at(j)) == 0)
		{
			sendDataDisabled.push_back(false);
			guiPanels.at(guiPanelSendData).getGroup(GUI_OUTPUT_GROUP_NAME).getControl(sendDataOptions.at(j))->setTextColor(deviceAvailableColor);
		}
		else
		{
			sendDataDisabled.push_back(true);
			guiPanels.at(guiPanelSendData).getGroup(GUI_OUTPUT_GROUP_NAME).getControl(sendDataOptions.at(j))->setTextColor(notAvailableColor);
		}
	}
	guiPanels.at(guiPanelSendData).getGroup(GUI_OUTPUT_GROUP_NAME).minimize();
	ofAddListener(sendDataGroup.parameterChangedE(), this, &ofApp2::sendDataSelection);

	//Set default widths of device
	deviceSelected.setDefaultWidth(220);
	batteryStatus.setDefaultWidth(259);
}

// ToDo: This function is marked to be removed when we complete our move to xmlFileSettings.
void ofApp2::updatePlotAttributeLists(std::string settingsFile)
{
	ofxXmlSettings scopeSettings;
	scopeSettings.loadFile(settingsFile);

	int nMultiScopes = scopeSettings.getNumTags("multiScope");
	samplingFreqs.resize(nMultiScopes);
	minYSpans.resize(nMultiScopes);
	plotNames.resize(nMultiScopes);
	plotColors.resize(nMultiScopes);
	yLims.resize(nMultiScopes);
	for (int m = 0; m < nMultiScopes; m++)
	{
		scopeSettings.pushTag("multiScope", m);
		int nScopes = scopeSettings.getNumTags("scope");
		samplingFreqs.at(m).resize(nScopes);
		minYSpans.at(m).resize(nScopes);
		plotNames.at(m).resize(nScopes);
		plotColors.at(m).resize(nScopes);
		yLims.at(m).resize(nScopes);
		for (int s = 0; s < nScopes; s++) 
		{
			scopeSettings.pushTag("scope", s);
			//float timeWindow = scopeSettings.getValue("timeWindow", 15.f); // maybe we keep this.
			float samplingFrequency = scopeSettings.getValue("samplingFrequency", 15.f);
			float minYSpan = scopeSettings.getValue("minYSpan", 0.f);
			float yMin = scopeSettings.getValue("yMin", 0.f);
			float yMax = scopeSettings.getValue("yMax", 0.f);
			samplingFreqs.at(m).at(s) = samplingFrequency;
			minYSpans.at(m).at(s) = minYSpan;
			vector<float> yLim = { yMin, yMax };
			yLims.at(m).at(s) = yLim;
			//samplingFreqs-2D vector
			//minYSpans-2D vector
			//plotNames-3D vector
			//plotColors-3D vector
			//yLims-3D vector
			int nPlots = scopeSettings.getNumTags("plot");
			for (int p = 0; p < nPlots; p++) {
				scopeSettings.pushTag("plot", p);
				plotNames.at(m).at(s).push_back(scopeSettings.getValue("plotName", "N/A"));
				scopeSettings.pushTag("plotColor");
				plotColors.at(m).at(s).push_back(ofColor(
					scopeSettings.getValue("r", 255),
					scopeSettings.getValue("g", 255),
					scopeSettings.getValue("b", 255)
				));
				scopeSettings.popTag(); // plotColor
				scopeSettings.popTag(); // plot p
			}
			scopeSettings.popTag(); // scope s
		}

		scopeSettings.popTag(); // multiScope m
	}
}

// ToDo: This function is marked to be removed when we complete our move to xmlFileSettings.
void ofApp2::updateTypeTagList()
{
	for (int i = 0; i < plotIds.size(); i++)// for multiscopes
	{
		vector<vector<std::string>> scopeTypeTagList;
		for (int j = 0; j < plotIds.at(i).size(); j++) // for scopes
		{
			vector<std::string> plotTypeTagList;
			for (int k = 0; k < plotIds.at(i).at(j).size(); k++) // for plots
			{
				for (auto key = patchboard.patchcords.begin(); key != patchboard.patchcords.end(); key++)
				{
					// for each plot plotId, get the typeTag
					// ToDo: there should be a loop here to go through all map values for a key. In case, the same signal is patched to multiple scopes
					if (ofToInt(key->second.back()) == plotIds.at(i).at(j).at(k))
					{
						plotTypeTagList.push_back(key->first);
					}
				}
			}
			scopeTypeTagList.push_back(plotTypeTagList);
		}
		typeTags.push_back(scopeTypeTagList);
	}

	// Create an index mapping for each type tag
	for (int w = 0; w < typeTags.size(); w++) {
		for (int s = 0; s < typeTags.at(w).size(); s++) {
			for (int p = 0; p < typeTags.at(w).at(s).size(); p++) {
				vector<int> indexes{ w, s, p };
				typeTagIndexes.emplace(typeTags.at(w).at(s).at(p), indexes);
			}
		}
	}
}

void ofApp2::setupOscilloscopes() 
{
	// read the patchboard file
	if (patchboard.loadFile(ofToDataPath("inputSettings.xml")))
	{
		ofLog(OF_LOG_NOTICE, "PatchBoard succesfully loaded");
	}
	else
	{
		ofLog(OF_LOG_NOTICE, "PatchBoard File Not Found!");
		while (1);
	}
	ofFile scopeSettingsFile(ofToDataPath("ofxOscilloscopeSettings.xml"));
	// check if oscilloscope settings file exists
	if (scopeSettingsFile.exists())
	{
		scopeWins = ofxMultiScope::loadScopeSettings();
	}
	else
	{
		ofLog(OF_LOG_NOTICE, "Scope Settings File Not Found!");
		while (1);
	}
	plotIds = ofxMultiScope::getPlotIds();
	updatePlotAttributeLists();
	updateTypeTagList();
	initMetaDataBuffers();
}

void ofApp2::clearOscilloscopes(bool connectedDeviceUpdated)
{
	for (int w = 0; w < scopeWins.size(); w++) {
		scopeWins.at(w).clearData();
	}

	// update the Scope plots ONLY if there is update to connected device
	if (connectedDeviceUpdated)
	{
		// ToDo: think of an elegant way to set scopes to default value
		// remove only THERM. TEMP1 is default for temperature scope..
		removeDataStream(EmotiBitPacket::TypeTag::THERMOPILE);
	}
}

void ofApp2::updateMenuButtons()
{
	if (!emotiBitWiFi.isConnected())
	{
		_recording = false;
		batteryStatus.setup(GUI_STRING_BATTERY_LEVEL,"?");
		_powerMode = EmotibitInfo::PowerMode::length;
		guiPanels.at(guiPanelPowerStatus).getGroup(GUI_POWER_MODE_GROUP_NAME).minimize();
		//if (guiPanels.at(guiPanelDevice).getGroup(GUI_DEVICE_GROUP_MENU_NAME).getGroup(GUI_DEVICE_GROUP_NAME).isMinimized())
		//{
		//	guiPanels.at(guiPanelDevice).getGroup(GUI_DEVICE_GROUP_MENU_NAME).getGroup(GUI_DEVICE_GROUP_NAME).maximize();
		//}
		dataClippingCount = 0;
		dataOverflowCount = 0;
	}

	if (_recording)
	{
		// ToDo: also control button/checkbox
		// ofRemoveListener(deviceGroup.parameterChangedE(), this, &ofApp2::deviceGroupSelection);
		recordingButton.removeListener(this, &ofApp2::recordButtonPressed);
		recordingButton.set(true);
		recordingButton.addListener(this, &ofApp2::recordButtonPressed);
		if (guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD) != NULL) {
			guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->setBackgroundColor(ofColor(0, 0, 0));
			recordingStatus.setBackgroundColor(recordControlColor);
			recordingStatus.getParameter().fromString(_recordingFilename);
		}
	}
	else
	{
		recordingButton.removeListener(this, &ofApp2::recordButtonPressed);
		recordingButton.set(false);
		recordingButton.addListener(this, &ofApp2::recordButtonPressed);
		if (guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD) != NULL) {
			guiPanels.at(guiPanelRecord).getControl(GUI_STRING_CONTROL_RECORD)->setBackgroundColor(ofColor(0, 0, 0));
			recordingStatus.setBackgroundColor(ofColor(0, 0, 0));
			recordingStatus.getParameter().fromString(GUI_STRING_NOT_RECORDING);
		}
	}

	// Set Power Mode Options
	string optionName = GUI_STRING_NORMAL_POWER;
	if (_powerMode == EmotibitInfo::PowerMode::NORMAL_POWER)
	{
		optionName = GUI_STRING_NORMAL_POWER;
	}
	else if (_powerMode == EmotibitInfo::PowerMode::LOW_POWER)
	{
		optionName = GUI_STRING_LOW_POWER;
	}
	else if(_powerMode == EmotibitInfo::PowerMode::WIRELESS_OFF)
	{
		optionName = GUI_STRING_WIRELESS_OFF;
	}
	else if (_powerMode == EmotibitInfo::PowerMode::HIBERNATE)
	{
		optionName = GUI_STRING_HIBERNATE;
	}
	ofRemoveListener(powerModeGroup.parameterChangedE(), this, &ofApp2::powerModeSelection);
	for (auto option = powerModeList.begin(); option != powerModeList.end(); option++)
	{
		if (option->getName().compare(optionName) == 0)
		{
			option->set(true);
		}
		else
		{
			option->set(false);
		}
	}
	ofAddListener(powerModeGroup.parameterChangedE(), this, &ofApp2::powerModeSelection);

	updateDeviceList();
}

void ofApp2::processModePacket(vector<string> &splitPacket)
{
	size_t startIndex = EmotiBitPacket::headerLength;
	string value;

	int pos = EmotiBitPacket::getPacketKeyedValue(splitPacket, EmotiBitPacket::PayloadLabel::RECORDING_STATUS, value);
	if (pos > -1)
	{
		if (value.compare(EmotiBitPacket::TypeTag::RECORD_BEGIN) == 0)
		{
			_recording = true;
			// See if we got a filename for the file we're recording to
			if (pos + 1 < splitPacket.size())
			{
				string filename = splitPacket.at(pos + 1);
				if (filename.size() > 4 && filename.substr(filename.size() - 4, 4).compare(".csv") == 0)
				{
					_testingHelper.updateSdCardFilename(filename);
					_recordingFilename = filename;
				}
				else
				{
					_recordingFilename = GUI_STRING_RECORDING;
				}
			}
		}
		else if (value.compare(EmotiBitPacket::TypeTag::RECORD_END) == 0)
		{
			_recording = false;
		}
	}

	if (EmotiBitPacket::getPacketKeyedValue(splitPacket, EmotiBitPacket::PayloadLabel::POWER_STATUS, value) > -1)
	{
		if (value.compare(EmotiBitPacket::TypeTag::MODE_NORMAL_POWER) == 0)
		{
			_powerMode = EmotibitInfo::PowerMode::NORMAL_POWER;
		}
		else if (value.compare(EmotiBitPacket::TypeTag::MODE_LOW_POWER) == 0)
		{
			_powerMode = EmotibitInfo::PowerMode::LOW_POWER;
		}
		else if (value.compare(EmotiBitPacket::TypeTag::MODE_MAX_LOW_POWER) == 0)
		{
			_powerMode = EmotibitInfo::PowerMode::MAX_LOW_POWER;
		}
		else if (value.compare(EmotiBitPacket::TypeTag::MODE_WIRELESS_OFF) == 0)
		{
			_powerMode = EmotibitInfo::PowerMode::WIRELESS_OFF;
			emotiBitWiFi.disconnect();
		}
		else if (value.compare(EmotiBitPacket::TypeTag::MODE_HIBERNATE) == 0)
		{
			_powerMode = EmotibitInfo::PowerMode::HIBERNATE;
			emotiBitWiFi.disconnect();
		}
	}
}

void ofApp2::drawConsole()
{
	// Draw console
	string _consoleString = "Status: ";
	if (_testingHelper.testingOn)
	{
		_consoleString += "TESTING MODE ON -- ";
	}
	if (DEBUGGING)
	{
		_consoleString += "DEBUGGING -- ";
	}
	if (isPaused)
	{
		_consoleString += "Data visualizer paused";
	}
	else
	{
		if (!emotiBitWiFi.isConnected())
		{
			_consoleString += "No EmotiBit selected";
		}
		else if (_powerMode == EmotibitInfo::PowerMode::LOW_POWER)
		{
			_consoleString += "Low power mode";
		}
		else if (_powerMode == EmotibitInfo::PowerMode::NORMAL_POWER)
		{
			_consoleString += "Data streaming";
		}
		else if (_powerMode == EmotibitInfo::PowerMode::WIRELESS_OFF)
		{
			_consoleString += "Wireless off";
		}
		else if (_powerMode == EmotibitInfo::PowerMode::HIBERNATE)
		{
			_consoleString += "Hibernating";
		}
	}
	// Handle printing LSL marker stream details
	// ToDo: consider if there is a more elegant solution for console output
	vector<EmotiBitLsl::MarkerStreamInfo> markerInputs = emotibitLsl.getMarkerStreamInfo();
	if (markerInputs.size() > 0 && !markerInputs.at(0).name.empty())
	{
		// ToDo: Handle more than one marker stream
		if (markerInputs.at(0).rxCount > 0)
		{
			_consoleString += EmotiBitPacket::PAYLOAD_DELIMITER;
			std::string updateStr = " LSL markers Received (" + EmotiBitLsl::MARKER_INFO_NAME_LABEL + ": " + markerInputs.at(0).name;
			if (!markerInputs.at(0).sourceId.empty())
			{
				updateStr += EmotiBitPacket::PAYLOAD_DELIMITER;
				updateStr += (" " + EmotiBitLsl::MARKER_INFO_SOURCE_ID_LABEL + ": ");
				updateStr += markerInputs.at(0).sourceId;
			}
			updateStr += ("): " + ofToString(markerInputs.at(0).rxCount));
			_consoleString += updateStr;
		}
		else
		{
			_consoleString += EmotiBitPacket::PAYLOAD_DELIMITER;
			std::string updateStr = "";
			_consoleString += (" Searching for LSL stream:: " + EmotiBitLsl::MARKER_INFO_NAME_LABEL + ": " + markerInputs.at(0).name);
			if (!markerInputs.at(0).sourceId.empty())
			{
				updateStr += EmotiBitPacket::PAYLOAD_DELIMITER;
				updateStr += (" " + EmotiBitLsl::MARKER_INFO_SOURCE_ID_LABEL + ": ");
				updateStr += markerInputs.at(0).sourceId;
			}
			_consoleString += updateStr;
		}
	}

	int consoleTextPadding = 3;
	ofPushStyle();
	ofFill();
	ofSetColor(0, 0, 0);
	ofDrawRectangle(0, ofGetWindowHeight() - _consoleHeight, ofGetWindowWidth(), _consoleHeight);
	ofPopStyle();
	ofPushStyle();
	ofSetColor(255, 255, 255);
    if (axesFont.isLoaded())
    {
        axesFont.drawString(_consoleString, 10, ofGetWindowHeight() - _consoleHeight / 2 + consoleTextPadding);
	}
    else
    {
        ofDrawBitmapString(_consoleString, 10, ofGetWindowHeight() - _consoleHeight / 2 + consoleTextPadding);
    }
    ofPopStyle();

}

void ofApp2::drawOscilloscopes()
{
	ofPushMatrix();
	ofTranslate(0, drawYTranslate);
	ofScale(1, drawYScale);	// for debugging menus

	for (int w = 0; w < scopeWins.size(); w++) {
		scopeWins.at(w).plot();
	}
	for (int i = 0; i < guiPanels.size(); i++) {
		ofPushStyle();
		ofSetLineWidth(5);
		guiPanels.at(i).draw();
		ofPopStyle();
	}



	// Draw dataFreqs and bufferSizes for each stream
	if (drawDataInfo) {

		// Calculated empirical sampling freq
		static uint64_t freqCalcTimer = ofGetElapsedTimeMillis();
		uint32_t elapsedTime = ofGetElapsedTimeMillis() - freqCalcTimer;
		if (elapsedTime > 2000)
		{
			freqCalcTimer = ofGetElapsedTimeMillis();
			for (int w = 0; w < typeTags.size(); w++) {
				for (int s = 0; s < typeTags.at(w).size(); s++) {
					ofPoint bl = scopeWins.at(w).scopes.at(s).getPosition().getBottomLeft();
					for (int p = 0; p < typeTags.at(w).at(s).size(); p++) {
						dataFreqs.at(w).at(s).at(p) = 1000.f * dataCounts.at(w).at(s).at(p) / (elapsedTime);
						dataCounts.at(w).at(s).at(p) = 0;
					}
				}
			}
		}

		for (int w = 0; w < typeTags.size(); w++) {
			for (int s = 0; s < typeTags.at(w).size(); s++) {
				ofPoint bl = scopeWins.at(w).scopes.at(s).getPosition().getBottomLeft();
				for (int p = 0; p < typeTags.at(w).at(s).size(); p++) {
					int padding = 10;
					int fontHeight = 9;
					ofPushMatrix();
					ofPushStyle();

					//ofScale(0.5f, 0.5f);

					ofSetColor(plotColors.at(w).at(s).at(p));
					ofTranslate(bl.x + padding, bl.y - fontHeight * typeTags.at(w).at(s).size());
					//ofScale(0.75f, 0.75f);
					ofTranslate(0, p * fontHeight);
                    if (subLegendFont.isLoaded())
                    {
                        subLegendFont.drawString(ofToString(bufferSizes.at(w).at(s).at(p)) + " (Bffr)", 0, 0);
                    }
                    else
                    {
                        ofDrawBitmapString(ofToString(bufferSizes.at(w).at(s).at(p)) + " (Bffr)", 0, 0);
                    }
					ofTranslate(0, (-fontHeight) * (int)(typeTags.at(w).at(s).size() + 1));
                    if (subLegendFont.isLoaded())
                    {
					    subLegendFont.drawString(ofToString(dataFreqs.at(w).at(s).at(p), 1) + " (Hz)", 0, 0);
                    }
                    else
                    {
                        ofDrawBitmapString(ofToString(dataFreqs.at(w).at(s).at(p), 1) + " (Hz)", 0, 0);
                    }
					ofPopStyle();
					ofPopMatrix();
				}
			}
		}
	}
	ofPopMatrix();


}

string ofApp2::loadTextFile(string filePath)
{
	ofFile commSettingsFile(ofToDataPath(filePath));
	if (commSettingsFile.exists())
	{
		ofBuffer b = commSettingsFile.readToBuffer();
		string s = b.getText();
		cout << s << endl;
		return s;
	}
	else
	{
		ofLog(OF_LOG_ERROR, "Error: file not found - " + filePath);
	}
	return "";
}

bool ofApp2::startOscOutput()
{
	oscPatchboard.loadFile(ofToDataPath(oscOutputSettingsFile));
	oscSender.clear();
	try
	{
		string xml;
		oscPatchboard.patchboard.copyXmlToString(xml);
		cout << xml << endl;
		string ipAddress = oscPatchboard.patchboard.getValue("patchboard:settings:output:ipAddress", "");
		string port = oscPatchboard.patchboard.getValue("patchboard:settings:output:port", "");
		if (ipAddress != "" && port != "")
		{
			cout << "Starting OSC output: " << ipAddress << "," << ofToInt(port) << endl;
			return oscSender.setup(ipAddress, ofToInt(port));
		}
		else
		{
			cout << "Starting OSC output failed -- ipAddress/port not valid: " << ipAddress << "/" << port << endl;
			return false;
		}
	}
	catch (exception e) 
	{
		cout << "OSC output setup failed" << endl;
		return false;
	}

	return false;
}

bool ofApp2::startUdpOutput()
{
	udpPatchboard.loadFile(ofToDataPath(udpOutputSettingsFile));
	try
	{
		string xml;
		udpPatchboard.patchboard.copyXmlToString(xml);
		cout << xml << endl;
		string ipAddress = udpPatchboard.patchboard.getValue("patchboard:settings:output:ipAddress", "");
		string port = udpPatchboard.patchboard.getValue("patchboard:settings:output:port", "");
		if (ipAddress != "" && port != "")
		{
			cout << "Starting UDP output: " << ipAddress << "," << ofToInt(port) << endl;
			ofxUDPSettings settings;
			settings.sendTo(ipAddress.c_str(), ofToInt(port));
			settings.blocking = false;
			return udpSender.Setup(settings);
		}
		else
		{
			cout << "Starting UDP output failed -- ipAddress/port not valid: " << ipAddress << "/" << port << endl;
			return false;
		}
	}
	catch (exception e) 
	{
		cout << "UDP output setup failed" << endl;
		return false;
	}

	return false;
}
#pragma endregion

#pragma region NewGuiStuff

void ofApp2::setup(){
	ofLogToConsole();
	ofSetLoggerChannel(std::make_shared<FileLogger>("log.txt"));
#ifdef TARGET_MAC_OS
	ofSetDataPathRoot("../Resources/");
	cout << "Changed the data pathroot for macOS." << endl;
#endif
	ofSetFrameRate(30);
	//ofBackground(255, 255, 255);
	//SoftwareVersionChecker::checkLatestVersion();
	ofSetLogLevel(OF_LOG_NOTICE);
	setTypeTagPlotAttributes2();

	string commSettings = loadTextFile(commSettingsFile);
	emotiBitWiFi.parseCommSettings(commSettings);

	emotiBitWiFi.begin2();
	timeWindowOnSetup = 10;

	logData = true;
	logConsole = true;
	dataLogger.setFilename("dataLog.txt");
	if (logData)
	{
		dataLogger.startThread();
	}
	consoleLogger.setFilename("consoleLog.txt");
	if (logConsole)
	{
		consoleLogger.startThread();
	}

	newGui.setup();
	setupOscilloscopes2();

	selectedTimeSlot = 5;
	customTimeSlot = 5;
	testCount = 0;
	discoveredDevices = emotiBitWiFi.getdiscoveredEmotibits();

	// set log level to FATAL_ERROR to remove unrelated LSL error overflow in the console
	//ofSetLogLevel(OF_LOG_FATAL_ERROR);
}

void ofApp2::setupOscilloscopes2()
{
	// read the patchboard file
	if (patchboard.loadFile(ofToDataPath("inputSettings.xml")))
	{
		ofLog(OF_LOG_NOTICE, "PatchBoard succesfully loaded");
	}
	else
	{
		ofLog(OF_LOG_NOTICE, "PatchBoard File Not Found!");
		while (1);
	}
	ofFile scopeSettingsFile(ofToDataPath("ofxOscilloscopeSettings.xml"));
	// check if oscilloscope settings file exists
	if (scopeSettingsFile.exists())
	{
		scopeWins0 = ofxMultiScope::loadScopeSettings();
		scopeWins1 = ofxMultiScope::loadScopeSettings();
		scopeWins2 = ofxMultiScope::loadScopeSettings();
		scopeWins3 = ofxMultiScope::loadScopeSettings();
		//This assumes that the width/height of both multiscopes is the same
		for (int i = 0; i < scopeWins0.size(); i++)
		{
			auto position = scopeWins0[i].getPosition();
			auto position2 = position;
			position.setX(position.getX() + position.getWidth() * 2);
			scopeWins1[i].setPosition(position);

			position2.setY(position2.getY() + position2.getHeight());
			scopeWins2[i].setPosition(position2);

			position.setY(position2.getY());
			scopeWins3[i].setPosition(position);
		}
	}
	else
	{
		ofLog(OF_LOG_NOTICE, "Scope Settings File Not Found!");
		while (1);
	}
	plotIds0 = ofxMultiScope::getPlotIds();
	plotIds1 = ofxMultiScope::getPlotIds();
	plotIds2 = ofxMultiScope::getPlotIds();
	plotIds3 = ofxMultiScope::getPlotIds();
	updatePlotAttributeLists2();
	updateTypeTagList2();
	initMetaDataBuffers2();
}

void ofApp2::setTypeTagPlotAttributes2()
{
	// ToDo: Add attributes for all streams for refactor
	// Note:: THERM plot attributes only live in code. we should move it to the XML file some day
	{
		// Add plot attributes for THERMOPILE data
		typeTagPlotAttr attr;
		attr.plotName = "THERM";
		attr.plotColor = ofColor(239, 97, 82);
		// ToDo: someday, we can even consider a standard layout considering even plot number p -> {w,s,p}
		std::vector<int> sIdx = { 1, 3 };  // {window, scope}
		attr.scopeIdx = sIdx;
		typeTagPlotAttributes0.emplace(EmotiBitPacket::TypeTag::THERMOPILE, attr);
		typeTagPlotAttributes1.emplace(EmotiBitPacket::TypeTag::THERMOPILE, attr);
		typeTagPlotAttributes2.emplace(EmotiBitPacket::TypeTag::THERMOPILE, attr);
		typeTagPlotAttributes3.emplace(EmotiBitPacket::TypeTag::THERMOPILE, attr);
	}

	{
		// Plot Attributes for TEMP1 data
		typeTagPlotAttr attr;
		attr.plotName = "TEMP1";
		attr.plotColor = ofColor(234, 174, 68);
		std::vector<int> sIdx = { 1, 3 };  // {window, scope}
		attr.scopeIdx = sIdx;
		typeTagPlotAttributes0.emplace(EmotiBitPacket::TypeTag::TEMPERATURE_1, attr);
		typeTagPlotAttributes1.emplace(EmotiBitPacket::TypeTag::TEMPERATURE_1, attr);
		typeTagPlotAttributes2.emplace(EmotiBitPacket::TypeTag::TEMPERATURE_1, attr);
		typeTagPlotAttributes3.emplace(EmotiBitPacket::TypeTag::TEMPERATURE_1, attr);
	}
}
void ofApp2::updatePlotAttributeLists2(std::string settingsFile)
{
	//same as updatePlotAttributeList but for the variables variableName0-3 (e.g. samplingFreqs0, samplingFreqs1, samlingFreqs2, samlingFreqs3 instead of only samplingFreqs
	ofxXmlSettings scopeSettings;
	scopeSettings.loadFile(settingsFile);

	int nMultiScopes = scopeSettings.getNumTags("multiScope");
	samplingFreqs0.resize(nMultiScopes);
	minYSpans0.resize(nMultiScopes);
	plotNames0.resize(nMultiScopes);
	plotColors0.resize(nMultiScopes);
	yLims0.resize(nMultiScopes);
	for (int m = 0; m < nMultiScopes; m++)
	{
		scopeSettings.pushTag("multiScope", m);
		int nScopes = scopeSettings.getNumTags("scope");
		samplingFreqs0.at(m).resize(nScopes);
		minYSpans0.at(m).resize(nScopes);
		plotNames0.at(m).resize(nScopes);
		plotColors0.at(m).resize(nScopes);
		yLims0.at(m).resize(nScopes);
		for (int s = 0; s < nScopes; s++)
		{
			scopeSettings.pushTag("scope", s);
			//float timeWindow = scopeSettings.getValue("timeWindow", 15.f); // maybe we keep this.
			float samplingFrequency = scopeSettings.getValue("samplingFrequency", 15.f);
			float minYSpan = scopeSettings.getValue("minYSpan", 0.f);
			float yMin = scopeSettings.getValue("yMin", 0.f);
			float yMax = scopeSettings.getValue("yMax", 0.f);
			samplingFreqs0.at(m).at(s) = samplingFrequency;
			minYSpans0.at(m).at(s) = minYSpan;
			vector<float> yLim = { yMin, yMax };
			yLims0.at(m).at(s) = yLim;
			//samplingFreqs-2D vector
			//minYSpans-2D vector
			//plotNames-3D vector
			//plotColors-3D vector
			//yLims-3D vector
			int nPlots = scopeSettings.getNumTags("plot");
			for (int p = 0; p < nPlots; p++) {
				scopeSettings.pushTag("plot", p);
				plotNames0.at(m).at(s).push_back(scopeSettings.getValue("plotName", "N/A"));
				scopeSettings.pushTag("plotColor");
				plotColors0.at(m).at(s).push_back(ofColor(
					scopeSettings.getValue("r", 255),
					scopeSettings.getValue("g", 255),
					scopeSettings.getValue("b", 255)
				));
				scopeSettings.popTag(); // plotColor
				scopeSettings.popTag(); // plot p
			}
			scopeSettings.popTag(); // scope s
		}

		scopeSettings.popTag(); // multiScope m
	}

	scopeSettings.loadFile(settingsFile);
	nMultiScopes = scopeSettings.getNumTags("multiScope");
	samplingFreqs1.resize(nMultiScopes);
	minYSpans1.resize(nMultiScopes);
	plotNames1.resize(nMultiScopes);
	plotColors1.resize(nMultiScopes);
	yLims1.resize(nMultiScopes);
	for (int m = 0; m < nMultiScopes; m++)
	{
		scopeSettings.pushTag("multiScope", m);
		int nScopes = scopeSettings.getNumTags("scope");
		samplingFreqs1.at(m).resize(nScopes);
		minYSpans1.at(m).resize(nScopes);
		plotNames1.at(m).resize(nScopes);
		plotColors1.at(m).resize(nScopes);
		yLims1.at(m).resize(nScopes);
		for (int s = 0; s < nScopes; s++)
		{
			scopeSettings.pushTag("scope", s);
			//float timeWindow = scopeSettings.getValue("timeWindow", 15.f); // maybe we keep this.
			float samplingFrequency = scopeSettings.getValue("samplingFrequency", 15.f);
			float minYSpan = scopeSettings.getValue("minYSpan", 0.f);
			float yMin = scopeSettings.getValue("yMin", 0.f);
			float yMax = scopeSettings.getValue("yMax", 0.f);
			samplingFreqs1.at(m).at(s) = samplingFrequency;
			minYSpans1.at(m).at(s) = minYSpan;
			vector<float> yLim = { yMin, yMax };
			yLims1.at(m).at(s) = yLim;
			//samplingFreqs-2D vector
			//minYSpans-2D vector
			//plotNames-3D vector
			//plotColors-3D vector
			//yLims-3D vector
			int nPlots = scopeSettings.getNumTags("plot");
			for (int p = 0; p < nPlots; p++) {
				scopeSettings.pushTag("plot", p);
				plotNames1.at(m).at(s).push_back(scopeSettings.getValue("plotName", "N/A"));
				scopeSettings.pushTag("plotColor");
				plotColors1.at(m).at(s).push_back(ofColor(
					scopeSettings.getValue("r", 255),
					scopeSettings.getValue("g", 255),
					scopeSettings.getValue("b", 255)
				));
				scopeSettings.popTag(); // plotColor
				scopeSettings.popTag(); // plot p
			}
			scopeSettings.popTag(); // scope s
		}

		scopeSettings.popTag(); // multiScope m
	}

	scopeSettings.loadFile(settingsFile);
	nMultiScopes = scopeSettings.getNumTags("multiScope");
	samplingFreqs2.resize(nMultiScopes);
	minYSpans2.resize(nMultiScopes);
	plotNames2.resize(nMultiScopes);
	plotColors2.resize(nMultiScopes);
	yLims2.resize(nMultiScopes);
	for (int m = 0; m < nMultiScopes; m++)
	{
		scopeSettings.pushTag("multiScope", m);
		int nScopes = scopeSettings.getNumTags("scope");
		samplingFreqs2.at(m).resize(nScopes);
		minYSpans2.at(m).resize(nScopes);
		plotNames2.at(m).resize(nScopes);
		plotColors2.at(m).resize(nScopes);
		yLims2.at(m).resize(nScopes);
		for (int s = 0; s < nScopes; s++)
		{
			scopeSettings.pushTag("scope", s);
			float samplingFrequency = scopeSettings.getValue("samplingFrequency", 15.f);
			float minYSpan = scopeSettings.getValue("minYSpan", 0.f);
			float yMin = scopeSettings.getValue("yMin", 0.f);
			float yMax = scopeSettings.getValue("yMax", 0.f);
			samplingFreqs2.at(m).at(s) = samplingFrequency;
			minYSpans2.at(m).at(s) = minYSpan;
			vector<float> yLim = { yMin, yMax };
			yLims2.at(m).at(s) = yLim;
			int nPlots = scopeSettings.getNumTags("plot");
			for (int p = 0; p < nPlots; p++) {
				scopeSettings.pushTag("plot", p);
				plotNames2.at(m).at(s).push_back(scopeSettings.getValue("plotName", "N/A"));
				scopeSettings.pushTag("plotColor");
				plotColors2.at(m).at(s).push_back(ofColor(
					scopeSettings.getValue("r", 255),
					scopeSettings.getValue("g", 255),
					scopeSettings.getValue("b", 255)
				));
				scopeSettings.popTag(); // plotColor
				scopeSettings.popTag(); // plot p
			}
			scopeSettings.popTag(); // scope s
		}
		scopeSettings.popTag(); // multiScope m
	}

	scopeSettings.loadFile(settingsFile);
	nMultiScopes = scopeSettings.getNumTags("multiScope");
	samplingFreqs3.resize(nMultiScopes);
	minYSpans3.resize(nMultiScopes);
	plotNames3.resize(nMultiScopes);
	plotColors3.resize(nMultiScopes);
	yLims3.resize(nMultiScopes);
	for (int m = 0; m < nMultiScopes; m++)
	{
		scopeSettings.pushTag("multiScope", m);
		int nScopes = scopeSettings.getNumTags("scope");
		samplingFreqs3.at(m).resize(nScopes);
		minYSpans3.at(m).resize(nScopes);
		plotNames3.at(m).resize(nScopes);
		plotColors3.at(m).resize(nScopes);
		yLims3.at(m).resize(nScopes);
		for (int s = 0; s < nScopes; s++)
		{
			scopeSettings.pushTag("scope", s);
			//float timeWindow = scopeSettings.getValue("timeWindow", 15.f); // maybe we keep this.
			float samplingFrequency = scopeSettings.getValue("samplingFrequency", 15.f);
			float minYSpan = scopeSettings.getValue("minYSpan", 0.f);
			float yMin = scopeSettings.getValue("yMin", 0.f);
			float yMax = scopeSettings.getValue("yMax", 0.f);
			samplingFreqs3.at(m).at(s) = samplingFrequency;
			minYSpans3.at(m).at(s) = minYSpan;
			vector<float> yLim = { yMin, yMax };
			yLims3.at(m).at(s) = yLim;
			//samplingFreqs-2D vector
			//minYSpans-2D vector
			//plotNames-3D vector
			//plotColors-3D vector
			//yLims-3D vector
			int nPlots = scopeSettings.getNumTags("plot");
			for (int p = 0; p < nPlots; p++) {
				scopeSettings.pushTag("plot", p);
				plotNames3.at(m).at(s).push_back(scopeSettings.getValue("plotName", "N/A"));
				scopeSettings.pushTag("plotColor");
				plotColors3.at(m).at(s).push_back(ofColor(
					scopeSettings.getValue("r", 255),
					scopeSettings.getValue("g", 255),
					scopeSettings.getValue("b", 255)
				));
				scopeSettings.popTag(); // plotColor
				scopeSettings.popTag(); // plot p
			}
			scopeSettings.popTag(); // scope s
		}

		scopeSettings.popTag(); // multiScope m
	}
}
void ofApp2::updateTypeTagList2()
{
	//same as updateTypeTagList but for 4 instances (e.g. plotIds0-3 instead of only plotIds)

	for (int i = 0; i < plotIds0.size(); i++)// for multiscopes
	{
		vector<vector<std::string>> scopeTypeTagList;
		for (int j = 0; j < plotIds0.at(i).size(); j++) // for scopes
		{
			vector<std::string> plotTypeTagList;
			for (int k = 0; k < plotIds0.at(i).at(j).size(); k++) // for plots
			{
				for (auto key = patchboard.patchcords.begin(); key != patchboard.patchcords.end(); key++)
				{
					// for each plot plotId, get the typeTag
					// ToDo: there should be a loop here to go through all map values for a key. In case, the same signal is patched to multiple scopes
					if (ofToInt(key->second.back()) == plotIds0.at(i).at(j).at(k))
					{
						plotTypeTagList.push_back(key->first);
					}
				}
			}
			scopeTypeTagList.push_back(plotTypeTagList);
		}
		typeTags0.push_back(scopeTypeTagList);
	}

	// Create an index mapping for each type tag
	for (int w = 0; w < typeTags0.size(); w++) {
		for (int s = 0; s < typeTags0.at(w).size(); s++) {
			for (int p = 0; p < typeTags0.at(w).at(s).size(); p++) {
				vector<int> indexes{ w, s, p };
				typeTagIndexes0.emplace(typeTags0.at(w).at(s).at(p), indexes);
			}
		}
	}

	for (int i = 0; i < plotIds1.size(); i++)// for multiscopes
	{
		vector<vector<std::string>> scopeTypeTagList;
		for (int j = 0; j < plotIds1.at(i).size(); j++) // for scopes
		{
			vector<std::string> plotTypeTagList;
			for (int k = 0; k < plotIds1.at(i).at(j).size(); k++) // for plots
			{
				for (auto key = patchboard.patchcords.begin(); key != patchboard.patchcords.end(); key++)
				{
					// for each plot plotId, get the typeTag
					// ToDo: there should be a loop here to go through all map values for a key. In case, the same signal is patched to multiple scopes
					if (ofToInt(key->second.back()) == plotIds1.at(i).at(j).at(k))
					{
						plotTypeTagList.push_back(key->first);
					}
				}
			}
			scopeTypeTagList.push_back(plotTypeTagList);
		}
		typeTags1.push_back(scopeTypeTagList);
	}

	// Create an index mapping for each type tag
	for (int w = 0; w < typeTags1.size(); w++) {
		for (int s = 0; s < typeTags1.at(w).size(); s++) {
			for (int p = 0; p < typeTags1.at(w).at(s).size(); p++) {
				vector<int> indexes{ w, s, p };
				typeTagIndexes1.emplace(typeTags1.at(w).at(s).at(p), indexes);
			}
		}
	}

	for (int i = 0; i < plotIds2.size(); i++)// for multiscopes
	{
		vector<vector<std::string>> scopeTypeTagList;
		for (int j = 0; j < plotIds2.at(i).size(); j++) // for scopes
		{
			vector<std::string> plotTypeTagList;
			for (int k = 0; k < plotIds2.at(i).at(j).size(); k++) // for plots
			{
				for (auto key = patchboard.patchcords.begin(); key != patchboard.patchcords.end(); key++)
				{
					// for each plot plotId, get the typeTag
					// ToDo: there should be a loop here to go through all map values for a key. In case, the same signal is patched to multiple scopes
					if (ofToInt(key->second.back()) == plotIds2.at(i).at(j).at(k))
					{
						plotTypeTagList.push_back(key->first);
					}
				}
			}
			scopeTypeTagList.push_back(plotTypeTagList);
		}
		typeTags2.push_back(scopeTypeTagList);
	}

	// Create an index mapping for each type tag
	for (int w = 0; w < typeTags2.size(); w++) {
		for (int s = 0; s < typeTags2.at(w).size(); s++) {
			for (int p = 0; p < typeTags2.at(w).at(s).size(); p++) {
				vector<int> indexes{ w, s, p };
				typeTagIndexes2.emplace(typeTags2.at(w).at(s).at(p), indexes);
			}
		}
	}

	for (int i = 0; i < plotIds3.size(); i++)// for multiscopes
	{
		vector<vector<std::string>> scopeTypeTagList;
		for (int j = 0; j < plotIds3.at(i).size(); j++) // for scopes
		{
			vector<std::string> plotTypeTagList;
			for (int k = 0; k < plotIds3.at(i).at(j).size(); k++) // for plots
			{
				for (auto key = patchboard.patchcords.begin(); key != patchboard.patchcords.end(); key++)
				{
					// for each plot plotId, get the typeTag
					// ToDo: there should be a loop here to go through all map values for a key. In case, the same signal is patched to multiple scopes
					if (ofToInt(key->second.back()) == plotIds3.at(i).at(j).at(k))
					{
						plotTypeTagList.push_back(key->first);
					}
				}
			}
			scopeTypeTagList.push_back(plotTypeTagList);
		}
		typeTags3.push_back(scopeTypeTagList);
	}

	// Create an index mapping for each type tag
	for (int w = 0; w < typeTags3.size(); w++) {
		for (int s = 0; s < typeTags3.at(w).size(); s++) {
			for (int p = 0; p < typeTags3.at(w).at(s).size(); p++) {
				vector<int> indexes{ w, s, p };
				typeTagIndexes3.emplace(typeTags3.at(w).at(s).at(p), indexes);
			}
		}
	}
}
void ofApp2::initMetaDataBuffers2()
{
	//should work the same as initMetaDataBuffers but for 4 instances instead of 1
	bufferSizes0 = initBuffer2(bufferSizes0,0);
	dataCounts0 = initBuffer2(dataCounts0,0);
	dataFreqs0 = initBuffer2(dataFreqs0,0);
	bufferSizes1 = initBuffer2(bufferSizes1, 1);
	dataCounts1 = initBuffer2(dataCounts1, 1);
	dataFreqs1 = initBuffer2(dataFreqs1, 1);
	bufferSizes2 = initBuffer2(bufferSizes2, 2);
	dataCounts2 = initBuffer2(dataCounts2, 2);
	dataFreqs2 = initBuffer2(dataFreqs2, 2);
	bufferSizes3 = initBuffer2(bufferSizes3, 3);
	dataCounts3 = initBuffer2(dataCounts3, 3);
	dataFreqs3 = initBuffer2(dataFreqs3, 3);
}
template <class T2>
vector<vector<vector<T2>>> ofApp2::initBuffer2(vector<vector<vector<T2>>> buffer,int index) {
	//should work the same as initBuffers but for 4 instances instead of 1 (searches or the right pointer by index (since only 4 instances of the necessary variables should exist)
	vector<vector<vector<string>>>* typeTagsIndexPointer;
	switch (index)
	{
		case 0: typeTagsIndexPointer = &typeTags0; break;
		case 1: typeTagsIndexPointer = &typeTags1; break;
		case 2: typeTagsIndexPointer = &typeTags2; break;
		case 3: typeTagsIndexPointer = &typeTags3; break;
		default: return buffer;
	}
	buffer.resize((*typeTagsIndexPointer).size());
	for (int w = 0; w < (*typeTagsIndexPointer).size(); w++) {
		buffer.at(w).resize((*typeTagsIndexPointer).at(w).size());
		for (int s = 0; s < (*typeTagsIndexPointer).at(w).size(); s++) {
			buffer.at(w).at(s).resize((*typeTagsIndexPointer).at(w).at(s).size());
			for (int p = 0; p < (*typeTagsIndexPointer).at(w).at(s).size(); p++) {
				buffer.at(w).at(s).at(p) = 0;
			}
		}
	}
	return buffer;
}

void ofApp2::draw() {
	newGui.begin();
	drawNewGui();
	newGui.end();
}

void ofApp2::update()
{
	/*
	if (recordButtonPressedNew)
	{
		for(auto it = discoveredDevices.begin(); it != discoveredDevices.end(); it++)
		{
			string deviceId = it->first;
			it->second.isRecording = true;
		}
		recordButtonPressedNew = false;
	}
	*/
	//emotiBitWiFi.processAdvertisingThread();
	discoveredDevices = emotiBitWiFi.getDiscoveredEmotibitsPointer();
	unordered_map<string, vector<string>> devicePackets = emotiBitWiFi.getAllDeviceDataPackets();


	for (auto& entry : devicePackets)
	{
		string deviceId = entry.first;
		for (auto& packet : entry.second)
		{
			vector<string> splitPacket = ofSplitString(packet, ",");
			processSlowResponseMessage2(deviceId, splitPacket);
			if (logData)
			{
				dataLogger.push(packet + "\n");
			}
		}
	}
	
	updateDeviceList2();
}

void ofApp2::drawNewGui() 
{
	//Left panel: device list
	if (ImGui::Begin("Devices", nullptr, ImGuiWindowFlags_NoCollapse)){

		ImGui::BeginChild("Header", ImVec2(columnWidth * 7, 30), true, ImGuiWindowFlags_NoScrollbar);
		{
			ImGui::SetCursorPosX(0);
			centeredText("Device names", columnWidth);
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth);
			centeredText("Battery", columnWidth);
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth*2);
			if (ImGui::Button("PowerMode", ImVec2(columnWidth, 0)))
			{
				ImGui::OpenPopup("PowerModeDropDown");
			}
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth*3);
			centeredText("Clipping count", columnWidth);
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth * 4);
			centeredText("Overflow count", columnWidth);
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth * 5);
			centeredText("SD File", columnWidth);
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth * 6);
			centeredText("Connect to EmotiBit", columnWidth);
			
		}
		
		if (ImGui::BeginPopup("PowerModeDropDown"))
		{
			//TODO: Set selected power mode to selected emotibits
			if (ImGui::Selectable(GUI_STRING_HIBERNATE))
			{
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::Selectable(GUI_STRING_LOW_POWER))
			{
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::Selectable(GUI_STRING_NORMAL_POWER))
			{
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::Selectable(GUI_STRING_WIRELESS_OFF))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::EndChild();
		
		//Select all button
		if (ImGui::Button("Select All"))
		{
			for (auto& device : discoveredDevices)
			{
				device.second.isSelected = !device.second.isSelected;
			}
		}
		ImGui::Separator();

		//list devices
		ImGui::BeginChild("Device List", ImVec2(columnWidth * 7, 0), false);

		for (auto it = discoveredDevices.begin(); it != discoveredDevices.end(); it++)
		{
			
			string deviceId = it->first;
			EmotibitInfo& deviceInfo = it->second;
			
			ImGui::PushID(deviceId.c_str());
			uniqueIds.insert(deviceId);

			ImGui::SetCursorPosX(0);
			if (ImGui::Checkbox(deviceId.c_str(), &deviceInfo.isSelected))
			{

			}
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth);
			centeredText(("%s", deviceInfo.currentBatteryStatus).c_str(), columnWidth);
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth * 2);
			centeredText(("%s", stringifyPowerMode(deviceInfo.currentPowerMode)), columnWidth);
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth * 3);
			std::stringstream stc;
			stc << deviceInfo.clippingCount;
			centeredText(stc.str(), columnWidth);
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth * 4);
			std::stringstream sto;
			sto << deviceInfo.overflowCount;
			centeredText(sto.str(), columnWidth);
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth * 5);
			centeredText(("%s", deviceInfo.isRecording ? ("Recording to %s",deviceInfo.recordingFileName) : "Idle"), columnWidth);
			ImGui::SameLine();

			ImGui::SetCursorPosX(columnWidth * 6);
			if (ImGui::Checkbox("Connected?", &deviceInfo.userWantsToConnect))
			{
				
			}
			if (!deviceInfo.isConnected && deviceInfo.userWantsToConnect)
			{
				connectToDevice(deviceId);
				emotiBitWiFi.pauseDataReception(deviceId, false);
				testCount = 1;
			} 
			else if (deviceInfo.isConnected && !deviceInfo.userWantsToConnect)
			{
				emotiBitWiFi.pauseDataReception(deviceId, true);
				deviceInfo.isConnected = false;
				testCount = 2;
			}
			else if (!deviceInfo.isConnected && !deviceInfo.userWantsToConnect)
			{
				testCount = 3;
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::End();	
	}

	//right panel: controls & oscilloscope
	if (ImGui::Begin("Recording Control", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		//time slot selection
		ImGui::Text("Time Slot Selection (mins):");
		if (ImGui::RadioButton("5", selectedTimeSlot == 5)) { selectedTimeSlot = 5; }
		ImGui::SameLine();
		if (ImGui::RadioButton("10", selectedTimeSlot == 10)) { selectedTimeSlot = 10; }
		ImGui::SameLine();
		if (ImGui::RadioButton("15", selectedTimeSlot == 15)) { selectedTimeSlot = 15; }
		ImGui::SameLine();
		if (ImGui::RadioButton("Custom", selectedTimeSlot == 0)) { selectedTimeSlot = 0; }
		if (selectedTimeSlot == 0) {
			ImGui::InputInt("Custom time slot (mins)", &customTimeSlot);
		}
		else
		{
			ImGui::Text("Selected time slot == %d",selectedTimeSlot);
		}
		
		//Record button
		if (ImGui::Button("Record")) 
		{
			recordButtonPressedNew = true;
			//TODO: Trigger recording on all devices
		}
		ImGui::End();
	}

	if (ImGui::Begin("Oscilloscope Control", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		float width = 220.f;
		static std::vector<int> selectedIndices(4, -1);
		std::vector<std::string> names;
		for (const auto& pair : discoveredDevices)
		{
			names.push_back(pair.first);
		}

		//Draws 4 dropdowns to choose which device to plot for which plot j
		for (int j = 0; j < 4; j++)
		{
			ImGui::PushID(j);
			std::string comboLabel = "##DropDown" + std::to_string(j);
			std::string selectionDefaultLabel = "Select EmotiBit for plot " + std::to_string(j+1);
			std::string currentSelection = (selectedIndices[j] >= 0) ? names[selectedIndices[j]] : selectionDefaultLabel;

			ImGui::SetCursorPosX((j%2)*width);
			ImGui::SetNextItemWidth(width);
			if (ImGui::BeginCombo(comboLabel.c_str(), currentSelection.c_str()))
			{
				int index = 0;
				for (auto it = discoveredDevices.begin(); it != discoveredDevices.end(); it++)
				{
					if (selectedIndices[j] != index && it->second.showPlot)
					{
						index++;
						continue;
					}
					string deviceId = it->first;
					bool isSelected = (selectedIndices[j] == index);
					if (ImGui::Selectable(deviceId.c_str(), isSelected))
					{
						if (selectedIndices[j] != index)
						{
							//If a different emotibit in this dropdown was selected, take name from name list and turn off plot showing flag, change oscilloscopeIndex to -1 (which means this device will not be plotted to oscilloscope 1,2,3 or 4)
							if (selectedIndices[j] != -1)
							{
								const std::string& oldSelection = names[selectedIndices[j]];
								discoveredDevices[oldSelection].showPlot = false;
								oscilloscopeIndex[oldSelection] = -1;
								clearOscilloscopes(j);
							}
							it->second.showPlot = true;
							selectedIndices[j] = index;
							oscilloscopeIndex[deviceId] = j;
						}
						else
						{
							//Toggling same emotibit
							it->second.showPlot = false;
							selectedIndices[j] = -1;
							oscilloscopeIndex[deviceId] = -1;
							clearOscilloscopes(j);
						}
					}
					if (isSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
					index++;	
				}
				ImGui::EndCombo();
			}
			if (j == 0 || j == 2) ImGui::SameLine();
			ImGui::PopID();
			
		}
		drawOscilloscopes2();

		ImGui::End();
	}

	if (ImGui::Begin("Test Case Control", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::Text("We are in case: %d", testCount);
		for (const auto& pair : discoveredDevices)
		{
			string deviceId = pair.first;
			ImGui::Text("This is the deviceInfo for %s: isAvailable = %s, isConnected = %s, isRecording = %s, isSelected = %s, Shows Plot = %s, UserWantsToConnect = %s", deviceId.c_str(),
				pair.second.isAvailable ? "True" : "False",
				pair.second.isConnected ? "True" : "False",
				pair.second.isRecording ? "True" : "False",
				pair.second.isSelected ? "True" : "False",
				pair.second.showPlot ? "True" : "False",
				pair.second.userWantsToConnect ? "True" : "False");
		}
		for (const auto& pair : oscilloscopeIndex)
		{
			string deviceId = pair.first;
			ImGui::Text("Device %s should have plot %d", deviceId.c_str(), pair.second);
		}
		ImGui::Text("Found packet of type: %s", testString.c_str());
		ImGui::End();
	}
	

}

void ofApp2::drawOscilloscopes2()
{
	for (int i = 0; i < scopeWins0.size(); i++)
	{
		scopeWins0[i].plot();
	}
	for (int i = 0; i < scopeWins1.size(); i++)
	{
		scopeWins1[i].plot();
	}
	for (int i = 0; i < scopeWins2.size(); i++)
	{
		scopeWins2[i].plot();
	}
	for (int i = 0; i < scopeWins3.size(); i++)
	{
		scopeWins3[i].plot();
	}
}

void ofApp2::connectToDevice(const string& deviceId)
{
	auto it = discoveredDevices.find(deviceId);
	if (it == discoveredDevices.end())
	{
		return;
	}
	if (!it->second.isConnected)
	{
		int8_t success = emotiBitWiFi.connect2(deviceId);
		if (success > -1) it->second.isConnected = true;
		ofLogNotice("ofApp2") << "Connection was established = " << it->second.isConnected;
	}

}

void ofApp2::clearOscilloscopes(int indexToClear)
{
	ofLogNotice("ofApp2") << "Clearing of oscilloscopes was called" << endl;
	vector<ofxMultiScope>* oscilloscopeToClear = nullptr;
	switch (indexToClear)
	{
		case 0: oscilloscopeToClear = &scopeWins0; break;
		case 1: oscilloscopeToClear = &scopeWins1; break;
		case 2: oscilloscopeToClear = &scopeWins2; break;
		case 3: oscilloscopeToClear = &scopeWins3; break;
		default: return;
	}
	for (int i = 0; i < (*oscilloscopeToClear).size(); i++)
	{
		(*oscilloscopeToClear).at(i).clearData();
	}

	//TODO: Implement the rest of clear oscilloscope i.e. removeDataStream 
	
}

void ofApp2::updateDeviceList2()
{
	//Iterate over discovered emotibits
	for (auto it = discoveredDevices.begin(); it != discoveredDevices.end(); it++)
	{
		string deviceId = it->first;
		EmotibitInfo _emotiBitInfo = it->second;

		//Add an entry to oscllioscopeindex (which holds the index of the plot to which the device should send data, -1 for no plot
		if (oscilloscopeIndex.find(deviceId) == oscilloscopeIndex.end())
		{
			oscilloscopeIndex[deviceId] = -1;
		}
	}

}

void ofApp2::processSlowResponseMessage2(const string& deviceId, const vector<string>& splitPacket)
{
	//Lookup the EmotiBitInfo for this device
	auto it = discoveredDevices.find(deviceId);
	if (it == discoveredDevices.end()){return;}

	auto itPlotIndex = oscilloscopeIndex.find(deviceId);
	if (itPlotIndex == oscilloscopeIndex.end()){return;}

	vector<ofxMultiScope>* oscilloscopeToPlotTo = nullptr;
	vector<vector<vector<int>>>* bufferSizesOfPlotI = nullptr;
	vector<vector<vector<int>>>* dataCountsOfPlotI = nullptr;
	vector<vector<float>>* minYSpansOfPlotI = nullptr;
	vector<vector<vector<float>>>* yLimsOfPlotI = nullptr;
	unordered_map<string, vector<int>>* typeTagIndexesOfPlotI = nullptr;
	vector<vector<vector<string>>>* typeTagsOfPlotI = nullptr;
	switch (itPlotIndex->second)
	{
		case 0: oscilloscopeToPlotTo = &scopeWins0; bufferSizesOfPlotI = &bufferSizes0; dataCountsOfPlotI = &dataCounts0; minYSpansOfPlotI = &minYSpans0; yLimsOfPlotI = &yLims0; typeTagIndexesOfPlotI = &typeTagIndexes0; typeTagsOfPlotI = &typeTags0; break;
		case 1: oscilloscopeToPlotTo = &scopeWins1; bufferSizesOfPlotI = &bufferSizes1; dataCountsOfPlotI = &dataCounts1; minYSpansOfPlotI = &minYSpans1; yLimsOfPlotI = &yLims1; typeTagIndexesOfPlotI = &typeTagIndexes0; typeTagsOfPlotI = &typeTags0; break;
		case 2: oscilloscopeToPlotTo = &scopeWins2; bufferSizesOfPlotI = &bufferSizes2; dataCountsOfPlotI = &dataCounts2; minYSpansOfPlotI = &minYSpans2; yLimsOfPlotI = &yLims2; typeTagIndexesOfPlotI = &typeTagIndexes0; typeTagsOfPlotI = &typeTags0; break;
		case 3: oscilloscopeToPlotTo = &scopeWins3; bufferSizesOfPlotI = &bufferSizes3; dataCountsOfPlotI = &dataCounts3; minYSpansOfPlotI = &minYSpans3; yLimsOfPlotI = &yLims3; typeTagIndexesOfPlotI = &typeTagIndexes0; typeTagsOfPlotI = &typeTags0; break;
		default: return; //No oscilloscopeIndex assigned in OscilloscopeControl 
	}
	

	EmotibitInfo& deviceInfos = it->second;

	EmotiBitPacket::Header packetHeader;
	if (EmotiBitPacket::getHeader(splitPacket, packetHeader))
	{
		if (packetHeader.dataLength >= MAX_BUFFER_LENGTH)
		{
			bufferUnderruns++;
			cout << "**** POSSIBLE BUFFER UNDERRUN EVENT " << bufferUnderruns << ", " << packetHeader.dataLength << " ****" << endl;
		}
		testString = packetHeader.typeTag;
		ofLogNotice("ofApp2") << "Packet found of type " << packetHeader.typeTag << " from emotibit " << deviceId;
		//Add streams to plot if data is detected and not already added
		if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::THERMOPILE) == 0 && (*typeTagIndexesOfPlotI).find(EmotiBitPacket::TypeTag::THERMOPILE) == (*typeTagIndexesOfPlotI).end())
		{
			addDataStream(EmotiBitPacket::TypeTag::THERMOPILE);
		}
		if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::TEMPERATURE_1) == 0 && (*typeTagIndexesOfPlotI).find(EmotiBitPacket::TypeTag::TEMPERATURE_1) == (*typeTagIndexesOfPlotI).end())
		{
			addDataStream(EmotiBitPacket::TypeTag::TEMPERATURE_1);
		}

		auto indexPtr = (*typeTagIndexesOfPlotI).find(packetHeader.typeTag);
		if (indexPtr != (*typeTagIndexesOfPlotI).end())
		{
			vector<vector<float>> data;
			int w = indexPtr->second.at(0); //scope window index
			int s = indexPtr->second.at(1); // scope index
			int p = indexPtr->second.at(2); //plot index
			data.resize((*typeTagsOfPlotI).at(w).at(s).size());

			ofLogNotice("ofApp2:ProcessSlowResponseMessage2") << "Scope Window =  " << w << " and scope index = " << s << " and plot index = " << p;

			vector<string> oscAddresses;
			vector<ofxOscMessage> oscMessages;
			if (sendOsc)//Prepare Osc message
			{
				oscAddresses = oscPatchboard.patchcords[packetHeader.typeTag];
				oscMessages.resize(oscAddresses.size());
				for (int a = 0; a < oscAddresses.size();a++)
				{
					oscMessages.at(a).setAddress(oscAddresses.at(a));
				}
			}

			for (size_t n = EmotiBitPacket::headerLength; n < splitPacket.size(); n++)
			{
				float value = ofToFloat(splitPacket.at(n));
				data.at(p).push_back(value);

				if (sendLsl)
				{
					vector<float> lslSample(1, value);
					emotibitLsl.addSample(lslSample, packetHeader.typeTag, deviceId);
				}

				if (sendOsc)
				{
					for (int a = 0; a < oscMessages.size(); a++)
					{
						oscMessages.at(a).addFloatArg(value);
					}
				}
			}

			if (sendOsc)
			{
				for (int a = 0; a < oscMessages.size(); a++)
				{
					oscSender.sendMessage(oscMessages.at(a));
				}
			}

			if (!isPaused)
			{
				processAperiodicData2(deviceId, packetHeader.typeTag, data.at(p));
				bool isAperiodic = false;
				for (uint8_t i = 0; i < EmotiBitPacket::TypeTagGroups::NUM_APERIODIC; i++)
				{
					if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTagGroups::APERIODIC[i]) == 0)
					{
						isAperiodic = true;
						break;
					}
				}
				if (!isAperiodic)
				{
					ofLogNotice("ofApp2:ProcessSlowResponseMessage2") << "Data is not aperiodic and will be plotted to " << oscilloscopeIndex.find(deviceId) ->second;
					(*oscilloscopeToPlotTo).at(w).scopes.at(s).updateData(data);
				}
			}


			(*bufferSizesOfPlotI).at(w).at(s).at(p) = packetHeader.dataLength;
			(*dataCountsOfPlotI).at(w).at(s).at(p) += packetHeader.dataLength;

			//Special autscaling for EDA data
			if (!DEBUGGING && packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::EDA) == 0 && !data.at(p).empty())
			{
				(*minYSpansOfPlotI).at(w).at(s) = 0.1f * pow(data.at(p).at(0), 1.5f);
				if ((*yLimsOfPlotI).at(w).at(s).at(0) == (*yLimsOfPlotI).at(w).at(s).at(1))
				{
					(*oscilloscopeToPlotTo).at(w).scopes.at(s).autoscaleY(true, (*minYSpansOfPlotI).at(w).at(s));
				}
			}
		}
		else
		{
			/*
			ofLogNotice("PacketHeader") << "Header type: " << packetHeader.typeTag << " (length: " << packetHeader.typeTag.size() << ")";
			for (size_t i = 0; i < packetHeader.typeTag.size(); i++)
			{
				ofLogNotice("PacketHeaderTTBytes") << "PacketHeaderTypetag[" << i << "] = " << int(packetHeader.typeTag[i]);
			}
			*/
			//Process non plotting messages
			if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::BATTERY_VOLTAGE) == 0)
			{
				deviceInfos.currentBatteryStatus = splitPacket.at(6) + "V";
			}
			else if(packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::BATTERY_PERCENT) == 0)
			{
				deviceInfos.currentBatteryStatus = splitPacket.at(6) + "%";
			}
			else if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::EMOTIBIT_MODE) == 0)
			{
				processModePacket(deviceId, splitPacket);
			}
			else if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::DATA_CLIPPING) == 0)
			{
				for (size_t n = EmotiBitPacket::headerLength; n < splitPacket.size(); n++)
				{
					for (size_t w = 0; w < (*typeTagsOfPlotI).size(); w++)
					{
						for (size_t s = 0; s < (*typeTagsOfPlotI).at(w).size();s++)
						{
							for (size_t p = 0; p < (*typeTagsOfPlotI).at(w).at(s).size();p++)
							{
								if (splitPacket.at(n).compare((*typeTagsOfPlotI).at(w).at(s).at(p)) == 0)
								{
									deviceInfos.clippingCount++;
								}
							}
						}
					}
				}
			}
			else if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::DATA_OVERFLOW) == 0)
			{
				for (size_t n = EmotiBitPacket::headerLength; n < splitPacket.size(); n++)
				{
					for (size_t w = 0; w < (*typeTagsOfPlotI).size(); w++)
					{
						for (size_t s = 0; s < (*typeTagsOfPlotI).at(w).size();s++)
						{
							for (size_t p = 0; p < (*typeTagsOfPlotI).at(w).at(s).size();p++)
							{
								if (splitPacket.at(n).compare((*typeTagsOfPlotI).at(w).at(s).at(p)) == 0)
								{
									deviceInfos.overflowCount++;
								}
							}
						}
					}
				}
			}
			else if (packetHeader.typeTag.compare(EmotiBitPacket::TypeTag::RESET) == 0)
			{
				//TODO Handle reset behaviour
			}
		}
	}
}

void ofApp2::processAperiodicData2(const string& deviceId, std::string signalId, std::vector<float> data)
{
	auto it = oscilloscopeIndex.find(deviceId);
	if (it == oscilloscopeIndex.end()) return;

	vector<ofxMultiScope>* oscilloscopeToUpdate = nullptr;
	unordered_map<string, vector<int>>* typeTagIndexesForPlotI = nullptr;
	vector<vector<vector<string>>>* typeTagsForPlotI = nullptr;
	switch (it->second)
	{
	case 0: oscilloscopeToUpdate = &scopeWins0; typeTagIndexesForPlotI = &typeTagIndexes0; typeTagsForPlotI = &typeTags0 ;break;
	case 1: oscilloscopeToUpdate = &scopeWins1; typeTagIndexesForPlotI = &typeTagIndexes1; typeTagsForPlotI = &typeTags1 ;break;
	case 2: oscilloscopeToUpdate = &scopeWins2; typeTagIndexesForPlotI = &typeTagIndexes2; typeTagsForPlotI = &typeTags2 ;break;
	case 3: oscilloscopeToUpdate = &scopeWins3; typeTagIndexesForPlotI = &typeTagIndexes3; typeTagsForPlotI = &typeTags3 ;break;
	default: return;
	}


	std::vector<float> periodizedData; // cleared before update inside every update call
	for (int i = 0; i < periodizerList.size(); i++)
	{
		// update() returns size of data which needs to be added into the plot buffers
		if (periodizerList.at(i).update(signalId, data, periodizedData))
		{
			auto indexPtr = (*typeTagIndexesForPlotI).find(periodizerList.at(i).outputSignal);
			if (indexPtr != (*typeTagIndexesForPlotI).end())
			{
				int w = indexPtr->second.at(0); // Scope window(multiscope)
				int s = indexPtr->second.at(1); // Scope
				int p = indexPtr->second.at(2); // Plot
				std::vector<std::vector<float>> plotData;
				plotData.resize((*typeTagsForPlotI).at(w).at(s).size());
				plotData.at(p) = periodizedData;
				// Add data to oscilloscope
				(*oscilloscopeToUpdate).at(w).scopes.at(s).updateData(plotData);
			}
		}
	}
}

void ofApp2::processModePacket(const string& deviceId, vector<string> splitPacket)
{
	auto it = discoveredDevices.find(deviceId);
	if (it == discoveredDevices.end())
	{
		return;
	}
	EmotibitInfo& deviceInfos = it->second;

	size_t startIndex = EmotiBitPacket::headerLength;
	string value;

	int pos = EmotiBitPacket::getPacketKeyedValue(splitPacket, EmotiBitPacket::PayloadLabel::RECORDING_STATUS, value);
	if (pos > -1)
	{
		if (value.compare(EmotiBitPacket::TypeTag::RECORD_BEGIN) == 0)
		{
			discoveredDevices[deviceId].isRecording = true;
			// See if we got a filename for the file we're recording to
			if (pos + 1 < splitPacket.size())
			{
				string filename = splitPacket.at(pos + 1);
				if (filename.size() > 4 && filename.substr(filename.size() - 4, 4).compare(".csv") == 0)
				{
					_testingHelper.updateSdCardFilename(filename);
					discoveredDevices[deviceId].recordingFileName = filename;
				}
				else
				{
					discoveredDevices[deviceId].recordingFileName = GUI_STRING_RECORDING;
				}
			}
		}
		else if (value.compare(EmotiBitPacket::TypeTag::RECORD_END) == 0)
		{
			discoveredDevices[deviceId].isRecording = false;
		}
	}

	if (EmotiBitPacket::getPacketKeyedValue(splitPacket, EmotiBitPacket::PayloadLabel::POWER_STATUS, value) > -1)
	{
		value = value.substr(0, 2);
		
		if (value.compare(EmotiBitPacket::TypeTag::MODE_NORMAL_POWER) == 0)
		{
			discoveredDevices[deviceId].currentPowerMode = EmotibitInfo::PowerMode::NORMAL_POWER;
		}
		else if (value.compare(EmotiBitPacket::TypeTag::MODE_LOW_POWER) == 0)
		{
			discoveredDevices[deviceId].currentPowerMode = EmotibitInfo::PowerMode::LOW_POWER;
		}
		else if (value.compare(EmotiBitPacket::TypeTag::MODE_MAX_LOW_POWER) == 0)
		{
			discoveredDevices[deviceId].currentPowerMode = EmotibitInfo::PowerMode::MAX_LOW_POWER;
		}
		else if (value.compare(EmotiBitPacket::TypeTag::MODE_WIRELESS_OFF) == 0)
		{
			discoveredDevices[deviceId].currentPowerMode = EmotibitInfo::PowerMode::WIRELESS_OFF;
			emotiBitWiFi.disconnect2(deviceId);
		}
		else if (value.compare(EmotiBitPacket::TypeTag::MODE_HIBERNATE) == 0)
		{
			discoveredDevices[deviceId].currentPowerMode = EmotibitInfo::PowerMode::HIBERNATE;
			emotiBitWiFi.disconnect2(deviceId);
		}
		else
		{
			ofLogNotice("ofApp2") << "no comparison was valid" << endl;
		}
	}
}

void ofApp2::centeredText(const std::string& text, float columWidth) {
	ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
	float textOffsetX = (columWidth - textSize.x) * 0.5f;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textOffsetX);
	ImGui::Text("%s", text.c_str());
	};

bool ofApp2::uniqueIdUsed(string idToCheck)
{
	return (uniqueIds.find(idToCheck) != uniqueIds.end());
}

const char* ofApp2::stringifyPowerMode(EmotibitInfo::PowerMode modeToStringify)
{
	switch (modeToStringify) {
		case EmotibitInfo::PowerMode::HIBERNATE:
			return GUI_STRING_HIBERNATE;
			break;
		case EmotibitInfo::PowerMode::WIRELESS_OFF:
			return GUI_STRING_WIRELESS_OFF;
			break;
		case EmotibitInfo::PowerMode::MAX_LOW_POWER:
			return GUI_STRING_MAX_LOW_POWER;
			break;
		case EmotibitInfo::PowerMode::LOW_POWER:
			return GUI_STRING_LOW_POWER;
			break;
		case EmotibitInfo::PowerMode::NORMAL_POWER:
			return GUI_STRING_NORMAL_POWER;
			break;
		default:
			return GUI_STRING_UNKNOWN_MODE;
			break;
	}
}

#pragma endregion
