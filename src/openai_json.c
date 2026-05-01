#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* cJSON wrapper for OpenAI */

typedef struct OpenAI_JSONNode OpenAI_JSONNode;

struct OpenAI_JSONNode {
    char* key;
    char* string_value;
    double number_value;
    int is_number;
    int is_array;
    int is_object;
    void* children;  /* OpenAI_JSONNode* array or cJSON* */
    void* next;
    int child_count;
};

static int openai_json_is_space(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static int openai_json_is_delim(char c) {
    return c == ',' || c == ':' || c == '}' || c == ']';
}

static const char* openai_json_skip_space(const char* p) {
    while (p && *p && openai_json_is_space(*p)) p++;
    return p;
}

static const char* openai_json_parse_string(const char* p, char** out) {
    if (*p != '"') {
        *out = NULL;
        return p;
    }
    p++;
    const char* start = p;
    while (*p && *p != '"') {
        if (*p == '\\') p++;
        p++;
    }
    size_t len = p - start;
    char* result = (char*)malloc(len + 1);
    memcpy(result, start, len);
    result[len] = '\0';
    *out = result;
    if (*p == '"') p++;
    return p;
}

static const char* openai_json_parse_number(const char* p, double* out) {
    char* end;
    *out = strtod(p, &end);
    return end;
}

static const char* openai_json_parse_value(const char* p, OpenAI_JSONNode* node);

static const char* openai_json_parse_object(const char* p, OpenAI_JSONNode* parent) {
    if (*p != '{') return p;
    p++;
    p = openai_json_skip_space(p);

    OpenAI_JSONNode* prev = NULL;
    while (*p && *p != '}') {
        OpenAI_JSONNode* child = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
        p = openai_json_skip_space(p);
        p = openai_json_parse_string(p, &child->key);
        p = openai_json_skip_space(p);
        if (*p == ':') p++;
        p = openai_json_skip_space(p);
        p = openai_json_parse_value(p, child);

        if (!parent->children) {
            parent->children = child;
        } else {
            prev->next = child;
        }
        prev = child;
        parent->child_count++;

        p = openai_json_skip_space(p);
        if (*p == ',') p++;
        p = openai_json_skip_space(p);
    }
    if (*p == '}') p++;
    return p;
}

static const char* openai_json_parse_array(const char* p, OpenAI_JSONNode* parent) {
    if (*p != '[') return p;
    p++;
    p = openai_json_skip_space(p);
    parent->is_array = 1;

    OpenAI_JSONNode* prev = NULL;
    while (*p && *p != ']') {
        OpenAI_JSONNode* child = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
        p = openai_json_skip_space(p);
        p = openai_json_parse_value(p, child);

        if (!parent->children) {
            parent->children = child;
        } else {
            prev->next = child;
        }
        prev = child;
        parent->child_count++;

        p = openai_json_skip_space(p);
        if (*p == ',') p++;
        p = openai_json_skip_space(p);
    }
    if (*p == ']') p++;
    return p;
}

static const char* openai_json_parse_value(const char* p, OpenAI_JSONNode* node) {
    p = openai_json_skip_space(p);

    if (*p == '{') {
        node->is_object = 1;
        return openai_json_parse_object(p, node);
    } else if (*p == '[') {
        return openai_json_parse_array(p, node);
    } else if (*p == '"') {
        p = openai_json_parse_string(p, &node->string_value);
        return p;
    } else if (isdigit(*p) || *p == '-' || *p == '+') {
        node->is_number = 1;
        return openai_json_parse_number(p, &node->number_value);
    } else if (strncmp(p, "true", 4) == 0) {
        node->number_value = 1;
        node->is_number = 1;
        return p + 4;
    } else if (strncmp(p, "false", 5) == 0) {
        node->number_value = 0;
        node->is_number = 1;
        return p + 5;
    } else if (strncmp(p, "null", 4) == 0) {
        return p + 4;
    }
    return p;
}

OpenAI_JSONNode* openai_json_parse(const char* json_string) {
    if (!json_string) return NULL;
    OpenAI_JSONNode* root = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
    openai_json_parse_value(json_string, root);
    return root;
}

void openai_json_free(OpenAI_JSONNode* node) {
    if (!node) return;

    OpenAI_JSONNode* child = (OpenAI_JSONNode*)node->children;
    while (child) {
        OpenAI_JSONNode* next = child->next;
        if (child->key) free(child->key);
        if (child->string_value) free(child->string_value);
        openai_json_free(child);
        child = next;
    }

    if (node->key) free(node->key);
    if (node->string_value) free(node->string_value);
    free(node);
}

const char* openai_json_get_string(OpenAI_JSONNode* parent, const char* key) {
    if (!parent || !key) return NULL;

    OpenAI_JSONNode* child = (OpenAI_JSONNode*)parent->children;
    while (child) {
        if (child->key && strcmp(child->key, key) == 0) {
            return child->string_value;
        }
        child = (OpenAI_JSONNode*)child->next;
    }
    return NULL;
}

double openai_json_get_number(OpenAI_JSONNode* parent, const char* key) {
    if (!parent || !key) return 0;

    OpenAI_JSONNode* child = (OpenAI_JSONNode*)parent->children;
    while (child) {
        if (child->key && strcmp(child->key, key) == 0) {
            return child->number_value;
        }
        child = (OpenAI_JSONNode*)child->next;
    }
    return 0;
}

OpenAI_JSONNode* openai_json_get_object(OpenAI_JSONNode* parent, const char* key) {
    if (!parent || !key) return NULL;

    OpenAI_JSONNode* child = (OpenAI_JSONNode*)parent->children;
    while (child) {
        if (child->key && strcmp(child->key, key) == 0) {
            return child;
        }
        child = (OpenAI_JSONNode*)child->next;
    }
    return NULL;
}

OpenAI_JSONNode* openai_json_get_array_item(OpenAI_JSONNode* parent, size_t index) {
    if (!parent || !parent->is_array) return NULL;

    OpenAI_JSONNode* child = (OpenAI_JSONNode*)parent->children;
    size_t i = 0;
    while (child) {
        if (i == index) return child;
        child = (OpenAI_JSONNode*)child->next;
        i++;
    }
    return NULL;
}

size_t openai_json_get_array_size(OpenAI_JSONNode* parent) {
    if (!parent || !parent->is_array) return 0;
    return parent->child_count;
}

char* openai_json_dump(OpenAI_JSONNode* node) {
    if (!node) return NULL;

    if (node->is_object) {
        size_t buf_size = 256;
        char* buf = (char*)malloc(buf_size);
        size_t len = 0;
        buf[0] = '{';
        buf[1] = '\0';
        len = 1;

        OpenAI_JSONNode* child = (OpenAI_JSONNode*)node->children;
        int first = 1;
        while (child) {
            if (!first) {
                buf[len++] = ',';
                if (len >= buf_size - 1) {
                    buf_size *= 2;
                    buf = (char*)realloc(buf, buf_size);
                }
                buf[len] = '\0';
            }
            first = 0;

            char key_buf[256];
            snprintf(key_buf, sizeof(key_buf), "\"%s\":", child->key ? child->key : "");
            size_t key_len = strlen(key_buf);

            if (len + key_len >= buf_size - 1) {
                buf_size *= 2;
                buf = (char*)realloc(buf, buf_size);
            }
            memcpy(buf + len, key_buf, key_len);
            len += key_len;
            buf[len] = '\0';

            if (child->is_object || child->is_array) {
                char* child_str = openai_json_dump(child);
                if (child_str) {
                    size_t child_len = strlen(child_str);
                    while (len + child_len >= buf_size - 1) {
                        buf_size *= 2;
                        buf = (char*)realloc(buf, buf_size);
                    }
                    memcpy(buf + len, child_str, child_len);
                    len += child_len;
                    buf[len] = '\0';
                    free(child_str);
                }
            } else if (child->string_value) {
                char val_buf[1024];
                snprintf(val_buf, sizeof(val_buf), "\"%s\"", child->string_value);
                size_t val_len = strlen(val_buf);
                while (len + val_len >= buf_size - 1) {
                    buf_size *= 2;
                    buf = (char*)realloc(buf, buf_size);
                }
                memcpy(buf + len, val_buf, val_len);
                len += val_len;
                buf[len] = '\0';
            } else if (child->is_number) {
                char val_buf[64];
                int written = snprintf(val_buf, sizeof(val_buf), "%g", child->number_value);
                while (len + written >= buf_size - 1) {
                    buf_size *= 2;
                    buf = (char*)realloc(buf, buf_size);
                }
                memcpy(buf + len, val_buf, written);
                len += written;
                buf[len] = '\0';
            }

            child = (OpenAI_JSONNode*)child->next;
        }

        buf[len++] = '}';
        buf[len] = '\0';
        return buf;
    } else if (node->is_array) {
        size_t buf_size = 256;
        char* buf = (char*)malloc(buf_size);
        size_t len = 0;
        buf[0] = '[';
        buf[1] = '\0';
        len = 1;

        OpenAI_JSONNode* child = (OpenAI_JSONNode*)node->children;
        int first = 1;
        while (child) {
            if (!first) {
                buf[len++] = ',';
                if (len >= buf_size - 1) {
                    buf_size *= 2;
                    buf = (char*)realloc(buf, buf_size);
                }
                buf[len] = '\0';
            }
            first = 0;

            if (child->is_object || child->is_array) {
                char* child_str = openai_json_dump(child);
                if (child_str) {
                    size_t child_len = strlen(child_str);
                    while (len + child_len >= buf_size - 1) {
                        buf_size *= 2;
                        buf = (char*)realloc(buf, buf_size);
                    }
                    memcpy(buf + len, child_str, child_len);
                    len += child_len;
                    buf[len] = '\0';
                    free(child_str);
                }
            } else if (child->string_value) {
                char val_buf[1024];
                snprintf(val_buf, sizeof(val_buf), "\"%s\"", child->string_value);
                size_t val_len = strlen(val_buf);
                while (len + val_len >= buf_size - 1) {
                    buf_size *= 2;
                    buf = (char*)realloc(buf, buf_size);
                }
                memcpy(buf + len, val_buf, val_len);
                len += val_len;
                buf[len] = '\0';
            } else if (child->is_number) {
                char val_buf[64];
                int written = snprintf(val_buf, sizeof(val_buf), "%g", child->number_value);
                while (len + written >= buf_size - 1) {
                    buf_size *= 2;
                    buf = (char*)realloc(buf, buf_size);
                }
                memcpy(buf + len, val_buf, written);
                len += written;
                buf[len] = '\0';
            }

            child = (OpenAI_JSONNode*)child->next;
        }

        buf[len++] = ']';
        buf[len] = '\0';
        return buf;
    } else if (node->string_value) {
        size_t len = strlen(node->string_value);
        char* result = (char*)malloc(len + 3);  // "" + null terminator
        if (result) {
            result[0] = '"';
            memcpy(result + 1, node->string_value, len);
            result[len + 1] = '"';
            result[len + 2] = '\0';
        }
        return result;
    } else if (node->is_number) {
        char* result = (char*)malloc(32);
        if (result) {
            snprintf(result, 32, "%g", node->number_value);
        }
        return result;
    }
    return NULL;
}