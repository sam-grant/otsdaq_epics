
#get_cmake_property(_variableNames VARIABLES)
#foreach(_variableName ${_variableNames})
#	message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()

include_directories($ENV{EPICS_BASE}/include)
include_directories($ENV{EPICS_BASE}/include/os/Linux)
include_directories($ENV{EPICS_BASE}/include/compiler/gcc)
include_directories($ENV{EPICS_BASE}/include/compiler/clang)
include_directories($ENV{EPICS_BASE}/include/lib/linux-x86_64)

link_directories($ENV{EPICS_LIBRARY})
link_directories($ENV{POSTGRESQL_LIBRARIES})

simple_plugin(EpicsInterface "slowcontrols"	
	 ca # From EPICS
	 Com # From EPICS
	 ${XDAQ_BASIC_LIB_LIST}	 
	 otsdaq::SlowControlsCore
	 otsdaq::PluginMakers
	 otsdaq::Configurable
	 otsdaq::ConfigurationInterface
	 otsdaq::SlowControlsTableBase
	 otsdaq::TableCore
	 otsdaq::Macros
	${XDAQ_XERCES-C} 
	MF_MessageLogger
	 pq # from postgresql
  )

#simple_plugin(ControlsOtsInterface "slowcontrols"	
#    	ControlsCore
#	cetlib
#	Configurable
#        ConfigurationInterface
#  )

install_headers()
install_source()
