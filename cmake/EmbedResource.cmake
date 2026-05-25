# EmbedResource — generate a .c source containing a named const byte array
# from an arbitrary input file. Usage:
#
#   include(EmbedResource.cmake)
#   embed_resource(my_name path/to/file.bin)
#
# Appends a generated .c path to EMBEDDED_RESOURCE_SOURCES in the parent scope.
# Declarations live in src/resources/embedded.hpp.

set(_EMBED_RESOURCE_OUT_DIR "${CMAKE_BINARY_DIR}/embedded")
file(MAKE_DIRECTORY "${_EMBED_RESOURCE_OUT_DIR}")

function(embed_resource NAME INPUT)
    set(_in_abs "${CMAKE_SOURCE_DIR}/${INPUT}")
    set(_out_abs "${_EMBED_RESOURCE_OUT_DIR}/${NAME}.c")
    add_custom_command(
        OUTPUT "${_out_abs}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT=${_in_abs}
            -DOUTPUT=${_out_abs}
            -DSYMBOL=${NAME}
            -P "${CMAKE_SOURCE_DIR}/cmake/embed_resource_script.cmake"
        DEPENDS "${_in_abs}" "${CMAKE_SOURCE_DIR}/cmake/embed_resource_script.cmake"
        COMMENT "Embedding ${INPUT} as ${NAME}"
        VERBATIM
    )
    set(EMBEDDED_RESOURCE_SOURCES ${EMBEDDED_RESOURCE_SOURCES} "${_out_abs}" PARENT_SCOPE)
endfunction()
