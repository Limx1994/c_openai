#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "openai_http.h"
#include "openai_config.h"

static int curl_init_count = 0;

struct memory_chunk {
    char* data;
    size_t size;
};

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct memory_chunk* mem = (struct memory_chunk*)userp;

    char* ptr = (char*)realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        OPENAI_LOG_ERROR("write_callback: realloc failed, size=%zu", mem->size + realsize + 1);
        free(mem->data);
        mem->data = NULL;
        return 0;
    }
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

int openai_http_init(void) {
    if (curl_init_count == 0) {
        CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (res != CURLE_OK) {
            OPENAI_LOG_ERROR("curl_global_init failed: %s", curl_easy_strerror(res));
            return -1;
        }
    }
    curl_init_count++;
    OPENAI_LOG_DEBUG("openai_http_init: curl_init_count = %d", curl_init_count);
    return 0;
}

void openai_http_cleanup(void) {
    if (curl_init_count > 0) {
        curl_init_count--;
        OPENAI_LOG_DEBUG("openai_http_cleanup: curl_init_count = %d", curl_init_count);
        if (curl_init_count == 0) {
            curl_global_cleanup();
        }
    }
}

OpenAI_HTTPResponse* openai_http_request(OpenAI_HTTPRequest* req) {
    if (!req || !req->url) return NULL;

    CURL* curl = curl_easy_init();
    if (!curl) {
        OPENAI_LOG_ERROR("curl_easy_init failed");
        return NULL;
    }

    OpenAI_HTTPResponse* resp = (OpenAI_HTTPResponse*)calloc(1, sizeof(OpenAI_HTTPResponse));
    if (!resp) {
        OPENAI_LOG_ERROR("Failed to allocate HTTP response structure");
        curl_easy_cleanup(curl);
        return NULL;
    }

    struct memory_chunk chunk = {0};
    chunk.data = (char*)malloc(1);
    if (!chunk.data) {
        OPENAI_LOG_ERROR("Failed to allocate HTTP response buffer");
        curl_easy_cleanup(curl);
        free(resp);
        return NULL;
    }
    chunk.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

    if (req->method && strcmp(req->method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (req->body && req->body_size > 0) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, req->body_size);
        }
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    if (req->auth_header) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", req->auth_header);
        headers = curl_slist_append(headers, auth_header);
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, OPENAI_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        OPENAI_LOG_ERROR("curl_easy_perform failed: %s", curl_easy_strerror(res));
        free(chunk.data);
        free(resp);
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

    resp->status_code = (int)status_code;
    resp->body = chunk.data;
    resp->body_size = chunk.size;
    OPENAI_LOG_DEBUG("HTTP response: status=%ld, body_size=%zu", status_code, chunk.size);

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return resp;
}

/* Stream handle for chunked responses */
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} OpenAI_StreamBuffer;

static size_t stream_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    OpenAI_StreamBuffer* buf = (OpenAI_StreamBuffer*)userp;

    if (buf->size + realsize + 1 > buf->capacity) {
        size_t new_cap = buf->capacity == 0 ? 4096 : buf->capacity * 2;
        while (new_cap < buf->size + realsize + 1) new_cap *= 2;
        char* new_data = (char*)realloc(buf->data, new_cap);
        if (!new_data) {
            OPENAI_LOG_ERROR("stream_write_callback: realloc failed, size=%zu", new_cap);
            return 0;
        }
        buf->data = new_data;
        buf->capacity = new_cap;
    }

    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';

    return realsize;
}

/* Streaming HTTP request - returns raw body for SSE parsing */
OpenAI_HTTPResponse* openai_http_request_stream(OpenAI_HTTPRequest* req) {
    if (!req || !req->url) return NULL;

    CURL* curl = curl_easy_init();
    if (!curl) {
        OPENAI_LOG_ERROR("curl_easy_init failed");
        return NULL;
    }

    OpenAI_HTTPResponse* resp = (OpenAI_HTTPResponse*)calloc(1, sizeof(OpenAI_HTTPResponse));
    if (!resp) {
        OPENAI_LOG_ERROR("Failed to allocate stream response structure");
        curl_easy_cleanup(curl);
        return NULL;
    }

    OpenAI_StreamBuffer buf = {0};
    buf.data = (char*)malloc(1);
    if (!buf.data) {
        OPENAI_LOG_ERROR("Failed to allocate stream buffer");
        curl_easy_cleanup(curl);
        free(resp);
        return NULL;
    }
    buf.data[0] = '\0';
    buf.capacity = 1;

    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    if (req->method && strcmp(req->method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (req->body && req->body_size > 0) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, req->body_size);
        }
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    if (req->auth_header) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", req->auth_header);
        headers = curl_slist_append(headers, auth_header);
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, OPENAI_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        OPENAI_LOG_ERROR("curl_easy_perform (stream) failed: %s", curl_easy_strerror(res));
        free(buf.data);
        free(resp);
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return NULL;
    }

    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

    resp->status_code = (int)status_code;
    resp->body = buf.data;
    resp->body_size = buf.size;
    OPENAI_LOG_DEBUG("HTTP stream response: status=%ld, body_size=%zu", status_code, buf.size);

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return resp;
}

void openai_http_response_free(OpenAI_HTTPResponse* resp) {
    if (resp) {
        if (resp->body) free(resp->body);
        if (resp->headers) free(resp->headers);
        free(resp);
    }
}