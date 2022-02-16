# ======================================================================
#  otsdaq-epics main build file
#
#  cd .../path/to/build/directory
#  source .../path/to/otsdaq-epics/ups/setup_for_development
#  buildtool
# ======================================================================


# use cmake 3.17 or later
cmake_minimum_required (VERSION 3.17)

find_package(cetmodules)
set(CET_CETBUILDTOOLS_COMPAT TRUE)
include(Compatibility)
set(CET_WARN_DEPRECATED)

project(otsdaq_epics VERSION 2.06.04)

set(CMAKE_MODULE_PATH 
        $ENV{XDAQ_DIR}/Modules
        $ENV{ARTDAQ_UTILITIES_DIR}/Modules
        ${CMAKE_MODULE_PATH})
message(STATUS "CMAKE_MODULE_PATH is ${CMAKE_MODULE_PATH}")

include(CetCMakeEnv)
cet_cmake_env()

cet_set_compiler_flags(
 DIAGS 
 VIGILANT
 WERROR
 NO_UNDEFINED
 EXTRA_FLAGS -pedantic
 )
 

#string(TOUPPER ${CMAKE_BUILD_TYPE} BTYPE_UC )
#if( ${BTYPE_UC} MATCHES "DEBUG" )
#  cet_add_compiler_flags(-fsanitize=address)
#endif()

cet_report_compiler_flags()

# these are minimum required versions, not the actual product versions
#find_ups_product( epics v3_15_2 )
find_ups_product( cetbuildtools v3_03_00 )
find_ups_product( epics v3_15_2 )
find_ups_product( postgresql v10_0 )
find_ups_product( otsdaq v1_00_00 )
find_ups_product( artdaq v0_04_02 )
find_ups_product( artdaq_core v1_00_00 )
find_ups_product( artdaq_utilities v1_00_00 )
find_ups_product( artdaq_database v1_03_07 )
find_ups_product( xdaq v16_7_0_1)
find_ups_product( art v1_06_00 )
find_ups_product( fhiclcpp v2_16_00 )
find_ups_product( messagefacility v1_10_00 )
find_ups_product( TRACE v3_03_00 )
find_ups_product( cetlib v1_03_00 )
find_ups_root( v5_34_00 )
find_ups_boost( v1_50_0 filesystem
                        program_options
                        unit_test_framework
 )
 
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

# macros for art_dictionary and simple_plugin
#include(ArtDictionary)
include(ArtMake)
include(BuildPlugins)
include(CetTest)

# Code
add_subdirectory(otsdaq-epics)

# Documentation (to comment out if not needed DOXYGEN locally)
if( $ENV{OTS_DOXY} MATCHES "DOIT" )
	add_subdirectory(doc)
endif()

cet_cmake_config()
