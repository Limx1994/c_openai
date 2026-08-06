#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "openai_http.h"
#include "openai_config.h"

/* Append extra_headers (raw \r\n-delimited) to a curl_slist.
   Returns the (possibly NULL) list on success, or NULL on allocation failure. */
static struct curl_slist* append_extra_headers(struct curl_slist* list, const char* extra_headers)
{
    const char* p = extra_headers;
    while (*p) {
        const char* line_end = strstr(p, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (line_len > 0) {
            char* hdr = (char*)malloc(line_len + 1);
            if (!hdr) {
                if (list) curl_slist_free_all(list);
                return NULL;
            }
            memcpy(hdr, p, line_len);
            hdr[line_len] = '\0';
            list = curl_slist_append(list, hdr);
            free(hdr);
        }
        if (!line_end) break;
        p = line_end + 2;
    }
    return list;
}

static int curl_init_count = 0;

struct memory_chunk {
    char* data;
    size_t size;
    size_t capacity;
};

#define OPENAI_STREAM_BUFFER_SIZE 65536

struct OpenAI_HTTPStream {
    CURL* easy;
    CURLM* multi;
    struct curl_slist* headers;
    char* buffer;
    size_t size;
    size_t pos;
    int header_complete;
    int done;
    int failed;
    int paused;
};

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct memory_chunk* mem = (struct memory_chunk*)userp;

    if (mem->size + realsize + 1 > mem->capacity) {
        size_t new_cap = mem->capacity == 0 ? 4096 : mem->capacity * 2;
        while (new_cap < mem->size + realsize + 1) new_cap *= 2;
        char* ptr = (char*)realloc(mem->data, new_cap);
        if (!ptr) {
            OPENAI_LOG_ERROR("write_callback: realloc failed, size=%zu", new_cap);
            free(mem->data);
            mem->data = NULL;
            return 0;
        }
        mem->data = ptr;
        mem->capacity = new_cap;
    }
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

/*
 * Core HTTP request function using libcurl.
 * is_stream=0: standard request (sets CURLOPT_TIMEOUT)
 * is_stream=1: streaming request (sets CURLOPT_CONNECTTIMEOUT only)
 */
static OpenAI_HTTPResponse* do_curl_request(OpenAI_HTTPRequest* req, int is_stream) {
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
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req->body_size);
        }
    }

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!headers) { OPENAI_LOG_ERROR("curl_slist_append failed"); curl_easy_cleanup(curl); free(resp); return NULL; }
    if (is_stream) {
        headers = curl_slist_append(headers, "Accept: text/event-stream");
    } else {
        headers = curl_slist_append(headers, "Accept: application/json");
    }
    if (!headers) { OPENAI_LOG_ERROR("curl_slist_append failed"); curl_easy_cleanup(curl); free(resp); return NULL; }
    if (req->auth_header) {
        char auth_header[512];
        if (req->auth_mode == 1) {
            snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", req->auth_header);
        } else {
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", req->auth_header);
        }
        headers = curl_slist_append(headers, auth_header);
        if (!headers) { OPENAI_LOG_ERROR("curl_slist_append failed"); curl_easy_cleanup(curl); free(resp); return NULL; }
    }
    headers = append_extra_headers(headers, req->extra_headers);
    if (!headers) {
        OPENAI_LOG_ERROR("Failed to append extra headers (allocation failure)");
        curl_easy_cleanup(curl);
        free(resp);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        /* For streaming, only set connect timeout (not total transfer timeout).
         * Long-running streams may exceed OPENAI_TIMEOUT. */
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, OPENAI_TIMEOUT);
    } else {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, OPENAI_TIMEOUT);
    }
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
    OPENAI_LOG_DEBUG("HTTP %s response: status=%ld, body_size=%zu",
        is_stream ? "stream" : "", status_code, chunk.size);

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return resp;
}

OpenAI_HTTPResponse* openai_http_request(OpenAI_HTTPRequest* req) {
    return do_curl_request(req, 0);
}

OpenAI_HTTPResponse* openai_http_request_stream(OpenAI_HTTPRequest* req) {
    return do_curl_request(req, 1);
}

void openai_http_response_free(OpenAI_HTTPResponse* resp) {
    if (resp) {
        if (resp->body) free(resp->body);
        free(resp);
    }
}

static size_t stream_header_callback(char* data, size_t size, size_t nmemb, void* userp) {
    size_t length = size * nmemb;
    OpenAI_HTTPStream* stream = (OpenAI_HTTPStream*)userp;
    if (length == 2 && data[0] == '\r' && data[1] == '\n') {
        stream->header_complete = 1;
    }
    return length;
}

static size_t stream_body_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t length = size * nmemb;
    OpenAI_HTTPStream* stream = (OpenAI_HTTPStream*)userp;

    if (stream->pos > 0 && stream->pos == stream->size) {
        stream->pos = 0;
        stream->size = 0;
    } else if (stream->pos > 0) {
        memmove(stream->buffer, stream->buffer + stream->pos, stream->size - stream->pos);
        stream->size -= stream->pos;
        stream->pos = 0;
    }

    if (length > OPENAI_STREAM_BUFFER_SIZE - stream->size) {
        stream->paused = 1;
        return CURL_WRITEFUNC_PAUSE;
    }

    memcpy(stream->buffer + stream->size, contents, length);
    stream->size += length;
    return length;
}

static int stream_check_messages(OpenAI_HTTPStream* stream) {
    int pending = 0;
    CURLMsg* message;
    while ((message = curl_multi_info_read(stream->multi, &pending)) != NULL) {
        if (message->msg == CURLMSG_DONE) {
            stream->done = 1;
            if (message->data.result != CURLE_OK) {
                OPENAI_LOG_ERROR("stream transfer failed: %s",
                                 curl_easy_strerror(message->data.result));
                stream->failed = 1;
            }
        }
    }
    return stream->failed ? -1 : 0;
}

static int stream_pump(OpenAI_HTTPStream* stream) {
    int running = 0;
    CURLMcode code = curl_multi_perform(stream->multi, &running);
    if (code != CURLM_OK) {
        OPENAI_LOG_ERROR("curl_multi_perform failed: %s", curl_multi_strerror(code));
        stream->failed = 1;
        return -1;
    }
    if (stream_check_messages(stream) != 0) return -1;
    if (!stream->done && stream->size == stream->pos && !stream->header_complete) {
        code = curl_multi_poll(stream->multi, NULL, 0, 1000, NULL);
        if (code != CURLM_OK) {
            OPENAI_LOG_ERROR("curl_multi_poll failed: %s", curl_multi_strerror(code));
            stream->failed = 1;
            return -1;
        }
    }
    return 0;
}

OpenAI_HTTPStream* openai_http_stream_open(OpenAI_HTTPRequest* req, int* status_code) {
    if (!req || !req->url || !status_code) return NULL;

    OpenAI_HTTPStream* stream = (OpenAI_HTTPStream*)calloc(1, sizeof(OpenAI_HTTPStream));
    if (!stream) return NULL;
    stream->buffer = (char*)malloc(OPENAI_STREAM_BUFFER_SIZE);
    stream->easy = curl_easy_init();
    stream->multi = curl_multi_init();
    if (!stream->buffer || !stream->easy || !stream->multi) {
        openai_http_stream_close(stream);
        return NULL;
    }

    stream->headers = curl_slist_append(stream->headers, "Content-Type: application/json");
    stream->headers = curl_slist_append(stream->headers, "Accept: text/event-stream");
    if (req->auth_header) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), req->auth_mode == 1 ?
                 "x-api-key: %s" : "Authorization: Bearer %s", req->auth_header);
        stream->headers = curl_slist_append(stream->headers, auth_header);
    }
    stream->headers = append_extra_headers(stream->headers, req->extra_headers);
    if (!stream->headers) {
        OPENAI_LOG_ERROR("Failed to append extra headers (allocation failure)");
        openai_http_stream_close(stream);
        return NULL;
    }

    curl_easy_setopt(stream->easy, CURLOPT_URL, req->url);
    curl_easy_setopt(stream->easy, CURLOPT_POST, 1L);
    curl_easy_setopt(stream->easy, CURLOPT_POSTFIELDSIZE, (long)req->body_size);
    curl_easy_setopt(stream->easy, CURLOPT_COPYPOSTFIELDS, req->body ? req->body : "");
    curl_easy_setopt(stream->easy, CURLOPT_HTTPHEADER, stream->headers);
    curl_easy_setopt(stream->easy, CURLOPT_HEADERFUNCTION, stream_header_callback);
    curl_easy_setopt(stream->easy, CURLOPT_HEADERDATA, stream);
    curl_easy_setopt(stream->easy, CURLOPT_WRITEFUNCTION, stream_body_callback);
    curl_easy_setopt(stream->easy, CURLOPT_WRITEDATA, stream);
    curl_easy_setopt(stream->easy, CURLOPT_CONNECTTIMEOUT, (long)OPENAI_TIMEOUT);
    curl_easy_setopt(stream->easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(stream->easy, CURLOPT_LOW_SPEED_TIME, (long)OPENAI_TIMEOUT);
    curl_easy_setopt(stream->easy, CURLOPT_FOLLOWLOCATION, 1L);

    if (curl_multi_add_handle(stream->multi, stream->easy) != CURLM_OK) {
        openai_http_stream_close(stream);
        return NULL;
    }

    while (!stream->header_complete && !stream->done) {
        if (stream_pump(stream) != 0) {
            openai_http_stream_close(stream);
            return NULL;
        }
    }
    if (!stream->header_complete || stream->failed) {
        openai_http_stream_close(stream);
        return NULL;
    }

    long response_code = 0;
    curl_easy_getinfo(stream->easy, CURLINFO_RESPONSE_CODE, &response_code);
    *status_code = (int)response_code;
    return stream;
}

int openai_http_stream_read(OpenAI_HTTPStream* stream, void* buffer, size_t size) {
    if (!stream || !buffer || size == 0) return -1;

    while (stream->pos == stream->size && !stream->done) {
        stream->pos = 0;
        stream->size = 0;
        if (stream->paused) {
            stream->paused = 0;
            if (curl_easy_pause(stream->easy, CURLPAUSE_CONT) != CURLE_OK) return -1;
        }
        if (stream_pump(stream) != 0) return -1;
        if (!stream->done && stream->pos == stream->size) {
            CURLMcode code = curl_multi_poll(stream->multi, NULL, 0, 1000, NULL);
            if (code != CURLM_OK) return -1;
        }
    }

    if (stream->pos == stream->size) return stream->failed ? -1 : 0;
    size_t available = stream->size - stream->pos;
    size_t count = size < available ? size : available;
    memcpy(buffer, stream->buffer + stream->pos, count);
    stream->pos += count;
    return (int)count;
}

void openai_http_stream_close(OpenAI_HTTPStream* stream) {
    if (!stream) return;
    if (stream->multi && stream->easy) curl_multi_remove_handle(stream->multi, stream->easy);
    if (stream->easy) curl_easy_cleanup(stream->easy);
    if (stream->multi) curl_multi_cleanup(stream->multi);
    if (stream->headers) curl_slist_free_all(stream->headers);
    free(stream->buffer);
    free(stream);
}
