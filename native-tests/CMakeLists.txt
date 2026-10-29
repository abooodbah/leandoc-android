cmake_minimum_required(VERSION 3.22)
project(leandoc_core_tests C CXX)

# Builds the desktop core tests against the copy of the core that ships in the
# app, with the desktop's fixtures (kept once, in the instrumented test assets).
set(CPP ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp)
set(FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/androidTest/assets/fixtures)

add_library(leandoc_core STATIC
    ${CPP}/core/Cfb.cpp ${CPP}/core/DocCore.cpp ${CPP}/core/Links.cpp
    ${CPP}/core/Word.cpp ${CPP}/core/Xml.cpp ${CPP}/core/Zip.cpp
    ${CPP}/miniz/miniz.c ${CPP}/pugixml/pugixml.cpp)
target_include_directories(leandoc_core PUBLIC ${CPP})
target_include_directories(leandoc_core SYSTEM PUBLIC ${CPP}/miniz ${CPP}/pugixml)
target_compile_definitions(leandoc_core PUBLIC
    MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_WRITING_APIS
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES PUGIXML_NO_EXCEPTIONS PUGIXML_NO_XPATH)
target_compile_features(leandoc_core PUBLIC cxx_std_17)

add_executable(leandoc_core_tests CoreTests.cpp TestZip.cpp)
target_include_directories(leandoc_core_tests PRIVATE ${FIXTURES})
target_link_libraries(leandoc_core_tests PRIVATE leandoc_core)

enable_testing()
add_test(NAME leandoc-core COMMAND leandoc_core_tests ${FIXTURES})
