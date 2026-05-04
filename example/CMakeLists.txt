add_executable(chat_example chat_example.c)

target_link_libraries(chat_example PRIVATE openai_client cJSON CURL::libcurl)

target_include_directories(chat_example PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/cJSON
)