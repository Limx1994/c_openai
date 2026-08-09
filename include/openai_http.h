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
    const char* extra_headers; /**< Additional headers ("N: V\r\n"), or NULL */
    int auth_mode;          /**< 0=Bearer (default), 1=x-api-key */
} OpenAI_HTTPRequest;

/**
 * @brief HTTP response structure
 */
typedef struct {
    int status_code;     /**< HTTP status code */
    char* body;          /**< Response body (caller must free) */
    size_t body_size;    /**< Body size in bytes */
} OpenAI_HTTPResponse;

/**
 * @brief Transport error reported by the active HTTP backend
 */
typedef enum {
    OPENAI_HTTP_ERROR_NONE = 0,
    OPENAI_HTTP_ERROR_NETWORK,
    OPENAI_HTTP_ERROR_TIMEOUT
} OpenAI_HTTPError;

typedef struct OpenAI_HTTPStream OpenAI_HTTPStream;

/**
 * @brief Initialize HTTP subsystem (not thread-safe, call once before use)
 * @return 0 on success
 */
int openai_http_init(void);

/**
 * @brief Cleanup HTTP subsystem (not thread-safe, call after all clients freed)
 */
void openai_http_cleanup(void);

/**
 * @brief Get the transport error from the most recent HTTP operation
 * @return Backend transport error category
 */
OpenAI_HTTPError openai_http_get_last_error(void);

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
 * @brief Open an incremental HTTP response stream
 * @param req HTTP request parameters
 * @param status_code Receives the HTTP status code after response headers arrive
 * @return Stream handle, or NULL on connection/protocol failure
 */
OpenAI_HTTPStream* openai_http_stream_open(OpenAI_HTTPRequest* req, int* status_code);

/**
 * @brief Read response-body bytes from an incremental HTTP stream
 * @return Number of bytes read, 0 on EOF, or -1 on error/timeout
 */
int openai_http_stream_read(OpenAI_HTTPStream* stream, void* buffer, size_t size);

/**
 * @brief Close an incremental HTTP stream and release transport resources
 */
void openai_http_stream_close(OpenAI_HTTPStream* stream);

/**
 * @brief Free HTTP response
 * @param resp Response handle from openai_http_request()
 */
void openai_http_response_free(OpenAI_HTTPResponse* resp);

#endif /* OPENAI_HTTP_H */
