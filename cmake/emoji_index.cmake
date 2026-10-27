# The emoji key index (a resource) and the emoji set's content hash (a header),
# generated at configure time so every target in this build tree — the app, its
# tests and the Wasm client — shares one copy. Configure re-runs when the qrc,
# any emoji image or the generator changes.
set(DECENZA_EMOJI_INDEX_DIR "${CMAKE_BINARY_DIR}/generated/emoji_index")

function(_decenza_generate_emoji_index)
    get_property(_done GLOBAL PROPERTY DECENZA_EMOJI_INDEX_GENERATED)
    if(_done)
        return()
    endif()
    set_property(GLOBAL PROPERTY DECENZA_EMOJI_INDEX_GENERATED TRUE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_script "${CMAKE_SOURCE_DIR}/scripts/generate_emoji_index.py")
    set(_qrc "${CMAKE_SOURCE_DIR}/resources/emoji.qrc")
    file(GLOB _images CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/emoji/*.svg")
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_script}" "${_qrc}" ${_images})
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_script}" --qrc "${_qrc}"
            --index "${DECENZA_EMOJI_INDEX_DIR}/emoji-index.txt"
            --header "${DECENZA_EMOJI_INDEX_DIR}/decenza_emoji_set.h"
        RESULT_VARIABLE _result ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Could not generate the emoji index: ${_error}")
    endif()
endfunction()

# Embeds :/emoji-index.txt in `target` and puts decenza_emoji_set.h on its include path.
function(decenza_add_emoji_index target)
    _decenza_generate_emoji_index()
    target_include_directories(${target} PRIVATE "${DECENZA_EMOJI_INDEX_DIR}")
    qt_add_resources(${target} emoji_index PREFIX "/" BASE "${DECENZA_EMOJI_INDEX_DIR}"
        FILES "${DECENZA_EMOJI_INDEX_DIR}/emoji-index.txt")
endfunction()
