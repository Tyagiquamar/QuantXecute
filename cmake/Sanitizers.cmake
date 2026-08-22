# Central sanitizer configuration.
#
# IMPORTANT: sanitizer instrumentation must be applied to the production
# static libraries (quantxecute_core, quantxecute_feed, quantxecute_feed_okx)
# AND to every final executable. Compiling only a sanitized test executable
# against unsanitized libraries would leave all library code (i.e. most of
# the engine) uninstrumented, silently defeating ASan/UBSan/TSan runs.
#
# - ASan and UBSan can be combined in one build.
# - TSan must stay separate from ASan.
function(qx_apply_sanitizers target)
    if(MSVC)
        if(QX_SANITIZE_ADDRESS OR QX_SANITIZE_UNDEFINED OR QX_SANITIZE_THREAD)
            message(WARNING "qx_apply_sanitizers: MSVC sanitizer flags are not configured; ignoring for ${target}")
        endif()
        return()
    endif()

    set(_qx_san_flags "")
    if(QX_SANITIZE_ADDRESS AND QX_SANITIZE_THREAD)
        message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be combined")
    endif()
    if(QX_SANITIZE_ADDRESS)
        list(APPEND _qx_san_flags address)
    endif()
    if(QX_SANITIZE_UNDEFINED)
        list(APPEND _qx_san_flags undefined)
    endif()
    if(QX_SANITIZE_THREAD)
        list(APPEND _qx_san_flags thread)
    endif()

    if(_qx_san_flags)
        string(REPLACE ";" "," _qx_san_str "${_qx_san_flags}")
        target_compile_options(${target} PRIVATE -fsanitize=${_qx_san_str} -fno-omit-frame-pointer)
        # Static libraries record but do not consume link options; final
        # executables need this to pull in the sanitizer runtime.
        target_link_options(${target} PRIVATE -fsanitize=${_qx_san_str})
    endif()
endfunction()