# iOS arm64 toolchain for the ios-arm64 preset. It needs a macOS host with
# Xcode: the SDK path comes from xcrun, so a configure on another host fails
# with the reason instead of a broken build.

set(CMAKE_SYSTEM_NAME iOS CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "")
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO" CACHE STRING "")

find_program(_zui_xcrun xcrun)
if(NOT _zui_xcrun)
  message(FATAL_ERROR
    "iOS builds need a macOS host with Xcode: xcrun is missing.")
endif()

execute_process(
  COMMAND "${_zui_xcrun}" --sdk iphoneos --show-sdk-path
  OUTPUT_VARIABLE _zui_ios_sdk
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT _zui_ios_sdk OR NOT IS_DIRECTORY "${_zui_ios_sdk}")
  message(FATAL_ERROR
    "xcrun could not find the iphoneos SDK. Install Xcode, then run "
    "`sudo xcode-select -s /Applications/Xcode.app`.")
endif()

set(CMAKE_OSX_SYSROOT "${_zui_ios_sdk}" CACHE STRING "")
message(STATUS "zui iOS SDK: ${_zui_ios_sdk}")
