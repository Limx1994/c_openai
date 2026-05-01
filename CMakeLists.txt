cmake_minimum_required(VERSION 3.10)
project(c_openai VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(OPENAI_BUILD_EXAMPLES "Build example programs" ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/cJSON)

# Add subdirectories
add_subdirectory(cJSON)
add_subdirectory(src)
if(OPENAI_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

# Third-party libraries for lwIP backend
if(OPENAI_HTTP_BACKEND EQUAL OPENAI_BACKEND_LWIP)
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/lwip/src/include/lwip/opt.h)
        message(FATAL_ERROR "lwIP submodule not found. Run: git submodule update --init --recursive")
    endif()
    add_subdirectory(third_party)
endif()

# Main library
add_library(openai INTERFACE)
target_include_directories(openai INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/cJSON>
)

# Dependencies for libcurl backend
find_package(CURL QUIET)
if(CURL_FOUND AND OPENAI_HTTP_BACKEND EQUAL OPENAI_BACKEND_CURL)
    target_link_libraries(openai INTERFACE CURL::libcurl)
endif()

# pkg-config for libcurl fallback
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CURL_PC libcurl QUIET)
    if(CURL_PC_FOUND)
        target_link_libraries(openai INTERFACE ${CURL_PC_LIBRARIES})
        target_include_directories(openai INTERFACE ${CURL_PC_INCLUDE_DIRS})
    endif()
endif()

# Installation
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY cJSON/ DESTINATION include/cJSON)