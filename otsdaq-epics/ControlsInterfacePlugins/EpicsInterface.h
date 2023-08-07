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
#include <dirent.h>
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

#include <libpq-fe.h>

#include "otsdaq/SlowControlsCore/SlowControlsVInterface.h"

// clang-format off

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

//db connection
PGconn *dcsArchiveDbConn;
PGconn *dcsAlarmDbConn;
PGconn *dcsLogDbConn;
int dcsArchiveDbConnStatus_;
int dcsAlarmDbConnStatus_;
int dcsLogDbConnStatus_;

class EpicsInterface : public SlowControlsVInterface
{
  public:
	EpicsInterface(
	    const std::string&       pluginType,
	    const std::string&       interfaceUID,
	    const ConfigurationTree& theXDAQContextConfigTree,
	    const std::string&       controlsConfigurationPath);
	~EpicsInterface();
	
	static const std::string 				EPICS_NO_ALARM;	
	static const std::string 				EPICS_INVALID_ALARM;
	static const std::string 				EPICS_MINOR_ALARM;
	static const std::string 				EPICS_MAJOR_ALARM;

	void 									initialize				(void) override;
	void 									destroy					(void);

	std::vector<std::string>   				getChannelList			(void) override;
	std::string               				getList					(const std::string& format) override;
	void                       				subscribe				(const std::string& pvName) override;
	void                       				subscribeJSON			(const std::string& JSONpvList) override;
	void                       				unsubscribe				(const std::string& pvName) override;
	std::array<std::string, 4> 				getCurrentValue			(const std::string& pvName) override;
	std::array<std::string, 9> 				getSettings				(const std::string& pvName) override;
	std::vector<std::vector<std::string>> 	getChannelHistory		(const std::string& pvName, int startTime, int endTime) override;
	std::vector<std::vector<std::string>>	getLastAlarms			(const std::string& pvName) override;
	std::vector<std::vector<std::string>>	getAlarmsLog			(const std::string& pvName) override;
	std::vector<std::vector<std::string>>	checkAlarmNotifications	(void) override;
	std::vector<std::string> 				checkAlarm				(const std::string& pvName, bool ignoreMinor = false);

	void 									dbSystemLogin			(void);
	void 									dbSystemLogout			(void);

 private:
	void 									handleAlarmsForFSM		(const std::string& fsmTransitionName, ConfigurationTree LinkToAlarmsToMonitor);

 public:

	virtual void 							configure				(void) override;
	virtual void 							halt					(void) override { handleAlarmsForFSM("halt",		getSelfNode().getNode("LinkToHaltAlarmsToMonitorTable")); }
	virtual void 							pause					(void) override { handleAlarmsForFSM("pause",		getSelfNode().getNode("LinkToPauseAlarmsToMonitorTable")); }
	virtual void 							resume					(void) override { handleAlarmsForFSM("resume",		getSelfNode().getNode("LinkToResumeAlarmsToMonitorTable")); }
	virtual void 							start					(std::string /*runNumber*/) override  { handleAlarmsForFSM("start",getSelfNode().getNode("LinkToStartAlarmsToMonitorTable")); }
	virtual void 							stop					(void) override { handleAlarmsForFSM("stop",		getSelfNode().getNode("LinkToStopAlarmsToMonitorTable")); }

	// States
	virtual bool 							running					(void) override { handleAlarmsForFSM("running",		getSelfNode().getNode("LinkToRunningAlarmsToMonitorTable")); sleep(1); return true;}
	//This is a workloop/thread, by default do nothing and end thread during running (Note: return true would repeat call)

  private:
	bool 									checkIfPVExists			(const std::string& pvName);
	void 									loadListOfPVs			(void);
	void 									getControlValues		(const std::string& pvName);
	void									createChannel			(const std::string& pvName);
	void 									destroyChannel			(const std::string& pvName);
	void 									subscribeToChannel		(const std::string& pvName, chtype subscriptionType);
	void 									cancelSubscriptionToChannel(const std::string& pvName);
	void 									readValueFromPV			(const std::string& pvName);
	void 									writePVValueToRecord	(const std::string& pvName, const std::string& pdata);
	//void writePVControlValueToRecord(std::string pvName, struct dbr_ctrl_char* pdata);
	void 									writePVControlValueToRecord(const std::string& pvName, struct dbr_ctrl_double* pdata);
	void 									writePVAlertToQueue		(const std::string& pvName, const char* status, const char* severity);
	void 									readPVRecord			(const std::string& pvName);
	void 									debugConsole			(const std::string& pvName);
	static void								eventCallback			(struct event_handler_args eha);
	static void 							staticChannelCallbackHandler(struct connection_handler_args cha);
	static void								accessRightsCallback	(struct access_rights_handler_args args);
	static void 							printChidInfo			(chid chid, const std::string& message);
	void        							channelCallbackHandler	(struct connection_handler_args& cha);
	void        							popQueue				(const std::string& pvName);

  private:
	//  std::map<chid, std::string> mapOfPVs_;
	std::map<std::string, PVInfo*> 			mapOfPVInfo_;
	int                            			status_;
};
// clang-format on
}  // namespace ots

#endif
