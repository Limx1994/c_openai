/**
 * @file openai_http.h
 * @brief OpenAI HTTP Interface
 */

#ifndef OPENAI_HTTP_H
#define OPENAI_HTTP_H

#include <stddef.h>

/**
 * @brief HTTP request structure
 */
typedef struct {
    const char* url;        /**< Target URL */
    const char* method;     /**< HTTP method (GET/POST) */
    const char* body;        /**< Request body */
    const char* auth_header; /**< Authorization header value */
    size_t body_size;       /**< Body size in bytes */
} OpenAI_HTTPRequest;

/**
 * @brief HTTP response structure
 */
typedef struct {
    int status_code;     /**< HTTP status code */
    char* body;          /**< Response body (caller must free) */
    size_t body_size;    /**< Body size in bytes */
    char* headers;       /**< Response headers */
} OpenAI_HTTPResponse;

/**
 * @brief Initialize HTTP subsystem
 * @return 0 on success
 */
int openai_http_init(void);

/**
 * @brief Cleanup HTTP subsystem
 */
void openai_http_cleanup(void);

/**
 * @brief Send HTTP request
 * @param req HTTP request parameters
 * @return Response handle, or NULL on failure
 */
OpenAI_HTTPResponse* openai_http_request(OpenAI_HTTPRequest* req);

/**
 * @brief Send HTTP request for streaming (collects full response)
 * @param req HTTP request parameters
 * @return Response handle with body, or NULL on failure
 */
OpenAI_HTTPResponse* openai_http_request_stream(OpenAI_HTTPRequest* req);

/**
 * @brief Free HTTP response
 * @param resp Response handle from openai_http_request()
 */
void openai_http_response_free(OpenAI_HTTPResponse* resp);

#endif /* OPENAI_HTTP_H */
