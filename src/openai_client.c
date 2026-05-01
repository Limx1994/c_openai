#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openai.h"
#include "openai_config.h"
#include "openai_json.h"
#include "openai_http.h"

struct OpenAI_Client {
    char* api_key;
    char auth_header[256];
};

OpenAI_Client* openai_client_new(const char* api_key) {
    if (!api_key) return NULL;

    OpenAI_Client* client = (OpenAI_Client*)malloc(sizeof(OpenAI_Client));
    if (!client) return NULL;

    memset(client, 0, sizeof(OpenAI_Client));

    client->api_key = (char*)malloc(strlen(api_key) + 1);
    if (!client->api_key) {
        free(client);
        return NULL;
    }
    strcpy(client->api_key, api_key);

    snprintf(client->auth_header, sizeof(client->auth_header), "%s", api_key);

    openai_http_init();

    return client;
}

void openai_client_free(OpenAI_Client* client) {
    if (client) {
        if (client->api_key) free(client->api_key);
        free(client);
        openai_http_cleanup();
    }
}

const char* openai_version(void) {
    return OPENAI_VERSION;
}

static char* build_chat_request_body(OpenAI_ChatRequest* req) {
    /* Build JSON request body manually */
    size_t buf_size = 1024;
    char* buf = (char*)malloc(buf_size);
    if (!buf) return NULL;

    int offset = snprintf(buf, buf_size,
        "{\"model\":\"%s\",\"messages\":[", req->model ? req->model : "gpt-3.5-turbo");

    for (size_t i = 0; i < req->message_count && req->messages; i++) {
        OpenAI_Message* msg = &req->messages[i];
        if (msg->role && msg->content) {
            int written = snprintf(buf + offset, buf_size - offset,
                "%s{\"role\":\"%s\",\"content\":\"%s\"}",
                i > 0 ? "," : "", msg->role, msg->content);
            offset += written;

            if (offset >= (int)buf_size - 1) {
                buf_size *= 2;
                char* new_buf = (char*)realloc(buf, buf_size);
                if (!new_buf) {
                    free(buf);
                    return NULL;
                }
                buf = new_buf;
            }
        }
    }

    /* Add optional parameters */
    if (req->temperature > 0) {
        char temp_buf[64];
        snprintf(temp_buf, sizeof(temp_buf), ",\"temperature\":%g", req->temperature);
        size_t temp_len = strlen(temp_buf);
        while (offset + temp_len >= buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }
        strcpy(buf + offset, temp_buf);
        offset += temp_len;
    }

    if (req->max_tokens > 0) {
        char tokens_buf[64];
        snprintf(tokens_buf, sizeof(tokens_buf), ",\"max_tokens\":%d", req->max_tokens);
        size_t tokens_len = strlen(tokens_buf);
        while (offset + tokens_len >= buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }
        strcpy(buf + offset, tokens_buf);
        offset += tokens_len;
    }

    /* Close messages array and object */
    snprintf(buf + offset, buf_size - offset, "],\"stream\":%s}",
        req->stream ? "true" : "false");

    return buf;
}

OpenAI_ChatResponse* openai_chat_create(OpenAI_Client* client, OpenAI_ChatRequest* req) {
    if (!client || !req) return NULL;

    char* url = OPENAI_API_BASE "/chat/completions";

    char* body = build_chat_request_body(req);
    if (!body) return NULL;

    OpenAI_HTTPRequest http_req = {
        .url = url,
        .method = "POST",
        .body = body,
        .auth_header = client->api_key,
        .body_size = strlen(body)
    };

    OpenAI_HTTPResponse* http_resp = openai_http_request(&http_req);
    free(body);

    if (!http_resp) return NULL;

    if (http_resp->status_code != 200) {
        openai_http_response_free(http_resp);
        return NULL;
    }

    /* Parse JSON response */
    OpenAI_JSONNode* json = openai_json_parse(http_resp->body);
    openai_http_response_free(http_resp);

    if (!json) return NULL;

    OpenAI_ChatResponse* resp = (OpenAI_ChatResponse*)calloc(1, sizeof(OpenAI_ChatResponse));
    if (!resp) {
        openai_json_free(json);
        return NULL;
    }

    /* Extract fields from JSON */
    const char* id = openai_json_get_string(json, "id");
    if (id) {
        resp->id = (char*)malloc(strlen(id) + 1);
        strcpy(resp->id, id);
    }

    const char* model = openai_json_get_string(json, "model");
    if (model) {
        resp->model = (char*)malloc(strlen(model) + 1);
        strcpy(resp->model, model);
    }

    /* Extract choices */
    OpenAI_JSONNode* choices = openai_json_get_object(json, "choices");
    if (choices && choices->child_count > 0) {
        resp->choice_count = choices->child_count;
        resp->choices = (OpenAI_Choice*)malloc(sizeof(OpenAI_Choice) * resp->choice_count);
        memset(resp->choices, 0, sizeof(OpenAI_Choice) * resp->choice_count);

        for (size_t i = 0; i < resp->choice_count; i++) {
            OpenAI_JSONNode* choice = openai_json_get_array_item(choices, i);
            if (choice) {
                OpenAI_JSONNode* msg = openai_json_get_object(choice, "message");
                if (msg) {
                    const char* content = openai_json_get_string(msg, "content");
                    if (content) {
                        resp->choices[i].content = (char*)malloc(strlen(content) + 1);
                        strcpy(resp->choices[i].content, content);
                    }
                    const char* role = openai_json_get_string(msg, "role");
                    if (role) {
                        resp->choices[i].role = (char*)malloc(strlen(role) + 1);
                        strcpy(resp->choices[i].role, role);
                    }
                }
                resp->choices[i].index = (int)openai_json_get_number(choice, "index");
            }
        }
    }

    openai_json_free(json);
    return resp;
}

void openai_chat_response_free(OpenAI_ChatResponse* resp) {
    if (resp) {
        if (resp->id) free(resp->id);
        if (resp->model) free(resp->model);
        if (resp->choices) {
            for (size_t i = 0; i < resp->choice_count; i++) {
                if (resp->choices[i].content) free(resp->choices[i].content);
                if (resp->choices[i].role) free(resp->choices[i].role);
            }
            free(resp->choices);
        }
        free(resp);
    }
}

/* Streaming support */
typedef struct {
    OpenAI_Client* client;
    char* buffer;
    size_t buffer_size;
    size_t buffer_pos;
    int eof;
} OpenAI_StreamHandle;

void* openai_chat_create_stream(OpenAI_Client* client, OpenAI_ChatRequest* req) {
    if (!client || !req) return NULL;

    /* For streaming, we set the stream flag and make the request */
    req->stream = 1;

    char* url = OPENAI_API_BASE "/chat/completions";
    char* body = build_chat_request_body(req);
    if (!body) return NULL;

    OpenAI_HTTPRequest http_req = {
        .url = url,
        .method = "POST",
        .body = body,
        .auth_header = client->api_key,
        .body_size = strlen(body)
    };

    /* For streaming, we'd need chunked response handling */
    /* This is a simplified version - real implementation would use
     * libcurl's CURLOPT_WRITEFUNCTION with streaming callback */

    free(body);

    /* Return NULL for now - streaming requires more complex implementation */
    (void)http_req;
    return NULL;
}

int openai_stream_read(void* stream, OpenAI_StreamEvent* event) {
    (void)stream;
    (void)event;
    return OPENAI_ERR_INVALID_PARAM;
}

void openai_stream_close(void* stream) {
    if (stream) {
        OpenAI_StreamHandle* handle = (OpenAI_StreamHandle*)stream;
        if (handle->buffer) free(handle->buffer);
        free(handle);
    }
}

/* Embeddings API */
OpenAI_EmbeddingResponse* openai_embeddings_create(OpenAI_Client* client, OpenAI_EmbeddingRequest* req) {
    if (!client || !req || !req->input) return NULL;

    char* url = OPENAI_API_BASE "/embeddings";

    /* Build request body */
    char body[1024];
    snprintf(body, sizeof(body),
        "{\"model\":\"%s\",\"input\":\"%s\"}",
        req->model ? req->model : "text-embedding-3-small",
        req->input);

    OpenAI_HTTPRequest http_req = {
        .url = url,
        .method = "POST",
        .body = body,
        .auth_header = client->api_key,
        .body_size = strlen(body)
    };

    OpenAI_HTTPResponse* http_resp = openai_http_request(&http_req);
    if (!http_resp) return NULL;

    if (http_resp->status_code != 200) {
        openai_http_response_free(http_resp);
        return NULL;
    }

    /* Parse JSON response */
    OpenAI_JSONNode* json = openai_json_parse(http_resp->body);
    openai_http_response_free(http_resp);

    if (!json) return NULL;

    OpenAI_EmbeddingResponse* resp = (OpenAI_EmbeddingResponse*)calloc(1, sizeof(OpenAI_EmbeddingResponse));
    if (!resp) {
        openai_json_free(json);
        return NULL;
    }

    /* Extract embedding from data[0].embedding */
    OpenAI_JSONNode* data = openai_json_get_object(json, "data");
    if (data && data->child_count > 0) {
        OpenAI_JSONNode* item = openai_json_get_array_item(data, 0);
        if (item) {
            OpenAI_JSONNode* embedding = openai_json_get_object(item, "embedding");
            if (embedding && embedding->child_count > 0) {
                resp->embedding_dim = embedding->child_count;
                resp->embedding = (float*)malloc(sizeof(float) * resp->embedding_dim);
                if (resp->embedding) {
                    for (size_t i = 0; i < resp->embedding_dim; i++) {
                        OpenAI_JSONNode* val = openai_json_get_array_item(embedding, i);
                        if (val) {
                            resp->embedding[i] = (float)val->number_value;
                        }
                    }
                }
            }
        }
    }

    openai_json_free(json);
    return resp;
}

void openai_embedding_response_free(OpenAI_EmbeddingResponse* resp) {
    if (resp) {
        if (resp->embedding) free(resp->embedding);
        free(resp);
    }
}