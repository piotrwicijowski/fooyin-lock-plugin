cmake_minimum_required(VERSION 3.19)
project(FooyinLock VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

include(GNUInstallDirs)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Sql Concurrent Network)
find_package(Fooyin CONFIG REQUIRED)
message(STATUS "Using installed Fooyin SDK: ${Fooyin_DIR}")

# The SDK exports an absolute host plugin directory. Use a prefix-relative
# destination so a staged plugin install cannot overwrite the SDK/host install.
set(LOCK_PLUGIN_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/fooyin/plugins"
    CACHE STRING "Session Lock plugin installation directory")
set(FOOYIN_PLUGIN_INSTALL_DIR "${LOCK_PLUGIN_INSTALL_DIR}")
set(LIB_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/fooyin")

add_library(LockRuntime STATIC
    src/lockruntime.cpp src/lockruntime.h
    src/lockcoordinator.cpp src/lockcoordinator.h
    src/recoveryjournal.cpp src/recoveryjournal.h)
set_target_properties(LockRuntime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
target_include_directories(LockRuntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(LockRuntime PUBLIC Fooyin::Core Fooyin::Gui Fooyin::Utils Qt6::Widgets)
target_compile_definitions(LockRuntime PUBLIC QT_NO_KEYWORDS)
if(MSVC)
    target_compile_options(LockRuntime PRIVATE /W4)
else()
    target_compile_options(LockRuntime PRIVATE -Wall -Wextra -Wpedantic)
endif()

create_fooyin_plugin(Lock
    DEPENDS LockRuntime
    SOURCES src/lockplugin.cpp src/lockplugin.h)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
