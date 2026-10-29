cmake_minimum_required(VERSION 3.22.1)
project(leandoc C CXX)

# core/, miniz and pugixml are copied unchanged from the desktop repository;
# see third_party/README.md for the commit.
add_library(leandoc SHARED
    leandoc_jni.cpp
    core/Cfb.cpp core/DocCore.cpp core/Links.cpp
    core/Word.cpp core/Xml.cpp core/Zip.cpp
    miniz/miniz.c
    pugixml/pugixml.cpp)

target_include_directories(leandoc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(leandoc SYSTEM PRIVATE miniz pugixml)
# The same macros the desktop build uses for these two libraries.
target_compile_definitions(leandoc PRIVATE
    MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_WRITING_APIS
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES PUGIXML_NO_EXCEPTIONS PUGIXML_NO_XPATH)
target_compile_features(leandoc PRIVATE cxx_std_17)
target_compile_options(leandoc PRIVATE
    -O2 -fvisibility=hidden -ffunction-sections -fdata-sections)
# Unused core functions (file and path helpers for desktop hosts) are dropped
# by --gc-sections. 16 KB pages are required by Google Play for apps that
# target Android 15 or later.
target_link_options(leandoc PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384)
