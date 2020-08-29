# ======================================================================
#  otsdaq-epics main build file
#
#  cd .../path/to/build/directory
#  source .../path/to/otsdaq-epics/ups/setup_for_development
#  buildtool
# ======================================================================


# use cmake 3.17 or later
cmake_minimum_required (VERSION 3.17)


project(otsdaq-epics)
# cetbuildtools contains our cmake modules
SET ( CETBUILDTOOLS_VERSION $ENV{CETBUILDTOOLS_VERSION} )
IF (NOT CETBUILDTOOLS_VERSION)
    MESSAGE (FATAL_ERROR "ERROR: setup cetbuildtools to get the cmake modules")
ENDIF()

set(CMAKE_MODULE_PATH 
        $ENV{ART_DIR}/Modules
        $ENV{CETBUILDTOOLS_DIR}/Modules
        $ENV{XDAQ_DIR}/Modules
        $ENV{ARTDAQ_UTILITIES_DIR}/Modules
        ${CMAKE_MODULE_PATH})
message(STATUS "CMAKE_MODULE_PATH is ${CMAKE_MODULE_PATH}")

include(CetCMakeEnv)
cet_cmake_env()
include(FindUpsXdaq)

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
find_ups_product( postgresql v9_6_11b )
find_ups_product( otsdaq v1_00_00 )
find_ups_product( artdaq v0_04_02 )
find_ups_product( artdaq_core v1_00_00 )
find_ups_product( artdaq_utilities v1_00_00 )
find_ups_product( art v1_06_00 )
find_ups_product( fhiclcpp v2_16_00 )
find_ups_product( messagefacility v1_10_00 )
find_ups_product( cetlib v1_03_00 )
find_ups_xdaq( v11_2_7 )
find_ups_root( v5_34_00 )
find_ups_boost( v1_50_0 filesystem
                        program_options
                        unit_test_framework
 )


##find_package(MPI QUIET)



find_library( EPICS_CA NAMES ca PATHS $ENV{EPICS_LIBRARY} )
find_library( EPICS_COM NAMES Com PATHS $ENV{EPICS_LIBRARY} )


check_ups_version(xdaq "$ENV{XDAQ_VERSION}" v14_0_0 PRODUCT_OLDER_VAR slf6_xdaq)
if(slf6_xdaq)
	message("Building on SLF6, defining NOCONST")
  add_definitions(-DXDAQ_NOCONST)
endif()
# macros for art_dictionary and simple_plugin
#include(ArtDictionary)
include(ArtMake)
include(BuildPlugins)
include(CetTest)

# Code
add_subdirectory(otsdaq-epics)

# doxygen
add_subdirectory(doc)

# UPS Config
add_subdirectory(ups)

# packaging utility
include(UseCPack)
