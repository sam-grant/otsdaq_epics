#include "alarm.h"  //Holds strings that we can use to access the alarm status, severity, and parameters
#include "epicsMutex.h"
#include "otsdaq-epics/ControlsInterfacePlugins/EpicsInterface.h"
#include "otsdaq/ConfigurationInterface/ConfigurationManager.h"
#include "otsdaq/Macros/SlowControlsPluginMacros.h"
#include "otsdaq/TablePlugins/SlowControlsTableBase/SlowControlsTableBase.h"

#pragma GCC diagnostic push
//#include "/mu2e/ups/epics/v3_15_4/Linux64bit+2.6-2.12-e10/include/alarm.h"
//#include "alarmString.h"
#include "cadef.h"  //EPICS Channel Access:
// http://www.aps.anl.gov/epics/base/R3-14/12-docs/CAref.html
// Example compile options:
// Compiling:
// Setup epics (See redmine wiki)
// g++ -std=c++11  EpicsCAMonitor.cpp EpicsCAMessage.cpp EpicsWebClient.cpp
// SocketUDP.cpp SocketTCP.cpp -L$EPICS_BASE/lib/linux-x86_64/
// -Wl,-rpath,$EPICS_BASE/lib/linux-x86_64 -lca -lCom -I$EPICS_BASE//include
// -I$EPICS_BASE//include/os/Linux -I$EPICS_BASE/include/compiler/gcc -o
// EpicsWebClient
#pragma GCC diagnostic pop

// clang-format off
#define DEBUG false
#define PV_FILE_NAME 		std::string(getenv("SERVICE_DATA_PATH")) + "/SlowControlsDashboardData/pv_list.dat";
#define PV_CSV_DIR 			"/home/mu2edcs/mu2e-dcs/make_db/csv";

using namespace ots;

const std::string EpicsInterface::EPICS_NO_ALARM 		= "NO_ALARM";
const std::string EpicsInterface::EPICS_INVALID_ALARM 	= "INVALID";
const std::string EpicsInterface::EPICS_MINOR_ALARM 	= "MINOR";
const std::string EpicsInterface::EPICS_MAJOR_ALARM 	= "MAJOR";

// clang-format on

EpicsInterface::EpicsInterface(const std::string&       pluginType,
                               const std::string&       interfaceUID,
                               const ConfigurationTree& theXDAQContextConfigTree,
                               const std::string&       controlsConfigurationPath)
    : SlowControlsVInterface(pluginType, interfaceUID, theXDAQContextConfigTree, controlsConfigurationPath)
{
	// this allows for handlers to happen "asynchronously"
	SEVCHK(ca_context_create(ca_enable_preemptive_callback),
	       "EpicsInterface::EpicsInterface() : "
	       "ca_enable_preemptive_callback_init()");
}

EpicsInterface::~EpicsInterface() { destroy(); }

void EpicsInterface::destroy()
{
	// __GEN_COUT__ << "mapOfPVInfo_.size() = " << mapOfPVInfo_.size() << __E__;
	for(auto it = mapOfPVInfo_.begin(); it != mapOfPVInfo_.end(); it++)
	{
		cancelSubscriptionToChannel(it->first);
		destroyChannel(it->first);
		delete(it->second->parameterPtr);
		delete(it->second);
		mapOfPVInfo_.erase(it);
	}

	// __GEN_COUT__ << "mapOfPVInfo_.size() = " << mapOfPVInfo_.size() << __E__;
	SEVCHK(ca_poll(), "EpicsInterface::destroy() : ca_poll");
	dbSystemLogout();
	return;
}

void EpicsInterface::initialize()
{
	__GEN_COUT__ << "Epics Interface now initializing!";
	destroy();
	dbSystemLogin();
	loadListOfPVs();
	return;
}

std::vector<std::string> EpicsInterface::getChannelList()
{
	std::vector<std::string> pvList;
	pvList.resize(mapOfPVInfo_.size());
	for(auto pv : mapOfPVInfo_)
	{
		__GEN_COUT__ << "getPVList() add: " + pv.first << __E__;
		pvList.push_back(pv.first);
	}
	return pvList;
}

std::string EpicsInterface::getList(const std::string& format)
{
	std::string pvList;

	std::string refreshRate = "";
	PGresult*   res;
	char        buffer[1024];

	// pvList = "[\"None\"]";
	// std::cout << "SUCA: Returning pvList as: " << pvList << __E__;
	// return pvList;

	__GEN_COUT__ << "Epics Interface now retrieving pvList!";

	if(format == "JSON")
	{
		__GEN_COUT__ << "Getting list in JSON format! There are " << mapOfPVInfo_.size() << " pv's.";
		if(mapOfPVInfo_.size() == 0 && loginErrorMsg_ != "")
		{
			__GEN_SS__ << "No PVs found and error message: " << loginErrorMsg_ << __E__;
			__GEN_SS_THROW__;
		}

		// pvList = "{\"PVList\" : [";
		pvList = "[";
		for(auto it = mapOfPVInfo_.begin(); it != mapOfPVInfo_.end(); it++)
		{
			if(dcsArchiveDbConnStatus_ == 1)
			{
				res = PQexec(dcsArchiveDbConn, buffer);
				/*int num = */ snprintf(buffer, sizeof(buffer), "SELECT smpl_mode_id, smpl_per FROM channel WHERE name = '%s'", (it->first).c_str());

				if(PQresultStatus(res) == PGRES_TUPLES_OK)
				{
					int smplMode = 0;
					try
					{
						smplMode = std::stoi(PQgetvalue(res, 0, 0));
					}
					catch(const std::exception& e)
					{
					}
					if(smplMode == 2)
						refreshRate = PQgetvalue(res, 0, 1);
					PQclear(res);
					__GEN_COUT__ << "getList() \"sample rate\" SELECT result: " << it->first << ":" << refreshRate << " (smpl_mode_id = " << smplMode << ")"
					             << __E__;
				}
				else
				{
					__GEN_COUT__ << "SELECT failed: " << PQerrorMessage(dcsArchiveDbConn) << __E__;
					PQclear(res);
				}
			}
			// pvList += "\"" + it->first + ":" + refreshRate + "\", ";
			pvList += "\"" + it->first + "\", ";
			//__GEN_COUT__ << it->first << __E__;
		}
		pvList.resize(pvList.size() - 2);
		pvList += "]";  //}";
		__GEN_COUT__ << pvList << __E__;
		return pvList;
	}
	return pvList;
}

void EpicsInterface::subscribe(const std::string& pvName)
{
	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	createChannel(pvName);
	usleep(10000);  // what makes the console hang at startup
	subscribeToChannel(pvName, mapOfPVInfo_.find(pvName)->second->channelType);
	// SEVCHK(ca_poll(), "EpicsInterface::subscribe() : ca_poll");  //print outs
	// that handle takeover the console; can make our own error handler
	return;
}

//{"PVList" : ["Mu2e_BeamData_IOC/CurrentTime"]}
void EpicsInterface::subscribeJSON(const std::string& JSONNameString)
{
	// if(DEBUG){__GEN_COUT__ << pvList << __E__;;}

	std::string JSON = "{\"PVList\" :";
	std::string pvName;
	std::string pvList = JSONNameString;  // FIXME -- someday fix parsing to not do
	                                      // so many copies/substr
	if(pvList.find(JSON) != std::string::npos)
	{
		pvList = pvList.substr(pvList.find(JSON) + JSON.length(), std::string::npos);
		do
		{
			pvList = pvList.substr(pvList.find("\"") + 1,
			                       std::string::npos);     // eliminate up to the next "
			pvName = pvList.substr(0, pvList.find("\""));  //
			// if(DEBUG){__GEN_COUT__ << "Read PV Name:  " << pvName << __E__;}
			pvList = pvList.substr(pvList.find("\"") + 1, std::string::npos);
			// if(DEBUG){__GEN_COUT__ << "pvList : " << pvList << __E__;}

			if(checkIfPVExists(pvName))
			{
				createChannel(pvName);
				subscribeToChannel(pvName, mapOfPVInfo_.find(pvName)->second->channelType);
				SEVCHK(ca_poll(), "EpicsInterface::subscribeJSON : ca_poll");
			}
			else if(DEBUG)
			{
				__GEN_COUT__ << pvName << " not found in file! Not subscribing!" << __E__;
			}

		} while(pvList.find(",") != std::string::npos);
	}

	return;
}

void EpicsInterface::unsubscribe(const std::string& pvName)
{
	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}

	cancelSubscriptionToChannel(pvName);
	return;
}

//------------------------------------------------------------------------------------------------------------
//--------------------------------------PRIVATE
// FUNCTION--------------------------------------
//------------------------------------------------------------------------------------------------------------
void EpicsInterface::eventCallback(struct event_handler_args eha)
{
	// chid chid = eha.chid;
	if(eha.status == ECA_NORMAL)
	{
		//		int                  i;
		union db_access_val* pBuf = (union db_access_val*)eha.dbr;
		if(DEBUG)
		{
			printf("channel %s: ", ca_name(eha.chid));
		}

		//__COUT__ << "event_handler_args.type: " << eha.type << __E__;
		switch(eha.type)
		{
		// case DBR_CTRL_CHAR:
		// 	if (DEBUG)
		// 	{
		// 		__COUT__ << "Response Type: DBR_CTRL_CHAR" << __E__;
		// 	}
		// 	((EpicsInterface *)eha.usr)
		// 		->writePVControlValueToRecord(
		// 			ca_name(eha.chid),
		// 			((struct dbr_ctrl_char *)
		// 					eha.dbr)); // write the PV's control values to
		// records 	break;
		case DBR_CTRL_DOUBLE:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_CTRL_DOUBLE" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVControlValueToRecord(ca_name(eha.chid),
			                                  ((struct dbr_ctrl_double*)eha.dbr));  // write the PV's control values to records
			break;
		case DBR_DOUBLE:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_DOUBLE" << __E__;
			}
			((EpicsInterface*)eha.usr)->writePVValueToRecord(ca_name(eha.chid),
			                                                 std::to_string(*((double*)eha.dbr)));  // write the PV's value to records
			break;
		case DBR_STS_STRING:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_STS_STRING" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVAlertToQueue(ca_name(eha.chid), epicsAlarmConditionStrings[pBuf->sstrval.status], epicsAlarmSeverityStrings[pBuf->sstrval.severity]);
			/*if(DEBUG)
			{
			printf("current %s:\n", eha.count > 1?"values":"value");
			for (i = 0; i < eha.count; i++)
			{
			printf("%s\t", *(&(pBuf->sstrval.value) + i));
			if ((i+1)%6 == 0) printf("\n");
			}
			printf("\n");
			}*/
			break;
		case DBR_STS_SHORT:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_STS_SHORT" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVAlertToQueue(ca_name(eha.chid), epicsAlarmConditionStrings[pBuf->sshrtval.status], epicsAlarmSeverityStrings[pBuf->sshrtval.severity]);
			/*if(DEBUG)
	  {
	  printf("current %s:\n", eha.count > 1?"values":"value");
	  for (i = 0; i < eha.count; i++){
	  printf("%-10d", *(&(pBuf->sshrtval.value) + i));
	  if ((i+1)%8 == 0) printf("\n");
	  }
	  printf("\n");
	  }*/
			break;
		case DBR_STS_FLOAT:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_STS_FLOAT" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVAlertToQueue(ca_name(eha.chid), epicsAlarmConditionStrings[pBuf->sfltval.status], epicsAlarmSeverityStrings[pBuf->sfltval.severity]);
			/*if(DEBUG)
	  {
	  printf("current %s:\n", eha.count > 1?"values":"value");
	  for (i = 0; i < eha.count; i++){
	  printf("-10.4f", *(&(pBuf->sfltval.value) + i));
	  if ((i+1)%8 == 0) printf("\n");
	  }
	  printf("\n");
	  }*/
			break;
		case DBR_STS_ENUM:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_STS_ENUM" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVAlertToQueue(ca_name(eha.chid), epicsAlarmConditionStrings[pBuf->senmval.status], epicsAlarmSeverityStrings[pBuf->senmval.severity]);
			/*if(DEBUG)
	  {
			printf("current %s:\n", eha.count > 1?"values":"value");
			for (i = 0; i < eha.count; i++){
			        printf("%d ", *(&(pBuf->senmval.value) + i));
			}
			printf("\n");
	  }*/
			break;
		case DBR_STS_CHAR:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_STS_CHAR" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVAlertToQueue(ca_name(eha.chid), epicsAlarmConditionStrings[pBuf->schrval.status], epicsAlarmSeverityStrings[pBuf->schrval.severity]);
			/*if(DEBUG)
	  {
			printf("current %s:\n", eha.count > 1?"values":"value");
			for (i = 0; i < eha.count; i++){
			        printf("%-5", *(&(pBuf->schrval.value) + i));
			        if ((i+1)%15 == 0) printf("\n");
			}
			printf("\n");
	  }*/
			break;
		case DBR_STS_LONG:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_STS_LONG" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVAlertToQueue(ca_name(eha.chid), epicsAlarmConditionStrings[pBuf->slngval.status], epicsAlarmSeverityStrings[pBuf->slngval.severity]);
			/*if(DEBUG)
	  {
			printf("current %s:\n", eha.count > 1?"values":"value");
			for (i = 0; i < eha.count; i++){
			        printf("%-15d", *(&(pBuf->slngval.value) + i));
			        if((i+1)%5 == 0) printf("\n");
			}
			printf("\n");
	  }*/
			break;
		case DBR_STS_DOUBLE:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_STS_DOUBLE" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVAlertToQueue(ca_name(eha.chid), epicsAlarmConditionStrings[pBuf->sdblval.status], epicsAlarmSeverityStrings[pBuf->sdblval.severity]);
			/*if(DEBUG)
	  {
			printf("current %s:\n", eha.count > 1?"values":"value");
			for (i = 0; i < eha.count; i++){
			        printf("%-15.4f", *(&(pBuf->sdblval.value) + i));
			}
			printf("\n");
			}*/
			break;
		default:
			if(ca_name(eha.chid))
			{
				if(DEBUG)
				{
					__COUT__ << " EpicsInterface::eventCallback: PV Name = " << ca_name(eha.chid) << __E__;
					__COUT__ << (char*)eha.dbr << __E__;
				}
				((EpicsInterface*)eha.usr)->writePVValueToRecord(ca_name(eha.chid),
				                                                 (char*)eha.dbr);  // write the PV's value to records
			}
			break;
		}
		/* if get operation failed, print channel name and message */
	}
	else
		printf("channel %s: get operation failed\n", ca_name(eha.chid));

	return;
}

void EpicsInterface::staticChannelCallbackHandler(struct connection_handler_args cha)
{
	__COUT__ << "webClientChannelCallbackHandler" << __E__;

	((PVHandlerParameters*)ca_puser(cha.chid))->webClient->channelCallbackHandler(cha);
	return;
}

void EpicsInterface::channelCallbackHandler(struct connection_handler_args& cha)
{
	std::string pv = ((PVHandlerParameters*)ca_puser(cha.chid))->pvName;
	if(cha.op == CA_OP_CONN_UP)
	{
		__GEN_COUT__ << pv << cha.chid << " connected! " << __E__;

		mapOfPVInfo_.find(pv)->second->channelType = ca_field_type(cha.chid);
		readPVRecord(pv);

		/*status_ =
		   ca_array_get_callback(dbf_type_to_DBR_STS(mapOfPVInfo_.find(pv)->second->channelType),
		                ca_element_count(cha.chid), cha.chid, eventCallback, this);
		   SEVCHK(status_, "ca_array_get_callback");*/
	}
	else
		__GEN_COUT__ << pv << " disconnected!" << __E__;

	return;
}

bool EpicsInterface::checkIfPVExists(const std::string& pvName)
{
	if(DEBUG)
	{
		__GEN_COUT__ << "EpicsInterface::checkIfPVExists(): PV Info Map Length is " << mapOfPVInfo_.size() << __E__;
	}

	if(mapOfPVInfo_.find(pvName) != mapOfPVInfo_.end())
		return true;

	return false;
}

void EpicsInterface::loadListOfPVs()
{
	__GEN_COUT__ << "LOADING LIST OF PVS!!!!";
	/*
	    std::string              pv_csv_dir_path = PV_CSV_DIR;
	    std::vector<std::string> files           = std::vector<std::string>();
	    DIR*                     dp;
	    struct dirent*           dirp;
	    if((dp = opendir(pv_csv_dir_path.c_str())) == NULL)
	    {
	        std::cout << "Error  opening: " << pv_csv_dir_path << __E__;
	        return;
	    }

	    while((dirp = readdir(dp)) != NULL)
	    {
	        files.push_back(std::string(dirp->d_name));
	    }
	    closedir(dp);

	    // Initialize Channel Access
	    status_ = ca_task_initialize();
	    SEVCHK(status_, "EpicsInterface::loadListOfPVs() : Unable to initialize");
	    if(status_ != ECA_NORMAL)
	        exit(-1);

	    // for each file
	    // int referenceLength = 0;
	    std::vector<std::string> csv_line;
	    std::string              pv_name, cluster, category, system, sensor;
	    cluster = "Mu2e";
	    unsigned int i, j;

	    // First two entries will be . & ..
	    for(i = 2; i < files.size(); i++)
	    {
	        // std::cout << pv_csv_dir_path << "/" <<files[i] << __E__;
	        std::string pv_list_file = pv_csv_dir_path + "/" + files[i];
	        __GEN_COUT__ << "Reading: " << pv_list_file << __E__;

	        // read file
	        // for each line in file
	        // std::string pv_list_file = PV_FILE_NAME;
	        //__GEN_COUT__ << pv_list_file;

	        std::ifstream infile(pv_list_file);
	        if(!infile.is_open())
	        {
	            __GEN_SS__ << "Failed to open PV list file: '" << pv_list_file << "'" << __E__;
	            __GEN_SS_THROW__;
	        }
	        __GEN_COUT__ << "Reading file" << __E__;

	        // make map of pvname -> PVInfo
	        // Example line of csv
	        // CompStatus,daq01,fans_fastest_rpm,0,rpm,16e3,12e3,2e3,1e3,,,,Passive,,fans_fastest_rpm
	        // daq01
	        for(std::string line; getline(infile, line);)
	        {
	            //__GEN_COUT__ << line << __E__;
	            csv_line.clear();
	            std::istringstream ss(line);
	            std::string        token;

	            while(std::getline(ss, token, ','))
	                csv_line.push_back(token);
	            if(csv_line.at(0)[0] != '#')
	            {
	                category = csv_line.at(0);
	                system   = csv_line.at(1);
	                sensor   = csv_line.at(2);

	                pv_name = cluster + "_" + category + "_" + system + "/" + sensor;
	                //__GEN_COUT__ << pv_name << __E__;
	                mapOfPVInfo_[pv_name] = new PVInfo(DBR_STRING);
	            }
	        }
	        __GEN_COUT__ << "Finished reading: " << pv_list_file << __E__;
	    }
	*/
	// HERE GET PVS LIST FROM DB
	if(dcsArchiveDbConnStatus_ == 1)
	{
		PGresult*   res;
		char        buffer[1024];
		std::string pv_name;
		std::string cluster = "Mu2e";

		__GEN_COUT__ << "Reading database PVS List" << __E__;
		/*int num =*/snprintf(buffer, sizeof(buffer), "SELECT COUNT(%s) FROM channel", std::string("channel_id").c_str());
		res = PQexec(dcsArchiveDbConn, buffer);

		if(PQresultStatus(res) == PGRES_TUPLES_OK)
		{
			int rows = 0;
			rows     = std::stoi(PQgetvalue(res, 0, 0));
			PQclear(res);
			for(int i = 1; i <= rows; i++)
			{
				/*int num =*/snprintf(buffer, sizeof(buffer), "SELECT name FROM channel WHERE channel_id = '%d'", i);
				res = PQexec(dcsArchiveDbConn, buffer);
				if(PQresultStatus(res) == PGRES_TUPLES_OK)
				{
					pv_name               = PQgetvalue(res, 0, 0);
					mapOfPVInfo_[pv_name] = new PVInfo(DBR_STRING);
				}
				else
					__GEN_COUT__ << "SELECT failed: mapOfPVInfo_ not filled for channel_id: " << i << PQerrorMessage(dcsArchiveDbConn) << __E__;
			}
			__GEN_COUT__ << "Finished reading database PVs List!" << __E__;
			PQclear(res);
		}
		else
		{
			__GEN_COUT__ << "SELECT failed: " << PQerrorMessage(dcsArchiveDbConn) << __E__;
			PQclear(res);
		}
	}

	__GEN_COUT__ << "Here is our pv list!" << __E__;
	// subscribe for each pv
	for(auto pv : mapOfPVInfo_)
	{
		__GEN_COUT__ << pv.first << __E__;
		subscribe(pv.first);
	}

	// channels are subscribed to by here.

	// get parameters (e.g. HIHI("upper alarm") HI("upper warning") LOLO("lower
	// alarm")) for each pv
	// for(auto pv : mapOfPVInfo_)
	// {
	// 	getControlValues(pv.first);
	// }

	__GEN_COUT__ << "Finished reading file and subscribing to pvs!" << __E__;
	SEVCHK(ca_pend_event(0.0),
	       "EpicsInterface::subscribe() : ca_pend_event(0.0)");  // Start listening

	return;
}

void EpicsInterface::getControlValues(const std::string& pvName)
{
	if(true)
	{
		__GEN_COUT__ << "EpicsInterface::getControlValues(" << pvName << ")" << __E__;
	}
	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}

	SEVCHK(ca_array_get_callback(
	           // DBR_CTRL_CHAR,
	           DBR_CTRL_DOUBLE,
	           0,
	           mapOfPVInfo_.find(pvName)->second->channelID,
	           eventCallback,
	           this),
	       "ca_array_get_callback");
	// SEVCHK(ca_poll(), "EpicsInterface::getControlValues() : ca_poll");
	return;
}

void EpicsInterface::createChannel(const std::string& pvName)
{
	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	__GEN_COUT__ << "Trying to create channel to " << pvName << ":" << mapOfPVInfo_.find(pvName)->second->channelID << __E__;

	if(mapOfPVInfo_.find(pvName)->second != NULL)                 // Check to see if the pvName
	                                                              // maps to a null pointer so we
	                                                              // don't have any errors
		if(mapOfPVInfo_.find(pvName)->second->channelID != NULL)  // channel might exist, subscription doesn't so create a
		                                                          // subscription
		{
			// if state of channel is connected then done, use it
			if(ca_state(mapOfPVInfo_.find(pvName)->second->channelID) == cs_conn)
			{
				if(DEBUG)
				{
					__GEN_COUT__ << "Channel to " << pvName << " already exists!" << __E__;
				}
				return;
			}
			if(DEBUG)
			{
				__GEN_COUT__ << "Channel to " << pvName << " exists, but is not connected! Destroying current channel." << __E__;
			}
			destroyChannel(pvName);
		}

	// create pvs handler
	if(mapOfPVInfo_.find(pvName)->second->parameterPtr == NULL)
	{
		mapOfPVInfo_.find(pvName)->second->parameterPtr = new PVHandlerParameters(pvName, this);
	}

	// at this point, make a new channel
	SEVCHK(
	    ca_create_channel(
	        pvName.c_str(), staticChannelCallbackHandler, mapOfPVInfo_.find(pvName)->second->parameterPtr, 0, &(mapOfPVInfo_.find(pvName)->second->channelID)),
	    "EpicsInterface::createChannel() : ca_create_channel");
	__GEN_COUT__ << "channelID: " << pvName << mapOfPVInfo_.find(pvName)->second->channelID << __E__;

	SEVCHK(ca_replace_access_rights_event(mapOfPVInfo_.find(pvName)->second->channelID, accessRightsCallback),
	       "EpicsInterface::createChannel() : ca_replace_access_rights_event");
	// SEVCHK(ca_poll(), "EpicsInterface::createChannel() : ca_poll"); //This
	// routine will perform outstanding channel access background activity and then
	// return.
	return;
}

void EpicsInterface::destroyChannel(const std::string& pvName)
{
	if(mapOfPVInfo_.find(pvName)->second != NULL)
	{
		if(mapOfPVInfo_.find(pvName)->second->channelID != NULL)
		{
			status_ = ca_clear_channel(mapOfPVInfo_.find(pvName)->second->channelID);
			SEVCHK(status_, "EpicsInterface::destroyChannel() : ca_clear_channel");
			if(status_ == ECA_NORMAL)
			{
				mapOfPVInfo_.find(pvName)->second->channelID = NULL;
				if(DEBUG)
				{
					__GEN_COUT__ << "Killed channel to " << pvName << __E__;
				}
			}
			SEVCHK(ca_poll(), "EpicsInterface::destroyChannel() : ca_poll");
		}
		else
		{
			if(DEBUG)
			{
				__GEN_COUT__ << "No channel to " << pvName << " exists" << __E__;
			}
		}
	}
	return;
}

void EpicsInterface::accessRightsCallback(struct access_rights_handler_args args)
{
	chid chid = args.chid;

	printChidInfo(chid, "EpicsInterface::createChannel() : accessRightsCallback");
}

void EpicsInterface::printChidInfo(chid chid, const std::string& message)
{
	__COUT__ << message.c_str() << __E__;
	__COUT__ << "pv: " << ca_name(chid) << " type(" << ca_field_type(chid) << ") nelements(" << ca_element_count(chid) << ") host(" << ca_host_name(chid) << ")"
	         << __E__;
	__COUT__ << "read(" << ca_read_access(chid) << ") write(" << ca_write_access(chid) << ") state(" << ca_state(chid) << ")" << __E__;
}

void EpicsInterface::subscribeToChannel(const std::string& pvName, chtype /*subscriptionType*/)
{
	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	if(DEBUG)
	{
		__GEN_COUT__ << "Trying to subscribe to " << pvName << ":" << mapOfPVInfo_.find(pvName)->second->channelID << __E__;
	}

	if(mapOfPVInfo_.find(pvName)->second != NULL)  // Check to see if the pvName
	                                               // maps to a null pointer so we
	                                               // don't have any errors
	{
		if(mapOfPVInfo_.find(pvName)->second->eventID != NULL)  // subscription already exists
		{
			if(DEBUG)
			{
				__GEN_COUT__ << "Already subscribed to " << pvName << "!" << __E__;
			}
			// FIXME No way to check if the event ID is valid
			// Just cancel the subscription if it already exists?
		}
	}

	//	int i=0;
	//	while(ca_state(mapOfPVInfo_.find(pvName)->second->channelID) == cs_conn
	//&& i<2) 		Sleep(1); 	if(i==2)
	//		{__SS__;throw std::runtime_error(ss.str() + "Channel failed for "
	//+
	// pvName);}

	SEVCHK(ca_create_subscription(dbf_type_to_DBR(mapOfPVInfo_.find(pvName)->second->channelType),
	                              1,
	                              mapOfPVInfo_.find(pvName)->second->channelID,
	                              DBE_VALUE | DBE_ALARM | DBE_PROPERTY,
	                              eventCallback,
	                              this,
	                              &(mapOfPVInfo_.find(pvName)->second->eventID)),
	       "EpicsInterface::subscribeToChannel() : ca_create_subscription "
	       "dbf_type_to_DBR");

	SEVCHK(ca_create_subscription(DBR_STS_DOUBLE,
	                              1,
	                              mapOfPVInfo_.find(pvName)->second->channelID,
	                              DBE_VALUE | DBE_ALARM | DBE_PROPERTY,
	                              eventCallback,
	                              this,
	                              &(mapOfPVInfo_.find(pvName)->second->eventID)),
	       "EpicsInterface::subscribeToChannel() : ca_create_subscription "
	       "DBR_STS_DOUBLE");

	SEVCHK(ca_create_subscription(DBR_CTRL_DOUBLE,
	                              1,
	                              mapOfPVInfo_.find(pvName)->second->channelID,
	                              DBE_VALUE | DBE_ALARM | DBE_PROPERTY,
	                              eventCallback,
	                              this,
	                              &(mapOfPVInfo_.find(pvName)->second->eventID)),
	       "EpicsInterface::subscribeToChannel() : ca_create_subscription");

	if(DEBUG)
	{
		__GEN_COUT__ << "EpicsInterface::subscribeToChannel: Created Subscription to " << mapOfPVInfo_.find(pvName)->first << "!\n" << __E__;
	}
	// SEVCHK(ca_poll(), "EpicsInterface::subscribeToChannel() : ca_poll");
	return;
}

void EpicsInterface::cancelSubscriptionToChannel(const std::string& pvName)
{
	if(mapOfPVInfo_.find(pvName)->second != NULL)
		if(mapOfPVInfo_.find(pvName)->second->eventID != NULL)
		{
			status_ = ca_clear_subscription(mapOfPVInfo_.find(pvName)->second->eventID);
			SEVCHK(status_,
			       "EpicsInterface::cancelSubscriptionToChannel() : "
			       "ca_clear_subscription");
			if(status_ == ECA_NORMAL)
			{
				mapOfPVInfo_.find(pvName)->second->eventID = NULL;
				if(DEBUG)
				{
					__GEN_COUT__ << "Killed subscription to " << pvName << __E__;
				}
			}
			SEVCHK(ca_poll(), "EpicsInterface::cancelSubscriptionToChannel() : ca_poll");
		}
		else
		{
			if(DEBUG)
			{
				__GEN_COUT__ << pvName << "does not have a subscription!" << __E__;
			}
		}
	else
	{
		// __GEN_COUT__ << pvName << "does not have a subscription!" << __E__;
	}
	//  SEVCHK(ca_flush_io(),"ca_flush_io");
	return;
}

void EpicsInterface::readValueFromPV(const std::string& /*pvName*/)
{
	// SEVCHK(ca_get(DBR_String, 0, mapOfPVInfo_.find(pvName)->second->channelID,
	// &(mapOfPVInfo_.find(pvName)->second->pvValue), eventCallback,
	// &(mapOfPVInfo_.find(pvName)->second->callbackPtr)), "ca_get");

	return;
}

void EpicsInterface::writePVControlValueToRecord(const std::string& pvName,
                                                 //                                                 struct dbr_ctrl_char*
                                                 //                                                 pdata)
                                                 struct dbr_ctrl_double* pdata)
{
	if(DEBUG)
	{
		__GEN_COUT__ << "Reading Control Values from " << pvName << "!" << __E__;
	}

	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	mapOfPVInfo_.find(pvName)->second->settings = *pdata;

	if(DEBUG)
	{
		__GEN_COUT__ << "pvName: " << pvName << __E__;
		__GEN_COUT__ << "status: " << pdata->status << __E__;
		__GEN_COUT__ << "severity: " << pdata->severity << __E__;
		__GEN_COUT__ << "units: " << pdata->units << __E__;
		__GEN_COUT__ << "upper disp limit: " << (int)(pdata->upper_disp_limit) << __E__;
		__GEN_COUT__ << "lower disp limit: " << pdata->lower_disp_limit << __E__;
		__GEN_COUT__ << "upper alarm limit: " << pdata->upper_alarm_limit << __E__;
		__GEN_COUT__ << "upper warning limit: " << pdata->upper_warning_limit << __E__;
		__GEN_COUT__ << "lower warning limit: " << pdata->lower_warning_limit << __E__;
		__GEN_COUT__ << "lower alarm limit: " << pdata->lower_alarm_limit << __E__;
		__GEN_COUT__ << "upper control limit: " << pdata->upper_ctrl_limit << __E__;
		__GEN_COUT__ << "lower control limit: " << pdata->lower_ctrl_limit << __E__;
		//__GEN_COUT__ << "RISC_pad: " << pdata->RISC_pad << __E__;
		__GEN_COUT__ << "Value: " << pdata->value << __E__;
	}
	return;
}

// Enforces the circular buffer
void EpicsInterface::writePVValueToRecord(const std::string& pvName, const std::string& pdata)
{
	std::pair<time_t, std::string> currentRecord(time(0), pdata);

	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	// __GEN_COUT__ << pdata << __E__;

	PVInfo* pvInfo = mapOfPVInfo_.find(pvName)->second;

	if(pvInfo->mostRecentBufferIndex != pvInfo->dataCache.size() - 1 && pvInfo->mostRecentBufferIndex != (unsigned int)(-1))
	{
		if(pvInfo->dataCache[pvInfo->mostRecentBufferIndex].first == currentRecord.first)
		{
			pvInfo->valueChange = true;  // false;
		}
		else
		{
			pvInfo->valueChange = true;
		}

		++pvInfo->mostRecentBufferIndex;
		pvInfo->dataCache[pvInfo->mostRecentBufferIndex] = currentRecord;
	}
	else
	{
		pvInfo->dataCache[0]          = currentRecord;
		pvInfo->mostRecentBufferIndex = 0;
	}
	// debugConsole(pvName);

	return;
}

void EpicsInterface::writePVAlertToQueue(const std::string& pvName, const char* status, const char* severity)
{
	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	PVAlerts alert(time(0), status, severity);
	mapOfPVInfo_.find(pvName)->second->alerts.push(alert);
	//__GEN_COUT__ << "writePVAlertToQueue(): " << pvName << " " << status << " "
	//<< severity << __E__;

	// debugConsole(pvName);

	return;
}

void EpicsInterface::readPVRecord(const std::string& pvName)
{
	status_ = ca_array_get_callback(dbf_type_to_DBR_STS(mapOfPVInfo_.find(pvName)->second->channelType),
	                                ca_element_count(mapOfPVInfo_.find(pvName)->second->channelID),
	                                mapOfPVInfo_.find(pvName)->second->channelID,
	                                eventCallback,
	                                this);
	SEVCHK(status_, "EpicsInterface::readPVRecord(): ca_array_get_callback");
	return;
}

void EpicsInterface::debugConsole(const std::string& pvName)
{
	__GEN_COUT__ << "==============================================================="
	                "==============="
	             << __E__;
	for(unsigned int it = 0; it < mapOfPVInfo_.find(pvName)->second->dataCache.size() - 1; it++)
	{
		if(it == mapOfPVInfo_.find(pvName)->second->mostRecentBufferIndex)
		{
			__GEN_COUT__ << "-----------------------------------------------------------"
			                "----------"
			             << __E__;
		}
		__GEN_COUT__ << "Iteration: " << it << " | " << mapOfPVInfo_.find(pvName)->second->mostRecentBufferIndex << " | "
		             << mapOfPVInfo_.find(pvName)->second->dataCache[it].second << __E__;
		if(it == mapOfPVInfo_.find(pvName)->second->mostRecentBufferIndex)
		{
			__GEN_COUT__ << "-----------------------------------------------------------"
			                "----------"
			             << __E__;
		}
	}
	__GEN_COUT__ << "==============================================================="
	                "==============="
	             << __E__;
	__GEN_COUT__ << "Status:     "
	             << " | " << mapOfPVInfo_.find(pvName)->second->alerts.size() << " | " << mapOfPVInfo_.find(pvName)->second->alerts.front().status << __E__;
	__GEN_COUT__ << "Severity:   "
	             << " | " << mapOfPVInfo_.find(pvName)->second->alerts.size() << " | " << mapOfPVInfo_.find(pvName)->second->alerts.front().severity << __E__;
	__GEN_COUT__ << "==============================================================="
	                "==============="
	             << __E__;

	return;
}

void EpicsInterface::popQueue(const std::string& pvName)
{
	if(DEBUG)
	{
		__GEN_COUT__ << "EpicsInterface::popQueue() " << __E__;
	}
	mapOfPVInfo_.find(pvName)->second->alerts.pop();

	if(mapOfPVInfo_.find(pvName)->second->alerts.empty())
	{
		readPVRecord(pvName);
		SEVCHK(ca_poll(), "EpicsInterface::popQueue() : ca_poll");
	}
	return;
}

//========================================================================================================================
std::array<std::string, 4> EpicsInterface::getCurrentValue(const std::string& pvName)
{
	__GEN_COUT__ << "void EpicsInterface::getCurrentValue() reached" << __E__;

	if(mapOfPVInfo_.find(pvName) != mapOfPVInfo_.end())
	{
		PVInfo*     pv = mapOfPVInfo_.find(pvName)->second;
		std::string time, value, status, severity;

		int index = pv->mostRecentBufferIndex;

		__GEN_COUT__ << pv << index << __E__;

		if(0 <= index && index < pv->circularBufferSize)
		{
			//__GEN_COUT__ << pv->dataCache[index].first <<" "<< std::time(0)-60 <<
			//__E__;

			time     = std::to_string(pv->dataCache[index].first);
			value    = pv->dataCache[index].second;
			status   = pv->alerts.back().status;
			severity = pv->alerts.back().severity;
		}
		else if(index == -1)
		{
			time     = "N/a";
			value    = "N/a";
			status   = "DC";
			severity = "DC";
		}
		else
		{
			time     = "N/a";
			value    = "N/a";
			status   = "UDF";
			severity = "INVALID";
		}
		// Time, Value, Status, Severity

		__GEN_COUT__ << "Index:    " << index << __E__;
		__GEN_COUT__ << "Time:     " << time << __E__;
		__GEN_COUT__ << "Value:    " << value << __E__;
		__GEN_COUT__ << "Status:   " << status << __E__;
		__GEN_COUT__ << "Severity: " << severity << __E__;

		/*	if(pv->valueChange)
		        {
		                pv->valueChange = false;
		        }
		        else
		        {
		                __GEN_COUT__ << pvName << " has no change" << __E__;
		                time     = "NO_CHANGE";
		                value    = "";
		                status   = "";
		                severity = "";
		        }
		*/
		std::array<std::string, 4> currentValues = {time, value, status, severity};

		return currentValues;
	}
	else
	{
		__GEN_COUT__ << pvName << " was not found!" << __E__;
		__GEN_COUT__ << "Trying to resubscribe to " << pvName << __E__;
		// subscribe(pvName);
	}

	std::array<std::string, 4> currentValues = {"PV Not Found", "NF", "N/a", "N/a"};
	// std::string currentValues [4] = {"N/a", "N/a", "N/a", "N/a"};
	return currentValues;
}

//========================================================================================================================
std::array<std::string, 9> EpicsInterface::getSettings(const std::string& pvName)
{
	__GEN_COUT__ << "EpicsInterface::getPVSettings() reached" << __E__;

	if(mapOfPVInfo_.find(pvName) != mapOfPVInfo_.end())
	{
		std::string units = "DC'd", upperDisplayLimit = "DC'd", lowerDisplayLimit = "DC'd", upperAlarmLimit = "DC'd", upperWarningLimit = "DC'd",
		            lowerWarningLimit = "DC'd", lowerAlarmLimit = "DC'd", upperControlLimit = "DC'd", lowerControlLimit = "DC'd";
		if(mapOfPVInfo_.find(pvName)->second != NULL)                 // Check to see if the pvName
		                                                              // maps to a null pointer so
		                                                              // we don't have any errors
			if(mapOfPVInfo_.find(pvName)->second->channelID != NULL)  // channel might exist, subscription doesn't so create a
			                                                          // subscription
			{
				// dbr_ctrl_char* set = &mapOfPVInfo_.find(pvName)->second->settings;
				dbr_ctrl_double* set = &mapOfPVInfo_.find(pvName)->second->settings;

				// sprintf(&units[0],"%d",set->units);
				units             = set->units;
				upperDisplayLimit = std::to_string(set->upper_disp_limit);
				lowerDisplayLimit = std::to_string(set->lower_disp_limit);
				upperWarningLimit = std::to_string(set->upper_warning_limit);
				lowerWarningLimit = std::to_string(set->lower_warning_limit);
				upperAlarmLimit   = std::to_string(set->upper_alarm_limit);
				lowerAlarmLimit   = std::to_string(set->lower_alarm_limit);
				upperControlLimit = std::to_string(set->upper_ctrl_limit);
				lowerControlLimit = std::to_string(set->lower_ctrl_limit);

				__GEN_COUT__ << "Units              :    " << units << __E__;
				__GEN_COUT__ << "Upper Display Limit:    " << upperDisplayLimit << __E__;
				__GEN_COUT__ << "Lower Display Limit:    " << lowerDisplayLimit << __E__;
				__GEN_COUT__ << "Upper Alarm Limit  :    " << upperAlarmLimit << __E__;
				__GEN_COUT__ << "Upper Warning Limit:    " << upperWarningLimit << __E__;
				__GEN_COUT__ << "Lower Warning Limit:    " << lowerWarningLimit << __E__;
				__GEN_COUT__ << "Lower Alarm Limit  :    " << lowerAlarmLimit << __E__;
				__GEN_COUT__ << "Upper Control Limit:    " << upperControlLimit << __E__;
				__GEN_COUT__ << "Lower Control Limit:    " << lowerControlLimit << __E__;
			}

		std::array<std::string, 9> s = {units,
		                                upperDisplayLimit,
		                                lowerDisplayLimit,
		                                upperAlarmLimit,
		                                upperWarningLimit,
		                                lowerWarningLimit,
		                                lowerAlarmLimit,
		                                upperControlLimit,
		                                lowerControlLimit};

		return s;
	}
	else
	{
		__GEN_COUT__ << pvName << " was not found!" << __E__;
		__GEN_COUT__ << "Trying to resubscribe to " << pvName << __E__;
		subscribe(pvName);
	}
	std::array<std::string, 9> s = {"DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd"};
	return s;
}

//========================================================================================================================
void EpicsInterface::dbSystemLogin()
{
	dcsArchiveDbConnStatus_ = 0;
	dcsAlarmDbConnStatus_   = 0;
	dcsLogDbConnStatus_     = 0;

	char* dbname_ = const_cast<char*>(getenv("DCS_ARCHIVE_DATABASE") ? getenv("DCS_ARCHIVE_DATABASE") : "dcs_archive");
	char* dbhost_ = const_cast<char*>(getenv("DCS_ARCHIVE_DATABASE_HOST") ? getenv("DCS_ARCHIVE_DATABASE_HOST") : "");
	char* dbport_ = const_cast<char*>(getenv("DCS_ARCHIVE_DATABASE_PORT") ? getenv("DCS_ARCHIVE_DATABASE_PORT") : "");
	char* dbuser_ = const_cast<char*>(getenv("DCS_ARCHIVE_DATABASE_USER") ? getenv("DCS_ARCHIVE_DATABASE_USER") : "");
	char* dbpwd_  = const_cast<char*>(getenv("DCS_ARCHIVE_DATABASE_PWD") ? getenv("DCS_ARCHIVE_DATABASE_PWD") : "");

	// open db connections
	char dcsArchiveDbConnInfo[1024];
	sprintf(dcsArchiveDbConnInfo,
	        "dbname=%s host=%s port=%s  \
		user=%s password=%s",
	        dbname_,
	        dbhost_,
	        dbport_,
	        dbuser_,
	        dbpwd_);

	// dcs_archive Db Connection
	dcsArchiveDbConn = PQconnectdb(dcsArchiveDbConnInfo);

	if(PQstatus(dcsArchiveDbConn) == CONNECTION_BAD)
	{
		loginErrorMsg_ = "Unable to connect to the dcs_archive database!";
		__GEN_COUT__ << "Unable to connect to the dcs_archive database!\n" << __E__;
		PQfinish(dcsArchiveDbConn);
	}
	else
	{
		__GEN_COUT__ << "Connected to the dcs_archive database!\n" << __E__;
		dcsArchiveDbConnStatus_ = 1;
	}

	// dcs_alarm Db Connection
	dbname_ = const_cast<char*>(getenv("DCS_ALARM_DATABASE") ? getenv("DCS_ALARM_DATABASE") : "dcs_alarm");
	dbhost_ = const_cast<char*>(getenv("DCS_ALARM_DATABASE_HOST") ? getenv("DCS_ALARM_DATABASE_HOST") : "");
	dbport_ = const_cast<char*>(getenv("DCS_ALARM_DATABASE_PORT") ? getenv("DCS_ALARM_DATABASE_PORT") : "");
	dbuser_ = const_cast<char*>(getenv("DCS_ALARM_DATABASE_USER") ? getenv("DCS_ALARM_DATABASE_USER") : "");
	dbpwd_  = const_cast<char*>(getenv("DCS_ALARM_DATABASE_PWD") ? getenv("DCS_ALARM_DATABASE_PWD") : "");
	char dcsAlarmDbConnInfo[1024];
	sprintf(dcsAlarmDbConnInfo,
	        "dbname=%s host=%s port=%s  \
		user=%s password=%s",
	        dbname_,
	        dbhost_,
	        dbport_,
	        dbuser_,
	        dbpwd_);

	dcsAlarmDbConn = PQconnectdb(dcsAlarmDbConnInfo);

	if(PQstatus(dcsAlarmDbConn) == CONNECTION_BAD)
	{
		loginErrorMsg_ = "Unable to connect to the dcs_alarm database!";
		__GEN_COUT__ << "Unable to connect to the dcs_alarm database!\n" << __E__;
		PQfinish(dcsAlarmDbConn);
	}
	else
	{
		__GEN_COUT__ << "Connected to the dcs_alarm database!\n" << __E__;
		dcsAlarmDbConnStatus_ = 1;
	}

	// dcs_log Db Connection
	dbname_ = const_cast<char*>(getenv("DCS_LOG_DATABASE") ? getenv("DCS_LOG_DATABASE") : "dcs_log");
	dbhost_ = const_cast<char*>(getenv("DCS_LOG_DATABASE_HOST") ? getenv("DCS_LOG_DATABASE_HOST") : "");
	dbport_ = const_cast<char*>(getenv("DCS_LOG_DATABASE_PORT") ? getenv("DCS_LOG_DATABASE_PORT") : "");
	dbuser_ = const_cast<char*>(getenv("DCS_LOG_DATABASE_USER") ? getenv("DCS_LOG_DATABASE_USER") : "");
	dbpwd_  = const_cast<char*>(getenv("DCS_LOG_DATABASE_PWD") ? getenv("DCS_LOG_DATABASE_PWD") : "");
	char dcsLogDbConnInfo[1024];
	sprintf(dcsLogDbConnInfo,
	        "dbname=%s host=%s port=%s  \
		user=%s password=%s",
	        dbname_,
	        dbhost_,
	        dbport_,
	        dbuser_,
	        dbpwd_);

	dcsLogDbConn = PQconnectdb(dcsLogDbConnInfo);

	if(PQstatus(dcsLogDbConn) == CONNECTION_BAD)
	{
		loginErrorMsg_ = "Unable to connect to the dcs_log database!";
		__GEN_COUT__ << "Unable to connect to the dcs_log database!\n" << __E__;
		PQfinish(dcsLogDbConn);
	}
	else
	{
		__GEN_COUT__ << "Connected to the dcs_log database!\n" << __E__;
		dcsLogDbConnStatus_ = 1;
	}
}

//========================================================================================================================
void EpicsInterface::dbSystemLogout()
{
	if(PQstatus(dcsArchiveDbConn) == CONNECTION_OK)
	{
		PQfinish(dcsArchiveDbConn);
		__GEN_COUT__ << "DCS_ARCHIVE DB CONNECTION CLOSED\n" << __E__;
	}
	if(PQstatus(dcsAlarmDbConn) == CONNECTION_OK)
	{
		PQfinish(dcsAlarmDbConn);
		__GEN_COUT__ << "DCS_ALARM DB CONNECTION CLOSED\n" << __E__;
	}
	if(PQstatus(dcsLogDbConn) == CONNECTION_OK)
	{
		PQfinish(dcsLogDbConn);
		__GEN_COUT__ << "DCS_LOG DB CONNECTION CLOSED\n" << __E__;
	}
}

//========================================================================================================================
std::vector<std::vector<std::string>> EpicsInterface::getChannelHistory(const std::string& pvName, int startTime, int endTime)
{
	__GEN_COUT__ << "getChannelHistory() reached" << __E__;
	std::vector<std::vector<std::string>> history;

	if(mapOfPVInfo_.find(pvName) != mapOfPVInfo_.end())
	{
		if(dcsArchiveDbConnStatus_ == 1)
		{
			PGresult* res = nullptr;
			try
			{
				char        buffer[1024];
				std::string row;

				// VIEW LAST 10 UPDATES
				/*int num =*/snprintf(buffer,
				                      sizeof(buffer),
				                      "SELECT FLOOR(EXTRACT(EPOCH FROM smpl_time)), float_val, status.name, "
				                      "severity.name, smpl_per FROM channel, sample, status, severity WHERE "
				                      "channel.channel_id = sample.channel_id AND sample.severity_id = "
				                      "severity.severity_id  AND sample.status_id = status.status_id AND "
				                      "channel.name = \'%s\' AND smpl_time >= TO_TIMESTAMP(\'%d\') AND smpl_time < TO_TIMESTAMP(\'%d\') ORDER BY smpl_time desc",
				                      pvName.c_str(), startTime, endTime);

				res = PQexec(dcsArchiveDbConn, buffer);

				if(PQresultStatus(res) != PGRES_TUPLES_OK)
				{
					__SS__ << "getChannelHistory(): SELECT FROM ARCHIVER DATABASE FAILED!!! PQ ERROR: " << PQresultErrorMessage(res) << __E__;
					PQclear(res);
					__SS_THROW__;
				}

				if(PQntuples(res) > 0)
				{
					/* first, print out the attribute names */
					int nFields = PQnfields(res);
					history.resize(PQntuples(res));

					/* next, print out the rows */
					for(int i = 0; i < PQntuples(res); i++)
					{
						history[i].resize(nFields);
						for(int j = 0; j < nFields; j++)
						{
							history[i][j] = PQgetvalue(res, i, j);
							row.append(PQgetvalue(res, i, j));
							row.append(" ");
						}
						row.append("\n");
					}
					__GEN_COUT__ << "getChannelHistory(): row from select: " << row << __E__;
					PQclear(res);
				}
			}
			catch(...)
			{
				__SS__ << "getChannelHistory(): FAILING GETTING DATA FROM ARCHIVER DATABASE!!! PQ ERROR: " << PQresultErrorMessage(res) << __E__;
				try	{ throw; } //one more try to printout extra info
				catch(const std::exception &e)
				{
					ss << "Exception message: " << e.what();
				}
				catch(...){}
				__SS_THROW__;
			}
		}
		else
		{
			__SS__ << "getChannelHistory(): ARCHIVER DATABASE CONNECTION FAILED!!! " << __E__;
			__SS_THROW__;
		}
	}
	else
	{
		history.resize(1);
		history[0] = {"PV Not Found", "NF", "N/a", "N/a"};
		__GEN_COUT__ << "getChannelHistory() pvName " << pvName << " was not found!" << __E__;
		__GEN_COUT__ << "Trying to resubscribe to " << pvName << __E__;
		subscribe(pvName);
	}

	return history;
}  // end getChannelHistory()

//========================================================================================================================
std::vector<std::vector<std::string>> EpicsInterface::getLastAlarms(const std::string& pvName)
{
	__GEN_COUT__ << "EpicsInterface::getLastAlarms() reached" << __E__;
	std::vector<std::vector<std::string>> alarms;

	if(dcsAlarmDbConnStatus_ == 1)
	{
		PGresult* res = nullptr;
		try
		{
			char        buffer[1024];
			std::string row;

			// ACTION FOR ALARM DB CHANNEL TABLE
			/*int num =*/snprintf(buffer,
			                      sizeof(buffer),
			                      "SELECT   pv.component_id							\
								, alarm_tree.name							\
								, pv.descr									\
								, pv.pv_value								\
								, status.name as status						\
								, severity.name as severity					\
								, pv.alarm_time								\
								, pv.enabled_ind							\
								, pv.annunciate_ind							\
								, pv.latch_ind								\
								, pv.delay									\
								, pv.filter									\
								, pv.delay_count							\
								, pv.act_global_alarm_ind					\
						FROM alarm_tree, pv, status, severity				\
						WHERE	pv.component_id = alarm_tree.component_id	\
						AND	pv.status_id = status.status_id					\
						AND	pv.severity_id = severity.severity_id			\
						AND	alarm_tree.name LIKE \'%%%s%%\'					\
 						ORDER BY pv.severity_id DESC;",
			                      pvName.c_str());

			res = PQexec(dcsAlarmDbConn, buffer);
			__COUT__ << "getLastAlarms(): SELECT pv table PQntuples(res): " << PQntuples(res) << __E__;

			if(PQresultStatus(res) != PGRES_TUPLES_OK)
			{
				__SS__ << "getLastAlarms(): SELECT FROM ALARM DATABASE FAILED!!! PQ ERROR: " << PQresultErrorMessage(res) << __E__;
				PQclear(res);
				__SS_THROW__;
			}

			if(PQntuples(res) > 0)
			{
				// UPDATE ALARMS LIST
				int nFields = PQnfields(res);
				alarms.resize(PQntuples(res));

				/* next, print out the rows */
				for(int i = 0; i < PQntuples(res); i++)
				{
					alarms[i].resize(nFields);
					for(int j = 0; j < nFields; j++)
					{
						alarms[i][j] = PQgetvalue(res, i, j);
						row.append(PQgetvalue(res, i, j));
						row.append(" ");
					}
					row.append("\n");
					//__GEN_COUT__ << "getLastAlarms(): " << row << __E__;
				}
			}
			else
			{
				alarms.resize(1);
				alarms[0] = {
				    "0",
				    "Alarms List Not Found",
				    "N/a",
				    "N/a",
				    "N/a",
				    "N/a",
				    "N/a",
				    "N/a",
				    "N/a",
				    "N/a",
				    "N/a",
				    "N/a",
				    "N/a",
				    "N/a",
				};
			}

			PQclear(res);
		}
		catch(...)
		{
			__SS__ << "getLastAlarms(): FAILING GETTING DATA FROM ARCHIVER DATABASE!!! PQ ERROR: " << PQresultErrorMessage(res) << __E__;
			try	{ throw; } //one more try to printout extra info
			catch(const std::exception &e)
			{
				ss << "Exception message: " << e.what();
			}
			catch(...){}
			__SS_THROW__;
		}
	}
	else
	{
		__SS__ << "getLastAlarms(): ALARM DATABASE CONNECTION FAILED!!! " << __E__;
		__SS_THROW__;
	}
	return alarms;
}  // end getLastAlarms()

//========================================================================================================================
std::vector<std::vector<std::string>> EpicsInterface::getAlarmsLog(const std::string& pvName)
{
	__GEN_COUT__ << "EpicsInterface::getAlarmsLog() reached" << __E__;
	std::vector<std::vector<std::string>> alarmsHistory;

	if(dcsLogDbConnStatus_ == 1)
	{
		PGresult* res = nullptr;
		try
		{
			char        buffer[1024];
			std::string row;

			// ACTION FOR ALARM DB CHANNEL TABLE
			/*int num = */ snprintf(buffer,
			                        sizeof(buffer),
			                        "SELECT DISTINCT												\
							  message.id												\
							, message.name												\
							, message_content.value										\
							, msg_property_type.name as \"status\"						\
							, message.severity											\
							, message.datum	as \"time\"									\
						FROM  	message, message_content, msg_property_type				\
						WHERE 	message.id = message_content.message_id					\
						AND	message_content.msg_property_type_id = msg_property_type.id \
						AND	message.type = 'alarm'										\
						AND	message.severity != 'OK'									\
						AND	message.datum >= current_date -20							\
						AND	message.name LIKE '%%%s%%'									\
						ORDER BY message.datum DESC;",
			                        pvName.c_str());

			res = PQexec(dcsLogDbConn, buffer);
			__COUT__ << "getAlarmsLog(): SELECT message table PQntuples(res): " << PQntuples(res) << __E__;

			if(PQresultStatus(res) != PGRES_TUPLES_OK)
			{
				__SS__ << "getAlarmsLog(): SELECT FROM ALARM LOG DATABASE FAILED!!! PQ ERROR: " << PQresultErrorMessage(res) << __E__;
				PQclear(res);
				__SS_THROW__;
			}

			if(PQntuples(res) > 0)
			{
				// UPDATE ALARMS LIST
				int nFields = PQnfields(res);
				alarmsHistory.resize(PQntuples(res));

				/* next, print out the rows */
				for(int i = 0; i < PQntuples(res); i++)
				{
					alarmsHistory[i].resize(nFields);
					for(int j = 0; j < nFields; j++)
					{
						alarmsHistory[i][j] = PQgetvalue(res, i, j);
						row.append(PQgetvalue(res, i, j));
						row.append(" ");
					}
					row.append("\n");
					//__GEN_COUT__ << "getAlarmsLog(): " << row << __E__;
				}
			}
			else
			{
				alarmsHistory.resize(1);
				alarmsHistory[0] = {
				    "0",
				    "Alarms List Not Found",
				    "N/a",
				    "N/a",
				    "N/a",
				    "N/a",
				};
			}

			PQclear(res);
		}
		catch(...)
		{
			__SS__ << "getAlarmsLog(): FAILING GETTING DATA FROM ARCHIVER DATABASE!!! PQ ERROR: " << PQresultErrorMessage(res) << __E__;
			try	{ throw; } //one more try to printout extra info
			catch(const std::exception &e)
			{
				ss << "Exception message: " << e.what();
			}
			catch(...){}
			__SS_THROW__;
		}
	}
	else
	{
		__SS__ << "getAlarmsLog(): ALARM LOG DATABASE CONNECTION FAILED!!! " << __E__;
		__SS_THROW__;
	}
	return alarmsHistory;
}  // end getAlarmsLog()

//========================================================================================================================
// Check Alarms from Epics
//	returns empty vector if no alarm status
//
//	Possible severity values = {NO_ALARM, INVALID, MINOR, MAJOR}
//	Note: Archiver also has "NONE" and "OK" but should not be a current
// value
std::vector<std::string> EpicsInterface::checkAlarm(const std::string& pvName, bool ignoreMinor /*=false*/)
{
	__COUT__ << "checkAlarm()" << __E__;

	auto pvIt = mapOfPVInfo_.find(pvName);
	if(pvIt == mapOfPVInfo_.end())
	{
		__SS__ << "While checking for alarm status, PV name '" << pvName << "' was not found in PV list!" << __E__;
		__SS_THROW__;
	}

	auto valueArray = getCurrentValue(pvIt->first);

	std::string& time     = valueArray[0];
	std::string& value    = valueArray[1];
	std::string& status   = valueArray[2];
	std::string& severity = valueArray[3];
	__COUTV__(pvName);
	__COUTV__(time);
	__COUTV__(value);
	__COUTV__(status);
	__COUTV__(severity);
	if(severity == EPICS_NO_ALARM || (ignoreMinor && severity == EPICS_MINOR_ALARM))
		return std::vector<std::string>();  // empty vector, i.e. no alarm

	// if here, alarm!
	return std::vector<std::string>({pvIt->first, time, value, status, severity});
}  // end checkAlarm()

//========================================================================================================================
// Check Alarms from Epics
std::vector<std::vector<std::string>> EpicsInterface::checkAlarmNotifications()
{
	std::vector<std::vector<std::string>> alarmReturn;
	std::vector<std::string>              alarmRow;
	auto                                  linkToAlarmsToNotify = getSelfNode().getNode("LinkToAlarmAlertNotificationsTable");

	if(!linkToAlarmsToNotify.isDisconnected())
	{
		auto alarmsToNotifyGroups = linkToAlarmsToNotify.getChildren();

		for(const auto& alarmsToNotifyGroup : alarmsToNotifyGroups)
		{
			__COUT__ << "checkAlarmNotifications() alarmsToNotifyGroup: " << alarmsToNotifyGroup.first << __E__;

			auto alarmsToNotify = alarmsToNotifyGroup.second.getNode("LinkToAlarmsToMonitorTable");
			if(!alarmsToNotify.isDisconnected())
			{
				for(const auto& alarmToNotify : alarmsToNotify.getChildren())
				{
					__COUT__ << "checkAlarmNotifications() alarmToNotify: " << alarmToNotify.first << __E__;

					try
					{
						alarmRow = checkAlarm(alarmToNotify.second.getNode("AlarmChannelName").getValue<std::string>(),
						                      alarmToNotify.second.getNode("IgnoreMinorSeverity").getValue<bool>());
					}
					catch(const std::exception& e)
					{
						__COUT__ << "checkAlarmNotifications() alarmToNotify: " << alarmToNotify.first << " not in PVs List!!!" << __E__;
						continue;
					}
					alarmRow.push_back(alarmToNotify.first);
					alarmRow.push_back(alarmsToNotifyGroup.second.getNode("WhoToNotify").getValue<std::string>());
					alarmRow.push_back(alarmsToNotifyGroup.second.getNode("DoSendEmail").getValue<std::string>());
					alarmRow.push_back(alarmsToNotifyGroup.first);
					alarmReturn.push_back(alarmRow);
				}
			}
		}
	}
	// __COUT__
	// << "checkAlarmNotifications().size(): "
	// << alarmReturn.size()
	// << " content:";
	// for (const auto& row : alarmReturn)
	// 	for(const auto& s : row)
	// 		__COUT__ << " " + s;
	// __COUT__<< __E__;

	return alarmReturn;
}  // end checkAlarmNotifications()

//========================================================================================================================
// handle Alarms For FSM from Epics
void EpicsInterface::handleAlarmsForFSM(const std::string& fsmTransitionName, ConfigurationTree linkToAlarmsToMonitor)
{
	if(!linkToAlarmsToMonitor.isDisconnected())
	{
		auto alarmsToMonitor = linkToAlarmsToMonitor.getChildren();

		__SS__;

		ss << "During '" << fsmTransitionName << "'... Alarms monitoring (count=" << alarmsToMonitor.size() << "):" << __E__;
		for(const auto& alarmToMonitor : alarmsToMonitor)
			ss << "\t" << alarmToMonitor.first << __E__;
		ss << __E__;

		unsigned foundCount = 0;
		for(const auto& alarmToMonitor : alarmsToMonitor)
		{
			std::vector<std::string> alarmReturn = checkAlarm(alarmToMonitor.second.getNode("AlarmChannelName").getValue<std::string>(),
			                                                  alarmToMonitor.second.getNode("IgnoreMinorSeverity").getValue<bool>());

			if(alarmReturn.size())
			{
				ss << "Found alarm for channel '" << alarmReturn[0] << "' = {"
				   << "time=" << alarmReturn[1] << ", value=" << alarmReturn[2] << ", status=" << alarmReturn[3] << ", severity=" << alarmReturn[4] << "}!"
				   << __E__;
				++foundCount;
			}
		}
		if(foundCount)
		{
			ss << __E__ << "Total alarms found = " << foundCount << __E__;
			__SS_THROW__;
		}
		__COUT__ << ss.str();
	}
	else
		__COUT__ << "Disconnected alarms to monitor!" << __E__;

}  // end handleAlarmsForFSM()

//========================================================================================================================
// Configure override for Epics
void EpicsInterface::configure()
{

	handleAlarmsForFSM("configure", getSelfNode().getNode("LinkToConfigureAlarmsToMonitorTable"));

	__COUT__ << "configure(): Preparing EPICS for PVs..." << __E__;

	// Steps to update EPICS
	//	1. DTC_TABLE handles: scp *.dbg mu2edcs:mu2edaq01.fnal.gov:mu2e-dcs/apps/OTSReader/db/
	//  2. SQL insert or modify of ROW for PV
	//	3. force restart SW-IOC instance
	//  4. mark 'dirty' for EPICS cronjob restart or archiver and

	std::string slowControlsChannelsSourceTablesString =  // "DTCInterfaceTable,CFOInterfaceTable"
	    getSelfNode().getNode("SlowControlsChannelSourceTableList").getValueWithDefault<std::string>("");

	__COUTV__(slowControlsChannelsSourceTablesString);

	std::vector<std::string> slowControlsChannelsSourceTables = StringMacros::getVectorFromString(slowControlsChannelsSourceTablesString);
	__COUTV__(StringMacros::vectorToString(slowControlsChannelsSourceTables));

	for(const auto& slowControlsChannelsSourceTable : slowControlsChannelsSourceTables)
	{
		__COUTV__(slowControlsChannelsSourceTable);

		const SlowControlsTableBase* slowControlsTable = getConfigurationManager()->getTable<SlowControlsTableBase>(slowControlsChannelsSourceTable);

		if(slowControlsTable->slowControlsChannelListHasChanged())
		{
			__COUT__ << "configure(): Handling channel list change!" << __E__;

			std::vector<std::pair<std::string, std::vector<std::string>>> channels;
			slowControlsTable->getSlowControlsChannelList(channels);

			for(const auto& channel : channels)
			{
				std::string pvName       = channel.first;
				std::string descr        = channel.second.at(0);
				int         grp_id       = 4;
				int         smpl_mode_id = 1;
				double      smpl_val     = 0.;
				double      smpl_per     = 60.;
				int         retent_id    = 9999;
				double      retent_val   = 9999.;

				double      low_disp_rng   = 0.;
				double      high_disp_rng  = 0.;
				double      low_warn_lmt   = atof(channel.second.at(1).c_str());
				double      high_warn_lmt  = atof(channel.second.at(2).c_str());
				double      low_alarm_lmt  = atof(channel.second.at(3).c_str());
				double      high_alarm_lmt = atof(channel.second.at(4).c_str());
				int         prec           = atoi(channel.second.at(5).c_str());
				std::string unit           = channel.second.at(6);

				if(!checkIfPVExists(pvName))
				{
					mapOfPVInfo_[pvName] = new PVInfo(DBR_STRING);
					__COUT__ << "configure(): new PV '" << pvName << "' found! Now subscribing" << __E__;
					subscribe(pvName);
				}

				if(dcsArchiveDbConnStatus_ == 1)
				{
					PGresult* res = nullptr;
					char      buffer[1024];
					try
					{
						// ACTION FOR DB ARCHIVER CHANNEL TABLE
						snprintf(buffer, sizeof(buffer), "SELECT name FROM channel WHERE name = '%s';", pvName.c_str());

						res = PQexec(dcsArchiveDbConn, buffer);
						__COUT__ << "configure(): SELECT channel table PQntuples(res): " << PQntuples(res) << __E__;

						if(PQresultStatus(res) != PGRES_TUPLES_OK)
						{
							__SS__ << "configure(): SELECT FOR DATABASE CHANNEL TABLE FAILED!!! PV Name: " << pvName
							       << " PQ ERROR: " << PQresultErrorMessage(res) << __E__;
							PQclear(res);
							__SS_THROW__;
						}

						if(PQntuples(res) > 0)
						{
							// UPDATE DB ARCHIVER CHANNEL TABLE
							PQclear(res);
							__COUT__ << "configure(): Updating PV: " << pvName << " in the Archiver Database channel table" << __E__;
							snprintf(buffer,
							         sizeof(buffer),
							         "UPDATE channel SET					\
															  grp_id=%d			\
															, smpl_mode_id=%d	\
															, smpl_val=%f		\
															, smpl_per=%f		\
															, retent_id=%d		\
															, retent_val=%f		\
											WHERE name = '%s';",
							         grp_id,
							         smpl_mode_id,
							         smpl_val,
							         smpl_per,
							         retent_id,
							         retent_val,
							         pvName.c_str());
							//__COUT__ << "configure(): channel update select: " << buffer << __E__;

							res = PQexec(dcsArchiveDbConn, buffer);

							if(PQresultStatus(res) != PGRES_COMMAND_OK)
							{
								__SS__ << "configure(): CHANNEL UPDATE INTO DATABASE CHANNEL TABLE FAILED!!! PV Name: " << pvName
								       << " PQ ERROR: " << PQresultErrorMessage(res) << __E__;
								PQclear(res);
								__SS_THROW__;
							}
							PQclear(res);
						}
						else
						{
							// INSERT INTO DB ARCHIVER CHANNEL TABLE
							PQclear(res);
							__COUT__ << "configure(): Writing new PV in the Archiver Database channel table" << __E__;
							snprintf(buffer,
							         sizeof(buffer),
							         "INSERT INTO channel(					\
												  name				\
												, descr				\
												, grp_id			\
												, smpl_mode_id		\
												, smpl_val			\
												, smpl_per			\
												, retent_id			\
												, retent_val)		\
							VALUES ('%s', '%s', %d, %d, %f, %f, %d, %f);",
							         pvName.c_str(),
							         descr.c_str(),
							         grp_id,
							         smpl_mode_id,
							         smpl_val,
							         smpl_per,
							         retent_id,
							         retent_val);

							res = PQexec(dcsArchiveDbConn, buffer);
							if(PQresultStatus(res) != PGRES_COMMAND_OK)
							{
								__SS__ << "configure(): CHANNEL INSERT INTO DATABASE CHANNEL TABLE FAILED!!! PV Name: " << pvName
								       << " PQ ERROR: " << PQresultErrorMessage(res) << __E__;
								PQclear(res);
								__SS_THROW__;
							}
							PQclear(res);
						}

						// ACTION FOR DB ARCHIVER NUM_METADATA TABLE
						snprintf(
						    buffer,
						    sizeof(buffer),
						    "SELECT channel.channel_id FROM channel, num_metadata WHERE channel.channel_id = num_metadata.channel_id AND channel.name = '%s';",
						    pvName.c_str());

						res = PQexec(dcsArchiveDbConn, buffer);
						__COUT__ << "configure(): SELECT num_metadata table PQntuples(res): " << PQntuples(res) << __E__;

						if(PQresultStatus(res) != PGRES_TUPLES_OK)
						{
							__SS__ << "configure(): SELECT FOR DATABASE NUM_METADATA TABLE FAILED!!! PV Name: " << pvName
							       << " PQ ERROR: " << PQresultErrorMessage(res) << __E__;
							PQclear(res);
							__SS_THROW__;
						}

						if(PQntuples(res) > 0)
						{
							// UPDATE DB ARCHIVER NUM_METADATA TABLE
							std::string channel_id = PQgetvalue(res, 0, 0);
							__COUT__ << "configure(): Updating PV: " << pvName << " channel_id: " << channel_id
							         << " in the Archiver Database num_metadata table" << __E__;
							PQclear(res);
							snprintf(buffer,
							         sizeof(buffer),
							         "UPDATE num_metadata SET					\
												  low_disp_rng=%f		\
												, high_disp_rng=%f		\
												, low_warn_lmt=%f		\
												, high_warn_lmt=%f		\
												, low_alarm_lmt=%f		\
												, high_alarm_lmt=%f		\
												, prec=%d				\
												, unit='%s'				\
							WHERE channel_id='%s';",
							         low_disp_rng,
							         high_disp_rng,
							         low_warn_lmt,
							         high_warn_lmt,
							         low_alarm_lmt,
							         high_alarm_lmt,
							         prec,
							         unit.c_str(),
							         channel_id.c_str());

							res = PQexec(dcsArchiveDbConn, buffer);
							if(PQresultStatus(res) != PGRES_COMMAND_OK)
							{
								__SS__ << "configure(): CHANNEL UPDATE INTO DATABASE NUM_METADATA TABLE FAILED!!! PV Name(channel_id): " << pvName << " "
								       << channel_id << " PQ ERROR: " << PQresultErrorMessage(res) << __E__;
								PQclear(res);
								__SS_THROW__;
							}
							PQclear(res);
						}
						else
						{
							// INSERT INTO DB ARCHIVER NUM_METADATA TABLE
							snprintf(buffer, sizeof(buffer), "SELECT channel_id FROM channel WHERE name = '%s';", pvName.c_str());

							res = PQexec(dcsArchiveDbConn, buffer);
							__COUT__ << "configure(): SELECT channel table to check channel_id for num_metadata table. PQntuples(res): " << PQntuples(res)
							         << __E__;

							if(PQresultStatus(res) != PGRES_TUPLES_OK)
							{
								__SS__ << "configure(): SELECT TO DATABASE CHANNEL TABLE FOR NUM_MATADATA TABLE FAILED!!! PV Name: " << pvName
								       << " PQ ERROR: " << PQresultErrorMessage(res) << __E__;
								PQclear(res);
								__SS_THROW__;
							}

							if(PQntuples(res) > 0)
							{
								std::string channel_id = PQgetvalue(res, 0, 0);
								__COUT__ << "configure(): Writing new PV in the Archiver Database num_metadata table" << __E__;
								PQclear(res);

								snprintf(buffer,
								         sizeof(buffer),
								         "INSERT INTO num_metadata(			\
												  channel_id		\
												, low_disp_rng		\
												, high_disp_rng		\
												, low_warn_lmt		\
												, high_warn_lmt		\
												, low_alarm_lmt		\
												, high_alarm_lmt	\
												, prec				\
												, unit)				\
												VALUES ('%s',%f,%f,%f,%f,%f,%f,%d,'%s');",
								         channel_id.c_str(),
								         low_disp_rng,
								         high_disp_rng,
								         low_warn_lmt,
								         high_warn_lmt,
								         low_alarm_lmt,
								         high_alarm_lmt,
								         prec,
								         unit.c_str());

								res = PQexec(dcsArchiveDbConn, buffer);
								if(PQresultStatus(res) != PGRES_COMMAND_OK)
								{
									__SS__ << "configure(): CHANNEL INSERT INTO DATABASE NUM_METADATA TABLE FAILED!!! PV Name: " << pvName
									       << " PQ ERROR: " << PQresultErrorMessage(res) << __E__;
									PQclear(res);
									__SS_THROW__;
								}
								PQclear(res);
							}
							else
							{
								__SS__ << "configure(): CHANNEL INSERT INTO DATABASE NUM_METADATA TABLE FAILED!!! PV Name: " << pvName
								       << " NOT RECOGNIZED IN CHANNEL TABLE" << __E__;
								PQclear(res);
								__SS_THROW__;
							}
						}
					}
					catch(...)
					{
						__SS__ << "configure(): CHANNEL INSERT OR UPDATE INTO DATABASE FAILED!!! "
						       << " PQ ERROR: " << PQresultErrorMessage(res) << __E__;
						try	{ throw; } //one more try to printout extra info
						catch(const std::exception &e)
						{
							ss << "Exception message: " << e.what();
						}
						catch(...){}
						__SS_THROW__;
					}
				}
				else
				{
					// RAR 21-Dec-2022: remove exception throwing for cases when db connection not expected
					__COUT_INFO__ << "configure(): Archiver Database connection does not exist, so skipping channel update." << __E__;
					// __SS_THROW__;
					break;
				}
			}  // end channel name loop
		}
	}  // end slowControlsChannelsSourceTables loop
}  // end configure()

DEFINE_OTS_SLOW_CONTROLS(EpicsInterface)
