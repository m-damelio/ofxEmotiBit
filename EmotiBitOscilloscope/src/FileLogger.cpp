#include "ofMain.h"
#include "ofLog.h"
#include <fstream>


class FileLogger : public ofBaseLoggerChannel {
public:
	FileLogger(const std::string& filename) 
		: ofs(filename, std::ios::out | std::ios::app) { }

	~FileLogger()
	{
		if (ofs.is_open())
		{
			ofs.close();
		}
	}

	void log(ofLogLevel logLevel, const std::string& module, const std::string& message) override
	{
		if (module == "ofxNetwork" || message.find("EINVAL: invalid argument") != std::string::npos)
		{
			return;
		}
		std::string logMessage = formatLog(logLevel, module, message);
		if (ofs.is_open())
		{
			ofs << logMessage << std::endl;
		}

		std::cout << logMessage << std::endl;
	}
	
	void log(ofLogLevel level, const std::string& module, const char* format, ...) override
	{
		va_list args;
		va_start(args, format);
		log(level, module, format, args);
		va_end(args);
	}

	void log(ofLogLevel level, const std::string& module, const char* format, va_list args) override
	{
		char buffer[1024];
		vsnprintf(buffer, sizeof(buffer), format, args);
		log(level, module, std::string(buffer));
	}
private:
	std::ofstream ofs;

	std::string formatLog(ofLogLevel logLevel, const std::string& module, const std::string& message)
	{
		std::string timeStamp = ofGetTimestampString();
		std::string levelStr;
		switch (logLevel)
		{
		case OF_LOG_FATAL_ERROR: levelStr = "FATAL";break;
		case OF_LOG_ERROR: levelStr = "ERROR"; break;
		case OF_LOG_WARNING: levelStr = "WARNING"; break;
		case OF_LOG_NOTICE: levelStr = "NOTICE";break;
		case OF_LOG_VERBOSE: levelStr = "VERBOSE";break;
		default: levelStr = "UNKNOWN";break;
		}
		return "[" + timeStamp + "][" + levelStr + "][" + module + "] " + message;
	}
};