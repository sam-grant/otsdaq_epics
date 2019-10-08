#include "otsdaq/Macros/SlowControlsPluginMacros.h"
#include "otsdaq-epics/ControlsInterfacePlugins/EpicsInterface.h"
#include "alarm.h"  //Holds strings that we can use to access the alarm status, severity, and parameters
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

#define DEBUG false
#define PV_FILE_NAME \
	std::string(getenv("SERVICE_DATA_PATH")) + "/SlowControlsDashboardData/pv_list.dat";
#define PV_CSV_DIR \
	"/home/mu2edcs/mu2e-dcs/make_db/csv";
using namespace ots;

EpicsInterface::EpicsInterface(
    const std::string&       pluginType,
    const std::string&       interfaceUID,
    const ConfigurationTree& theXDAQContextConfigTree,
    const std::string&       controlsConfigurationPath)
: SlowControlsVInterface(
          pluginType, interfaceUID, theXDAQContextConfigTree, controlsConfigurationPath)
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

	__GEN_COUT__ << "Epics Interface initialized!";
	return;
}

std::string EpicsInterface::getList(std::string format)
{
	std::string pvList;
	//pvList = "[\"None\"]";
	//std::cout << "SUCA: Returning pvList as: " << pvList << std::endl;
	//return pvList;
	
	__GEN_COUT__ << "Epics Interface now retrieving pvList!";

	if(format == "JSON")
	{
		__GEN_COUT__ << "Getting list in JSON format! There are " << mapOfPVInfo_.size() << " pv's.";
		// pvList = "{\"PVList\" : [";
		pvList = "[";
		for(auto it = mapOfPVInfo_.begin(); it != mapOfPVInfo_.end(); it++)
		{
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

void EpicsInterface::subscribe(std::string pvName)
{
	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	createChannel(pvName);
	sleep(1); 	//what makes the console hang at startup
	subscribeToChannel(pvName, mapOfPVInfo_.find(pvName)->second->channelType);
	SEVCHK(ca_poll(), "EpicsInterface::subscribe() : ca_poll");   //print outs that handle takeover the console; can make our own error handler

	return;
}

//{"PVList" : ["Mu2e_BeamData_IOC/CurrentTime"]}
void EpicsInterface::subscribeJSON(std::string pvList)
{
	// if(DEBUG){__GEN_COUT__ << pvList << __E__;;}

	std::string JSON = "{\"PVList\" :";
	std::string pvName;
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
				subscribeToChannel(pvName,
				                   mapOfPVInfo_.find(pvName)->second->channelType);
				SEVCHK(ca_poll(), "EpicsInterface::subscribeJSON : ca_poll");
			}
			else if(DEBUG)
			{
				__GEN_COUT__ << pvName << " not found in file! Not subscribing!"
				          << __E__;
			}

		} while(pvList.find(",") != std::string::npos);
	}

	return;
}

void EpicsInterface::unsubscribe(std::string pvName)
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
	chid chid = eha.chid;
	if(eha.status == ECA_NORMAL)
	{
		int                  i;
		union db_access_val* pBuf = (union db_access_val*)eha.dbr;
		if(DEBUG)
		{
			printf("channel %s: ", ca_name(eha.chid));
		}

		switch(eha.type)
		{
		case DBR_CTRL_CHAR:
			if(true)
			{
				__COUT__ << "Response Type: DBR_CTRL_CHAR" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVControlValueToRecord(
			        ca_name(eha.chid),
			        ((struct dbr_ctrl_char*)
			             eha.dbr));  // write the PV's control values to records
			break;
		case DBF_DOUBLE:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_DOUBLE" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVValueToRecord(
			        ca_name(eha.chid),
			        std::to_string(
			            *((double*)eha.dbr)));  // write the PV's value to records
			break;
		case DBR_STS_STRING:
			if(DEBUG)
			{
				__COUT__ << "Response Type: DBR_STS_STRING" << __E__;
			}
			((EpicsInterface*)eha.usr)
			    ->writePVAlertToQueue(ca_name(eha.chid),
			                          epicsAlarmConditionStrings[pBuf->sstrval.status],
			                          epicsAlarmSeverityStrings[pBuf->sstrval.severity]);
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
			    ->writePVAlertToQueue(ca_name(eha.chid),
			                          epicsAlarmConditionStrings[pBuf->sshrtval.status],
			                          epicsAlarmSeverityStrings[pBuf->sshrtval.severity]);
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
			    ->writePVAlertToQueue(ca_name(eha.chid),
			                          epicsAlarmConditionStrings[pBuf->sfltval.status],
			                          epicsAlarmSeverityStrings[pBuf->sfltval.severity]);
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
			    ->writePVAlertToQueue(ca_name(eha.chid),
			                          epicsAlarmConditionStrings[pBuf->senmval.status],
			                          epicsAlarmSeverityStrings[pBuf->senmval.severity]);
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
			    ->writePVAlertToQueue(ca_name(eha.chid),
			                          epicsAlarmConditionStrings[pBuf->schrval.status],
			                          epicsAlarmSeverityStrings[pBuf->schrval.severity]);
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
			    ->writePVAlertToQueue(ca_name(eha.chid),
			                          epicsAlarmConditionStrings[pBuf->slngval.status],
			                          epicsAlarmSeverityStrings[pBuf->slngval.severity]);
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
			    ->writePVAlertToQueue(ca_name(eha.chid),
			                          epicsAlarmConditionStrings[pBuf->sdblval.status],
			                          epicsAlarmSeverityStrings[pBuf->sdblval.severity]);
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
					__COUT__ << " EpicsInterface::eventCallback: PV Name = "
					          << ca_name(eha.chid) << __E__;
					__COUT__ << (char*)eha.dbr << __E__;
				}
				((EpicsInterface*)eha.usr)
				    ->writePVValueToRecord(
				        ca_name(eha.chid),
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

		/*status_ =	ca_array_get_callback(dbf_type_to_DBR_STS(mapOfPVInfo_.find(pv)->second->channelType),
					ca_element_count(cha.chid), cha.chid, eventCallback, this); SEVCHK(status_,
					"ca_array_get_callback");*/
	}
	else
		__GEN_COUT__ << pv << " disconnected!" << __E__;

	return;
}

bool EpicsInterface::checkIfPVExists(std::string pvName)
{
	if(DEBUG)
	{
		__GEN_COUT__ << "EpicsInterface::checkIfPVExists(): PV Info Map Length is "
		          << mapOfPVInfo_.size() << __E__;
	}

	if(mapOfPVInfo_.find(pvName) != mapOfPVInfo_.end())
		return true;

	return false;
}

void EpicsInterface::loadListOfPVs()
{
	__GEN_COUT__ << "LOADING LIST OF PVS!!!!";
	std::string pv_csv_dir_path = PV_CSV_DIR;
	std::vector<std::string> files = std::vector <std::string>();
	DIR *dp;
    	struct dirent *dirp;
    	if((dp  = opendir(pv_csv_dir_path.c_str())) == NULL) {
        	std::cout << "Error  opening: " << pv_csv_dir_path << std::endl;
        	return;
    	}
	
    	while ((dirp = readdir(dp)) != NULL) {
			files.push_back(std::string(dirp->d_name));
    	}
    	closedir(dp);

    	/*	
	for (unsigned int i = 0;i < files.size();i++) {
        	std::cout << files[i] << std::endl;
	}
	*/
	
	// Initialize Channel Access
	status_ = ca_task_initialize();
	SEVCHK(status_, "EpicsInterface::loadListOfPVs() : Unable to initialize");
	if(status_ != ECA_NORMAL)
		exit(-1);

	//for each file
	//int referenceLength = 0;
	std::vector <std::string> csv_line;
	std::string pv_name, cluster, category, system, sensor;
	cluster = "Mu2e";
	unsigned int i,j;
	
	//First two entries will be . & ..
	for (i = 2; i < files.size(); i++) 
	{

        	//std::cout << pv_csv_dir_path << "/" <<files[i] << std::endl;
		std::string pv_list_file = pv_csv_dir_path + "/" + files[i];
		__GEN_COUT__ << "Reading: " << pv_list_file << std::endl;

		// read file
		// for each line in file
		//std::string pv_list_file = PV_FILE_NAME;
		//__GEN_COUT__ << pv_list_file;	
	
		std::ifstream infile(pv_list_file);
		if(!infile.is_open())
		{
			__GEN_SS__ << "Failed to open PV list file: '" << pv_list_file << "'" << __E__;
			__GEN_SS_THROW__;
		}
		__GEN_COUT__ << "Reading file" << __E__;

		// make map of pvname -> PVInfo
		//Example line of csv
 		//CompStatus,daq01,fans_fastest_rpm,0,rpm,16e3,12e3,2e3,1e3,,,,Passive,,fans_fastest_rpm daq01
		for(std::string line; getline(infile, line);)
		{
			//__GEN_COUT__ << line << __E__;
			csv_line.clear();	
			std::istringstream ss(line);
			std::string token;

			while(std::getline(ss, token, ','))
				csv_line.push_back(token);
			if (csv_line.at(0)[0] != '#')
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

	__GEN_COUT__ << "Here is our pv list!"  << __E__;
	// subscribe for each pv
	for(auto pv : mapOfPVInfo_)
	{
		__GEN_COUT__ << pv.first << __E__;
		subscribe(pv.first);
	}
		
	// channels are subscribed to by here.

	// get parameters (e.g. HIHI("upper alarm") HI("upper warning") LOLO("lower
	// alarm")) for each pv
	for(auto pv : mapOfPVInfo_)
	{
		getControlValues(pv.first);
	}

	__GEN_COUT__ << "Finished reading file and subscribing to pvs!" << __E__;
	return;
}

void EpicsInterface::getControlValues(std::string pvName)
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

	SEVCHK(ca_array_get_callback(DBR_CTRL_CHAR,
	                             0,
	                             mapOfPVInfo_.find(pvName)->second->channelID,
	                             eventCallback,
	                             this),
	       "ca_array_get_callback");
	SEVCHK(ca_poll(), "EpicsInterface::getControlValues() : ca_poll");
	return;
}

void EpicsInterface::createChannel(std::string pvName)
{
	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	__GEN_COUT__ << "Trying to create channel to " << pvName << ":"
	          << mapOfPVInfo_.find(pvName)->second->channelID << __E__;

	if(mapOfPVInfo_.find(pvName)->second != NULL)  // Check to see if the pvName
	                                               // maps to a null pointer so we
	                                               // don't have any errors
		if(mapOfPVInfo_.find(pvName)->second->channelID !=
		   NULL)  // channel might exist, subscription doesn't so create a
		          // subscription
		{
			// if state of channel is connected then done, use it
			if(ca_state(mapOfPVInfo_.find(pvName)->second->channelID) == cs_conn)
			{
				if(DEBUG)
				{
					__GEN_COUT__ << "Channel to " << pvName << " already exists!"
					          << __E__;
				}
				return;
			}
			if(DEBUG)
			{
				__GEN_COUT__ << "Channel to " << pvName
				          << " exists, but is not connected! Destroying current channel."
				          << __E__;
			}
			destroyChannel(pvName);
		}

	// create pvs handler
	if(mapOfPVInfo_.find(pvName)->second->parameterPtr == NULL)
	{
		mapOfPVInfo_.find(pvName)->second->parameterPtr =
		    new PVHandlerParameters(pvName, this);
	}

	// at this point, make a new channel

	SEVCHK(ca_create_channel(pvName.c_str(),
	                         staticChannelCallbackHandler,
	                         mapOfPVInfo_.find(pvName)->second->parameterPtr,
	                         0,
	                         &(mapOfPVInfo_.find(pvName)->second->channelID)),
	       "EpicsInterface::createChannel() : ca_create_channel");
	__GEN_COUT__ << "channelID: " << pvName << mapOfPVInfo_.find(pvName)->second->channelID	<< __E__;
	SEVCHK(ca_poll(), "EpicsInterface::createChannel() : ca_poll"); //This routine will perform outstanding channel access background activity and then return.

	
	return;
}

void EpicsInterface::destroyChannel(std::string pvName)
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

void EpicsInterface::subscribeToChannel(std::string pvName, chtype subscriptionType)
{
	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	if(DEBUG)
	{
		__GEN_COUT__ << "Trying to subscribe to " << pvName << ":"
		          << mapOfPVInfo_.find(pvName)->second->channelID << __E__;
	}

	if(mapOfPVInfo_.find(pvName)->second != NULL)  // Check to see if the pvName
	                                               // maps to a null pointer so we
	                                               // don't have any errors
	{
		if(mapOfPVInfo_.find(pvName)->second->eventID !=
		   NULL)  // subscription already exists
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
	//		{__SS__;throw std::runtime_error(ss.str() + "Channel failed for " +
	// pvName);}

	SEVCHK(ca_create_subscription(
	           dbf_type_to_DBR(mapOfPVInfo_.find(pvName)->second->channelType),
	           1,
	           mapOfPVInfo_.find(pvName)->second->channelID,
	           DBE_VALUE | DBE_ALARM | DBE_PROPERTY,
	           eventCallback,
	           this,
	           &(mapOfPVInfo_.find(pvName)->second->eventID)),
	       "EpicsInterface::subscribeToChannel() : ca_create_subscription");
	if(DEBUG)
	{
		__GEN_COUT__ << "EpicsInterface::subscribeToChannel: Created Subscription to "
		          << mapOfPVInfo_.find(pvName)->first << "!\n"
		          << __E__;
	}
	SEVCHK(ca_poll(), "EpicsInterface::subscribeToChannel() : ca_poll");

	return;
}

void EpicsInterface::cancelSubscriptionToChannel(std::string pvName)
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

void EpicsInterface::readValueFromPV(std::string pvName)
{
	// SEVCHK(ca_get(DBR_String, 0, mapOfPVInfo_.find(pvName)->second->channelID,
	// &(mapOfPVInfo_.find(pvName)->second->pvValue), eventCallback,
	// &(mapOfPVInfo_.find(pvName)->second->callbackPtr)), "ca_get");

	return;
}

void EpicsInterface::writePVControlValueToRecord(std::string           pvName,
                                                 struct dbr_ctrl_char* pdata)
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

	if(true)
	{
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
		__GEN_COUT__ << "RISC_pad: " << pdata->RISC_pad << __E__;
		__GEN_COUT__ << "Value: " << pdata->value << __E__;
	}
	return;
}

// Enforces the circular buffer
void EpicsInterface::writePVValueToRecord(std::string pvName, std::string pdata)
{
	std::pair<time_t, std::string> currentRecord(time(0), pdata);

	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	// __GEN_COUT__ << pdata << __E__;

	PVInfo* pvInfo = mapOfPVInfo_.find(pvName)->second;

	if(pvInfo->mostRecentBufferIndex != pvInfo->dataCache.size() - 1 &&
	   pvInfo->mostRecentBufferIndex != (unsigned int)(-1))
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
	// debugConsole(pvName);

	return;
}

void EpicsInterface::writePVAlertToQueue(std::string pvName,
                                         const char* status,
                                         const char* severity)
{
	if(!checkIfPVExists(pvName))
	{
		__GEN_COUT__ << pvName << " doesn't exist!" << __E__;
		return;
	}
	PVAlerts alert(time(0), status, severity);
	mapOfPVInfo_.find(pvName)->second->alerts.push(alert);

	// debugConsole(pvName);

	return;
}

void EpicsInterface::readPVRecord(std::string pvName)
{
	status_ = ca_array_get_callback(
	    dbf_type_to_DBR_STS(mapOfPVInfo_.find(pvName)->second->channelType),
		ca_element_count(mapOfPVInfo_.find(pvName)->second->channelID),
	    mapOfPVInfo_.find(pvName)->second->channelID,
	    eventCallback,
	    this);
	SEVCHK(status_, "EpicsInterface::readPVRecord(): ca_array_get_callback");
	return;
}

void EpicsInterface::debugConsole(std::string pvName)
{
	__GEN_COUT__ << "==============================================================="
	             "==============="
	          << __E__;
	for(unsigned int it = 0; it < mapOfPVInfo_.find(pvName)->second->dataCache.size() - 1;
	    it++)
	{
		if(it == mapOfPVInfo_.find(pvName)->second->mostRecentBufferIndex)
		{
			__GEN_COUT__ << "-----------------------------------------------------------"
			             "----------"
			          << __E__;
		}
		__GEN_COUT__ << "Iteration: " << it << " | "
		          << mapOfPVInfo_.find(pvName)->second->mostRecentBufferIndex << " | "
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
	          << " | " << mapOfPVInfo_.find(pvName)->second->alerts.size() << " | "
	          << mapOfPVInfo_.find(pvName)->second->alerts.front().status << __E__;
	__GEN_COUT__ << "Severity:   "
	          << " | " << mapOfPVInfo_.find(pvName)->second->alerts.size() << " | "
	          << mapOfPVInfo_.find(pvName)->second->alerts.front().severity << __E__;
	__GEN_COUT__ << "==============================================================="
	             "==============="
	          << __E__;

	return;
}

void EpicsInterface::popQueue(std::string pvName)
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

std::array<std::string, 4> EpicsInterface::getCurrentValue(std::string pvName)
{
	__GEN_COUT__ << "void EpicsInterface::getCurrentValue() reached" << __E__;

	if(mapOfPVInfo_.find(pvName) != mapOfPVInfo_.end())
	{
		//unsubscribe(pvName);
		//subscribe(pvName);

		PVInfo*     pv = mapOfPVInfo_.find(pvName)->second;
		std::string time, value, status, severity;

		int index = pv->mostRecentBufferIndex;

		__GEN_COUT__ << pv << index << __E__;

		if(0 <= index && index < pv->circularBufferSize)
		{
			//__GEN_COUT__ << pv->dataCache[index].first <<" "<< std::time(0)-60 << __E__;

			time     = std::to_string(pv->dataCache[index].first);
			value    = pv->dataCache[index].second;
			status   = pv->alerts.front().status;
			severity = pv->alerts.front().severity;
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

std::array<std::string, 9> EpicsInterface::getSettings(std::string pvName)
{
	__GEN_COUT__ << "void EpicsInterface::getPVSettings() reached" << __E__;

	if(mapOfPVInfo_.find(pvName) != mapOfPVInfo_.end())
	{
		if(mapOfPVInfo_.find(pvName)->second != NULL)  // Check to see if the pvName
		                                               // maps to a null pointer so
		                                               // we don't have any errors
			if(mapOfPVInfo_.find(pvName)->second->channelID !=
			   NULL)  // channel might exist, subscription doesn't so create a
			          // subscription
			{
				dbr_ctrl_char* set = &mapOfPVInfo_.find(pvName)->second->settings;
				std::string units, upperDisplayLimit, lowerDisplayLimit, upperAlarmLimit,
				    upperWarningLimit, lowerWarningLimit, lowerAlarmLimit,
				    upperControlLimit, lowerControlLimit;
				// sprintf(&units[0],"%d",set->units);
				//			    	units = set->units;
				//					sprintf(&upperDisplayLimit[0],"%u",set->upper_disp_limit);
				//					sprintf(&lowerDisplayLimit[0],"%u",set->lower_disp_limit
				//);
				//					sprintf(&lowerDisplayLimit[0],"%u",set->lower_disp_limit
				//); 					sprintf(
				//&upperAlarmLimit[0],"%u",set->upper_alarm_limit  );
				//					sprintf(&upperWarningLimit[0],"%u",set->upper_warning_limit);
				//					sprintf(&lowerWarningLimit[0],"%u",set->lower_warning_limit);
				//					sprintf(
				//&lowerAlarmLimit[0],"%u",set->lower_alarm_limit  );
				//					sprintf(&upperControlLimit[0],"%u",set->upper_ctrl_limit);
				//					sprintf(&lowerControlLimit[0],"%u",set->lower_ctrl_limit);

				//					std::string units             =
				// set->units;
				//					std::string upperDisplayLimit
				//(reinterpret_cast<char*>(set->upper_disp_limit   ));
				//					std::string lowerDisplayLimit
				//(reinterpret_cast<char*>(set->lower_disp_limit   ));
				//					std::string upperAlarmLimit
				//(reinterpret_cast<char*>(set->upper_alarm_limit  ));
				//					std::string upperWarningLimit
				//(reinterpret_cast<char*>(set->upper_warning_limit));
				//					std::string lowerWarningLimit
				//(reinterpret_cast<char*>(set->lower_warning_limit));
				//					std::string lowerAlarmLimit
				//(reinterpret_cast<char*>(set->lower_alarm_limit  ));
				//					std::string upperControlLimit
				//(reinterpret_cast<char*>(set->upper_ctrl_limit   ));
				//					std::string lowerControlLimit
				//(reinterpret_cast<char*>(set->lower_ctrl_limit   ));
				if(DEBUG)
				{
					__GEN_COUT__ << "Units              :    " << units << __E__;
					__GEN_COUT__ << "Upper Display Limit:    " << upperDisplayLimit
					          << __E__;
					__GEN_COUT__ << "Lower Display Limit:    " << lowerDisplayLimit
					          << __E__;
					__GEN_COUT__ << "Upper Alarm Limit  :    " << upperAlarmLimit
					          << __E__;
					__GEN_COUT__ << "Upper Warning Limit:    " << upperWarningLimit
					          << __E__;
					__GEN_COUT__ << "Lower Warning Limit:    " << lowerWarningLimit
					          << __E__;
					__GEN_COUT__ << "Lower Alarm Limit  :    " << lowerAlarmLimit
					          << __E__;
					__GEN_COUT__ << "Upper Control Limit:    " << upperControlLimit
					          << __E__;
					__GEN_COUT__ << "Lower Control Limit:    " << lowerControlLimit
					          << __E__;
				}
			}

		std::array<std::string, 9> s = {
		    "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd"};

		// std::array<std::string, 9> s = {units, upperDisplayLimit,
		// lowerDisplayLimit, upperAlarmLimit, upperWarningLimit, lowerWarningLimit,
		// lowerAlarmLimit, upperControlLimit, lowerControlLimit};

		return s;
	}
	else
	{
		__GEN_COUT__ << pvName << " was not found!" << __E__;
		__GEN_COUT__ << "Trying to resubscribe to " << pvName << __E__;
		subscribe(pvName);
	}
	std::array<std::string, 9> s = {
	    "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd", "DC'd"};
	return s;
}

/*****************************************************************************/
/*                                                                           */
/*  dbSystemLogin dbSystemLogout getHistory Antonio 09.24.2019               */
/*                                                                           */
/*****************************************************************************/
void EpicsInterface::dbSystemLogin()
{
	int i = 0;

	dbconn = PQconnectdb("dbname=dcs_archive host=mu2edaq12 port=5432 user=dcs_reader password=ses3e-17!dcs_reader");
		
	if (PQstatus(dbconn) == CONNECTION_BAD) {
		__GEN_COUT__ << "Unable to connect to the database!\n" << __E__;
		PQfinish(dbconn);
	}
	else{
		__GEN_COUT__ << "Connected to the database!\n" << __E__;
		i = 1;
	}
}

void EpicsInterface::dbSystemLogout()
{
	if (PQstatus(dbconn) == CONNECTION_OK)
	{
		PQfinish(dbconn);
		__GEN_COUT__ << "DB CONNECTION CLOSED\n" << __E__;
	}
}

std::array<std::array<std::string, 5>, 10> EpicsInterface::getPVHistory(std::string pvName)
{
	__GEN_COUT__ << "void EpicsInterface::getPVHistory() reached" << __E__;

	if(mapOfPVInfo_.find(pvName) != mapOfPVInfo_.end())
	{
		PGresult *res;
		char buffer[1024];

		//VIEW LAST 10 UPDATES
		int num = snprintf(buffer, sizeof(buffer),
		"SELECT FLOOR(EXTRACT(EPOCH FROM smpl_time)), float_val, status.name, severity.name, smpl_per FROM channel, sample, status, severity WHERE channel.channel_id = sample.channel_id AND sample.severity_id = severity.severity_id  AND sample.status_id = status.status_id AND channel.name = \'%s\' ORDER BY smpl_time desc LIMIT 10", pvName.c_str());

		res = PQexec(dbconn, buffer);

		if (PQresultStatus(res) == PGRES_TUPLES_OK)
		{
			std::string s;
			std::array<std::array<std::string, 5>, 10> history;

			/* first, print out the attribute names */
			int nFields = PQnfields(res);

			/* next, print out the rows */
			for (int i = 0; i < PQntuples(res); i++)
			{
				for (int j = 0; j < nFields; j++)
				{
					history[i][j] = PQgetvalue(res, i, j);
					s.append( PQgetvalue(res, i, j));
					s.append(" ");
				}
				s.append("\n");
			}
			__GEN_COUT__ << s << __E__;
			PQclear(res);
			return history;
		}
		else
		{
			__GEN_COUT__ << "SELECT failed: " << PQerrorMessage(dbconn) << __E__;
			PQclear(res);
		}

		PQclear(res);
	}
	else
	{
		__GEN_COUT__ << pvName << " was not found!" << __E__;
		__GEN_COUT__ << "Trying to resubscribe to " << pvName << __E__;
		subscribe(pvName);
	}

	std::array<std::array<std::string, 5>, 10> history;
	for (size_t i=0; i<history.size(); i++)
		history[i]= {"PV Not Found", "NF", "N/a", "N/a"};
	return history;
}

DEFINE_OTS_SLOW_CONTROLS(EpicsInterface)
