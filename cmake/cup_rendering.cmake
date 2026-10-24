# Both application targets compile the same shaders under their canonical QML URLs.
function(decenza_add_cup_rendering target)
    qt_add_shaders(${target} "cup_shaders"
        BATCHABLE
        PREFIX "/"
        BASE "${CMAKE_SOURCE_DIR}"
        FILES
            "${CMAKE_SOURCE_DIR}/shaders/cup_mask.frag"
            "${CMAKE_SOURCE_DIR}/shaders/cup_lighten.frag"
    )
endfunction()
