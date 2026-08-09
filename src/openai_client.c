#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include <float.h>
#include <math.h>

#include "openai.h"
#include "openai_config.h"
#include "openai_json.h"
#include "openai_http.h"

struct OpenAI_Client {
    char* api_key;
    char* base_url;
    int provider;  /* OpenAI_Provider value, default OPENAI_PROVIDER_OPENAI (0) */
    int last_error; /* Last OpenAI_ErrorCode */
};

static int s_client_count = 0;  /* Not thread-safe; only use single-threaded */

/* Safe size addition with overflow check; returns -1 on overflow, 0 on success */
static int size_add_overflow(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) return -1;
    *out = a + b;
    return 0;
}

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

    /* Set new base_url if provided.
       Strip any URL fragment (#...) so it doesn't contaminate path concatenation. */
    if (base_url) {
        /* Find and truncate at '#' */
        const char* frag = strchr(base_url, '#');
        size_t base_len = frag ? (size_t)(frag - base_url) : strlen(base_url);
        client->base_url = (char*)malloc(base_len + 1);
        if (!client->base_url) {
            OPENAI_LOG_ERROR("Failed to allocate base_url string");
            return -1;
        }
        memcpy(client->base_url, base_url, base_len);
        client->base_url[base_len] = '\0';
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

int openai_client_get_last_error(OpenAI_Client* client) {
    if (!client) return OPENAI_ERR_INVALID_PARAM;
    return client->last_error;
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

/* Helper: set last_error based on HTTP status code */
static void set_http_error(OpenAI_Client* client, int status_code) {
    if (status_code == 401) client->last_error = OPENAI_ERR_AUTH;
    else if (status_code == 429) client->last_error = OPENAI_ERR_RATE_LIMIT;
    else if (status_code >= 500) client->last_error = OPENAI_ERR_SERVER;
    else client->last_error = OPENAI_ERR_API;
}

static int get_transport_error(void) {
    return openai_http_get_last_error() == OPENAI_HTTP_ERROR_TIMEOUT
        ? OPENAI_ERR_TIMEOUT : OPENAI_ERR_NETWORK;
}

static int validate_anthropic_request(OpenAI_ChatRequest* req) {
    if (!req) return OPENAI_ERR_INVALID_PARAM;
    if (req->thinking_enabled) {
        int max_tokens = req->max_tokens > 0 ? req->max_tokens : 4096;
        int budget = req->thinking_budget > 0 ? req->thinking_budget : max_tokens / 2;
        if (budget <= 0 || budget >= max_tokens) {
            return OPENAI_ERR_INVALID_PARAM;
        }
    }
    return OPENAI_OK;
}

static char* build_chat_request_body(OpenAI_ChatRequest* req) {
    /* Build JSON request body manually */
    size_t buf_size = 1024;
    char* buf = (char*)malloc(buf_size);
    if (!buf) return NULL;

    /* Escape model to prevent JSON injection */
    char* escaped_model = openai_json_escape_string(req->model ? req->model : "gpt-4o-mini");
    int offset = snprintf(buf, buf_size,
        "{\"model\":\"%s\",\"messages\":[", escaped_model ? escaped_model : "gpt-4o-mini");
    if (offset < 0 || (size_t)offset >= buf_size) {
        free(buf);
        return NULL;
    }
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
        if (written < 0) {
            free(escaped_content);
            free(escaped_role);
            free(buf);
            return NULL;
        }
        offset += written;
        first_msg = 0;
        free(escaped_content);
        free(escaped_role);
    }

    /* Add optional parameters */
    if (req->temperature >= 0.0f) {
        char temp_buf[128];
        int temp_w = snprintf(temp_buf, sizeof(temp_buf), ",\"temperature\":%g", req->temperature);
        if (temp_w < 0 || (size_t)temp_w >= sizeof(temp_buf)) { free(buf); return NULL; }
        size_t temp_len = (size_t)temp_w;
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
        int tokens_w = snprintf(tokens_buf, sizeof(tokens_buf), ",\"max_tokens\":%d", req->max_tokens);
        if (tokens_w < 0 || (size_t)tokens_w >= sizeof(tokens_buf)) { free(buf); return NULL; }
        size_t tokens_len = (size_t)tokens_w;
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
        char top_p_buf[128];
        int top_w = snprintf(top_p_buf, sizeof(top_p_buf), ",\"top_p\":%g", req->top_p);
        if (top_w < 0 || (size_t)top_w >= sizeof(top_p_buf)) { free(buf); return NULL; }
        size_t top_p_len = (size_t)top_w;
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
            int stop_w = snprintf(stop_buf, stop_param_len, ",\"stop\":\"%s\"", stop_str);
            if (stop_w < 0 || (size_t)stop_w >= stop_param_len) { free(stop_buf); free(escaped_stop); free(buf); return NULL; }
            size_t stop_len = (size_t)stop_w;
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

    /* Close messages array and object.
       Only include "stream":true for streaming requests; omit "stream" entirely
       for non-streaming since false is the API default. */
    size_t closing_len = strlen("]}");
    if (req->stream) {
        closing_len = strlen("],\"stream\":true}");
    }
    while (offset + closing_len >= buf_size - 1) {
        buf_size *= 2;
        char* new_buf = (char*)realloc(buf, buf_size);
        if (!new_buf) {
            free(buf);
            return NULL;
        }
        buf = new_buf;
    }

    if (req->stream) {
        snprintf(buf + offset, buf_size - offset, "],\"stream\":true}");
    } else {
        snprintf(buf + offset, buf_size - offset, "]}");
    }

    return buf;
}

static char* build_anthropic_request_body(OpenAI_ChatRequest* req) {
    size_t buf_size = 1024;
    char* buf = (char*)malloc(buf_size);
    if (!buf) return NULL;

    /* First pass: collect system messages */
    char* system_content = NULL;
    size_t system_len = 0;

    for (size_t i = 0; i < req->message_count && req->messages; i++) {
        OpenAI_Message* msg = &req->messages[i];
        if (msg->role && strcmp(msg->role, "system") == 0) {
            /* Accumulate system messages */
            char* escaped = openai_json_escape_string(msg->content ? msg->content : "");
            if (!escaped) {
                free(system_content);
                free(buf);
                return NULL;
            }
            size_t part_len = strlen(escaped);
            if (system_content) {
                /* SIZE_MAX overflow protection */
                if (part_len > SIZE_MAX - 3 ||
                    system_len > SIZE_MAX - part_len - 3) {
                    OPENAI_LOG_ERROR("system_content size overflow: sys=%zu part=%zu", system_len, part_len);
                    free(escaped);
                    free(system_content);
                    free(buf);
                    return NULL;
                } else {
                    char* new_sys = (char*)realloc(system_content, system_len + part_len + 3);
                    if (!new_sys) {
                        OPENAI_LOG_ERROR("realloc system_content failed, need %zu bytes", system_len + part_len + 3);
                        free(escaped);
                        free(system_content);
                        free(buf);
                        return NULL;
                    } else {
                        system_content = new_sys;
                        if (system_len > 0) {
                            system_content[system_len++] = '\\';
                            system_content[system_len++] = 'n';
                            system_content[system_len] = '\0';
                        }
                        memcpy(system_content + system_len, escaped, part_len);
                        system_len += part_len;
                        system_content[system_len] = '\0';
                    }
                }
            } else {
                system_content = (char*)malloc(part_len + 1);
                if (!system_content) {
                    free(escaped);
                    free(buf);
                    return NULL;
                }
                memcpy(system_content, escaped, part_len);
                system_len = part_len;
                system_content[system_len] = '\0';
            }
            free(escaped);
        }
    }

    /* Build JSON: model, max_tokens, system (if any), messages, optional params, stream */
    char* escaped_model = openai_json_escape_string(req->model ? req->model : "claude-sonnet-4-20250514");
    if (!escaped_model) {
        free(system_content);
        free(buf);
        return NULL;
    }
    int offset = snprintf(buf, buf_size,
        "{\"model\":\"%s\",\"max_tokens\":%d",
        escaped_model,
        req->max_tokens > 0 ? req->max_tokens : 4096);
    if (offset < 0 || (size_t)offset >= buf_size) {
        free(escaped_model);
        free(system_content);
        free(buf);
        return NULL;
    }
    free(escaped_model);

    /* Add system field if present */
    if (system_content) {
        while (offset + (int)system_len + 64 >= (int)buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) { free(system_content); free(buf); return NULL; }
            buf = new_buf;
        }
        size_t remaining = buf_size - (size_t)offset;
        int written = snprintf(buf + offset, remaining, ",\"system\":\"%s\"", system_content);
        if (written < 0 || (size_t)written >= remaining) { free(system_content); free(buf); return NULL; }
        offset += written;
        free(system_content);
    }

    /* Add messages array (non-system only) */
    {
        int w = snprintf(buf + offset, buf_size - offset, ",\"messages\":[");
        if (w < 0 || (size_t)(offset + w) >= buf_size) { free(buf); return NULL; }
        offset += w;
    }
    int first_non_system = 1;
    for (size_t i = 0; i < req->message_count && req->messages; i++) {
        OpenAI_Message* msg = &req->messages[i];
        if (msg->role && strcmp(msg->role, "system") == 0) continue;
        if (!msg->role || !msg->content) continue;
        char* escaped_content = openai_json_escape_string(msg->content);
        char* escaped_role = openai_json_escape_string(msg->role);
        if (!escaped_content || !escaped_role) {
            free(escaped_content);
            free(escaped_role);
            free(buf);
            return NULL;
        }
        size_t msg_len = strlen(escaped_role) + strlen(escaped_content) + 64;
        while (offset + (int)msg_len >= (int)buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) {
                free(escaped_content); free(escaped_role); free(buf);
                return NULL;
            }
            buf = new_buf;
        }
        int msg_w = snprintf(buf + offset, buf_size - offset,
            "%s{\"role\":\"%s\",\"content\":\"%s\"}",
            first_non_system ? "" : ",",
            escaped_role, escaped_content);
        if (msg_w < 0) {
            free(escaped_content); free(escaped_role); free(buf);
            return NULL;
        }
        if ((size_t)(offset + msg_w) >= buf_size) {
            /* Buffer too small, realloc and retry */
            size_t needed = (size_t)offset + msg_w + 1;
            while (buf_size < needed) buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) { free(escaped_content); free(escaped_role); free(buf); return NULL; }
            buf = new_buf;
            msg_w = snprintf(buf + offset, buf_size - offset,
                "%s{\"role\":\"%s\",\"content\":\"%s\"}",
                first_non_system ? "" : ",",
                escaped_role, escaped_content);
            if (msg_w < 0) { free(escaped_content); free(escaped_role); free(buf); return NULL; }
        }
        offset += msg_w;
        first_non_system = 0;
        free(escaped_content);
        free(escaped_role);
    }
    int arr_w = snprintf(buf + offset, buf_size - offset, "]");
    if (arr_w < 0 || (size_t)(offset + arr_w) >= buf_size) { free(buf); return NULL; }
    offset += arr_w;

    /* Add temperature. Anthropic Extended Thinking does not allow temperature changes. */
    if (!req->thinking_enabled && req->temperature >= 0.0f) {
        char temp_buf[128];
        int temp_w = snprintf(temp_buf, sizeof(temp_buf), ",\"temperature\":%g", req->temperature);
        if (temp_w < 0 || (size_t)temp_w >= sizeof(temp_buf)) { free(buf); return NULL; }
        size_t temp_len = (size_t)temp_w;
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
        char top_p_buf[128];
        int top_w = snprintf(top_p_buf, sizeof(top_p_buf), ",\"top_p\":%g", req->top_p);
        if (top_w < 0 || (size_t)top_w >= sizeof(top_p_buf)) { free(buf); return NULL; }
        size_t top_p_len = (size_t)top_w;
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
        if (!escaped_stop) { free(buf); return NULL; }
        const char* stop_str = escaped_stop;
        size_t stop_param_len = strlen(stop_str) + 48;
        char* stop_buf = (char*)malloc(stop_param_len);
        if (!stop_buf) { free(escaped_stop); free(buf); return NULL; }
        int stop_w = snprintf(stop_buf, stop_param_len, ",\"stop_sequences\":[\"%s\"]", stop_str);
        if (stop_w < 0 || (size_t)stop_w >= stop_param_len) {
            free(stop_buf); free(escaped_stop); free(buf); return NULL;
        }
        size_t stop_len = (size_t)stop_w;
        while (offset + (int)stop_len >= (int)buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) { free(stop_buf); free(escaped_stop); free(buf); return NULL; }
            buf = new_buf;
        }
        strcpy(buf + offset, stop_buf);
        offset += stop_len;
        free(stop_buf);
        free(escaped_stop);
    }

    /* Add thinking parameter (Anthropic Extended Thinking) */
    if (req->thinking_enabled) {
        int max_tokens = req->max_tokens > 0 ? req->max_tokens : 4096;
        int budget = req->thinking_budget > 0 ? req->thinking_budget : max_tokens / 2;
        if (budget <= 0 || budget >= max_tokens) {
            free(buf);
            return NULL;
        }
        char think_buf[128];
        int tlen = snprintf(think_buf, sizeof(think_buf),
            ",\"thinking\":{\"type\":\"enabled\",\"budget_tokens\":%d}", budget);
        while (offset + tlen >= (int)buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) { free(buf); return NULL; }
            buf = new_buf;
        }
        memcpy(buf + offset, think_buf, tlen);
        offset += tlen;
    }

    /* Add stream flag */
    {
        const char* stream_value = req->stream ? "true" : "false";
        size_t closing_len = strlen(",\"stream\":") + strlen(stream_value) + strlen("}");
        while (offset + (int)closing_len >= (int)buf_size - 1) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(buf, buf_size);
            if (!new_buf) { free(buf); return NULL; }
            buf = new_buf;
        }
        snprintf(buf + offset, buf_size - offset, ",\"stream\":%s}", stream_value);
    }

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
    size_t total_thinking_len = 0;
    size_t text_block_count = 0;
    int has_thinking = 0;

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
            } else if (type && strcmp(type, "tool_use") == 0) {
                /* tool_use blocks: estimate JSON size without serialization.
                   First pass only needs a rough estimate; second pass will
                   serialize once with snprintf (growing buffer if needed). */
                const char* id = openai_json_get_string(block, "id");
                const char* name = openai_json_get_string(block, "name");
                size_t id_est = id ? strlen(id) : 0;
                size_t name_est = name ? strlen(name) : 0;
                /* Overhead: {"id":"","name":"","input":null} �� 32 chars
                   plus worst-case escaping (2�� for id/name). */
                size_t overhead = 32 + id_est * 2 + name_est * 2;
                size_t new_total = 0;
                if (size_add_overflow(overhead, total_content_len, &new_total) != 0) {
                    goto parse_cleanup;
                }
                total_content_len = new_total;
                text_block_count++;
            } else if (type && strcmp(type, "thinking") == 0) {
                const char* thinking = openai_json_get_string(block, "thinking");
                if (thinking) {
                    total_thinking_len += strlen(thinking);
                    has_thinking = 1;
                }
            }
        }
    }

    /* Always allocate choices for Anthropic responses */
    resp->choice_count = 1;
    resp->choices = (OpenAI_Choice*)calloc(1, sizeof(OpenAI_Choice));
    if (!resp->choices) {
        OPENAI_LOG_ERROR("Failed to allocate choices array");
        openai_chat_response_free(resp);
        return NULL;
    }
    if (resp->choices) {
        /* Build concatenated content string (text + tool_use) */
        if (text_block_count > 0 && total_content_len > 0) {
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
                    } else if (type && strcmp(type, "tool_use") == 0) {
                        /* tool_use blocks: serialize as JSON string including input.
                           Buffer may need to grow if escaping produces more chars than
                           our first-pass estimate reserved. */
                        const char* id = openai_json_get_string(block, "id");
                        const char* name = openai_json_get_string(block, "name");
                        OpenAI_JSONNode* input = openai_json_get_object(block, "input");
                        char* input_str = input ? openai_json_serialize(input) : NULL;
                        char* escaped_id = openai_json_escape_string(id ? id : "");
                        char* escaped_name = openai_json_escape_string(name ? name : "");
                        if (!escaped_id || !escaped_name || (input && !input_str)) {
                            free(input_str); free(escaped_id); free(escaped_name);
                            goto content_cleanup;
                        }
                        size_t remaining = total_content_len + 1 - pos;
                        int written = snprintf(resp->choices[0].content + pos, remaining,
                            "{\"id\":\"%s\",\"name\":\"%s\",\"input\":%s}",
                            escaped_id, escaped_name, input_str ? input_str : "null");
                        if (written < 0) {
                            free(input_str); free(escaped_id); free(escaped_name);
                            goto content_cleanup;
                        }
                        if ((size_t)written >= remaining) {
                            /* First-pass estimate was too small (rare; only when
                               input object is large). Grow buffer to actual need. */
                            size_t new_size = pos + (size_t)written + 1;
                            if (new_size < pos + 1) {
                                free(input_str); free(escaped_id); free(escaped_name);
                                goto content_cleanup;
                            }
                            char* new_buf = (char*)realloc(resp->choices[0].content, new_size);
                            if (!new_buf) {
                                free(input_str); free(escaped_id); free(escaped_name);
                                goto content_cleanup;
                            }
                            resp->choices[0].content = new_buf;
                            snprintf(resp->choices[0].content + pos, (size_t)written + 1,
                                "{\"id\":\"%s\",\"name\":\"%s\",\"input\":%s}",
                                escaped_id, escaped_name, input_str ? input_str : "null");
                        }
                        pos += (size_t)written;
                        free(input_str);
                        free(escaped_id);
                        free(escaped_name);
                    }
                }
                resp->choices[0].content[pos] = '\0';
            }
        }
        if (has_thinking && total_thinking_len > 0) {
            resp->choices[0].thinking = (char*)malloc(total_thinking_len + 1);
            if (resp->choices[0].thinking) {
                resp->choices[0].thinking[0] = '\0';
                size_t tpos = 0;
                for (OpenAI_JSONNode* block = openai_json_array_first(content_arr); block; block = openai_json_array_next(block)) {
                    const char* type = openai_json_get_string(block, "type");
                    if (type && strcmp(type, "thinking") == 0) {
                        const char* thk = openai_json_get_string(block, "thinking");
                        if (thk) {
                            size_t len = strlen(thk);
                            memcpy(resp->choices[0].thinking + tpos, thk, len);
                            tpos += len;
                        }
                    }
                }
                resp->choices[0].thinking[tpos] = '\0';
            }
        }

        resp->choices[0].role = (char*)malloc(strlen(role ? role : "assistant") + 1);
        if (resp->choices[0].role)
                strcpy(resp->choices[0].role, role ? role : "assistant");

        /* Extract stop_reason */
        const char* stop_reason = openai_json_get_string(json, "stop_reason");
        if (stop_reason) {
            resp->choices[0].finish_reason = (char*)malloc(strlen(stop_reason) + 1);
            if (resp->choices[0].finish_reason) strcpy(resp->choices[0].finish_reason, stop_reason);
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
    }

    return resp;

content_cleanup:
    if (resp && resp->choices) {
        free(resp->choices[0].content);
        resp->choices[0].content = NULL;
    }
    /* fall through to parse_cleanup */

parse_cleanup:
    openai_chat_response_free(resp);
    return NULL;
}

OpenAI_ChatResponse* openai_chat_create(OpenAI_Client* client, OpenAI_ChatRequest* req) {
    if (!client || !req) return NULL;

    if (client->provider == OPENAI_PROVIDER_ANTHROPIC) {
        int validation = validate_anthropic_request(req);
        if (validation != OPENAI_OK) {
            client->last_error = validation;
            return NULL;
        }
    }

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
        client->last_error = OPENAI_ERR_MEMORY;
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
        client->last_error = get_transport_error();
        return NULL;
    }

    if (http_resp->status_code != 200) {
        OPENAI_LOG_ERROR("API error: status_code=%ld, body=%.200s",
            (long)http_resp->status_code,
            http_resp->body ? http_resp->body : "(null)");
        set_http_error(client, http_resp->status_code);
        openai_http_response_free(http_resp);
        return NULL;
    }

    /* Parse JSON response */
    OpenAI_JSONNode* json = openai_json_parse(http_resp->body);
    openai_http_response_free(http_resp);

    if (!json) {
        OPENAI_LOG_ERROR("Failed to parse JSON response");
        client->last_error = OPENAI_ERR_PARSE;
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
                    /* Extract tool_calls if present */
                    OpenAI_JSONNode* tool_calls = openai_json_get_object(msg, "tool_calls");
                    if (tool_calls && tool_calls->child_count > 0) {
                        /* Serialize tool_calls array as JSON string */
                        char* tc_str = openai_json_serialize(tool_calls);
                        if (tc_str) {
                            /* If content is NULL, set tool_calls as content */
                            if (!resp->choices[ci].content) {
                                resp->choices[ci].content = tc_str;
                            } else {
                                /* Append tool_calls to content */
                                size_t old_len = strlen(resp->choices[ci].content);
                                size_t tc_len = strlen(tc_str);
                                char* merged = (char*)malloc(old_len + tc_len + 2);
                                if (merged) {
                                    memcpy(merged, resp->choices[ci].content, old_len);
                                    merged[old_len] = '\n';
                                    memcpy(merged + old_len + 1, tc_str, tc_len + 1);
                                    free(resp->choices[ci].content);
                                    resp->choices[ci].content = merged;
                                }
                                free(tc_str);
                            }
                        }
                    }
                }
                resp->choices[ci].index = (int)openai_json_get_number(choice, "index");

                /* Extract finish_reason */
                const char* finish_reason = openai_json_get_string(choice, "finish_reason");
                if (finish_reason) {
                    resp->choices[ci].finish_reason = (char*)malloc(strlen(finish_reason) + 1);
                    if (resp->choices[ci].finish_reason) strcpy(resp->choices[ci].finish_reason, finish_reason);
                }
            }
            }
        }

        /* Extract usage */
        OpenAI_JSONNode* usage = openai_json_get_object(json, "usage");
        if (usage) {
            char usage_buf[256];
            usage_buf[0] = '\0';

            double p_tokens = openai_json_get_number(usage, "prompt_tokens");
            double c_tokens = openai_json_get_number(usage, "completion_tokens");
            double t_tokens = openai_json_get_number(usage, "total_tokens");
            if (p_tokens > 0 || c_tokens > 0 || t_tokens > 0) {
                snprintf(usage_buf, sizeof(usage_buf),
                    "prompt_tokens=%.0f, completion_tokens=%.0f, total_tokens=%.0f",
                    p_tokens, c_tokens, t_tokens);
            }

            if (usage_buf[0] != '\0') {
                resp->usage = (char*)malloc(strlen(usage_buf) + 1);
                if (resp->usage) strcpy(resp->usage, usage_buf);
            }
        } /* usage block end */
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
                if (resp->choices[i].finish_reason) free(resp->choices[i].finish_reason);
                if (resp->choices[i].thinking) free(resp->choices[i].thinking);
            }
            free(resp->choices);
        }
        free(resp);
    }
}

/* Streaming support */
typedef enum {
    SSE_PAYLOAD_NONE = 0,
    SSE_PAYLOAD_TEXT,
    SSE_PAYLOAD_TOOL_CALLS
} SSEPayloadType;

typedef struct {
    SSEPayloadType payload_type;
    char* payload;
    char* finish_reason;
    char* role;
    int choice_index;
} SSEParseResult;

static void sse_parse_result_clear(SSEParseResult* result) {
    if (!result) return;
    free(result->payload);
    free(result->finish_reason);
    free(result->role);
    result->payload = NULL;
    result->finish_reason = NULL;
    result->role = NULL;
    result->payload_type = SSE_PAYLOAD_NONE;
    result->choice_index = -1;
}

typedef struct {
    OpenAI_HTTPStream* http_stream;
    char* buffer;
    size_t buffer_size;
    size_t buffer_capacity;
    size_t parse_pos;
    int transport_eof;
    int eof;
    int provider;           /* OpenAI_Provider value */
    char current_event[64]; /* For Anthropic: tracks current event: line */
    char* current_role;     /* For Anthropic: current block role */
    int current_index;      /* For Anthropic: current block index */
    char current_block_type[32]; /* For Anthropic: "text" or "thinking" */
    char* current_tool_id;  /* For Anthropic tool_use: current block id */
} OpenAI_StreamHandle;

static int parse_sse_line(const char* data, size_t data_len, SSEParseResult* result) {
    if (!result) return OPENAI_ERR_INVALID_PARAM;
    memset(result, 0, sizeof(*result));
    result->payload_type = SSE_PAYLOAD_NONE;
    result->choice_index = -1;

    /* SSE format: "data: {\"key\": \"value\"}" */
    if (data_len < 6 || strncmp(data, "data: ", 6) != 0) {
        return OPENAI_ERR_EOF;
    }
    data += 6;
    data_len -= 6;

    /* Check for [DONE] marker */
    if (data_len >= 7 && strncmp(data, "[DONE]", 7) == 0) {
        return OPENAI_ERR_EOF;
    }

    /* Parse JSON payload */
    char* json_str = (char*)malloc(data_len + 1);
    if (!json_str) return OPENAI_ERR_MEMORY;
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';

    OpenAI_JSONNode* root = openai_json_parse(json_str);
    free(json_str);
    if (!root) return OPENAI_ERR_PARSE;

    /* Get choices[0] */
    OpenAI_JSONNode* choices = openai_json_get_object(root, "choices");
    if (!choices || choices->child_count == 0) {
        /* Usage-only chunk: no valid payload fields */
        openai_json_free(root);
        return 0;
    }

    OpenAI_JSONNode* choice = openai_json_array_first(choices);

    /* Extract choice index (must be integer) */
    if (choice->is_number) {
        result->choice_index = (int)choice->number_value;
    } else {
        /* index might be a sibling field; try to find it */
        int found = 0;
        for (OpenAI_JSONNode* c = choice; c; c = openai_json_array_next(c)) {
            if (c->key && strcmp(c->key, "index") == 0 && c->is_number) {
                result->choice_index = (int)c->number_value;
                found = 1;
                break;
            }
        }
        if (!found) result->choice_index = 0;
    }

    /* Extract finish_reason (orthogonal to payload) */
    const char* finish_reason = openai_json_get_string(choice, "finish_reason");
    if (finish_reason) {
        size_t fr_len = strlen(finish_reason);
        result->finish_reason = (char*)malloc(fr_len + 1);
        if (result->finish_reason) {
            memcpy(result->finish_reason, finish_reason, fr_len + 1);
        }
    }

    /* Extract delta object */
    OpenAI_JSONNode* delta = openai_json_get_object(choice, "delta");
    if (!delta) {
        openai_json_free(root);
        return 0;
    }

    /* Extract role from delta (always, never discard) */
    const char* role = openai_json_get_string(delta, "role");
    if (role) {
        size_t role_len = strlen(role);
        result->role = (char*)malloc(role_len + 1);
        if (result->role) {
            memcpy(result->role, role, role_len + 1);
        }
    }

    /* Extract tool_calls (priority: TOOL_CALLS over TEXT) */
    OpenAI_JSONNode* tool_calls = openai_json_get_object(delta, "tool_calls");
    if (tool_calls && tool_calls->child_count > 0) {
        /* Serialize the full delta object as payload */
        OpenAI_JSONNode* delta_copy = openai_json_get_object(choice, "delta");
        result->payload = openai_json_serialize(delta_copy);
        result->payload_type = SSE_PAYLOAD_TOOL_CALLS;
        openai_json_free(root);
        return 0;
    }

    /* Extract text content */
    const char* content = openai_json_get_string(delta, "content");
    if (content && content[0] != '\0') {
        size_t content_len = strlen(content);
        result->payload = (char*)malloc(content_len + 1);
        if (result->payload) {
            memcpy(result->payload, content, content_len + 1);
        }
        result->payload_type = SSE_PAYLOAD_TEXT;
    }

    openai_json_free(root);
    return 0;
}

/* Delta parse return values */
#define DELTA_READY          0   /* Event populated; caller should return it */
#define DELTA_CONSUMED       1   /* Known type but empty content; advance and continue */
#define DELTA_UNKNOWN        2   /* Unknown type; fall through to legacy parser */
#define DELTA_PARSE_ERROR   -1   /* JSON parse failure or missing required field */
#define DELTA_MEMORY_ERROR  -2   /* Memory allocation failure */

/* Extract delta content from an already-parsed JSON DOM.
 * Checks delta.type to determine event_type; avoids redundant JSON re-parse.
 * Returns DELTA_READY when event->content/thinking is populated and caller should return.
 * Returns DELTA_CONSUMED for empty content (caller should advance parse_pos and continue).
 * Returns DELTA_UNKNOWN for unhandled delta types (caller falls through to legacy parser).
 * Returns DELTA_PARSE_ERROR on malformed JSON or missing required fields.
 * Returns DELTA_MEMORY_ERROR on allocation failure.
 */
static int parse_anthropic_delta_from_dom(OpenAI_JSONNode* root, OpenAI_StreamEvent* event) {
    if (!root || !event) return DELTA_PARSE_ERROR;

    OpenAI_JSONNode* delta = openai_json_get_object(root, "delta");
    if (!delta) return DELTA_PARSE_ERROR;

    const char* type = openai_json_get_string(delta, "type");
    if (!type) return DELTA_PARSE_ERROR;

    if (strcmp(type, "text_delta") == 0) {
        const char* text = openai_json_get_string(delta, "text");
        if (!text) return DELTA_PARSE_ERROR;
        if (text[0] == '\0') return DELTA_CONSUMED;
        event->content = (char*)malloc(strlen(text) + 1);
        if (!event->content) return DELTA_MEMORY_ERROR;
        strcpy(event->content, text);
        event->event_type = OPENAI_EVENT_CHUNK;
        return DELTA_READY;
    } else if (strcmp(type, "thinking_delta") == 0) {
        const char* thinking = openai_json_get_string(delta, "thinking");
        if (!thinking) return DELTA_PARSE_ERROR;
        if (thinking[0] == '\0') return DELTA_CONSUMED;
        event->thinking = (char*)malloc(strlen(thinking) + 1);
        if (!event->thinking) return DELTA_MEMORY_ERROR;
        strcpy(event->thinking, thinking);
        event->event_type = OPENAI_EVENT_CHUNK;
        return DELTA_READY;
    } else if (strcmp(type, "input_json_delta") == 0) {
        const char* partial = openai_json_get_string(delta, "partial_json");
        if (!partial) return DELTA_PARSE_ERROR;
        if (partial[0] == '\0') return DELTA_CONSUMED;
        event->content = (char*)malloc(strlen(partial) + 1);
        if (!event->content) return DELTA_MEMORY_ERROR;
        strcpy(event->content, partial);
        event->event_type = OPENAI_EVENT_TOOL_USE_DELTA;
        return DELTA_READY;
    } else if (strcmp(type, "signature_delta") == 0) {
        const char* signature = openai_json_get_string(delta, "signature");
        if (!signature) return DELTA_PARSE_ERROR;
        if (signature[0] == '\0') return DELTA_CONSUMED;
        event->signature = (char*)malloc(strlen(signature) + 1);
        if (!event->signature) return DELTA_MEMORY_ERROR;
        strcpy(event->signature, signature);
        event->event_type = OPENAI_EVENT_THINKING_SIG;
        return DELTA_READY;
    }
    return DELTA_UNKNOWN;
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

static int stream_fill_buffer(OpenAI_StreamHandle* handle) {
    if (handle->parse_pos > 0) {
        size_t remaining = handle->buffer_size - handle->parse_pos;
        if (remaining > 0) {
            memmove(handle->buffer, handle->buffer + handle->parse_pos, remaining);
        }
        handle->buffer_size = remaining;
        handle->parse_pos = 0;
    }

    if (handle->buffer_size == handle->buffer_capacity) {
        if (handle->buffer_capacity >= 65536) return OPENAI_ERR_MEMORY;
        size_t new_capacity = handle->buffer_capacity * 2;
        if (new_capacity > 65536) new_capacity = 65536;
        char* new_buffer = (char*)realloc(handle->buffer, new_capacity);
        if (!new_buffer) return OPENAI_ERR_MEMORY;
        handle->buffer = new_buffer;
        handle->buffer_capacity = new_capacity;
    }

    int received = openai_http_stream_read(handle->http_stream,
        handle->buffer + handle->buffer_size,
        handle->buffer_capacity - handle->buffer_size);
    if (received < 0) return get_transport_error();
    if (received == 0) {
        handle->transport_eof = 1;
        return OPENAI_ERR_EOF;
    }
    handle->buffer_size += (size_t)received;
    return OPENAI_OK;
}

void* openai_chat_create_stream(OpenAI_Client* client, OpenAI_ChatRequest* req) {
    if (!client || !req) return NULL;

    if (client->provider == OPENAI_PROVIDER_ANTHROPIC) {
        int validation = validate_anthropic_request(req);
        if (validation != OPENAI_OK) {
            client->last_error = validation;
            return NULL;
        }
    }

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
        client->last_error = OPENAI_ERR_MEMORY;
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

    int status_code = 0;
    OpenAI_HTTPStream* http_stream = openai_http_stream_open(&http_req, &status_code);
    free(body);

    if (!http_stream) {
        OPENAI_LOG_ERROR("Streaming HTTP request failed");
        client->last_error = get_transport_error();
        return NULL;
    }

    if (status_code != 200) {
        OPENAI_LOG_ERROR("Streaming API error: status_code=%ld", (long)status_code);
        set_http_error(client, status_code);
        openai_http_stream_close(http_stream);
        return NULL;
    }

    OpenAI_StreamHandle* handle = (OpenAI_StreamHandle*)calloc(1, sizeof(OpenAI_StreamHandle));
    if (!handle) {
        OPENAI_LOG_ERROR("Failed to allocate stream handle");
        openai_http_stream_close(http_stream);
        return NULL;
    }

    handle->buffer_capacity = 4096;
    handle->buffer = (char*)malloc(handle->buffer_capacity);
    if (!handle->buffer) {
        openai_http_stream_close(http_stream);
        free(handle);
        client->last_error = OPENAI_ERR_MEMORY;
        return NULL;
    }
    handle->http_stream = http_stream;
    handle->buffer_size = 0;
    handle->parse_pos = 0;
    handle->eof = 0;
    handle->provider = client->provider;
    handle->current_event[0] = '\0';

    return handle;
}

int openai_stream_read(void* stream, OpenAI_StreamEvent* event) {
    if (!stream || !event) return OPENAI_ERR_INVALID_PARAM;

    OpenAI_StreamHandle* handle = (OpenAI_StreamHandle*)stream;

    if (handle->eof) {
        return OPENAI_ERR_EOF;
    }

    memset(event, 0, sizeof(OpenAI_StreamEvent));

    while (!handle->eof) {
        if (handle->parse_pos >= handle->buffer_size) {
            if (handle->transport_eof) {
                handle->eof = 1;
                return OPENAI_ERR_EOF;
            }
            int fill_result = stream_fill_buffer(handle);
            if (fill_result != OPENAI_OK && fill_result != OPENAI_ERR_EOF) {
                return fill_result;
            }
            if (handle->buffer_size == 0 && handle->transport_eof) {
                handle->eof = 1;
                return OPENAI_ERR_EOF;
            }
        }

        size_t line_start = find_line_start(handle->buffer, handle->buffer_size, handle->parse_pos);
        size_t line_end = find_line_end(handle->buffer, handle->buffer_size, line_start);

        if (line_end == handle->buffer_size && !handle->transport_eof) {
            handle->parse_pos = line_start;
            int fill_result = stream_fill_buffer(handle);
            if (fill_result != OPENAI_OK && fill_result != OPENAI_ERR_EOF) {
                return fill_result;
            }
            continue;
        }

        size_t line_len = line_end - line_start;

        if (line_len > 0) {
            char* line_buffer = (char*)malloc(line_len + 1);
            if (!line_buffer) return OPENAI_ERR_MEMORY;
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
                    /* Check for message_stop */
                    if (strcmp(handle->current_event, "message_stop") == 0) {
                        /* If there's an active block, protocol error */
                        if (handle->current_block_type[0] != '\0') {
                            OPENAI_LOG_ERROR("Anthropic: message_stop with active block");
                            free(handle->current_tool_id);
                            handle->current_tool_id = NULL;
                            handle->current_block_type[0] = '\0';
                            handle->current_index = -1;
                            handle->parse_pos = line_end + 1;
                            handle->eof = 1;
                            free(line_buffer);
                            return OPENAI_ERR_PARSE;
                        }
                        handle->eof = 1;
                        free(line_buffer);
                        return OPENAI_ERR_EOF;
                    }
                    /* Parse message_start for role (Anthropic sends role here, not in content_block_start) */
                    if (strcmp(handle->current_event, "message_start") == 0) {
                        /* Look for "role":" inside "message":{...} */
                        const char* msg_role = strstr(data_str, "\"role\":\"");
                        if (msg_role) {
                            msg_role += 8;
                            const char* role_end = strchr(msg_role, '"');
                            if (role_end && (size_t)(role_end - msg_role) < 64) {
                                if (handle->current_role) free(handle->current_role);
                                handle->current_role = (char*)malloc(role_end - msg_role + 1);
                                if (handle->current_role) {
                                    memcpy(handle->current_role, msg_role, role_end - msg_role);
                                    handle->current_role[role_end - msg_role] = '\0';
                                }
                            }
                        }
                    }
                    /* Parse content_block_start for index and type */
                    if (strcmp(handle->current_event, "content_block_start") == 0) {
                        /* Check for active block (protocol error if exists) */
                        if (handle->current_block_type[0] != '\0') {
                            OPENAI_LOG_ERROR("Anthropic: new block_start without prior stop");
                            handle->eof = 1;
                            free(line_buffer);
                            return OPENAI_ERR_PARSE;
                        }

                        /* Parse JSON using DOM */
                        OpenAI_JSONNode* root = NULL;
                        char* serialized_json = NULL;
                        int error_code = OPENAI_OK;

                        root = openai_json_parse(data_str);
                        if (!root) goto anthropic_block_error;

                        /* Extract index */
                        OpenAI_JSONNode* index_node = openai_json_get_object(root, "index");
                        if (index_node && index_node->is_number && index_node->number_value >= 0 &&
                            index_node->number_value <= INT_MAX) {
                            handle->current_index = (int)index_node->number_value;
                        } else {
                            goto anthropic_block_error;
                        }

                        /* Extract content_block fields */
                        OpenAI_JSONNode* cb = openai_json_get_object(root, "content_block");
                        if (!cb) goto anthropic_block_error;

                        const char* type = openai_json_get_string(cb, "type");
                        if (!type || !type[0]) goto anthropic_block_error;

                        /* Commit block type atomically */
                        snprintf(handle->current_block_type, sizeof(handle->current_block_type), "%s", type);

                        /* Handle tool_use: extract id and name atomically */
                        if (strcmp(type, "tool_use") == 0) {
                            const char* tool_id = openai_json_get_string(cb, "id");
                            const char* tool_name = openai_json_get_string(cb, "name");
                            if (!tool_id || !tool_name) goto anthropic_block_error;

                            handle->current_tool_id = (char*)malloc(strlen(tool_id) + 1);
                            if (!handle->current_tool_id) {
                                OPENAI_LOG_ERROR("Anthropic tool_use: OOM allocating id");
                                error_code = OPENAI_ERR_MEMORY;
                                goto anthropic_block_error;
                            }
                            strcpy(handle->current_tool_id, tool_id);

                            /* Build START event content: {"index":N,"id":"...","name":"..."} */
                            OpenAI_JSONNode* start_obj = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
                            if (!start_obj) {
                                error_code = OPENAI_ERR_MEMORY;
                                goto anthropic_block_error;
                            }
                            /* index */
                            start_obj->is_number = 1;
                            start_obj->number_value = (double)handle->current_index;
                            start_obj->key = (char*)malloc(strlen("index") + 1);
                            if (!start_obj->key) { error_code = OPENAI_ERR_MEMORY; goto anthropic_block_error_obj; }
                            strcpy(start_obj->key, "index");
                            /* id */
                            OpenAI_JSONNode* id_node = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
                            if (!id_node) { error_code = OPENAI_ERR_MEMORY; goto anthropic_block_error_obj; }
                            id_node->key = (char*)malloc(strlen("id") + 1);
                            if (!id_node->key) { error_code = OPENAI_ERR_MEMORY; openai_json_free(id_node); goto anthropic_block_error_obj; }
                            strcpy(id_node->key, "id");
                            id_node->string_value = (char*)malloc(strlen(tool_id) + 1);
                            if (!id_node->string_value) { error_code = OPENAI_ERR_MEMORY; openai_json_free(id_node); goto anthropic_block_error_obj; }
                            strcpy(id_node->string_value, tool_id);
                            id_node->next = start_obj->children;
                            start_obj->children = id_node;
                            start_obj->child_count++;
                            /* name */
                            OpenAI_JSONNode* name_node = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
                            if (!name_node) { error_code = OPENAI_ERR_MEMORY; goto anthropic_block_error_obj; }
                            name_node->key = (char*)malloc(strlen("name") + 1);
                            if (!name_node->key) { error_code = OPENAI_ERR_MEMORY; openai_json_free(name_node); goto anthropic_block_error_obj; }
                            strcpy(name_node->key, "name");
                            name_node->string_value = (char*)malloc(strlen(tool_name) + 1);
                            if (!name_node->string_value) { error_code = OPENAI_ERR_MEMORY; openai_json_free(name_node); goto anthropic_block_error_obj; }
                            strcpy(name_node->string_value, tool_name);
                            name_node->next = start_obj->children;
                            start_obj->children = name_node;
                            start_obj->child_count++;
                            /* input (serialized from content_block input or "null") */
                            OpenAI_JSONNode* input_node = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
                            if (!input_node) { error_code = OPENAI_ERR_MEMORY; goto anthropic_block_error_obj; }
                            input_node->key = (char*)malloc(strlen("input") + 1);
                            if (!input_node->key) { error_code = OPENAI_ERR_MEMORY; openai_json_free(input_node); goto anthropic_block_error_obj; }
                            strcpy(input_node->key, "input");
                            OpenAI_JSONNode* cb_input = openai_json_get_object(cb, "input");
                            if (cb_input) {
                                input_node->string_value = openai_json_serialize(cb_input);
                                if (!input_node->string_value) { error_code = OPENAI_ERR_MEMORY; openai_json_free(input_node); goto anthropic_block_error_obj; }
                                input_node->is_object = 1;
                            } else {
                                input_node->string_value = (char*)malloc(strlen("{}") + 1);
                                if (!input_node->string_value) { error_code = OPENAI_ERR_MEMORY; openai_json_free(input_node); goto anthropic_block_error_obj; }
                                strcpy(input_node->string_value, "{}");
                            }
                            input_node->next = start_obj->children;
                            start_obj->children = input_node;
                            start_obj->child_count++;
                            start_obj->is_object = 1;

                            serialized_json = openai_json_serialize(start_obj);
                            openai_json_free(start_obj);
                            if (!serialized_json) {
                                error_code = OPENAI_ERR_MEMORY;
                                goto anthropic_block_error;
                            }

                            event->event_type = OPENAI_EVENT_TOOL_USE_START;
                            event->content = serialized_json;
                            serialized_json = NULL;
                            event->index = handle->current_index;
                            OPENAI_LOG_DEBUG("Anthropic tool_use: event=%s, idx=%d, id=%s",
                                event->event_type, event->index,
                                handle->current_tool_id ? handle->current_tool_id : "");
                            handle->parse_pos = line_end + 1;
                            if (handle->parse_pos < handle->buffer_size &&
                                (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                                handle->parse_pos++;
                            }
                            free(line_buffer);
                            openai_json_free(root);
                            return 0;

                        anthropic_block_error_obj:
                            openai_json_free(start_obj);
                        } else if (strcmp(type, "text") == 0 || strcmp(type, "thinking") == 0) {
                            const char* initial_content = openai_json_get_string(cb,
                                strcmp(type, "text") == 0 ? "text" : "thinking");
                            if (!initial_content) goto anthropic_block_error;
                            handle->parse_pos = line_end + 1;
                            while (handle->parse_pos < handle->buffer_size &&
                                   (handle->buffer[handle->parse_pos] == '\n' ||
                                    handle->buffer[handle->parse_pos] == '\r')) {
                                handle->parse_pos++;
                            }
                            free(line_buffer);
                            openai_json_free(root);
                            continue;
                        } else {
                            goto anthropic_block_error;
                        }

                    anthropic_block_error:
                        if (error_code == OPENAI_OK) error_code = OPENAI_ERR_PARSE;
                        OPENAI_LOG_ERROR("Anthropic block_start parse error");
                        free(handle->current_tool_id); handle->current_tool_id = NULL;
                        handle->current_block_type[0] = '\0';
                        handle->current_index = -1;
                        handle->parse_pos = line_end + 1;
                        handle->eof = 1;
                        free(line_buffer);
                        openai_json_free(root);
                        free(serialized_json);
                        return error_code;
                    }
                    /* Parse message_delta for stop_reason / usage */
                    if (strcmp(handle->current_event, "message_delta") == 0) {
                        int have_fields = 0;
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
                                have_fields = 1;
                            }
                        }
                        const char* seq_start = strstr(data_str, "\"stop_sequence\":");
                        if (seq_start) {
                            const char* val = seq_start + strlen("\"stop_sequence\":");
                            while (*val == ' ' || *val == '\t') val++;
                            if (*val != '\0' && *val != '}') {
                                have_fields = 1;
                            }
                        }
                        /* Extract usage if present (cumulative token stats) */
                        const char* usage_start = strstr(data_str, "\"usage\":");
                        if (usage_start) {
                            const char* obj_start = strchr(usage_start, '{');
                            if (obj_start) {
                                const char* obj_end = strchr(obj_start, '}');
                                if (obj_end) {
                                    size_t usage_len = (size_t)(obj_end - obj_start) + 1;
                                    event->usage = (char*)malloc(usage_len + 1);
                                    if (event->usage) {
                                        memcpy(event->usage, obj_start, usage_len);
                                        event->usage[usage_len] = '\0';
                                    }
                                    have_fields = 1;
                                }
                            }
                        }
                        if (have_fields) {
                            event->event_type = OPENAI_EVENT_CHUNK;
                            handle->parse_pos = line_end + 1;
                            if (handle->parse_pos < handle->buffer_size &&
                                (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                                handle->parse_pos++;
                            }
                            free(line_buffer);
                            return 0;
                        }
                        /* No exposable fields: consume this SSE line and continue */
                        handle->parse_pos = line_end + 1;
                        while (handle->parse_pos < handle->buffer_size &&
                               (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                            handle->parse_pos++;
                        }
                        free(line_buffer);
                        continue;
                    }
                    /* Parse content_block_delta */
                    if (strcmp(handle->current_event, "content_block_delta") == 0) {
                        if (handle->current_block_type[0] == '\0') {
                            OPENAI_LOG_ERROR("Anthropic delta without active block");
                            free(line_buffer);
                            handle->eof = 1;
                            return OPENAI_ERR_PARSE;
                        }

                        /* Read event's own index, validate against current */
                        int incoming_index = -1;
                        OpenAI_JSONNode* delta_root = openai_json_parse(data_str);
                        if (delta_root) {
                            OpenAI_JSONNode* idx_node = openai_json_get_object(delta_root, "index");
                            if (idx_node && idx_node->is_number) {
                                incoming_index = (int)idx_node->number_value;
                            }
                        }
                        if (incoming_index != handle->current_index) {
                            OPENAI_LOG_ERROR("Anthropic delta index mismatch: got=%d expected=%d",
                                incoming_index, handle->current_index);
                            openai_json_free(delta_root);
                            free(line_buffer);
                            handle->eof = 1;
                            return OPENAI_ERR_PARSE;
                        }

                        OpenAI_JSONNode* delta_node = openai_json_get_object(delta_root, "delta");
                        const char* delta_type = delta_node
                            ? openai_json_get_string(delta_node, "type") : NULL;
                        int type_matches = delta_type &&
                            ((strcmp(handle->current_block_type, "text") == 0 &&
                              strcmp(delta_type, "text_delta") == 0) ||
                             (strcmp(handle->current_block_type, "thinking") == 0 &&
                              (strcmp(delta_type, "thinking_delta") == 0 ||
                               strcmp(delta_type, "signature_delta") == 0)) ||
                             (strcmp(handle->current_block_type, "tool_use") == 0 &&
                              strcmp(delta_type, "input_json_delta") == 0));
                        if (!type_matches) {
                            OPENAI_LOG_ERROR("Anthropic delta type mismatch: got=%s block=%s",
                                delta_type ? delta_type : "(null)", handle->current_block_type);
                            openai_json_free(delta_root);
                            free(line_buffer);
                            handle->eof = 1;
                            return OPENAI_ERR_PARSE;
                        }

                        int ret = parse_anthropic_delta_from_dom(delta_root, event);
                        if (ret == DELTA_READY) {
                            /* Fill role/index from saved handle state */
                            if (handle->current_role) {
                                event->role = (char*)malloc(strlen(handle->current_role) + 1);
                                if (event->role) strcpy(event->role, handle->current_role);
                            }
                            event->index = handle->current_index;
                            handle->parse_pos = line_end + 1;
                            if (handle->parse_pos < handle->buffer_size &&
                                (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                                handle->parse_pos++;
                            }
                            openai_json_free(delta_root);
                            free(line_buffer);
                            return 0;
                        } else if (ret == DELTA_CONSUMED) {
                            /* Empty delta; advance and continue reading next event */
                            handle->parse_pos = line_end + 1;
                            if (handle->parse_pos < handle->buffer_size &&
                                (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                                handle->parse_pos++;
                            }
                            openai_json_free(delta_root);
                            continue;
                        } else if (ret == DELTA_PARSE_ERROR) {
                            openai_json_free(delta_root);
                            free(line_buffer);
                            handle->eof = 1;
                            return OPENAI_ERR_PARSE;
                        } else if (ret == DELTA_MEMORY_ERROR) {
                            openai_json_free(delta_root);
                            free(line_buffer);
                            openai_stream_event_free(event);
                            return OPENAI_ERR_MEMORY;
                        }
                        /* DELTA_UNKNOWN (-2) falls through to legacy parser below */
                    }
                    /* Parse content_block_stop */
                    if (strcmp(handle->current_event, "content_block_stop") == 0) {
                        if (handle->current_block_type[0] == '\0') {
                            OPENAI_LOG_ERROR("Anthropic block_stop without active block");
                            free(line_buffer);
                            handle->eof = 1;
                            return OPENAI_ERR_PARSE;
                        }

                        /* Read event's own index, validate against current */
                        int incoming_index = -1;
                        OpenAI_JSONNode* stop_root = openai_json_parse(data_str);
                        if (stop_root) {
                            OpenAI_JSONNode* idx_node = openai_json_get_object(stop_root, "index");
                            if (idx_node && idx_node->is_number) {
                                incoming_index = (int)idx_node->number_value;
                            }
                        }
                        if (incoming_index != handle->current_index) {
                            OPENAI_LOG_ERROR("Anthropic stop index mismatch: got=%d expected=%d",
                                incoming_index, handle->current_index);
                            openai_json_free(stop_root);
                            free(line_buffer);
                            handle->eof = 1;
                            return OPENAI_ERR_PARSE;
                        }

                        /* If tool_use block, emit TOOL_USE_STOP event */
                        if (strcmp(handle->current_block_type, "tool_use") == 0) {
                            OPENAI_LOG_DEBUG("Anthropic tool_use: event=%s, idx=%d, id=%s",
                                OPENAI_EVENT_TOOL_USE_STOP, handle->current_index,
                                handle->current_tool_id ? handle->current_tool_id : "");
                            event->event_type = OPENAI_EVENT_TOOL_USE_STOP;
                            event->index = handle->current_index;
                            handle->parse_pos = line_end + 1;
                            if (handle->parse_pos < handle->buffer_size &&
                                (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                                handle->parse_pos++;
                            }
                            openai_json_free(stop_root);
                            free(line_buffer);
                            /* Clear block state after event construction */
                            free(handle->current_tool_id);
                            handle->current_tool_id = NULL;
                            handle->current_block_type[0] = '\0';
                            handle->current_index = -1;
                            return 0;
                        }

                        /* All block types: clear state */
                        free(handle->current_tool_id);
                        handle->current_tool_id = NULL;
                        handle->current_block_type[0] = '\0';
                        handle->current_index = -1;
                        openai_json_free(stop_root);
                    }
                }
                /* Skip empty lines and non-data lines for Anthropic */
            } else {
                /* OpenAI SSE parsing */
                SSEParseResult result;
                memset(&result, 0, sizeof(result));
                int parse_ret = parse_sse_line(line_buffer, line_len, &result);
                if (parse_ret == OPENAI_ERR_EOF) {
                    handle->eof = 1;
                    sse_parse_result_clear(&result);
                    free(line_buffer);
                    return OPENAI_ERR_EOF;
                } else if (parse_ret != 0) {
                    sse_parse_result_clear(&result);
                } else if (result.payload_type == SSE_PAYLOAD_NONE &&
                           result.finish_reason == NULL &&
                           result.role == NULL) {
                    /* Usage-only chunk or empty delta: no event to expose */
                    sse_parse_result_clear(&result);
                } else if (result.payload_type == SSE_PAYLOAD_NONE &&
                           result.finish_reason == NULL &&
                           result.role != NULL) {
                    /* Role-only event */
                    event->event_type = OPENAI_EVENT_CHUNK;
                    event->role = result.role;
                    result.role = NULL;
                    event->index = result.choice_index;
                    OPENAI_LOG_DEBUG("OpenAI stream: payload_type=NONE, role-only");
                    handle->parse_pos = line_end + 1;
                    if (handle->parse_pos < handle->buffer_size &&
                        (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                        handle->parse_pos++;
                    }
                    free(line_buffer);
                    sse_parse_result_clear(&result);
                    return 0;
                } else if (result.payload_type == SSE_PAYLOAD_NONE &&
                           result.finish_reason != NULL) {
                    /* Finish reason only (delta empty) */
                    event->event_type = OPENAI_EVENT_CHUNK;
                    event->stop_reason = result.finish_reason;
                    result.finish_reason = NULL;
                    event->index = result.choice_index;
                    OPENAI_LOG_DEBUG("OpenAI stream: payload_type=NONE, finish_reason=%s", event->stop_reason);
                handle->parse_pos = line_end + 1;
                if (handle->parse_pos < handle->buffer_size &&
                    (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                    handle->parse_pos++;
                }
                free(line_buffer);
                return 0;
            } else {
                /* TEXT or TOOL_CALLS payload */
                event->event_type = (result.payload_type == SSE_PAYLOAD_TOOL_CALLS)
                    ? OPENAI_EVENT_TOOL_CALL_DELTA : OPENAI_EVENT_CHUNK;
                event->content = result.payload;
                result.payload = NULL;
                event->stop_reason = result.finish_reason;
                result.finish_reason = NULL;
                event->index = result.choice_index;
                OPENAI_LOG_DEBUG("OpenAI stream: payload_type=%d, event_type=%s",
                    result.payload_type, event->event_type);
                handle->parse_pos = line_end + 1;
                if (handle->parse_pos < handle->buffer_size &&
                    (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
                    handle->parse_pos++;
                }
                free(line_buffer);
                return 0;
            }
            } /* OpenAI stream payload branch end */
            free(line_buffer);
        }

        handle->parse_pos = line_end + 1;
        if (handle->parse_pos < handle->buffer_size &&
            (handle->buffer[handle->parse_pos] == '\n' || handle->buffer[handle->parse_pos] == '\r')) {
            handle->parse_pos++;
        }
    }

    return OPENAI_ERR_EOF;
}

void openai_stream_close(void* stream) {
    if (stream) {
        OpenAI_StreamHandle* handle = (OpenAI_StreamHandle*)stream;
        if (handle->http_stream) openai_http_stream_close(handle->http_stream);
        if (handle->buffer) free(handle->buffer);
        if (handle->current_role) free(handle->current_role);
        if (handle->current_tool_id) free(handle->current_tool_id);
        free(handle);
    }
}

void openai_stream_event_free(OpenAI_StreamEvent* event) {
    if (event) {
        if (event->content) free(event->content);
        if (event->role) free(event->role);
        if (event->stop_reason) free(event->stop_reason);
        if (event->thinking) free(event->thinking);
        if (event->signature) free(event->signature);
        if (event->usage) free(event->usage);
        event->content = NULL;
        event->role = NULL;
        event->stop_reason = NULL;
        event->thinking = NULL;
        event->signature = NULL;
        event->usage = NULL;
        /* event_type is static string, no need to free */
    }
}

static const char* embedding_json_skip_space(const char* p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    return p;
}

static int embedding_json_is_hex(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static unsigned int embedding_json_hex_value(char c) {
    if (c >= '0' && c <= '9') return (unsigned int)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned int)(c - 'a' + 10);
    return (unsigned int)(c - 'A' + 10);
}

/* Returns one byte past the closing quote, or NULL for malformed JSON. */
static const char* embedding_json_skip_string(const char* p) {
    if (*p != '"') return NULL;
    p++;
    while (*p && *p != '"') {
        if ((unsigned char)*p < 0x20) return NULL;
        if (*p != '\\') {
            p++;
            continue;
        }

        p++;
        if (*p == '"' || *p == '\\' || *p == '/' || *p == 'b' ||
            *p == 'f' || *p == 'n' || *p == 'r' || *p == 't') {
            p++;
            continue;
        }
        if (*p != 'u') return NULL;
        p++;
        for (int i = 0; i < 4; i++) {
            if (!embedding_json_is_hex(p[i])) return NULL;
        }
        p += 4;
    }
    return *p == '"' ? p + 1 : NULL;
}

/* Compare a validated JSON string token with an ASCII object key without allocating. */
static int embedding_json_key_equals(const char* start, const char* end, const char* expected) {
    const char* p = start + 1;
    const char* limit = end - 1;
    size_t index = 0;

    while (p < limit) {
        unsigned int ch = (unsigned char)*p++;
        if (ch == '\\') {
            if (p >= limit) return 0;
            char escaped = *p++;
            switch (escaped) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u':
                    if ((size_t)(limit - p) < 4) return 0;
                    ch = 0;
                    for (int i = 0; i < 4; i++) {
                        if (!embedding_json_is_hex(p[i])) return 0;
                        ch = (ch << 4) | embedding_json_hex_value(p[i]);
                    }
                    p += 4;
                    break;
                default:
                    return 0;
            }
        }
        if (ch > 0x7F || expected[index] == '\0' ||
            ch != (unsigned char)expected[index]) {
            return 0;
        }
        index++;
    }
    return expected[index] == '\0';
}

/* Strict RFC 8259 number scanner. Embeddings must also fit in a finite float. */
static const char* embedding_json_parse_number(const char* p, double* value) {
    const char* start = p;
    if (*p == '-') p++;

    if (*p == '0') {
        p++;
        if (*p >= '0' && *p <= '9') return NULL;
    } else if (*p >= '1' && *p <= '9') {
        do { p++; } while (*p >= '0' && *p <= '9');
    } else {
        return NULL;
    }

    if (*p == '.') {
        p++;
        if (*p < '0' || *p > '9') return NULL;
        do { p++; } while (*p >= '0' && *p <= '9');
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (*p < '0' || *p > '9') return NULL;
        do { p++; } while (*p >= '0' && *p <= '9');
    }

    char* parsed_end = NULL;
    double parsed = strtod(start, &parsed_end);
    if (parsed_end != p || !isfinite(parsed) || parsed > FLT_MAX || parsed < -FLT_MAX) {
        return NULL;
    }
    if (value) *value = parsed;
    return p;
}

static const char* embedding_json_skip_value(const char* p, int depth) {
    if (depth > OPENAI_JSON_MAX_DEPTH) return NULL;
    p = embedding_json_skip_space(p);

    if (*p == '"') return embedding_json_skip_string(p);
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        return embedding_json_parse_number(p, NULL);
    }
    if (strncmp(p, "true", 4) == 0) return p + 4;
    if (strncmp(p, "false", 5) == 0) return p + 5;
    if (strncmp(p, "null", 4) == 0) return p + 4;

    if (*p == '[') {
        p = embedding_json_skip_space(p + 1);
        if (*p == ']') return p + 1;
        while (*p) {
            p = embedding_json_skip_value(p, depth + 1);
            if (!p) return NULL;
            p = embedding_json_skip_space(p);
            if (*p == ']') return p + 1;
            if (*p != ',') return NULL;
            p = embedding_json_skip_space(p + 1);
            if (*p == ']' || *p == '\0') return NULL;
        }
        return NULL;
    }

    if (*p == '{') {
        p = embedding_json_skip_space(p + 1);
        if (*p == '}') return p + 1;
        while (*p) {
            const char* key_end = embedding_json_skip_string(p);
            if (!key_end) return NULL;
            p = embedding_json_skip_space(key_end);
            if (*p != ':') return NULL;
            p = embedding_json_skip_value(p + 1, depth + 1);
            if (!p) return NULL;
            p = embedding_json_skip_space(p);
            if (*p == '}') return p + 1;
            if (*p != ',') return NULL;
            p = embedding_json_skip_space(p + 1);
            if (*p == '}' || *p == '\0') return NULL;
        }
    }
    return NULL;
}

static int embedding_find_array_in_data(const char* value, const char** array_start) {
    const char* p = embedding_json_skip_space(value);
    if (*p != '[') return OPENAI_ERR_PARSE;
    p = embedding_json_skip_space(p + 1);
    if (*p == ']' || *p != '{') return OPENAI_ERR_PARSE;

    p = embedding_json_skip_space(p + 1);
    if (*p == '}') return OPENAI_ERR_PARSE;
    while (*p) {
        const char* key_start = p;
        const char* key_end = embedding_json_skip_string(key_start);
        if (!key_end) return OPENAI_ERR_PARSE;
        p = embedding_json_skip_space(key_end);
        if (*p != ':') return OPENAI_ERR_PARSE;
        const char* item_value = embedding_json_skip_space(p + 1);

        if (embedding_json_key_equals(key_start, key_end, "embedding")) {
            if (*item_value != '[') return OPENAI_ERR_PARSE;
            *array_start = item_value;
            return OPENAI_OK;
        }

        p = embedding_json_skip_value(item_value, 1);
        if (!p) return OPENAI_ERR_PARSE;
        p = embedding_json_skip_space(p);
        if (*p == '}') return OPENAI_ERR_PARSE;
        if (*p != ',') return OPENAI_ERR_PARSE;
        p = embedding_json_skip_space(p + 1);
        if (*p == '}' || *p == '\0') return OPENAI_ERR_PARSE;
    }
    return OPENAI_ERR_PARSE;
}

static int embedding_find_array(const char* json, const char** array_start) {
    const char* p = embedding_json_skip_space(json);
    if (*p != '{') return OPENAI_ERR_PARSE;
    p = embedding_json_skip_space(p + 1);
    if (*p == '}') return OPENAI_ERR_PARSE;

    while (*p) {
        const char* key_start = p;
        const char* key_end = embedding_json_skip_string(key_start);
        if (!key_end) return OPENAI_ERR_PARSE;
        p = embedding_json_skip_space(key_end);
        if (*p != ':') return OPENAI_ERR_PARSE;
        const char* value = embedding_json_skip_space(p + 1);

        if (embedding_json_key_equals(key_start, key_end, "data")) {
            return embedding_find_array_in_data(value, array_start);
        }

        p = embedding_json_skip_value(value, 1);
        if (!p) return OPENAI_ERR_PARSE;
        p = embedding_json_skip_space(p);
        if (*p == '}') return OPENAI_ERR_PARSE;
        if (*p != ',') return OPENAI_ERR_PARSE;
        p = embedding_json_skip_space(p + 1);
        if (*p == '}' || *p == '\0') return OPENAI_ERR_PARSE;
    }
    return OPENAI_ERR_PARSE;
}

static int embedding_parse_vector(char* json, float** values, size_t* dimension) {
    const char* array_start = NULL;
    int find_result = embedding_find_array(json, &array_start);
    if (find_result != OPENAI_OK) return find_result;

    const char* p = embedding_json_skip_space(array_start + 1);
    if (*p == ']') return OPENAI_ERR_PARSE;

    size_t count = 0;
    const char* array_end = NULL;
    while (*p) {
        const char* number_end = embedding_json_parse_number(p, NULL);
        if (!number_end || count == SIZE_MAX) return OPENAI_ERR_PARSE;
        count++;
        p = embedding_json_skip_space(number_end);
        if (*p == ']') {
            array_end = p;
            break;
        }
        if (*p != ',') return OPENAI_ERR_PARSE;
        p = embedding_json_skip_space(p + 1);
        if (*p == ']' || *p == '\0') return OPENAI_ERR_PARSE;
    }
    if (!array_end || count == 0 || count > SIZE_MAX / sizeof(float)) {
        return OPENAI_ERR_PARSE;
    }

    float* parsed_values = (float*)malloc(count * sizeof(float));
    if (!parsed_values) return OPENAI_ERR_MEMORY;

    p = embedding_json_skip_space(array_start + 1);
    for (size_t i = 0; i < count; i++) {
        double value = 0;
        const char* number_end = embedding_json_parse_number(p, &value);
        if (!number_end) {
            free(parsed_values);
            return OPENAI_ERR_PARSE;
        }
        parsed_values[i] = (float)value;
        p = embedding_json_skip_space(number_end);
        if (i + 1 < count) {
            if (*p != ',') {
                free(parsed_values);
                return OPENAI_ERR_PARSE;
            }
            p = embedding_json_skip_space(p + 1);
        }
    }
    if (p != array_end) {
        free(parsed_values);
        return OPENAI_ERR_PARSE;
    }

    /* Keep the body valid while preventing the DOM parser from allocating one node per value. */
    memset((char*)array_start + 1, ' ', (size_t)(array_end - array_start - 1));
    *values = parsed_values;
    *dimension = count;
    return OPENAI_OK;
}

/* Embeddings API */
OpenAI_EmbeddingResponse* openai_embeddings_create(OpenAI_Client* client, OpenAI_EmbeddingRequest* req) {
    if (!client) return NULL;
    if (!req || !req->input) {
        client->last_error = OPENAI_ERR_INVALID_PARAM;
        return NULL;
    }

    const char* base_url = get_effective_base_url(client);
    char url[512];
    snprintf(url, sizeof(url), "%s/embeddings", base_url);

    OPENAI_LOG_INFO("Sending embedding request to %s, model=%s", url, req->model ? req->model : "default");

    /* Build request body - input must be escaped to prevent JSON injection */
    char* escaped_input = openai_json_escape_string(req->input);
    char* escaped_model = openai_json_escape_string(req->model ? req->model : "text-embedding-3-small");
    if (!escaped_input || !escaped_model) {
        free(escaped_input);
        free(escaped_model);
        client->last_error = OPENAI_ERR_MEMORY;
        return NULL;
    }
    const char* model_str = escaped_model;
    const char* input_str = escaped_input;
    size_t body_size = 0;
    if (size_add_overflow(strlen(model_str), strlen(input_str), &body_size) != 0 ||
        size_add_overflow(body_size, 64, &body_size) != 0) {
        free(escaped_input);
        free(escaped_model);
        client->last_error = OPENAI_ERR_MEMORY;
        return NULL;
    }
    char* body = (char*)malloc(body_size);
    if (!body) {
        free(escaped_input);
        free(escaped_model);
        client->last_error = OPENAI_ERR_MEMORY;
        return NULL;
    }
    int body_written = snprintf(body, body_size,
        "{\"model\":\"%s\",\"input\":\"%s\"}",
        model_str, input_str);
    free(escaped_input);
    free(escaped_model);
    if (body_written < 0 || (size_t)body_written >= body_size) {
        free(body);
        client->last_error = OPENAI_ERR_MEMORY;
        return NULL;
    }

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
        client->last_error = get_transport_error();
        return NULL;
    }

    if (http_resp->status_code != 200) {
        OPENAI_LOG_ERROR("Embeddings API error: status_code=%ld, body=%.200s",
            (long)http_resp->status_code,
            http_resp->body ? http_resp->body : "(null)");
        set_http_error(client, http_resp->status_code);
        openai_http_response_free(http_resp);
        return NULL;
    }

    /* Parse the large numeric array directly into its final contiguous buffer. */
    float* embedding_values = NULL;
    size_t embedding_dim = 0;
    int vector_result = http_resp->body
        ? embedding_parse_vector(http_resp->body, &embedding_values, &embedding_dim)
        : OPENAI_ERR_PARSE;
    if (vector_result != OPENAI_OK) {
        OPENAI_LOG_ERROR("Failed to parse embeddings vector: error=%d", vector_result);
        client->last_error = vector_result;
        openai_http_response_free(http_resp);
        return NULL;
    }

    /* The vector contents were blanked in-place, so this DOM remains small. */
    OpenAI_JSONNode* json = openai_json_parse(http_resp->body);
    openai_http_response_free(http_resp);
    if (!json) {
        OPENAI_LOG_ERROR("Failed to parse embeddings JSON response");
        free(embedding_values);
        client->last_error = OPENAI_ERR_PARSE;
        return NULL;
    }

    OpenAI_EmbeddingResponse* resp = (OpenAI_EmbeddingResponse*)calloc(1, sizeof(OpenAI_EmbeddingResponse));
    if (!resp) {
        OPENAI_LOG_ERROR("Failed to allocate embedding response structure");
        free(embedding_values);
        openai_json_free(json);
        client->last_error = OPENAI_ERR_MEMORY;
        return NULL;
    }
    resp->embedding = embedding_values;
    resp->embedding_dim = embedding_dim;

    /* Extract object and model fields */
    const char* object = openai_json_get_string(json, "object");
    if (object) {
        resp->object = (char*)malloc(strlen(object) + 1);
        if (!resp->object) goto embedding_memory_error;
        strcpy(resp->object, object);
    }
    const char* model = openai_json_get_string(json, "model");
    if (model) {
        resp->model = (char*)malloc(strlen(model) + 1);
        if (!resp->model) goto embedding_memory_error;
        strcpy(resp->model, model);
    }

    openai_json_free(json);
    return resp;

embedding_memory_error:
    OPENAI_LOG_ERROR("Failed to allocate embeddings metadata");
    openai_json_free(json);
    openai_embedding_response_free(resp);
    client->last_error = OPENAI_ERR_MEMORY;
    return NULL;
}

void openai_embedding_response_free(OpenAI_EmbeddingResponse* resp) {
    if (resp) {
        if (resp->object) free(resp->object);
        if (resp->model) free(resp->model);
        if (resp->embedding) free(resp->embedding);
        free(resp);
    }
}
