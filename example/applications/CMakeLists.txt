add_subdirectory(album)
add_subdirectory(calendar)
add_subdirectory(chat)
add_subdirectory(contacts)
add_subdirectory(kanban)
add_subdirectory(reader)
# The particle session translation unit lands after the header. Configuring the
# object library before that file exists fails the whole tree.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/particle/cpp/session.cpp")
  add_subdirectory(particle)
endif()
add_subdirectory(share)
add_subdirectory(writer)
add_subdirectory(zdb)
# zmd's sources are optional in a partial tree; configuring before they exist fails the build.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/zmd/CMakeLists.txt")
  add_subdirectory(zmd)
endif()
# file-explorer has no native session.
