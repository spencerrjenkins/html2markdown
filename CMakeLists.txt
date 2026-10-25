# html2md - HTML to Markdown converter
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#   cmake --install build --prefix /usr/local

cmake_minimum_required(VERSION 3.10)
project(html2md VERSION 1.0.0 LANGUAGES C)

option(HTML2MD_BUILD_TESTS "Build and register the test suites" ON)
option(HTML2MD_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    set(HTML2MD_WARNINGS /W4 /wd4100 /D_CRT_SECURE_NO_WARNINGS)
else()
    set(HTML2MD_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes
        -Wmissing-prototypes -Wformat=2 -Wundef -Wvla -Wpointer-arith -Wcast-qual
        -Wwrite-strings -Wno-unused-parameter)
    if(HTML2MD_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

set(HTML2MD_LIB_SOURCES
    src/arena.c src/buf.c src/convert.c src/dom.c src/encoding.c src/entities.c
    src/html2md.c src/parser.c src/selector.c src/sink.c src/tags.c src/url.c
    src/utf8.c)

add_library(html2md_lib STATIC ${HTML2MD_LIB_SOURCES})
set_target_properties(html2md_lib PROPERTIES OUTPUT_NAME html2md
                                             PUBLIC_HEADER include/html2md.h)
target_include_directories(html2md_lib
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE src)
target_compile_options(html2md_lib PRIVATE ${HTML2MD_WARNINGS})

add_executable(html2md src/main.c src/cli.c)
target_include_directories(html2md PRIVATE src)
target_link_libraries(html2md PRIVATE html2md_lib)
target_compile_options(html2md PRIVATE ${HTML2MD_WARNINGS})

add_executable(html2md_example examples/library_usage.c)
target_link_libraries(html2md_example PRIVATE html2md_lib)

include(GNUInstallDirs)
install(TARGETS html2md RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS html2md_lib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES docs/html2md.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
configure_file(html2md.pc.in ${CMAKE_CURRENT_BINARY_DIR}/html2md.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/html2md.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(HTML2MD_BUILD_TESTS)
    enable_testing()
    add_executable(unit_tests
        tests/unit/test_main.c tests/unit/test_utils.c tests/unit/test_parser.c
        tests/unit/test_selector.c tests/unit/test_url.c tests/unit/test_convert.c
        tests/unit/test_cli.c src/cli.c)
    target_include_directories(unit_tests PRIVATE src tests/unit)
    target_link_libraries(unit_tests PRIVATE html2md_lib)
    add_test(NAME unit COMMAND unit_tests)

    find_program(SH_PROGRAM sh)
    if(SH_PROGRAM)
        add_test(NAME fixtures
                 COMMAND ${SH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_fixtures.sh $<TARGET_FILE:html2md>)
        add_test(NAME cli
                 COMMAND ${SH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cli_tests.sh $<TARGET_FILE:html2md>)
    endif()

    find_program(CMARK_GFM cmark-gfm)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(CMARK_GFM AND Python3_Interpreter_FOUND)
        add_test(NAME roundtrip
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip.py
                         --html2md $<TARGET_FILE:html2md> --cmark ${CMARK_GFM})
    endif()
endif()
