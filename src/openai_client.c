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

    /* Extract usage */
    OpenAI_JSONNode* usage = openai_json_get_object(json, "usage");
    if (usage) {
        char usage_buf[256];
        usage_buf[0] = '\0';

        const char* prompt_tokens = openai_json_get_string(usage, "prompt_tokens");
        const char* completion_tokens = openai_json_get_string(usage, "completion_tokens");
        const char* total_tokens = openai_json_get_string(usage, "total_tokens");

        if (prompt_tokens && completion_tokens && total_tokens) {
            snprintf(usage_buf, sizeof(usage_buf),
                "prompt_tokens=%s, completion_tokens=%s, total_tokens=%s",
                prompt_tokens, completion_tokens, total_tokens);
        } else {
            double p_tokens = openai_json_get_number(usage, "prompt_tokens");
            double c_tokens = openai_json_get_number(usage, "completion_tokens");
            double t_tokens = openai_json_get_number(usage, "total_tokens");
            if (p_tokens > 0 || c_tokens > 0 || t_tokens > 0) {
                snprintf(usage_buf, sizeof(usage_buf),
                    "prompt_tokens=%.0f, completion_tokens=%.0f, total_tokens=%.0f",
                    p_tokens, c_tokens, t_tokens);
            }
        }

        if (usage_buf[0] != '\0') {
            resp->usage = (char*)malloc(strlen(usage_buf) + 1);
            strcpy(resp->usage, usage_buf);
        }
    }

    openai_json_free(json);
    return resp;
}

void openai_chat_response_free(OpenAI_ChatResponse* resp) {
    if (resp) {
        if (resp->id) free(resp->id);
        if (resp->model) free(resp->model);
        if (resp->usage) free(resp->usage);
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
    size_t parse_pos;
    int eof;
} OpenAI_StreamHandle;

static int parse_sse_line(const char* line, size_t len, char* output, size_t output_size) {
    /* SSE format: "data: {\"key\": \"value\"}" */
    if (len < 6 || strncmp(line, "data: ", 6) != 0) {
        return -1;
    }
    line += 6;
    len -= 6;

    /* Check for [DONE] marker */
    if (len >= 7 && strncmp(line, "[DONE]", 7) == 0) {
        return 1;
    }

    /* Parse the JSON to extract delta.content */
    /* Simple approach: look for "content\":\" ... \"" */
    const char* content_start = strstr(line, "\"content\":\"");
    if (content_start) {
        content_start += 10; /* skip "\"content\":\"" */
        const char* content_end = strstr(content_start, "\"");
        if (content_end) {
            size_t content_len = content_end - content_start;
            if (content_len < output_size - 1) {
                memcpy(output, content_start, content_len);
                output[content_len] = '\0';
                return 0;
            }
        }
    }
    return -1;
}

static int find_line_start(const char* buffer, size_t size, size_t start) {
    while (start < size) {
        if (buffer[start] == '\n' || buffer[start] == '\r') {
            break;
        }
        start++;
    }
    return start;
}

static int find_line_end(const char* buffer, size_t size, size_t start) {
    while (start < size) {
        if (buffer[start] == '\n' || buffer[start] == '\r') {
            return start;
        }
        start++;
    }
    return size;
}

void* openai_chat_create_stream(OpenAI_Client* client, OpenAI_ChatRequest* req) {
    if (!client || !req) return NULL;

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

    OpenAI_HTTPResponse* http_resp = openai_http_request_stream(&http_req);
    free(body);

    if (!http_resp) return NULL;

    OpenAI_StreamHandle* handle = (OpenAI_StreamHandle*)calloc(1, sizeof(OpenAI_StreamHandle));
    if (!handle) {
        openai_http_response_free(http_resp);
        return NULL;
    }

    handle->buffer = http_resp->body;
    handle->buffer_size = http_resp->body_size;
    handle->buffer_pos = 0;
    handle->parse_pos = 0;
    handle->eof = 0;

    /* Clean up http_resp struct but NOT the body (transferred to handle) */
    if (http_resp->headers) free(http_resp->headers);
    free(http_resp);

    return handle;
}

int openai_stream_read(void* stream, OpenAI_StreamEvent* event) {
    if (!stream || !event) return OPENAI_ERR_INVALID_PARAM;

    OpenAI_StreamHandle* handle = (OpenAI_StreamHandle*)stream;

    if (handle->eof) {
        return OPENAI_ERR_EOF;
    }

    memset(event, 0, sizeof(OpenAI_StreamEvent));

    char line_buffer[1024];
    int found_content = 0;

    while (handle->parse_pos < handle->buffer_size) {
        int line_start = find_line_start(handle->buffer, handle->buffer_size, handle->parse_pos);
        int line_end = find_line_end(handle->buffer, handle->buffer_size, line_start);

        size_t line_len = line_end - line_start;
        if (line_len >= sizeof(line_buffer)) {
            line_len = sizeof(line_buffer) - 1;
        }

        if (line_len > 0) {
            memcpy(line_buffer, handle->buffer + line_start, line_len);
            line_buffer[line_len] = '\0';

            char content[512] = {0};
            int ret = parse_sse_line(line_buffer, line_len, content, sizeof(content));

            if (ret == 1) {
                handle->eof = 1;
                return OPENAI_ERR_EOF;
            } else if (ret == 0 && content[0] != '\0') {
                event->content = (char*)malloc(strlen(content) + 1);
                strcpy(event->content, content);
                event->event_type = (char*)OPENAI_EVENT_CHUNK;
                handle->parse_pos = line_end + 1;
                if (handle->parse_pos < handle->buffer_size &&
                    (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                    handle->parse_pos++;
                }
                return 0;
            }
        }

        handle->parse_pos = line_end + 1;
        if (handle->parse_pos < handle->buffer_size &&
            (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
            handle->parse_pos++;
        }
    }

    return OPENAI_ERR_BUFFER_EMPTY;
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