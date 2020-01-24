#ifndef _ots_EpicsInterface_h
#define _ots_EpicsInterface_h

#include <ctime>
#include <fstream>
#include <map>
#include <queue>
#include <thread>
#include <utility>
#include <vector>
#include "cadef.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <dirent.h>

#include "otsdaq/SlowControlsCore/SlowControlsVInterface.h"

struct dbr_ctrl_char;

namespace ots
{
class EpicsInterface;

struct PVHandlerParameters
{
	PVHandlerParameters(std::string pv, EpicsInterface* client)
	{
		pvName    = pv;
		webClient = client;
	}
	std::string     pvName;
	EpicsInterface* webClient;
};

struct PVAlerts
{
	PVAlerts(time_t t, const char* c, const char* d)
	{
		status   = c;
		severity = d;
		time     = t;
	}
	std::string status;
	std::string severity;
	time_t      time;
};

struct PVInfo
{
	PVInfo(chtype tmpChannelType)
	{
		channelType = tmpChannelType;
		dataCache.resize(circularBufferSize);
		for(int i = 0; i < circularBufferSize; i++)
		{
			std::pair<time_t, std::string> filler(0, "");
			dataCache[i] = filler;
		}
		// mostRecentBufferIndex = 0;
	}

	chid                 channelID    = NULL;
	PVHandlerParameters* parameterPtr = NULL;
	evid                 eventID      = NULL;
	chtype               channelType;
	std::string          pvValue;
	int                  circularBufferSize    = 10;  // Default Guess
	unsigned int         mostRecentBufferIndex = -1;
	std::vector<std::pair<time_t, std::string>>
	     dataCache;           // (10, std::pair<time_t, std::string> (0, ""));
	bool valueChange = true;  // so that it automatically reports the status when
	                          // we open the viewer for the first time - get to see
	                          // what is DC'd
	std::queue<PVAlerts> alerts;
	//struct dbr_ctrl_char settings;
	struct dbr_ctrl_double settings;

};

/* Antonio 09/24/2019 */
#include <libpq-fe.h>


//db connection
PGconn *dbconn;
int dbconnStatus_;

//end Antonio

class EpicsInterface : public SlowControlsVInterface
{
  public:
	EpicsInterface(
	    const std::string&       pluginType,
	    const std::string&       interfaceUID,
	    const ConfigurationTree& theXDAQContextConfigTree,
	    const std::string&       controlsConfigurationPath);
	~EpicsInterface();

	void initialize();
	void destroy();

	std::vector<std::string>   getChannelList();
	std::string                getList(std::string format);
	void                       subscribe(std::string pvName);
	void                       subscribeJSON(std::string pvList);
	void                       unsubscribe(std::string pvName);
	std::array<std::string, 4> getCurrentValue(std::string pvName);
	std::vector<std::vector<std::string>> getChannelHistory(std::string pvName);
	std::array<std::string, 9> getSettings(std::string pvName);
	std::vector<std::vector<std::string>> checkAlarms();

	/* Antonio 09/24/2019 */
	void dbSystemLogin(void);
	void dbSystemLogout(void);

	virtual void configure(void) override {
		std::vector<std::vector<std::string>> alarms = checkAlarms();
		if(alarms.size()) {__SS__ << "configure error. n. of Alarms: " << alarms.size(); __SS_THROW__};
	}
	virtual void halt(void) override {
		std::vector<std::vector<std::string>> alarms = checkAlarms();
		if(alarms.size()) {__SS__ << "halt error. n. of Alarms: " << alarms.size(); __SS_THROW__};
	}
	virtual void pause(void) override {
		std::vector<std::vector<std::string>> alarms = checkAlarms();
		if(alarms.size()) {__SS__ << "pause error. n. of Alarms: " << alarms.size(); __SS_THROW__};
	}
	virtual void resume(void) override {
		std::vector<std::vector<std::string>> alarms = checkAlarms();
		if(alarms.size()) {__SS__ << "resume error. n. of Alarms: " << alarms.size(); __SS_THROW__};
	}
	virtual void start(std::string runNumber) override  {
		std::vector<std::vector<std::string>> alarms = checkAlarms();
		if(alarms.size()) {__SS__ << "start error. n. of Alarms: " << alarms.size(); __SS_THROW__};
	}
	virtual void stop(void) override {
		std::vector<std::vector<std::string>> alarms = checkAlarms();
		if(alarms.size()) {__SS__ << "stop error. n. of Alarms: " << alarms.size(); __SS_THROW__};
	}

	// States
	virtual bool running(void) override {
		std::vector<std::vector<std::string>> alarms = checkAlarms();
		if(alarms.size()) {return true;}
		return false;
	} //This is a workloop/thread, by default do nothing and end thread during running (Note: return true would repeat call)

  private:
	bool checkIfPVExists(std::string pvName);
	void loadListOfPVs();
	void getControlValues(std::string pvName);
	void createChannel(std::string pvName);
	void destroyChannel(std::string pvName);
	void subscribeToChannel(std::string pvName, chtype subscriptionType);
	void cancelSubscriptionToChannel(std::string pvName);
	void readValueFromPV(std::string pvName);
	void writePVValueToRecord(std::string pvName, std::string pdata);
	//void writePVControlValueToRecord(std::string pvName, struct dbr_ctrl_char* pdata);
	void writePVControlValueToRecord(std::string pvName, struct dbr_ctrl_double* pdata);
	void writePVAlertToQueue(std::string pvName,
	                         const char* status,
	                         const char* severity);
	void readPVRecord(std::string pvName);
	void debugConsole(std::string pvName);
	static void eventCallback(struct event_handler_args eha);
	static void staticChannelCallbackHandler(struct connection_handler_args cha);
	static void accessRightsCallback(struct access_rights_handler_args args);
	static void printChidInfo(chid chid, std::string message);
	void        channelCallbackHandler(struct connection_handler_args& cha);
	void        popQueue(std::string pvName);

  private:
	//  std::map<chid, std::string> mapOfPVs_;
	std::map<std::string, PVInfo*> mapOfPVInfo_;
	int                            status_;
};

}  // namespace ots

#endif
