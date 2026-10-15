cmake_minimum_required(VERSION 3.16)

project(mustela
    VERSION 0.2.0
    DESCRIPTION "Privacy-first, cross-platform browser engine built on WebKit"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mustela
    src/shell/main.cpp
    src/core/config.cpp
    src/core/privacy.cpp)

target_include_directories(mustela PRIVATE src)

if(WIN32)
    target_sources(mustela PRIVATE src/shell/win/shell_win.cpp)
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

if(MSVC)
    target_compile_options(mustela PRIVATE /W4)
else()
    target_compile_options(mustela PRIVATE -Wall -Wextra)
endif()
