#[================================================================[.rst:
FindEPICS
----------
  find EPICS

#]================================================================]
if (EPICS_FIND_REQUIRED)
  set(_cet_EPICS_FIND_REQUIRED ${EPICS_FIND_REQUIRED})
  unset(EPICS_FIND_REQUIRED)
else()
  unset(_cet_EPICS_FIND_REQUIRED)
endif()
find_package(EPICS CONFIG QUIET)
if (_cet_EPICS_FIND_REQUIRED)
  set(EPICS_FIND_REQUIRED ${_cet_EPICS_FIND_REQUIRED})
  unset(_cet_EPICS_FIND_REQUIRED)
endif()
if (EPICS_FOUND)
  set(_cet_EPICS_config_mode CONFIG_MODE)
else()
  unset(_cet_EPICS_config_mode)
  find_file(_cadef_h NAMES cadef.h HINTS ENV EPICS_BASE PATH_SUFFIXES include)
  if (_cadef_h)
    #message("Found cadef.h: ${_cadef_h}")
    get_filename_component(_cet_EPICS_include_dir "${_cadef_h}" PATH)
    if (_cet_EPICS_include_dir STREQUAL "/")
      unset(_cet_EPICS_include_dir)
    endif()
  endif()

  find_file(_compilerSpecific_h NAMES compilerSpecific.h HINTS ENV EPICS_BASE ${_cet_EPICS_include_dir} PATH_SUFFIXES compiler/clang compiler/gcc)
  if (_compilerSpecific_h)
    #message("Found compilerSpecific.h: ${_compilerSpecific_h}")
    get_filename_component(_cet_EPICS_include_dir_compiler "${_compilerSpecific_h}" PATH)
    if (_cet_EPICS_include_dir_compiler STREQUAL "/")
      unset(_cet_EPICS_include_dir_compiler)
    endif()
  endif()

  if (EXISTS "${_cet_EPICS_include_dir}")
    set(EPICS_FOUND TRUE)
    get_filename_component(_cet_EPICS_dir "${_cet_EPICS_include_dir}" PATH)
    if (_cet_EPICS_dir STREQUAL "/")
      unset(_cet_EPICS_dir)
    endif()
    set(EPICS_INCLUDE_DIRS "${_cet_EPICS_include_dir};${_cet_EPICS_include_dir_compiler};${_cet_EPICS_include_dir}/os/Linux")
    set(EPICS_LIBRARY_DIR "${_cet_EPICS_dir}/lib/linux-x86_64")
    find_library( EPICS_LIBRARY NAMES ca PATHS ${EPICS_LIBRARY_DIR} REQUIRED)
    find_library( EPICS_COM_LIBRARY NAMES Com PATHS ${EPICS_LIBRARY_DIR} REQUIRED)
  endif()
endif()
if (EPICS_FOUND)
  if (NOT TARGET EPICS::ca)
    add_library(EPICS::ca SHARED IMPORTED)
    set_target_properties(EPICS::ca PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${EPICS_INCLUDE_DIRS}"
      IMPORTED_LOCATION "${EPICS_LIBRARY}"
      )
    set(EPICS_LIBRARY "EPICS::ca")
  endif()
  if (NOT TARGET EPICS::Com)
    add_library(EPICS::Com SHARED IMPORTED)
    set_target_properties(EPICS::Com PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${EPICS_INCLUDE_DIRS}"
      IMPORTED_LOCATION "${EPICS_COM_LIBRARY}"
      )
    set(EPICS_COM_LIBRARY "EPICS::Com")
  endif()
  if (CETMODULES_CURRENT_PROJECT_NAME AND
      ${CETMODULES_CURRENT_PROJECT_NAME}_OLD_STYLE_CONFIG_VARS)
    include_directories("${EPICS_INCLUDE_DIRS}")
    set(ca "${EPICS_LIBRARY}")
    set(Com "${EPICS_COM_LIBRARY}")
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(EPICS ${_cet_EPICS_config_mode}
  REQUIRED_VARS EPICS_FOUND
  EPICS_INCLUDE_DIRS
  EPICS_LIBRARY EPICS_COM_LIBRARY)

unset(_cet_EPICS_FIND_REQUIRED)
unset(_cet_EPICS_config_mode)
unset(_cet_EPICS_dir)
unset(_cet_EPICS_include_dir)
unset(_cadef_h CACHE)

