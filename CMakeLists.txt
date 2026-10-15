cmake_minimum_required(VERSION 3.16)

project(mustela
    VERSION 0.7.0
    DESCRIPTION "Privacy-first, cross-platform browser engine built on WebKit"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MUSTELA_BUILD_SHELL "Build the mustela shell harness" ON)
option(MUSTELA_BUILD_TESTS "Build the core test suite" ON)
set(MUSTELA_WEBKIT_SDK "" CACHE PATH "Windows: directory with include/ (WebKit C API headers) and lib/ (WebKit import libraries)")

add_library(mustela_warnings INTERFACE)
if(MSVC)
    target_compile_options(mustela_warnings INTERFACE /W4)
else()
    target_compile_options(mustela_warnings INTERFACE -Wall -Wextra)
endif()

add_library(mustela_core STATIC
    src/core/config.cpp
    src/core/privacy.cpp
    src/core/site_rules.cpp
    src/core/fingerprint.cpp
    src/core/history.cpp
    src/core/url.cpp)
target_include_directories(mustela_core PUBLIC src src/core)
target_link_libraries(mustela_core PUBLIC mustela_warnings)

if(MUSTELA_BUILD_TESTS)
    enable_testing()
    add_executable(mustela_core_tests src/tests/test_core.cpp)
    target_link_libraries(mustela_core_tests PRIVATE mustela_core)
    add_test(NAME mustela_core_tests COMMAND mustela_core_tests)
    set_tests_properties(mustela_core_tests PROPERTIES
        ENVIRONMENT "MUSTELA_TEST_TMP=${CMAKE_CURRENT_BINARY_DIR}/test-tmp")
endif()

if(MUSTELA_BUILD_SHELL)
    add_executable(mustela src/shell/main.cpp)
    target_link_libraries(mustela PRIVATE mustela_core)

    if(WIN32)
        if(MUSTELA_WEBKIT_SDK)
            target_sources(mustela PRIVATE src/shell/win/shell_win.cpp)
            target_include_directories(mustela PRIVATE ${MUSTELA_WEBKIT_SDK}/include)
            target_link_directories(mustela PRIVATE ${MUSTELA_WEBKIT_SDK}/lib)
            target_link_libraries(mustela PRIVATE
                WebKit WebCore WTF JavaScriptCore PAL
                user32 gdi32 comctl32 ole32 shell32)
        else()
            target_sources(mustela PRIVATE src/shell/win/shell_win_stub.cpp)
            target_link_libraries(mustela PRIVATE user32)
        endif()
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)

        target_sources(mustela PRIVATE
            src/shell/gtk/shell_gtk.cpp
            src/shell/gtk/tab.cpp)

        target_include_directories(mustela PRIVATE ${WEBKIT_INCLUDE_DIRS})
        target_link_directories(mustela PRIVATE ${WEBKIT_LIBRARY_DIRS})
        target_link_libraries(mustela PRIVATE ${WEBKIT_LIBRARIES})
        target_compile_options(mustela PRIVATE ${WEBKIT_CFLAGS_OTHER})
    endif()

    target_link_libraries(mustela PRIVATE mustela_warnings)
endif()
