cmake_minimum_required(VERSION 3.15)
project(FontExact LANGUAGES CXX C)

# ---------------------------------------------------------------
# Standalone HD-font DLL for WoW 3.3.5 (build 12340)
# Extracted from Transmorpher 2.0.0 — font subsystem only.
# Requires: MSVC (32-bit target), WinSDK, cmake, vcpkg or bundled deps.
# ---------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

# ---------------------------------------------------------------
# Third-party dependencies (same as original Transmorpher)
# These must be present under third_party/ — copy from original.
# ---------------------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG       ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ  ON CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/Detours"
                 "${CMAKE_BINARY_DIR}/third_party/Detours")

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/freetype-2.14.1"
                 "${CMAKE_BINARY_DIR}/third_party/freetype-2.14.1")
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/msdfgen"
                 "${CMAKE_BINARY_DIR}/third_party/msdfgen")

# ---------------------------------------------------------------
# font_exact sources  (exclude MSDF.cpp — included by outer MSDF.cpp)
# ---------------------------------------------------------------
file(GLOB FONT_EXACT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/font_exact/*.cpp"
)
list(REMOVE_ITEM FONT_EXACT_SOURCES
    "${CMAKE_SOURCE_DIR}/src/font_exact/MSDF.cpp")

# ---------------------------------------------------------------
# DLL sources  (no Morpher / Hooks / SpellMorph / D3DHooks / etc.)
# ---------------------------------------------------------------
set(SOURCES
    "${CMAKE_SOURCE_DIR}/src/dllmain.cpp"
    "${CMAKE_SOURCE_DIR}/src/Proxy.cpp"
    "${CMAKE_SOURCE_DIR}/src/MSDF.cpp"
    ${FONT_EXACT_SOURCES}
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release")

add_library(dinput8 SHARED ${SOURCES}
    "${CMAKE_SOURCE_DIR}/src/dinput8_exports.def")

target_include_directories(dinput8 PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party"
    "${CMAKE_SOURCE_DIR}/third_party/unordered_dense/unordered_dense-4.8.1/include"
)

target_link_libraries(dinput8
    psapi
    d3d9
    Detours
    Freetype::Freetype
    msdfgen-core
    msdfgen-ext
)

set_target_properties(dinput8 PROPERTIES
    OUTPUT_NAME "dinput8"
    PREFIX      ""
    SUFFIX      ".dll"
)

if(MSVC)
    target_compile_options(dinput8 PRIVATE /W3)
    set_target_properties(dinput8 PROPERTIES
        LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/src/dinput8_exports.def\""
    )
else()
    target_compile_options(dinput8 PRIVATE -m32 -Wall)
    set_target_properties(dinput8 PROPERTIES
        LINK_FLAGS "-m32 -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup"
    )
endif()
