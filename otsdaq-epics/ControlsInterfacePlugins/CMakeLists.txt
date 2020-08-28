
get_cmake_property(_variableNames VARIABLES)
foreach(_variableName ${_variableNames})
	message(STATUS "${_variableName}=${${_variableName}}")
endforeach()



include_directories($ENV{XERCES_C_INC})



find_library( EPICS_CA NAMES ca PATHS $ENV{EPICS_LIBRARY} )
find_library( EPICS_COM NAMES Com PATHS $ENV{EPICS_LIBRARY} )
include_directories($ENV{EPICS_BASE}/include)
#include($ENV{EPICS_BASE}/include/alarm.h)
include_directories($ENV{EPICS_BASE}/include/os/Linux)
include_directories($ENV{EPICS_BASE}/include/compiler/gcc)
include_directories($ENV{EPICS_BASE}/include/lib/linux-x86_64)
include_directories($ENV{XERCES_C_INC})

simple_plugin(EpicsInterface "slowcontrols"	
	 ${EPICS_CA}
	 ${EPICS_COM}
	 ${XDAQ_BASIC_LIB_LIST}	 
	 SlowControlsCore
	 PluginMakers
	 Configurable
	 ConfigurationInterface
	 SlowControlsTableBase
	 TableCore
	 Macros
	${XDAQ_XERCES-C} 
	${MF_MESSAGELOGGER}
	 $ENV{POSTGRESQL_LIBRARIES}/libpq.so
  )

#simple_plugin(ControlsOtsInterface "slowcontrols"	
#    	ControlsCore
#	${CETLIB}
#	Configurable
#        ConfigurationInterface
#  )

install_headers()
install_source()
