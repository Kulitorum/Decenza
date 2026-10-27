# The emoji key index (a resource) and the content hashes of the emoji and map image
# sets the host serves to the Wasm client (a header), generated at configure time so
# every target in this build tree — the app, its tests and the Wasm client — shares
# one copy. Configure re-runs when the emoji qrc, any of these images or the
# generator changes.
set(DECENZA_IMAGE_SETS_DIR "${CMAKE_BINARY_DIR}/generated/image_sets")

function(_decenza_generate_image_sets)
    get_property(_done GLOBAL PROPERTY DECENZA_IMAGE_SETS_GENERATED)
    if(_done)
        return()
    endif()
    set_property(GLOBAL PROPERTY DECENZA_IMAGE_SETS_GENERATED TRUE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_script "${CMAKE_SOURCE_DIR}/scripts/generate_image_sets.py")
    set(_qrc "${CMAKE_SOURCE_DIR}/resources/emoji.qrc")
    file(GLOB _emoji CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/emoji/*.svg")
    file(GLOB _maps CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/maps/*")
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_script}" "${_qrc}" ${_emoji} ${_maps})
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_script}" --emoji-qrc "${_qrc}" --maps ${_maps}
            --index "${DECENZA_IMAGE_SETS_DIR}/emoji-index.txt"
            --header "${DECENZA_IMAGE_SETS_DIR}/decenza_image_sets.h"
        RESULT_VARIABLE _result ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Could not generate the image sets: ${_error}")
    endif()
endfunction()

# Embeds :/emoji-index.txt in `target` and puts decenza_image_sets.h on its include path.
function(decenza_add_image_sets target)
    _decenza_generate_image_sets()
    target_include_directories(${target} PRIVATE "${DECENZA_IMAGE_SETS_DIR}")
    qt_add_resources(${target} emoji_index PREFIX "/" BASE "${DECENZA_IMAGE_SETS_DIR}"
        FILES "${DECENZA_IMAGE_SETS_DIR}/emoji-index.txt")
endfunction()
