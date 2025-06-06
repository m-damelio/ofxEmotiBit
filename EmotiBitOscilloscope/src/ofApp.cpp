# include "ofApp.h"

void ofApp::setup() {
	ofSetLoggerChannel(std::make_shared<FileLogger>("log.txt"));
	ofSetLogLevel(OF_LOG_VERBOSE);
	gui.setup();
	wifiHost.begin();

	setupOscilloscopes();
	updatePlotAttributeLists();
	updateTypeTagList();
	initMetaDataBuffers();
}

void ofApp::update() {
	for (auto& kv : deviceScopes) {
		const auto& devId = kv.first;
		auto& scopes = kv.second;

		vector<string> multiPackets;
		wifiHost.readData(devId, multiPackets);

		map<pair<int, int>, vector<vector<float>>> scopeUpdates;

		for (auto& packets : multiPackets) {
			auto packet = ofSplitString(packets, "\n");
			for (auto splitPackets : packet)
			{
				auto splitPacket = ofSplitString(splitPackets, ",");
				EmotiBitPacket::Header h;
				if (!EmotiBitPacket::getHeader(splitPacket, h)) continue;
				ofLogVerbose("ofApp") << "TypeTag: " << h.typeTag;
				//Check if typeTag exists in mapping
				auto typeTagIt = typeTagIndexes.find(h.typeTag);
				if (typeTagIt == typeTagIndexes.end())
				{
					ofLogWarning("ofApp") << "unknown typetag: " << h.typeTag << " - skipping packet";
					continue;
				}
				vector<float> dataVec;
				for (int i = EmotiBitPacket::headerLength; i < splitPacket.size(); ++i) {
					dataVec.push_back(ofToFloat(splitPacket[i]));
				}

				auto idx = typeTagIt->second;
				int w = idx[0], s = idx[1], p = idx[2];

				if (w >= typeTags.size())
				{
					ofLogError("ofApp") << "Invalid w index: " << w << " >= " << typeTags.size();
					continue;
				}
				if (s >= typeTags[w].size()) {
					ofLogError("ofApp") << "Invalid s index: " << s << " >= " << typeTags[w].size();
					continue;
				}
				if (p >= typeTags[w][s].size()) {
					ofLogError("ofApp") << "Invalid p index: " << p << " >= " << typeTags[w][s].size();
					continue;
				}
				if (w >= scopes.size()) {
					ofLogError("ofApp") << "Invalid scope w index: " << w << " >= " << scopes.size();
					continue;
				}
				if (s >= scopes[w].scopes.size()) {
					ofLogError("ofApp") << "Invalid scope s index: " << s << " >= " << scopes[w].scopes.size();
					continue;
				}

				pair<int, int> scopeKey = make_pair(w, s);

				if (scopeUpdates.find(scopeKey) == scopeUpdates.end())
				{
					int nPlots = typeTags[w][s].size();
					scopeUpdates[scopeKey] = vector<vector<float>>(nPlots);
				}
				if (p >= scopeUpdates[scopeKey].size()) {
					ofLogError("ofApp") << "Invalid plot p index: " << p << " >= " << scopeUpdates[scopeKey].size();
					continue;
				}

				scopeUpdates[scopeKey][p].insert(
					scopeUpdates[scopeKey][p].end(),
					dataVec.begin(),
					dataVec.end());

				if ((h.typeTag.find("ACC") != string::npos ||
					h.typeTag.find("GYRO") != string::npos ||
					h.typeTag.find("MAG") != string::npos) &&
					(yLims[w][s][0] == yLims[w][s][1])) {
					scopes[w].scopes[s].autoscaleY(true, minYSpans[w][s]);
				}
				ofLogNotice("ofApp") << "Mapping " << h.typeTag << " to w=" << w << ", s=" << s << ", p=" << p;
			}
			
		}
		for (auto& updateKv : scopeUpdates)
		{
			int w = updateKv.first.first;
			int s = updateKv.first.second;
			auto& updateData = updateKv.second;

			scopes[w].scopes[s].updateData(updateData);
		}

	}
}

void ofApp::draw() {
	gui.begin();
	if (ImGui::Begin("EmotiBit Device Manager")) {
		drawDeviceList();
	}
	ImGui::End();
	gui.end();
}

void ofApp::exit() {

}
void ofApp::initMetaDataBuffers()
{
	bufferSizes = initBuffer(bufferSizes);
	dataCounts = initBuffer(dataCounts);
	dataFreqs = initBuffer(dataFreqs);
}
template <class T>
vector<vector<vector<T>>> ofApp::initBuffer(vector<vector<vector<T>>> buffer) {
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
void ofApp::updatePlotAttributeLists(std::string settingsFile)
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
void ofApp::updateTypeTagList()
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
void ofApp::setupOscilloscopes()
{
	// read the patchboard file
	if (patchboard.loadFile(ofToDataPath("inputSettings.xml")))
	{
		ofLog(OF_LOG_NOTICE, "PatchBoard succesfully loaded");
	}
	else
	{
		ofLogError("ofApp") << "PatchBoard File Not Found: " << ofToDataPath("inputSettings.xml");
		return;
	}
	ofFile scopeSettingsFile(ofToDataPath("ofxOscilloscopeSettings.xml"));
	// check if oscilloscope settings file exists
	if (scopeSettingsFile.exists())
	{
		prototypeScope = ofxMultiScope::loadScopeSettings();
	}
	else
	{
		ofLogError("ofApp") << "Scope Settings File Not Found: " << ofToDataPath("ofxOscilloscopeSettings.xml");
		return;
	}
	plotIds = ofxMultiScope::getPlotIds();
	updatePlotAttributeLists();
	updateTypeTagList();
	initMetaDataBuffers();

}

void ofApp::drawDeviceList() {
	//Decide column amount
	const int Ncols = 7;
	float fullWidth = ImGui::GetContentRegionAvail().x;
	float columnWidth = fullWidth / (float)Ncols;
	auto discovered = wifiHost.getDiscoveredDevices();
	//Setup Columns
	ImGui::Columns(Ncols, "headerDeviceColumns", true);
	ImGui::Separator();
	//Header
	centeredText("Device ID", columnWidth); ImGui::NextColumn();
	centeredText("Battery", columnWidth); ImGui::NextColumn();
	centeredText("Power Mode", columnWidth); ImGui::NextColumn();
	centeredText("Clipping #", columnWidth); ImGui::NextColumn();
	centeredText("Overflow #", columnWidth); ImGui::NextColumn();
	centeredText("SD File", columnWidth); ImGui::NextColumn();
	centeredText("Connect/Action", columnWidth); ImGui::NextColumn();
	ImGui::Separator();
	ImGui::Columns(1);

	//Body
	ImGui::BeginChild("##deviceListChild", ImVec2(0, 200), false, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::Columns(Ncols, "bodyDeviceColumns", true);
	for (auto& kv : discovered) {
		const auto &devId = kv.first;
		const auto &info = kv.second;

		if (ImGui::Selectable(devId.c_str(), selectedDevice == devId, ImGuiSelectableFlags_SpanAllColumns)) {
			selectedDevice = devId;
		}
		ImGui::NextColumn();

		//Battery
		char batBuf[16];
		if (info.batteryPercent >= 0) snprintf(batBuf, sizeof(batBuf), "%d%%", info.batteryPercent);
		else strcpy(batBuf, "--");
		centeredText(batBuf, columnWidth);
		ImGui::NextColumn();

		//Power Mode
		const char* pm = "--";
		switch (info.powerMode) {
		case PowerMode::NORMAL_POWER:	pm = "Normal"; break;
		case PowerMode::LOW_POWER:		pm = "Low"; break;
		case PowerMode::WIRELESS_OFF:	pm = "Off"; break;
		case PowerMode::HIBERNATE:		pm = "Hib"; break;
		default:						pm = "Unknown"; break;
		}
		centeredText(pm, columnWidth);
		ImGui::NextColumn();

		//Clipping count
		centeredText(std::to_string(info.clippingCount).c_str(), columnWidth);
		ImGui::NextColumn();

		//Overflow count
		centeredText(std::to_string(info.overflowCount).c_str(), columnWidth);
		ImGui::NextColumn();

		//SD Filename
		centeredText(info.currentSdFilename.empty()? "-": info.currentSdFilename.c_str(), columnWidth);
		ImGui::NextColumn();

		//Connect / Disconnect
		bool isConn = wifiHost.isSessionConnected(devId);
		if (isConn) {
			if (ImGui::Button(("Disconnect##" + devId).c_str(), ImVec2(columnWidth, 0))) {
				onDeviceDisconnect(devId);
			}
		}
		else {
			if (ImGui::Button(("Connect##" + devId).c_str(), ImVec2(columnWidth, 0))) {
				onDeviceConnect(devId);
			}
		}
		ImGui::NextColumn();
	}
	ImGui::Columns(1);
	ImGui::EndChild();

	if (!selectedDevice.empty() && discovered.count(selectedDevice)) {
		ImGui::Separator();
		auto& info = discovered.at(selectedDevice);
		ImGui::Text("Details for %s:", selectedDevice.c_str());
		ImGui::BulletText("IP: %s", info.identifier.ip.c_str());
		ImGui::BulletText("Last seen: %llu ms ago", ofGetElapsedTimeMillis() - info.lastSeen);
		int dp = wifiHost.getSessionDataPort(selectedDevice);
		ImGui::BulletText("Data Port: %d", dp);
		int cp = wifiHost.getSessionControlPort(selectedDevice);
		ImGui::BulletText("Control Port: %d", cp);
		bool isConn = wifiHost.isSessionConnected(selectedDevice);
		ImGui::BulletText("Connected: %s", isConn ? "Yes" : "No");
		ImGui::Separator();
		ImGui::BulletText("Timeout settings: %d", wifiHost.getSettingsTimeout());
		ImGui::Separator();
		ImGui::Text("Live Data for %s", selectedDevice.c_str());
		auto& scopes = deviceScopes[selectedDevice];
		for (auto& ms : scopes) {
			ms.plot();
		}
		
	}
}

void ofApp::onDeviceConnect(const std::string& devId) {
	wifiHost.connect(devId);

	deviceScopes[devId].clear();
	for (auto& proto : prototypeScope) {
		deviceScopes[devId].push_back(proto);
	}

	// Ensure the deviceScopes size matches our type structure
	if (deviceScopes[devId].size() != typeTags.size()) {
		ofLogWarning("ofApp") << "Device scope count (" << deviceScopes[devId].size()
			<< ") doesn't match typeTags count (" << typeTags.size() << ")";
	}

	//Apply Y-limits and autoscaling to each scope
	for (size_t msIndex = 0; msIndex < deviceScopes[devId].size(); ++msIndex) {
		auto& multiScope = deviceScopes[devId][msIndex];
		for (size_t scopeIndex = 0; scopeIndex < multiScope.scopes.size(); ++scopeIndex) {
			auto& scope = multiScope.scopes[scopeIndex];

			//Get Y-Limits from settings (assuming yLims is structured as [multiscope][scope][min/max]
			if (msIndex < yLims.size() && scopeIndex < yLims[msIndex].size()) {
				float yMin = yLims[msIndex][scopeIndex][0];
				float yMax = yLims[msIndex][scopeIndex][1];
				scope.setYLims({ yMin, yMax });

				//Re-enable autoscaling if Y-limits are unset (yMin == yMax)
				if (yMin == yMax && msIndex < minYSpans.size() && scopeIndex < minYSpans[msIndex].size()) {
					scope.autoscaleY(true, minYSpans[msIndex][scopeIndex]);
				}
			}
		}
	}

	initMetaDataBuffers();	
}

void ofApp::onDeviceDisconnect(const std::string& devId) {
	wifiHost.disconnect(devId);
	deviceScopes.erase(devId);
}
