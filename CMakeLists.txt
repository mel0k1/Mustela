cmake_minimum_required(VERSION 3.16)

project(mustela
    VERSION 0.24.0
    DESCRIPTION "Privacy-first, cross-platform browser engine built on WebKit"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MUSTELA_BUILD_SHELL "Build the mustela shell harness" ON)
option(MUSTELA_BUILD_TESTS "Build the core test suite" ON)
option(MUSTELA_BUILD_BENCH "Build the page-load benchmark harness" ON)
option(MUSTELA_BUILD_VIEW "Build the GTK view library for embedders" ON)
set(MUSTELA_WEBKIT_SDK "" CACHE PATH "Windows: directory with include/ (WebKit C API headers) and lib/ (WebKit import libraries)")
set(MUSTELA_WEBEXT_DIR "" CACHE PATH "Web extension directory baked into the GTK shell (defaults to the build directory)")

add_library(mustela_warnings INTERFACE)
if(MSVC)
    target_compile_options(mustela_warnings INTERFACE /W4)
else()
    target_compile_options(mustela_warnings INTERFACE -Wall -Wextra)
endif()

add_library(mustela_core STATIC
    src/core/config.cpp
    src/core/search.cpp
    src/core/autofill.cpp
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
    src/core/discard.cpp
    src/core/doh.cpp
    src/core/proxy_server.cpp
    src/core/session.cpp
    src/core/bookmarks.cpp
    src/core/import.cpp
    src/core/profile.cpp
    src/core/error_page.cpp)
target_include_directories(mustela_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/core>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(mustela_core PUBLIC mustela_warnings)

install(TARGETS mustela_core EXPORT mustela_targets ARCHIVE DESTINATION lib)
install(TARGETS mustela_warnings EXPORT mustela_targets)
install(DIRECTORY include/mustela DESTINATION include)
install(DIRECTORY src/core/ DESTINATION include/mustela FILES_MATCHING PATTERN "*.hpp")
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

if(MUSTELA_BUILD_VIEW)
    if(WIN32)
        if(MUSTELA_WEBKIT_SDK)
            add_library(mustela_view_win STATIC src/view/win/view_win.cpp)
            target_link_libraries(mustela_view_win PUBLIC mustela_core)
            target_include_directories(mustela_view_win PRIVATE ${MUSTELA_WEBKIT_SDK}/include)
            target_link_directories(mustela_view_win PRIVATE ${MUSTELA_WEBKIT_SDK}/lib)
            target_link_libraries(mustela_view_win PRIVATE
                WebKit2 JavaScriptCore user32 gdi32 ole32)
            install(TARGETS mustela_view_win EXPORT mustela_targets ARCHIVE DESTINATION lib)
            export(EXPORT mustela_targets
                FILE "${CMAKE_BINARY_DIR}/mustela-targets.cmake"
                NAMESPACE mustela::)
        else()
            message(STATUS "mustela_view_win needs MUSTELA_WEBKIT_SDK (WebKit C API headers and import libraries)")
        endif()
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)
        add_library(mustela_view_gtk STATIC src/view/gtk/view_gtk.cpp)
        target_link_libraries(mustela_view_gtk PUBLIC mustela_core)
        target_include_directories(mustela_view_gtk PRIVATE ${WEBKIT_INCLUDE_DIRS})
        target_link_directories(mustela_view_gtk PRIVATE ${WEBKIT_LIBRARY_DIRS})
        target_link_libraries(mustela_view_gtk PRIVATE ${WEBKIT_LIBRARIES})
        target_compile_options(mustela_view_gtk PRIVATE ${WEBKIT_CFLAGS_OTHER})
        if(NOT MUSTELA_WEBEXT_DIR)
            set(MUSTELA_WEBEXT_DIR "${CMAKE_BINARY_DIR}")
        endif()
        target_compile_definitions(mustela_view_gtk PRIVATE
            MUSTELA_WEBEXT_DIR="${MUSTELA_WEBEXT_DIR}")
        install(TARGETS mustela_view_gtk EXPORT mustela_targets ARCHIVE DESTINATION lib)
        export(EXPORT mustela_targets
            FILE "${CMAKE_BINARY_DIR}/mustela-targets.cmake"
            NAMESPACE mustela::)
        if(MUSTELA_BUILD_TESTS)
            add_executable(mustela_view_smoke src/tests/test_view_gtk.cpp)
            target_link_libraries(mustela_view_smoke PRIVATE mustela_view_gtk)
            target_include_directories(mustela_view_smoke PRIVATE ${WEBKIT_INCLUDE_DIRS})
            target_link_directories(mustela_view_smoke PRIVATE ${WEBKIT_LIBRARY_DIRS})
            target_link_libraries(mustela_view_smoke PRIVATE ${WEBKIT_LIBRARIES})
            target_compile_options(mustela_view_smoke PRIVATE ${WEBKIT_CFLAGS_OTHER})
            add_test(NAME mustela_view_smoke COMMAND mustela_view_smoke)
            set_tests_properties(mustela_view_smoke PROPERTIES TIMEOUT 240)
        endif()
    endif()
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
                WebKit2 JavaScriptCore
                user32 gdi32 comctl32 comdlg32 ole32 shell32 psapi winhttp)
            if(MUSTELA_BUILD_BENCH)
                add_executable(mustela-bench-win src/bench/bench_win.cpp)
                target_include_directories(mustela-bench-win PRIVATE ${MUSTELA_WEBKIT_SDK}/include)
                target_link_directories(mustela-bench-win PRIVATE ${MUSTELA_WEBKIT_SDK}/lib)
                target_link_libraries(mustela-bench-win PRIVATE WebKit2 user32 ole32)
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
        if(NOT MUSTELA_WEBEXT_DIR)
            set(MUSTELA_WEBEXT_DIR "${CMAKE_BINARY_DIR}")
        endif()
        target_compile_definitions(mustela PRIVATE
            MUSTELA_WEBEXT_DIR="${MUSTELA_WEBEXT_DIR}")
        install(TARGETS mustela RUNTIME DESTINATION bin)
        if(MUSTELA_BUILD_BENCH)
            install(TARGETS mustela-bench RUNTIME DESTINATION bin)
        endif()
        install(TARGETS mustela-webext LIBRARY DESTINATION lib/mustela/webext)
    endif()

    target_link_libraries(mustela PRIVATE mustela_warnings)
endif()

install(EXPORT mustela_targets
    FILE mustela-targets.cmake
    NAMESPACE mustela::
    DESTINATION lib/cmake/mustela)

set(CPACK_PACKAGE_NAME "mustela")
set(CPACK_PACKAGE_VENDOR "mustela")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Privacy-first, cross-platform browser engine built on WebKit")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "mustela")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_NAME "mustela")
set(CPACK_DEBIAN_PACKAGE_SECTION "web")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "mel0k1 <218306052+mel0k1@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
include(CPack)
