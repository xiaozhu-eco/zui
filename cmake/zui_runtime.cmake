# Copy runtime shared libs next to a target (Windows DLL side-by-side).
# Prefer COMMAND_EXPAND_LISTS so paths with drive letters are not broken by cmd.exe `|`.

function(zui_stage_runtime tgt)
  if(NOT TARGET ${tgt})
    message(FATAL_ERROR "zui_stage_runtime: missing target ${tgt}")
  endif()
  add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:${tgt}>"
            "$<TARGET_FILE_DIR:${tgt}>"
    COMMAND_EXPAND_LISTS
    COMMENT "Stage runtime DLLs next to ${tgt}"
  )
endfunction()