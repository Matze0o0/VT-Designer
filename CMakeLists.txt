cmake_minimum_required(VERSION 3.25)

project(VTDesigner
    VERSION 0.1.0
    DESCRIPTION "Graphical editor for ISOBUS virtual terminal object pools (ISO 11783-6)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VTD_BUILD_TESTS "Build unit tests" ON)
option(VTD_BUILD_CLI "Build the vtpool-cli command line tool" ON)

include(FetchContent)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(nlohmann_json)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)

if(VTD_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(VTD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
