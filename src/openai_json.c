#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "openai_json.h"

static int openai_json_is_space(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
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

    /* First pass: count unescaped length */
    const char* scan = p;
    size_t unescaped_len = 0;
    while (*scan && *scan != '"') {
        if (*scan == '\\') {
            scan++;
            if (*scan == '\0') break;
            /* Escaped char counts as 1 (except \\ and \uXXXX) */
            if (*scan == 'u') {
                /* Check if we have at least 4 more characters for \uXXXX */
                if (*(scan+1) != '\0' && *(scan+2) != '\0' &&
                    *(scan+3) != '\0' && *(scan+4) != '\0') {
                    unescaped_len += 6; /* \uXXXX outputs: \, u, + 4 hex digits */
                    scan += 4;
                } else {
                    /* Malformed \u escape, count as-is */
                    unescaped_len += 2;
                    scan++;  /* advance past 'u' to avoid double-counting */
                }
            } else {
                unescaped_len++;
                scan++;
            }
        } else {
            unescaped_len++;
            scan++;
        }
    }

    /* Second pass: copy and unescape */
    char* result = (char*)malloc(unescaped_len + 1);
    if (!result) {
        *out = NULL;
        return scan;
    }

    char* out_ptr = result;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (*p == '\0') break;
            switch (*p) {
                case '"':  *out_ptr++ = '"';  break;
                case '\\': *out_ptr++ = '\\'; break;
                case '/':  *out_ptr++ = '/';  break;
                case 'n':  *out_ptr++ = '\n'; break;
                case 'r':  *out_ptr++ = '\r'; break;
                case 't':  *out_ptr++ = '\t'; break;
                case 'b':  *out_ptr++ = '\b'; break;
                case 'f':  *out_ptr++ = '\f'; break;
                case 'u':  /* \uXXXX - copy as-is for simplicity */
                    *out_ptr++ = '\\';
                    *out_ptr++ = 'u';
                    for (int i = 0; i < 4 && *(p+1) != '\0'; i++) {
                        p++;
                        *out_ptr++ = *p;
                    }
                    break;
                default:   *out_ptr++ = *p;   break;
            }
            p++;
        } else {
            *out_ptr++ = *p++;
        }
    }
    *out_ptr = '\0';
    *out = result;
    if (*p == '"') p++;
    return p;
}

static const char* openai_json_parse_number(const char* p, double* out) {
    char* end;
    *out = strtod(p, &end);
    if (end == p) {
        *out = 0;
        return p + 1; /* skip offending char to prevent infinite loop */
    }
    return end;
}

static const char* openai_json_parse_value(const char* p, OpenAI_JSONNode* node);

static const char* openai_json_parse_object(const char* p, OpenAI_JSONNode* parent) {
    if (!parent) return p;
    if (*p != '{') return p;
    p++;
    p = openai_json_skip_space(p);

    OpenAI_JSONNode* prev = NULL;
    while (*p && *p != '}') {
        OpenAI_JSONNode* child = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
        if (!child) return p;
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
    if (!parent) return p;
    if (*p != '[') return p;
    p++;
    p = openai_json_skip_space(p);
    parent->is_array = 1;

    OpenAI_JSONNode* prev = NULL;
    while (*p && *p != ']') {
        OpenAI_JSONNode* child = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
        if (!child) return p;
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

    /* Skip leading whitespace */
    const char* p = json_string;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    /* Valid JSON must start with { or [ */
    if (*p != '{' && *p != '[') return NULL;

    OpenAI_JSONNode* root = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
    if (!root) return NULL;
    openai_json_parse_value(p, root);
    return root;
}

void openai_json_free(OpenAI_JSONNode* node) {
    if (!node) return;

    OpenAI_JSONNode* child = (OpenAI_JSONNode*)node->children;
    while (child) {
        OpenAI_JSONNode* next = child->next;
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


char* openai_json_escape_string(const char* str) {
    if (!str) return NULL;

    size_t escaped_len = 0;
    const char* p = str;
    while (*p) {
        switch (*p) {
            case '"':
            case '\\':
                escaped_len += 2;
                break;
            case '\n':
            case '\r':
            case '\t':
                escaped_len += 2;
                break;
            case '\b':
            case '\f':
                escaped_len += 2;
                break;
            default:
                if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7F) {
                    /* Control character (RFC 8259: U+0000-U+001F and U+007F): \u00XX format */
                    escaped_len += 6;
                } else {
                    escaped_len++;
                }
        }
        p++;
    }

    char* result = (char*)malloc(escaped_len + 1);
    if (!result) return NULL;

    char* out = result;
    p = str;
    while (*p) {
        switch (*p) {
            case '"':
                *out++ = '\\';
                *out++ = '"';
                break;
            case '\\':
                *out++ = '\\';
                *out++ = '\\';
                break;
            case '\n':
                *out++ = '\\';
                *out++ = 'n';
                break;
            case '\r':
                *out++ = '\\';
                *out++ = 'r';
                break;
            case '\t':
                *out++ = '\\';
                *out++ = 't';
                break;
            case '\b':
                *out++ = '\\';
                *out++ = 'b';
                break;
            case '\f':
                *out++ = '\\';
                *out++ = 'f';
                break;
            default:
                if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7F) {
                    /* Control character (RFC 8259: U+0000-U+001F and U+007F): \u00XX format */
                    snprintf(out, 7, "\\u%04x", (unsigned char)*p);
                    out += 6;
                } else {
                    *out++ = *p;
                }
        }
        p++;
    }
    *out = '\0';
    return result;
}