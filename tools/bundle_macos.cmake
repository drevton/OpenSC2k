cmake_minimum_required(VERSION 3.21)
include(BundleUtilities)
fixup_bundle("${SC2K_APP}" "" "")
# install_name_tool changes copied libraries, invalidating any upstream signature.
file(GLOB_RECURSE libraries "${SC2K_APP}/Contents/*.dylib")
foreach(library IN LISTS libraries)
  execute_process(COMMAND codesign --force --sign - "${library}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
