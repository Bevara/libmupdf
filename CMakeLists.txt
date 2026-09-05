cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

# The flag has to be on the compile line too, not only at link: dec_mupdf.c
# uses fz_try, which is a setjmp macro, and would otherwise be code-generated
# with the JavaScript SjLj and its invoke_* trampolines.
add_compile_options(-sSUPPORT_LONGJMP=wasm)

SET(LIBMUPDF_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_mupdf.c
)

# -sSUPPORT_LONGJMP=wasm: MuPDF's fz_try/fz_catch is built on setjmp/longjmp.
# Emscripten's default JavaScript SjLj turns that into invoke_* trampolines the
# host has to provide, and the solver exports none of the signatures MuPDF
# needs, so the side module fails to instantiate. The WebAssembly flavour keeps
# the whole mechanism inside the module.
#
# libmupdf-third bundles the codecs MuPDF needs (freetype, jbig2dec, openjpeg,
# libjpeg, zlib...); it has to come after libmupdf on the link line.
SET(LIBMUPDF_LIBS
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libmupdf.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libmupdf-third.a
)

SET(LIBMUPDF_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libmupdf
        "${LIBMUPDF_SRC}"
        "${LIBMUPDF_LIBS}"
        []
        ""
        "${LIBMUPDF_INC}"
        "-sSUPPORT_LONGJMP=wasm"
        "1")
