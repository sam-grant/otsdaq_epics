
#get_cmake_property(_variableNames VARIABLES)
#foreach(_variableName ${_variableNames})
#	message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()

include_directories(${PostgreSQL_INCLUDE_DIRS})

include(otsdaq::slowControls)
cet_build_plugin(EpicsInterface otsdaq::slowControls	LIBRARIES REG
otsdaq::SlowControlsTableBase
	EPICS::ca
	EPICS::Com
	 ${PostgreSQL_LIBRARIES}
  )

install_headers()
install_source()
