
set(TEE_LIBRARY_TARGETS)

function(tee_add_library target_name)
    add_library(${target_name} ${ARGN})
    list(APPEND TEE_LIBRARY_TARGETS ${target_name})
    set(TEE_LIBRARY_TARGETS ${TEE_LIBRARY_TARGETS} PARENT_SCOPE)
endfunction()

function(_addlibrary target_name)
    add_library(${target_name} ${ARGN})
endfunction()
