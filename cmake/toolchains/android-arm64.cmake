# Android arm64 toolchain for the android-arm64 preset.
# Locates an NDK on this machine and forwards to its CMake toolchain.
# The preset does not hard-code an SDK path.
#
# arm64-v8a, API 24 (Vulkan's system loader), static libc++ so the APK
# does not need a second C++ runtime.

if(ANDROID_NDK_TOOLCHAIN_INCLUDED)
  return()
endif()

set(ANDROID_ABI "arm64-v8a" CACHE STRING "Android ABI")
set(ANDROID_PLATFORM "android-24" CACHE STRING "Android API level")
set(ANDROID_STL "c++_static" CACHE STRING "Android C++ runtime")
set(ANDROID_CPP_FEATURES "rtti exceptions" CACHE STRING "Android C++ features")

function(_zui_ndk_ok path out_var)
  if(EXISTS "${path}/build/cmake/android.toolchain.cmake")
    file(TO_CMAKE_PATH "${path}" _normalized)
    set(${out_var} "${_normalized}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

set(_zui_ndk "")
foreach(_env ANDROID_NDK_HOME ANDROID_NDK_ROOT ANDROID_NDK)
  if(NOT _zui_ndk AND DEFINED ENV{${_env}})
    _zui_ndk_ok("$ENV{${_env}}" _zui_ndk)
  endif()
endforeach()

if(NOT _zui_ndk)
  set(_sdk_roots "")
  if(DEFINED ENV{ANDROID_HOME})
    list(APPEND _sdk_roots "$ENV{ANDROID_HOME}")
  endif()
  if(DEFINED ENV{ANDROID_SDK_ROOT})
    list(APPEND _sdk_roots "$ENV{ANDROID_SDK_ROOT}")
  endif()
  if(DEFINED ENV{LOCALAPPDATA})
    list(APPEND _sdk_roots "$ENV{LOCALAPPDATA}/Android/Sdk")
  endif()
  if(DEFINED ENV{USERPROFILE})
    list(APPEND _sdk_roots "$ENV{USERPROFILE}/AppData/Local/Android/Sdk")
  endif()
  list(APPEND _sdk_roots "$ENV{HOME}/Android/Sdk")
  list(APPEND _sdk_roots "C:/Android/Sdk")

  set(_best "")
  set(_best_key "")
  foreach(_sdk IN LISTS _sdk_roots)
    if(NOT IS_DIRECTORY "${_sdk}/ndk")
      continue()
    endif()
    file(GLOB _ndks LIST_DIRECTORIES true "${_sdk}/ndk/*")
    foreach(_cand IN LISTS _ndks)
      if(NOT EXISTS "${_cand}/build/cmake/android.toolchain.cmake")
        continue()
      endif()
      get_filename_component(_name "${_cand}" NAME)
      if(_best STREQUAL "" OR _name VERSION_GREATER _best_key)
        set(_best "${_cand}")
        set(_best_key "${_name}")
      endif()
    endforeach()
  endforeach()
  if(NOT _best STREQUAL "")
    file(TO_CMAKE_PATH "${_best}" _zui_ndk)
  endif()
endif()

if(NOT _zui_ndk)
  message(FATAL_ERROR
    "Android NDK not found. Set ANDROID_NDK_HOME, or install an NDK under the Android SDK ndk/ directory.")
endif()

message(STATUS "zui android NDK: ${_zui_ndk}")
include("${_zui_ndk}/build/cmake/android.toolchain.cmake")
