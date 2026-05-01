#ifndef OPENAI_HTTP_H
#define OPENAI_HTTP_H

#include <stddef.h>

/* HTTP request */
typedef struct {
    const char* url;
    const char* method;
    const char* body;
    const char* auth_header;
    size_t body_size;
} OpenAI_HTTPRequest;

/* HTTP response */
typedef struct {
    int status_code;
    char* body;
    size_t body_size;
    char* headers;
} OpenAI_HTTPResponse;

int openai_http_init(void);
void openai_http_cleanup(void);
OpenAI_HTTPResponse* openai_http_request(OpenAI_HTTPRequest* req);
void openai_http_response_free(OpenAI_HTTPResponse* resp);

#endif /* OPENAI_HTTP_H */