#pragma once

#include "ofMain.h"
#include "ofxOscilloscope.h"
//#include "ofxNetwork.h"
//#include "ofxNetworkUtils.h"
//#include "ofxThreadedLogger.h"
//#include "ofxGui.h"
//#include "ofxInputField.h"
//#include "ofxLSL.h"
//#include "DoubleBuffer.h"
#include "EmotiBitPacket.h"
#include "EmotiBitWiFiMultiHost.h"
//#include "ofxEmotiBitVersion.h"
//#include "EmotiBitTestingHelper.h"
//#include "ofxOsc.h"
#include "PatchboardJson.h"
#include "PatchboardXml.h"
#include "Periodizer.h"
//#include "ofxJSON.h"
//#include "SoftwareVersionChecker.h"
//#include "EmotiBitLsl.h"
#include "ofxImGui.h"
#include "imgui.h"
#include "FileLogger.cpp"

class ofApp : public ofBaseApp {
public:
	void setup();
	void update();
	void draw();
	void exit();

	//void keyPressed(int key);
	//void keyReleased(int key);
	//void mouseMoved(int x, int y);
	//void mouseDragged(int x, int y, int button);
	//void mousePressed(int x, int y, int button);
	//void mouseReleased(int x, int y, int button);
	//void windowResized(int w, int h);
	//void dragEvent(ofDragInfo dragInfo);
	//void gotMessage(ofMessage msg);
	template <class T>
	vector<vector<vector<T>>> initBuffer(vector<vector<vector<T>>> buffer);
	void initMetaDataBuffers();
	void updateTypeTagList();
	void updatePlotAttributeLists(std::string settingsFile = "ofxOscilloscopeSettings.xml");
	void setupOscilloscopes();
	void drawDeviceList();
	void onDeviceConnect(const std::string& deviceId);
	void onDeviceDisconnect(const std::string& deviceId);

	ofxImGui::Gui gui;
	EmotiBitWiFiMultiHost wifiHost;
	std::string selectedDevice;

	std::unordered_map<std::string, vector<ofxMultiScope>> deviceScopes;
	vector<ofxMultiScope> prototypeScope;

	struct typeTagPlotAttr {
		std::string plotName;
		ofColor plotColor;
		vector<int> scopeIdx;
	};
	PatchboardXml patchboard;
	// ToDo: change the input aperiodic and ouptut periodic typeTags when we resolve typetags for aperiodic signals
	// NOTE: New periodizers have to be added to the list below
	std::vector<Periodizer> periodizerList{ Periodizer(EmotiBitPacket::TypeTag::HEART_RATE,
														EmotiBitPacket::TypeTag::PPG_INFRARED,
														EmotiBitPacket::TypeTag::HEART_RATE) ,

											Periodizer(EmotiBitPacket::TypeTag::SKIN_CONDUCTANCE_RESPONSE_AMPLITUDE,
														EmotiBitPacket::TypeTag::EDA,
														EmotiBitPacket::TypeTag::SKIN_CONDUCTANCE_RESPONSE_AMPLITUDE,
														0),

											Periodizer(EmotiBitPacket::TypeTag::SKIN_CONDUCTANCE_RESPONSE_RISE_TIME,
														EmotiBitPacket::TypeTag::EDA,
														EmotiBitPacket::TypeTag::SKIN_CONDUCTANCE_RESPONSE_RISE_TIME,
														0)
	};
	unordered_map<int, vector<size_t>> plotIdIndexes;
	vector<vector<vector<string>>> typeTags;
	unordered_map<string, vector<int>> typeTagIndexes;
	vector<vector<float>> samplingFreqs;
	vector<vector<vector<string>>> plotNames;
	vector<vector<vector<float>>> yLims;
	vector<vector<float>> minYSpans;
	vector<vector<vector<ofColor>>> plotColors;
	//vector<ofColor> plotColors;
	unordered_map<std::string, typeTagPlotAttr>typeTagPlotAttributes;
	vector<vector<vector<int>>> plotIds;
	vector<vector<vector<int>>> bufferSizes;
	vector<vector<vector<int>>> dataCounts;
	vector<vector<vector<float>>> dataFreqs;

	void centeredText(const char* txt, float columnWidth) {
		float textWidth = ImGui::CalcTextSize(txt).x;
		float pad = (columnWidth - textWidth) * 0.5f;
		if (pad > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
		ImGui::TextUnformatted(txt);
	}

};