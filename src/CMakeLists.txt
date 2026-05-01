add_library(openai_client
    openai_client.c
    openai_error.c
    openai_json.c
)

target_include_directories(openai_client PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../cJSON>
)

# Select HTTP backend based on config
if(OPENAI_HTTP_BACKEND EQUAL OPENAI_BACKEND_LWIP)
    target_compile_definitions(openai_client PUBLIC OPENAI_USE_LWIP=1)
    target_sources(openai_client PRIVATE openai_http_lwip.c)
else()
    find_package(CURL REQUIRED)
    target_link_libraries(openai_client CURL::libcurl)
    target_sources(openai_client PRIVATE openai_http_curl.c)
endif()

target_compile_options(openai_client PRIVATE -Wall -Wextra)