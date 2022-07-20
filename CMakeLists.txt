# ======================================================================
#  otsdaq-epics main build file
#
#  cd .../path/to/build/directory
#  source .../path/to/otsdaq-epics/ups/setup_for_development
#  buildtool
# ======================================================================


# use cmake 3.17 or later
cmake_minimum_required (VERSION 3.19 FATAL_ERROR)

find_package(cetmodules 3.16.00 REQUIRED)

project(otsdaq_epics VERSION 2.06.06)

include(CetCMakeEnv)
cet_cmake_env()

# Find our own CMake modules, and tell everyone else where to find them.
cet_cmake_module_directories(Modules BINARY)

cet_set_compiler_flags(
 DIAGS 
 VIGILANT
 WERROR
 NO_UNDEFINED
 EXTRA_FLAGS -pedantic
 )
 
cet_report_compiler_flags()

find_package(otsdaq 2.06.06 REQUIRED)
find_package(PostgreSQL 13.2 REQUIRED)
find_package(EPICS 7.0.6.1 REQUIRED)

 # XDAQ Extra setup
 include_directories($ENV{XDAQ_INC}/linux)
link_directories($ENV{XDAQ_LIB})
link_directories($ENV{ARTDAQ_DATABASE_LIB})
 set(XDAQ_BASIC_LIB_LIST 
   xcept
   xoap
   xdaq
   toolbox
   xerces-c
   cgicc
 )
 set(XDAQ_XERCES-C xerces-c)

# Code
add_subdirectory(otsdaq-epics)

# Documentation (to comment out if not needed DOXYGEN locally)
if( $ENV{OTS_DOXY} MATCHES "DOIT" )
	add_subdirectory(doc)
endif()

cet_cmake_config()
