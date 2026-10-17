cmake_minimum_required(VERSION 3.16)

project(mustela
    VERSION 0.16.0
    DESCRIPTION "Privacy-first, cross-platform browser engine built on WebKit"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MUSTELA_BUILD_SHELL "Build the mustela shell harness" ON)
option(MUSTELA_BUILD_TESTS "Build the core test suite" ON)
option(MUSTELA_BUILD_BENCH "Build the page-load benchmark harness" ON)
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
    src/core/perf.cpp
    src/core/permissions.cpp
    src/core/ui_text.cpp
    src/core/url.cpp
    src/core/psl.cpp
    src/core/blocklist.cpp
    src/core/doh.cpp
    src/core/proxy_server.cpp
    src/core/session.cpp
    src/core/bookmarks.cpp
    src/core/profile.cpp
    src/core/error_page.cpp)
target_include_directories(mustela_core PUBLIC src src/core include)
target_link_libraries(mustela_core PUBLIC mustela_warnings)

install(TARGETS mustela_core ARCHIVE DESTINATION lib)
install(DIRECTORY include/mustela DESTINATION include)

if(WIN32)
    target_link_libraries(mustela_core PUBLIC ws2_32)
endif()

if(MUSTELA_BUILD_TESTS)
    enable_testing()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-tmp")
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
                user32 gdi32 comctl32 comdlg32 ole32 shell32 psapi winhttp)
            if(MUSTELA_BUILD_BENCH)
                add_executable(mustela-bench-win src/bench/bench_win.cpp)
                target_include_directories(mustela-bench-win PRIVATE ${MUSTELA_WEBKIT_SDK}/include)
                target_link_directories(mustela-bench-win PRIVATE ${MUSTELA_WEBKIT_SDK}/lib)
                target_link_libraries(mustela-bench-win PRIVATE WebKit user32 ole32)
            endif()
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

        if(MUSTELA_BUILD_BENCH)
            add_executable(mustela-bench src/bench/bench_gtk.cpp)
            target_link_libraries(mustela-bench PRIVATE mustela_core)
            target_include_directories(mustela-bench PRIVATE ${WEBKIT_INCLUDE_DIRS})
            target_link_directories(mustela-bench PRIVATE ${WEBKIT_LIBRARY_DIRS})
            target_link_libraries(mustela-bench PRIVATE ${WEBKIT_LIBRARIES})
            target_compile_options(mustela-bench PRIVATE ${WEBKIT_CFLAGS_OTHER})
        endif()

        pkg_check_modules(WEBEXT REQUIRED webkit2gtk-web-extension-4.1)
        add_library(mustela-webext MODULE src/webext/gtk/webext_gtk.cpp)
        target_include_directories(mustela-webext PRIVATE ${WEBEXT_INCLUDE_DIRS})
        target_link_directories(mustela-webext PRIVATE ${WEBEXT_LIBRARY_DIRS})
        target_link_libraries(mustela-webext PRIVATE ${WEBEXT_LIBRARIES})
        target_compile_options(mustela-webext PRIVATE ${WEBEXT_CFLAGS_OTHER})
        set_target_properties(mustela-webext PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
        target_compile_definitions(mustela PRIVATE
            MUSTELA_WEBEXT_DIR="${CMAKE_BINARY_DIR}")
    endif()

    target_link_libraries(mustela PRIVATE mustela_warnings)
endif()
