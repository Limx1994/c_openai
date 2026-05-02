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

# Dependencies for libcurl backend (using git submodule)
if(OPENAI_HTTP_BACKEND EQUAL OPENAI_BACKEND_CURL)
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/libcurl/CMakeLists.txt)
        message(FATAL_ERROR "libcurl submodule not found. Run: git submodule update --init --recursive")
    endif()
    add_subdirectory(third_party/libcurl)
    target_link_libraries(openai INTERFACE curl)
endif()

# Installation
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY cJSON/ DESTINATION include/cJSON)