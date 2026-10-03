cmake_minimum_required(VERSION 3.7)

project(Simple-WebSocket-Server VERSION 2.0.3 LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
endif()

option(USE_STANDALONE_ASIO "set ON to use standalone Asio instead of Boost.Asio" OFF)
if(CMAKE_SOURCE_DIR STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    option(BUILD_TESTING "set ON to build library tests" ON)
else()
    option(BUILD_TESTING "set ON to build library tests" OFF)
endif()

add_library(simple-websocket-server INTERFACE)
add_library(SimpleWebSocketServer::simple-websocket-server ALIAS simple-websocket-server)
target_compile_features(simple-websocket-server INTERFACE cxx_std_11)

if(CMAKE_SOURCE_DIR STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(simple-websocket-server INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/simple-websocket-server>)
else()
    target_include_directories(simple-websocket-server SYSTEM INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
endif()


find_package(Threads REQUIRED)
target_link_libraries(simple-websocket-server INTERFACE Threads::Threads)

# TODO 2020 when Debian Jessie LTS ends:
# Remove Boost system, thread, regex components; use Boost::<component> aliases; remove Boost target_include_directories
if(USE_STANDALONE_ASIO)
    target_compile_definitions(simple-websocket-server INTERFACE ASIO_STANDALONE)
    # There is no canonical way to use Asio from CMake.
    # In particular, Asio does not support CMake natively.
    # However, Conan and Vcpkg do provide CMake support on their own.
    # Prefer the CMake target and fall back to finding asio.hpp.
    if(NOT TARGET asio::asio)
        find_package(asio)
    endif()
    if(TARGET asio::asio)
        target_link_libraries(simple-websocket-server INTERFACE asio::asio)
    else()
        find_path(ASIO_PATH asio.hpp)
        if(NOT ASIO_PATH)
            message(FATAL_ERROR "Standalone Asio not found")
        endif()
        add_library(asio::asio INTERFACE IMPORTED)
        set_target_properties(asio::asio PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${ASIO_PATH}")
        target_link_libraries(simple-websocket-server INTERFACE asio::asio)
    endif()
else()
    # In Boost 1.89.0+, system is header-only, so we try to find it but make it optional
    find_package(Boost 1.54.0 COMPONENTS system)
    if(NOT Boost_FOUND)
        # Fallback: try without system component for newer Boost versions where it's header-only
        find_package(Boost 1.54.0 REQUIRED)
    endif()

    target_link_libraries(simple-websocket-server INTERFACE Boost::boost)

    # Only link Boost::system if the target exists (for older Boost < 1.89)
    if(TARGET Boost::system)
        target_link_libraries(simple-websocket-server INTERFACE Boost::system)
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.9)
        target_compile_definitions(simple-websocket-server INTERFACE USE_BOOST_REGEX)
        find_package(Boost 1.54.0 COMPONENTS regex REQUIRED)
        target_link_libraries(simple-websocket-server INTERFACE Boost::regex)
    endif()
endif()
if(WIN32)
    target_link_libraries(simple-websocket-server INTERFACE ws2_32 wsock32)
endif()

if(APPLE)
    if(EXISTS /usr/local/opt/openssl)
        set(OPENSSL_ROOT_DIR /usr/local/opt/openssl)
    elseif(EXISTS /opt/homebrew/opt/openssl)
        set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl)
    endif()
endif()
find_package(OpenSSL REQUIRED)
target_link_libraries(simple-websocket-server INTERFACE OpenSSL::SSL OpenSSL::Crypto)

# If Simple-WebSocket-Server is not a sub-project:
if(CMAKE_SOURCE_DIR STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT MSVC)
        add_compile_options(-Wall -Wextra -Wsign-conversion)
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options(-Wthread-safety)
        endif()
    else()
        add_compile_options(/W1)
    endif()

    add_executable(ws_examples ws_examples.cpp)
    target_link_libraries(ws_examples simple-websocket-server)
    set_target_properties(ws_examples PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON)
    add_executable(wss_examples wss_examples.cpp)
    target_link_libraries(wss_examples simple-websocket-server)
    set_target_properties(wss_examples PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON)
    
    install(FILES asio_compatibility.hpp server_ws.hpp client_ws.hpp server_wss.hpp client_wss.hpp crypto.hpp utility.hpp status_code.hpp mutex.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/simple-websocket-server)
    install(TARGETS simple-websocket-server EXPORT SimpleWebSocketServerTargets)
    install(EXPORT SimpleWebSocketServerTargets
            FILE SimpleWebSocketServerTargets.cmake
            NAMESPACE SimpleWebSocketServer::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SimpleWebSocketServer)

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SimpleWebSocketServerConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/SimpleWebSocketServerConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SimpleWebSocketServer)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/SimpleWebSocketServerConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/SimpleWebSocketServerConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/SimpleWebSocketServerConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SimpleWebSocketServer)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
