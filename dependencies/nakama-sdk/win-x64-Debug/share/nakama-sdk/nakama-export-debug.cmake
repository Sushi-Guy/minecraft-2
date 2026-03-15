#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "nakama-sdk" for configuration "Debug"
set_property(TARGET nakama-sdk APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(nakama-sdk PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/nakama-sdk.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/nakama-sdk.dll"
  )

list(APPEND _cmake_import_check_targets nakama-sdk )
list(APPEND _cmake_import_check_files_for_nakama-sdk "${_IMPORT_PREFIX}/lib/nakama-sdk.lib" "${_IMPORT_PREFIX}/lib/nakama-sdk.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
