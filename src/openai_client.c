#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openai.h"
#include "openai_config.h"
#include "openai_json.h"
#include "openai_http.h"

struct OpenAI_Client {
    char* api_key;
    char* base_url;
    int provider;  /* OpenAI_Provider value, default OPENAI_PROVIDER_OPENAI (0) */
};

static int s_client_count = 0;

/**
 * @brief Create a new OpenAI client
 * @param api_key Your OpenAI API key (e.g., "sk-...")
 * @return Client handle, or NULL on failure
 *
 * IMPORTANT: Only one client instance may exist at a time.
 * Creating a new client before freeing the previous one will
 * cause undefined behavior.
 */
OpenAI_Client* openai_client_new(const char* api_key) {
    if (!api_key) return NULL;

    if (s_client_count > 0) {
        OPENAI_LOG_ERROR("Only one client instance may exist at a time");
        return NULL;
    }

    OpenAI_Client* client = (OpenAI_Client*)malloc(sizeof(OpenAI_Client));
    if (!client) {
        OPENAI_LOG_ERROR("Failed to allocate client structure");
        return NULL;
    }

    memset(client, 0, sizeof(OpenAI_Client));

    client->api_key = (char*)malloc(strlen(api_key) + 1);
    if (!client->api_key) {
        OPENAI_LOG_ERROR("Failed to allocate api_key string");
        free(client);
        return NULL;
    }
    strcpy(client->api_key, api_key);

    if (openai_http_init() != 0) {
        OPENAI_LOG_ERROR("HTTP subsystem initialization failed");
        free(client->api_key);
        free(client);
        return NULL;
    }
    s_client_count++;

    OPENAI_LOG_DEBUG("Client created");

    return client;
}

int openai_client_set_base_url(OpenAI_Client* client, const char* base_url) {
    if (!client) return -1;

    /* Free existing base_url if any */
    if (client->base_url) {
        free(client->base_url);
        client->base_url = NULL;
    }

    /* Set new base_url if provided */
    if (base_url) {
        client->base_url = (char*)malloc(strlen(base_url) + 1);
        if (!client->base_url) {
            OPENAI_LOG_ERROR("Failed to allocate base_url string");
            return -1;
        }
        strcpy(client->base_url, base_url);
    }

    return 0;
}

int openai_client_set_provider(OpenAI_Client* client, int provider) {
    if (!client) return -1;
    if (provider != OPENAI_PROVIDER_OPENAI && provider != OPENAI_PROVIDER_ANTHROPIC) {
        OPENAI_LOG_ERROR("Invalid provider value: %d", provider);
        return -1;
    }
    client->provider = provider;
    OPENAI_LOG_DEBUG("Provider set to %d", provider);
    return 0;
}

void openai_client_free(OpenAI_Client* client) {
    if (client) {
        if (client->api_key) free(client->api_key);
        if (client->base_url) free(client->base_url);
        free(client);
        openai_http_cleanup();
        s_client_count--;
        OPENAI_LOG_DEBUG("Client freed");
    }
}

/* Helper function to get effective base URL */
static const char* get_effective_base_url(OpenAI_Client* client) {
    if (client->base_url) return client->base_url;
    if (client->provider == OPENAI_PROVIDER_ANTHROPIC) return ANTHROPIC_API_BASE;
    return OPENAI_API_BASE;
}

static char* build_chat_request_body(OpenAI_ChatRequest* req) {
    /* Build JSON request body manually */
    size_t buf_size = 1024;
    char* buf = (char*)malloc(buf_size);
    if (!buf) return NULL;

    /* Escape model to prevent JSON injection */
    char* escaped_model = openai_json_escape_string(req->model ? req->model : "gpt-3.5-turbo");
    int offset = snprintf(buf, buf_size,
        "{\"model\":\"%s\",\"messages\":[", escaped_model ? escaped_model : "gpt-3.5-turbo");
    free(escaped_model);

    int first_msg = 1;
    for (size_t i = 0; i < req->message_count && req->messages; i++) {
        OpenAI_Message* msg = &req->messages[i];
        if (!msg->role || !msg->content) continue;
        /* Escape content to prevent JSON injection */
        char* escaped_content = openai_json_escape_string(msg->content);
        char* escaped_role = openai_json_escape_string(msg->role);

        /* Calculate required space before writing */
        size_t msg_len = (escaped_role ? strlen(escaped_role) : strlen(msg->role)) +
                         (escaped_content ? strlen(escaped_content) : strlen(msg->content)) + 64;
        while (offset + msg_len >= buf_size) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) {
                free(escaped_content);
                free(escaped_role);
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }

        int written = snprintf(buf + offset, buf_size - offset,
            "%s{\"role\":\"%s\",\"content\":\"%s\"}",
            first_msg ? "" : ",", escaped_role ? escaped_role : msg->role,
            escaped_content ? escaped_content : msg->content);
        offset += written;
        first_msg = 0;
        free(escaped_content);
        free(escaped_role);
    }

    /* Add optional parameters */
    if (req->temperature >= 0) {
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

    /* Add top_p parameter */
    if (req->top_p > 0 && req->top_p < 1.0) {
        char top_p_buf[64];
        snprintf(top_p_buf, sizeof(top_p_buf), ",\"top_p\":%g", req->top_p);
        size_t top_p_len = strlen(top_p_buf);
        while (offset + top_p_len >= buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) { free(buf); return NULL; }
            buf = new_buf;
        }
        strcpy(buf + offset, top_p_buf);
        offset += top_p_len;
    }

    /* Add stop parameter */
    if (req->stop) {
        char* escaped_stop = openai_json_escape_string(req->stop);
        const char* stop_str = escaped_stop ? escaped_stop : req->stop;
        size_t stop_param_len = strlen(stop_str) + 32;
        char* stop_buf = (char*)malloc(stop_param_len);
        if (stop_buf) {
            snprintf(stop_buf, stop_param_len, ",\"stop\":\"%s\"", stop_str);
            size_t stop_len = strlen(stop_buf);
            while (offset + stop_len >= buf_size - 1) {
                buf_size *= 2;
                char* new_buf = (char*)realloc(buf, buf_size);
                if (!new_buf) { free(stop_buf); free(escaped_stop); free(buf); return NULL; }
                buf = new_buf;
            }
            strcpy(buf + offset, stop_buf);
            offset += stop_len;
            free(stop_buf);
        }
        free(escaped_stop);
    }

    /* Close messages array and object */
    /* Ensure buffer has space for closing characters */
    size_t closing_len = strlen("],\"stream\":true}");
    while (offset + closing_len >= buf_size - 1) {
        buf_size *= 2;
        char* new_buf = (char*)realloc(buf, buf_size);
        if (!new_buf) {
            free(buf);
            return NULL;
        }
        buf = new_buf;
    }

    snprintf(buf + offset, buf_size - offset, "],\"stream\":%s}",
        req->stream ? "true" : "false");

    return buf;
}

static char* build_anthropic_request_body(OpenAI_ChatRequest* req) {
    size_t buf_size = 1024;
    char* buf = (char*)malloc(buf_size);
    if (!buf) return NULL;

    /* First pass: collect system messages and count non-system messages */
    char* system_content = NULL;
    size_t system_len = 0;
    size_t non_system_count = 0;

    for (size_t i = 0; i < req->message_count && req->messages; i++) {
        OpenAI_Message* msg = &req->messages[i];
        if (msg->role && strcmp(msg->role, "system") == 0) {
            /* Accumulate system messages */
            char* escaped = openai_json_escape_string(msg->content ? msg->content : "");
            size_t part_len = escaped ? strlen(escaped) : (msg->content ? strlen(msg->content) : 0);
            if (system_content) {
                char* new_sys = (char*)realloc(system_content, system_len + part_len + 3);
                if (new_sys) {
                    system_content = new_sys;
                    if (system_len > 0) {
                        system_content[system_len++] = '\\';
                        system_content[system_len++] = 'n';
                        system_content[system_len] = '\0';
                    }
                    memcpy(system_content + system_len, escaped ? escaped : (msg->content ? msg->content : ""), part_len);
                    system_len += part_len;
                    system_content[system_len] = '\0';
                }
            } else {
                system_content = (char*)malloc(part_len + 1);
                if (system_content) {
                    memcpy(system_content, escaped ? escaped : (msg->content ? msg->content : ""), part_len);
                    system_len = part_len;
                    system_content[system_len] = '\0';
                }
            }
            free(escaped);
        } else {
            non_system_count++;
        }
    }

    /* Build JSON: model, max_tokens, system (if any), messages, optional params, stream */
    char* escaped_model = openai_json_escape_string(req->model ? req->model : "claude-3-opus-20240229");
    int offset = snprintf(buf, buf_size,
        "{\"model\":\"%s\",\"max_tokens\":%d",
        escaped_model ? escaped_model : "claude-3-opus-20240229",
        req->max_tokens > 0 ? req->max_tokens : 4096);
    free(escaped_model);

    /* Add system field if present */
    if (system_content) {
        while (offset + (int)system_len + 64 >= (int)buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) { free(system_content); free(buf); return NULL; }
            buf = new_buf;
        }
        offset += snprintf(buf + offset, buf_size - offset, ",\"system\":\"%s\"", system_content);
        free(system_content);
    }

    /* Add messages array (non-system only) */
    offset += snprintf(buf + offset, buf_size - offset, ",\"messages\":[");
    int first_non_system = 1;
    for (size_t i = 0; i < req->message_count && req->messages; i++) {
        OpenAI_Message* msg = &req->messages[i];
        if (msg->role && strcmp(msg->role, "system") == 0) continue;
        if (!msg->role || !msg->content) continue;
        char* escaped_content = openai_json_escape_string(msg->content);
        char* escaped_role = openai_json_escape_string(msg->role);
        size_t msg_len = (escaped_role ? strlen(escaped_role) : strlen(msg->role)) +
                         (escaped_content ? strlen(escaped_content) : strlen(msg->content)) + 64;
        while (offset + (int)msg_len >= (int)buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) {
                free(escaped_content); free(escaped_role); free(buf);
                return NULL;
            }
            buf = new_buf;
        }
        int written = snprintf(buf + offset, buf_size - offset,
            "%s{\"role\":\"%s\",\"content\":\"%s\"}",
            first_non_system ? "" : ",",
            escaped_role ? escaped_role : msg->role,
            escaped_content ? escaped_content : msg->content);
        if (written > 0) offset += written;
        first_non_system = 0;
        free(escaped_content);
        free(escaped_role);
    }
    offset += snprintf(buf + offset, buf_size - offset, "]");

    /* Add temperature */
    if (req->temperature >= 0) {
        char temp_buf[64];
        snprintf(temp_buf, sizeof(temp_buf), ",\"temperature\":%g", req->temperature);
        size_t temp_len = strlen(temp_buf);
        while (offset + (int)temp_len >= (int)buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) { free(buf); return NULL; }
            buf = new_buf;
        }
        strcpy(buf + offset, temp_buf);
        offset += temp_len;
    }

    /* Add top_p */
    if (req->top_p > 0 && req->top_p < 1.0) {
        char top_p_buf[64];
        snprintf(top_p_buf, sizeof(top_p_buf), ",\"top_p\":%g", req->top_p);
        size_t top_p_len = strlen(top_p_buf);
        while (offset + (int)top_p_len >= (int)buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) { free(buf); return NULL; }
            buf = new_buf;
        }
        strcpy(buf + offset, top_p_buf);
        offset += top_p_len;
    }

    /* Add stop_sequences (array format for Anthropic) */
    if (req->stop) {
        char* escaped_stop = openai_json_escape_string(req->stop);
        const char* stop_str = escaped_stop ? escaped_stop : req->stop;
        size_t stop_param_len = strlen(stop_str) + 48;
        char* stop_buf = (char*)malloc(stop_param_len);
        if (stop_buf) {
            snprintf(stop_buf, stop_param_len, ",\"stop_sequences\":[\"%s\"]", stop_str);
            size_t stop_len = strlen(stop_buf);
            while (offset + (int)stop_len >= (int)buf_size - 1) {
                buf_size *= 2;
                char* new_buf = (char*)realloc(buf, buf_size);
                if (!new_buf) { free(stop_buf); free(escaped_stop); free(buf); return NULL; }
                buf = new_buf;
            }
            strcpy(buf + offset, stop_buf);
            offset += stop_len;
            free(stop_buf);
        }
        free(escaped_stop);
    }

    /* Add stream flag */
    snprintf(buf + offset, buf_size - offset, ",\"stream\":%s}",
        req->stream ? "true" : "false");

    return buf;
}

static OpenAI_ChatResponse* parse_anthropic_response(OpenAI_JSONNode* json) {
    if (!json) return NULL;

    OpenAI_ChatResponse* resp = (OpenAI_ChatResponse*)calloc(1, sizeof(OpenAI_ChatResponse));
    if (!resp) return NULL;

    /* Extract id */
    const char* id = openai_json_get_string(json, "id");
    if (id) {
        resp->id = (char*)malloc(strlen(id) + 1);
        if (resp->id) strcpy(resp->id, id);
    }

    /* Extract model */
    const char* model = openai_json_get_string(json, "model");
    if (model) {
        resp->model = (char*)malloc(strlen(model) + 1);
        if (resp->model) strcpy(resp->model, model);
    }

    /* Set object type */
    resp->object = (char*)malloc(strlen("message") + 1);
    if (resp->object) strcpy(resp->object, "message");

    /* Extract role */
    const char* role = openai_json_get_string(json, "role");

    /* Extract content array - concatenate all text blocks */
    OpenAI_JSONNode* content_arr = openai_json_get_object(json, "content");
    size_t total_content_len = 0;
    size_t text_block_count = 0;

    if (content_arr && content_arr->child_count > 0) {
        /* First pass: calculate total length */
        for (OpenAI_JSONNode* block = openai_json_array_first(content_arr); block; block = openai_json_array_next(block)) {
            const char* type = openai_json_get_string(block, "type");
            if (type && strcmp(type, "text") == 0) {
                const char* text = openai_json_get_string(block, "text");
                if (text) {
                    total_content_len += strlen(text);
                    text_block_count++;
                }
            }
        }
    }

    /* Build concatenated content string */
    if (text_block_count > 0 && total_content_len > 0) {
        resp->choice_count = 1;
        resp->choices = (OpenAI_Choice*)calloc(1, sizeof(OpenAI_Choice));
        if (!resp->choices) {
            resp->choice_count = 0;
        }
        if (resp->choices) {
            resp->choices[0].content = (char*)malloc(total_content_len + 1);
            if (resp->choices[0].content) {
                resp->choices[0].content[0] = '\0';
                size_t pos = 0;
                for (OpenAI_JSONNode* block = openai_json_array_first(content_arr); block; block = openai_json_array_next(block)) {
                    const char* type = openai_json_get_string(block, "type");
                    if (type && strcmp(type, "text") == 0) {
                        const char* text = openai_json_get_string(block, "text");
                        if (text) {
                            size_t len = strlen(text);
                            memcpy(resp->choices[0].content + pos, text, len);
                            pos += len;
                        }
                    }
                }
                resp->choices[0].content[pos] = '\0';
            }
            resp->choices[0].role = (char*)malloc(strlen(role ? role : "assistant") + 1);
            if (resp->choices[0].role)
                strcpy(resp->choices[0].role, role ? role : "assistant");
        }
    }

    /* Extract usage */
    OpenAI_JSONNode* usage = openai_json_get_object(json, "usage");
    if (usage) {
        double input_tokens = openai_json_get_number(usage, "input_tokens");
        double output_tokens = openai_json_get_number(usage, "output_tokens");
        char usage_buf[128];
        snprintf(usage_buf, sizeof(usage_buf), "input_tokens=%.0f, output_tokens=%.0f",
            input_tokens, output_tokens);
        resp->usage = (char*)malloc(strlen(usage_buf) + 1);
        if (resp->usage) strcpy(resp->usage, usage_buf);
    }

    return resp;
}

OpenAI_ChatResponse* openai_chat_create(OpenAI_Client* client, OpenAI_ChatRequest* req) {
    if (!client || !req) return NULL;

    const char* base_url = get_effective_base_url(client);
    char url[512];

    if (client->provider == OPENAI_PROVIDER_ANTHROPIC) {
        snprintf(url, sizeof(url), "%s/messages", base_url);
    } else {
        snprintf(url, sizeof(url), "%s/chat/completions", base_url);
    }

    OPENAI_LOG_INFO("Sending chat request to %s, model=%s", url, req->model ? req->model : "default");

    char* body;
    if (client->provider == OPENAI_PROVIDER_ANTHROPIC) {
        body = build_anthropic_request_body(req);
    } else {
        body = build_chat_request_body(req);
    }
    if (!body) {
        OPENAI_LOG_ERROR("Failed to build request body");
        return NULL;
    }

    OpenAI_HTTPRequest http_req = {
        .url = url,
        .method = "POST",
        .body = body,
        .auth_header = client->api_key,
        .body_size = strlen(body)
    };

    if (client->provider == OPENAI_PROVIDER_ANTHROPIC) {
        http_req.auth_mode = 1;
        http_req.extra_headers = "anthropic-version: 2023-06-01\r\n";
    }

    OpenAI_HTTPResponse* http_resp = openai_http_request(&http_req);
    free(body);

    if (!http_resp) {
        OPENAI_LOG_ERROR("HTTP request failed");
        return NULL;
    }

    if (http_resp->status_code != 200) {
        OPENAI_LOG_ERROR("API error: status_code=%ld, body=%.200s",
            (long)http_resp->status_code,
            http_resp->body ? http_resp->body : "(null)");
        openai_http_response_free(http_resp);
        return NULL;
    }

    /* Parse JSON response */
    OpenAI_JSONNode* json = openai_json_parse(http_resp->body);
    openai_http_response_free(http_resp);

    if (!json) {
        OPENAI_LOG_ERROR("Failed to parse JSON response");
        return NULL;
    }

    /* Use provider-specific response parser */
    OpenAI_ChatResponse* resp;
    if (client->provider == OPENAI_PROVIDER_ANTHROPIC) {
        resp = parse_anthropic_response(json);
    } else {
        resp = (OpenAI_ChatResponse*)calloc(1, sizeof(OpenAI_ChatResponse));
        if (!resp) {
            OPENAI_LOG_ERROR("Failed to allocate chat response structure");
            openai_json_free(json);
            return NULL;
        }

    /* Extract fields from JSON */
    const char* id = openai_json_get_string(json, "id");
    if (id) {
        resp->id = (char*)malloc(strlen(id) + 1);
        if (resp->id) strcpy(resp->id, id);
    }

    const char* model = openai_json_get_string(json, "model");
    if (model) {
        resp->model = (char*)malloc(strlen(model) + 1);
        if (resp->model) strcpy(resp->model, model);
    }

    const char* object = openai_json_get_string(json, "object");
    if (object) {
        resp->object = (char*)malloc(strlen(object) + 1);
        if (resp->object) strcpy(resp->object, object);
    }

    resp->created = (long)openai_json_get_number(json, "created");

    /* Extract choices */
    OpenAI_JSONNode* choices = openai_json_get_object(json, "choices");
    if (choices && choices->child_count > 0) {
        resp->choice_count = choices->child_count;
        resp->choices = (OpenAI_Choice*)malloc(sizeof(OpenAI_Choice) * resp->choice_count);
        if (!resp->choices) {
            resp->choice_count = 0;
        } else {
        memset(resp->choices, 0, sizeof(OpenAI_Choice) * resp->choice_count);

        size_t ci = 0;
        for (OpenAI_JSONNode* choice = openai_json_array_first(choices); choice && ci < resp->choice_count; choice = openai_json_array_next(choice), ci++) {
            OpenAI_JSONNode* msg = openai_json_get_object(choice, "message");
            if (msg) {
                const char* content = openai_json_get_string(msg, "content");
                if (content) {
                    resp->choices[ci].content = (char*)malloc(strlen(content) + 1);
                    if (resp->choices[ci].content) strcpy(resp->choices[ci].content, content);
                }
                const char* role = openai_json_get_string(msg, "role");
                if (role) {
                    resp->choices[ci].role = (char*)malloc(strlen(role) + 1);
                    if (resp->choices[ci].role) strcpy(resp->choices[ci].role, role);
                }
            }
            resp->choices[ci].index = (int)openai_json_get_number(choice, "index");
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
            if (resp->usage) strcpy(resp->usage, usage_buf);
        }
    } /* end else (OpenAI) */
    }

    openai_json_free(json);
    return resp;
}

void openai_chat_response_free(OpenAI_ChatResponse* resp) {
    if (resp) {
        if (resp->id) free(resp->id);
        if (resp->model) free(resp->model);
        if (resp->object) free(resp->object);
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
    int provider;           /* OpenAI_Provider value */
    char current_event[64]; /* For Anthropic: tracks current event: line */
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
    /* Safe approach: handle escaped quotes properly */
    const char* content_start = strstr(line, "\"content\":\"");
    if (content_start) {
        content_start += 11; /* skip "\"content\":\"" */

        /* Find closing quote, handling escaped characters */
        const char* p = content_start;
        size_t content_len = 0;

        while (*p && *p != '"' && content_len < output_size - 1) {
            if (*p == '\\') {
                p++;
                if (*p == '\0') break;
                if (*p == 'n') { output[content_len++] = '\n'; }
                else if (*p == 't') { output[content_len++] = '\t'; }
                else if (*p == 'r') { output[content_len++] = '\r'; }
                else if (*p == '\\') { output[content_len++] = '\\'; }
                else if (*p == '"') { output[content_len++] = '"'; }
                else { output[content_len++] = *p; }
                p++;
            } else {
                output[content_len++] = *p;
                p++;
            }
        }
        output[content_len] = '\0';
        return 0;
    }
    return -1;
}

static int parse_anthropic_sse_line(const char* data, size_t data_len,
                                     char* output, size_t output_size) {
    /* Anthropic SSE data: {"type":"content_block_delta","index":0,
     * "delta":{"type":"text_delta","text":"..."}} */
    const char* text_start = strstr(data, "\"text\":\"");
    if (!text_start) {
        /* Also try delta.text format */
        text_start = strstr(data, "\"delta\":{\"type\":\"text_delta\",\"text\":\"");
        if (text_start) text_start += 34; /* skip to text value */
        else return -1;
    } else {
        text_start += 8; /* skip "\"text\":\"" */
    }

    const char* p = text_start;
    size_t content_len = 0;
    while (*p && *p != '"' && content_len < output_size - 1) {
        if (*p == '\\') {
            p++;
            if (*p == '\0') break;
            if (*p == 'n') { output[content_len++] = '\n'; }
            else if (*p == 't') { output[content_len++] = '\t'; }
            else if (*p == 'r') { output[content_len++] = '\r'; }
            else if (*p == '\\') { output[content_len++] = '\\'; }
            else if (*p == '"') { output[content_len++] = '"'; }
            else { output[content_len++] = *p; }
            p++;
        } else {
            output[content_len++] = *p;
            p++;
        }
    }
    output[content_len] = '\0';
    return 0;
}

static size_t find_line_start(const char* buffer, size_t size, size_t start) {
    while (start < size) {
        if (buffer[start] != '\n' && buffer[start] != '\r') {
            break;
        }
        start++;
    }
    return start;
}

static size_t find_line_end(const char* buffer, size_t size, size_t start) {
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

    const char* base_url = get_effective_base_url(client);
    char url[512];

    if (client->provider == OPENAI_PROVIDER_ANTHROPIC) {
        snprintf(url, sizeof(url), "%s/messages", base_url);
    } else {
        snprintf(url, sizeof(url), "%s/chat/completions", base_url);
    }

    OPENAI_LOG_INFO("Sending streaming chat request to %s, model=%s", url, req->model ? req->model : "default");

    /* Create a temporary copy of req with stream=1 instead of mutating caller's req.
     * This avoids a data race when multiple threads use the same req object. */
    OpenAI_ChatRequest stream_req = *req;
    stream_req.stream = 1;

    char* body;
    if (client->provider == OPENAI_PROVIDER_ANTHROPIC) {
        body = build_anthropic_request_body(&stream_req);
    } else {
        body = build_chat_request_body(&stream_req);
    }
    if (!body) {
        OPENAI_LOG_ERROR("Failed to build streaming request body");
        return NULL;
    }

    OpenAI_HTTPRequest http_req = {
        .url = url,
        .method = "POST",
        .body = body,
        .auth_header = client->api_key,
        .body_size = strlen(body)
    };

    if (client->provider == OPENAI_PROVIDER_ANTHROPIC) {
        http_req.auth_mode = 1;
        http_req.extra_headers = "anthropic-version: 2023-06-01\r\n";
    }

    OpenAI_HTTPResponse* http_resp = openai_http_request_stream(&http_req);
    free(body);

    if (!http_resp) {
        OPENAI_LOG_ERROR("Streaming HTTP request failed");
        return NULL;
    }

    if (http_resp->status_code != 200) {
        OPENAI_LOG_ERROR("Streaming API error: status_code=%ld, body=%.200s",
            (long)http_resp->status_code,
            http_resp->body ? http_resp->body : "(null)");
        openai_http_response_free(http_resp);
        return NULL;
    }

    OpenAI_StreamHandle* handle = (OpenAI_StreamHandle*)calloc(1, sizeof(OpenAI_StreamHandle));
    if (!handle) {
        OPENAI_LOG_ERROR("Failed to allocate stream handle");
        openai_http_response_free(http_resp);
        return NULL;
    }

    handle->buffer = http_resp->body;
    handle->buffer_size = http_resp->body_size;
    handle->buffer_pos = 0;
    handle->parse_pos = 0;
    handle->eof = 0;
    handle->provider = client->provider;
    handle->current_event[0] = '\0';

    /* Clean up http_resp struct but NOT the body (transferred to handle) */
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

    char line_buffer[4096];

    while (handle->parse_pos < handle->buffer_size) {
        size_t line_start = find_line_start(handle->buffer, handle->buffer_size, handle->parse_pos);
        size_t line_end = find_line_end(handle->buffer, handle->buffer_size, line_start);

        size_t line_len = line_end - line_start;
        if (line_len >= sizeof(line_buffer)) {
            line_len = sizeof(line_buffer) - 1;
        }

        if (line_len > 0) {
            memcpy(line_buffer, handle->buffer + line_start, line_len);
            line_buffer[line_len] = '\0';

            /* Anthropic SSE: track event type from "event: " lines */
            if (handle->provider == OPENAI_PROVIDER_ANTHROPIC) {
                if (line_len > 7 && strncmp(line_buffer, "event: ", 7) == 0) {
                    size_t ev_len = line_len - 7;
                    if (ev_len >= sizeof(handle->current_event)) ev_len = sizeof(handle->current_event) - 1;
                    memcpy(handle->current_event, line_buffer + 7, ev_len);
                    handle->current_event[ev_len] = '\0';
                } else if (line_len > 6 && strncmp(line_buffer, "data: ", 6) == 0) {
                    const char* data_str = line_buffer + 6;
                    size_t data_len = line_len - 6;
                    /* Check for message_stop */
                    if (strcmp(handle->current_event, "message_stop") == 0) {
                        handle->eof = 1;
                        return OPENAI_ERR_EOF;
                    }
                    /* Parse content_block_start for role and index */
                    if (strcmp(handle->current_event, "content_block_start") == 0) {
                        const char* role_start = strstr(data_str, "\"role\":\"");
                        if (role_start) {
                            role_start += 8;
                            const char* role_end = strchr(role_start, '"');
                            if (role_end && (size_t)(role_end - role_start) < 64) {
                                event->role = (char*)malloc(role_end - role_start + 1);
                                if (event->role) {
                                    memcpy(event->role, role_start, role_end - role_start);
                                    event->role[role_end - role_start] = '\0';
                                }
                            }
                        }
                        const char* index_start = strstr(data_str, "\"index\":");
                        if (index_start) {
                            event->index = atoi(index_start + 8);
                        }
                    }
                    /* Parse message_delta for stop_reason */
                    if (strcmp(handle->current_event, "message_delta") == 0) {
                        const char* stop_start = strstr(data_str, "\"stop_reason\":\"");
                        if (stop_start) {
                            stop_start += 15;
                            const char* stop_end = strchr(stop_start, '"');
                            if (stop_end) {
                                size_t stop_len = stop_end - stop_start;
                                event->stop_reason = (char*)malloc(stop_len + 1);
                                if (event->stop_reason) {
                                    memcpy(event->stop_reason, stop_start, stop_len);
                                    event->stop_reason[stop_len] = '\0';
                                }
                            }
                        }
                    }
                    /* Parse content_block_delta */
                    if (strcmp(handle->current_event, "content_block_delta") == 0) {
                        char content[2048] = {0};
                        int ret = parse_anthropic_sse_line(data_str, data_len,
                                                           content, sizeof(content));
                        if (ret == 0 && content[0] != '\0') {
                            event->content = (char*)malloc(strlen(content) + 1);
                            if (event->content) strcpy(event->content, content);
                            event->event_type = OPENAI_EVENT_CHUNK;
                            handle->parse_pos = line_end + 1;
                            if (handle->parse_pos < handle->buffer_size &&
                                (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                                handle->parse_pos++;
                            }
                            return 0;
                        }
                    }
                }
                /* Skip empty lines and non-data lines for Anthropic */
            } else {
                /* OpenAI SSE parsing */
                char content[2048] = {0};
                int ret = parse_sse_line(line_buffer, line_len, content, sizeof(content));

            if (ret == 1) {
                handle->eof = 1;
                return OPENAI_ERR_EOF;
            } else if (ret == 0 && content[0] != '\0') {
                event->content = (char*)malloc(strlen(content) + 1);
                if (event->content) strcpy(event->content, content);
                event->event_type = OPENAI_EVENT_CHUNK;

                /* Parse role from line_buffer */
                const char* role_start = strstr(line_buffer, "\"role\":\"");
                if (role_start) {
                    role_start += 8;
                    const char* role_end = strstr(role_start, "\"");
                    if (role_end && (size_t)(role_end - role_start) < 64) {
                        event->role = (char*)malloc(role_end - role_start + 1);
                        if (event->role) {
                            memcpy(event->role, role_start, role_end - role_start);
                            event->role[role_end - role_start] = '\0';
                        }
                    }
                }

                /* Parse index from line_buffer */
                const char* index_start = strstr(line_buffer, "\"index\":");
                if (index_start) {
                    event->index = atoi(index_start + 8);
                }

                handle->parse_pos = line_end + 1;
                if (handle->parse_pos < handle->buffer_size &&
                    (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                    handle->parse_pos++;
                }
                return 0;
            }
            } /* end else (OpenAI) */
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

void openai_stream_event_free(OpenAI_StreamEvent* event) {
    if (event) {
        if (event->content) free(event->content);
        if (event->role) free(event->role);
        if (event->stop_reason) free(event->stop_reason);
        event->content = NULL;
        event->role = NULL;
        event->stop_reason = NULL;
        /* event_type is static string, no need to free */
    }
}

/* Embeddings API */
OpenAI_EmbeddingResponse* openai_embeddings_create(OpenAI_Client* client, OpenAI_EmbeddingRequest* req) {
    if (!client || !req || !req->input) return NULL;

    const char* base_url = get_effective_base_url(client);
    char url[512];
    snprintf(url, sizeof(url), "%s/embeddings", base_url);

    OPENAI_LOG_INFO("Sending embedding request to %s, model=%s", url, req->model ? req->model : "default");

    /* Build request body - input must be escaped to prevent JSON injection */
    char* escaped_input = openai_json_escape_string(req->input);
    char* escaped_model = openai_json_escape_string(req->model ? req->model : "text-embedding-3-small");
    const char* model_str = escaped_model ? escaped_model : "text-embedding-3-small";
    const char* input_str = escaped_input ? escaped_input : "";
    size_t body_size = strlen(model_str) + strlen(input_str) + 64;
    char* body = (char*)malloc(body_size);
    if (!body) {
        free(escaped_input);
        free(escaped_model);
        return NULL;
    }
    snprintf(body, body_size,
        "{\"model\":\"%s\",\"input\":\"%s\"}",
        model_str, input_str);
    free(escaped_input);
    free(escaped_model);

    OpenAI_HTTPRequest http_req = {
        .url = url,
        .method = "POST",
        .body = body,
        .auth_header = client->api_key,
        .body_size = strlen(body)
    };

    OpenAI_HTTPResponse* http_resp = openai_http_request(&http_req);
    free(body);
    if (!http_resp) {
        OPENAI_LOG_ERROR("Embeddings HTTP request failed");
        return NULL;
    }

    if (http_resp->status_code != 200) {
        OPENAI_LOG_ERROR("Embeddings API error: status_code=%ld, body=%.200s",
            (long)http_resp->status_code,
            http_resp->body ? http_resp->body : "(null)");
        openai_http_response_free(http_resp);
        return NULL;
    }

    /* Parse JSON response */
    OpenAI_JSONNode* json = openai_json_parse(http_resp->body);
    openai_http_response_free(http_resp);

    if (!json) {
        OPENAI_LOG_ERROR("Failed to parse embeddings JSON response");
        return NULL;
    }

    OpenAI_EmbeddingResponse* resp = (OpenAI_EmbeddingResponse*)calloc(1, sizeof(OpenAI_EmbeddingResponse));
    if (!resp) {
        OPENAI_LOG_ERROR("Failed to allocate embedding response structure");
        openai_json_free(json);
        return NULL;
    }

    /* Extract object and model fields */
    const char* object = openai_json_get_string(json, "object");
    if (object) {
        resp->object = (char*)malloc(strlen(object) + 1);
        if (resp->object) strcpy(resp->object, object);
    }
    const char* model = openai_json_get_string(json, "model");
    if (model) {
        resp->model = (char*)malloc(strlen(model) + 1);
        if (resp->model) strcpy(resp->model, model);
    }

    /* Extract embedding from data[0].embedding */
    OpenAI_JSONNode* data = openai_json_get_object(json, "data");
    if (data && data->child_count > 0) {
        OpenAI_JSONNode* item = openai_json_array_first(data);
        if (item) {
            OpenAI_JSONNode* embedding = openai_json_get_object(item, "embedding");
            if (embedding && embedding->child_count > 0) {
                resp->embedding_dim = embedding->child_count;
                resp->embedding = (float*)malloc(sizeof(float) * resp->embedding_dim);
                if (resp->embedding) {
                    size_t ei = 0;
                    for (OpenAI_JSONNode* val = openai_json_array_first(embedding); val && ei < resp->embedding_dim; val = openai_json_array_next(val), ei++) {
                        resp->embedding[ei] = (float)val->number_value;
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
        if (resp->object) free(resp->object);
        if (resp->model) free(resp->model);
        if (resp->embedding) free(resp->embedding);
        free(resp);
    }
}