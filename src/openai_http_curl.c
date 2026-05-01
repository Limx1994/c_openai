#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "openai_http.h"

static int curl_initialized = 0;

struct memory_chunk {
    char* data;
    size_t size;
};

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct memory_chunk* mem = (struct memory_chunk*)userp;

    char* ptr = (char*)realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

int openai_http_init(void) {
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = 1;
    }
    return 0;
}

void openai_http_cleanup(void) {
    if (curl_initialized) {
        curl_global_cleanup();
        curl_initialized = 0;
    }
}

OpenAI_HTTPResponse* openai_http_request(OpenAI_HTTPRequest* req) {
    if (!req || !req->url) return NULL;

    CURL* curl = curl_easy_init();
    if (!curl) return NULL;

    OpenAI_HTTPResponse* resp = (OpenAI_HTTPResponse*)calloc(1, sizeof(OpenAI_HTTPResponse));
    if (!resp) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    struct memory_chunk chunk = {0};
    chunk.data = (char*)malloc(1);
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
    if (req->auth_header) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", req->auth_header);
        headers = curl_slist_append(headers, auth_header);
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, OPENAI_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
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