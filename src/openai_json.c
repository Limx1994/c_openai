#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>

#include "openai_json.h"

static int openai_json_is_space(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static const char* openai_json_skip_space(const char* p) {
    while (p && *p && openai_json_is_space(*p)) p++;
    return p;
}

static int openai_json_is_hex(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static unsigned int openai_json_hex_value(char c) {
    if (c >= '0' && c <= '9') return (unsigned int)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned int)(c - 'a' + 10);
    return (unsigned int)(c - 'A' + 10);
}

static int openai_json_parse_hex_quad(const char* p, unsigned int* value) {
    unsigned int result = 0;
    for (int i = 0; i < 4; i++) {
        if (!openai_json_is_hex(p[i])) return -1;
        result = (result << 4) | openai_json_hex_value(p[i]);
    }
    *value = result;
    return 0;
}

static size_t openai_json_utf8_length(unsigned int codepoint) {
    if (codepoint <= 0x7F) return 1;
    if (codepoint <= 0x7FF) return 2;
    if (codepoint <= 0xFFFF) return 3;
    if (codepoint <= 0x10FFFF) return 4;
    return 0;
}

static char* openai_json_write_utf8(char* out, unsigned int codepoint) {
    if (codepoint <= 0x7F) {
        *out++ = (char)codepoint;
    } else if (codepoint <= 0x7FF) {
        *out++ = (char)(0xC0 | (codepoint >> 6));
        *out++ = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        *out++ = (char)(0xE0 | (codepoint >> 12));
        *out++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        *out++ = (char)(0x80 | (codepoint & 0x3F));
    } else {
        *out++ = (char)(0xF0 | (codepoint >> 18));
        *out++ = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        *out++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        *out++ = (char)(0x80 | (codepoint & 0x3F));
    }
    return out;
}

static int openai_json_decode_unicode_escape(const char* slash, unsigned int* codepoint,
                                              size_t* consumed) {
    unsigned int first = 0;
    if (slash[0] != '\\' || slash[1] != 'u' ||
        openai_json_parse_hex_quad(slash + 2, &first) != 0) {
        return -1;
    }

    if (first >= 0xD800 && first <= 0xDBFF) {
        unsigned int second = 0;
        if (slash[6] != '\\' || slash[7] != 'u' ||
            openai_json_parse_hex_quad(slash + 8, &second) != 0 ||
            second < 0xDC00 || second > 0xDFFF) {
            return -1;
        }
        *codepoint = 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
        *consumed = 12;
        return 0;
    }
    if (first >= 0xDC00 && first <= 0xDFFF) return -1;

    *codepoint = first;
    *consumed = 6;
    return 0;
}

static const char* openai_json_parse_string(const char* p, char** out, size_t* out_length) {
    const char* start = p;
    *out = NULL;
    if (out_length) *out_length = 0;
    if (*p != '"') {
        return p;
    }
    p++;

    /* First pass: validate and count decoded UTF-8 bytes. */
    const char* scan = p;
    size_t unescaped_len = 0;
    while (*scan && *scan != '"') {
        if (*scan == '\\') {
            char escaped = scan[1];
            if (escaped == '\0') {
                return start;
            }
            if (escaped == 'u') {
                unsigned int codepoint = 0;
                size_t consumed = 0;
                if (openai_json_decode_unicode_escape(scan, &codepoint, &consumed) != 0) {
                    return start;
                }
                size_t utf8_len = openai_json_utf8_length(codepoint);
                if (utf8_len == 0 || unescaped_len > SIZE_MAX - utf8_len) return start;
                unescaped_len += utf8_len;
                scan += consumed;
            } else {
                if (escaped != '"' && escaped != '\\' && escaped != '/' &&
                    escaped != 'n' && escaped != 'r' && escaped != 't' &&
                    escaped != 'b' && escaped != 'f') {
                    return start;
                }
                if (unescaped_len == SIZE_MAX) return start;
                unescaped_len++;
                scan += 2;
            }
        } else {
            if ((unsigned char)*scan < 0x20) return start;
            if (unescaped_len == SIZE_MAX) return start;
            unescaped_len++;
            scan++;
        }
    }
    if (*scan != '"') {
        return start;
    }

    /* Second pass: copy and decode escapes. */
    char* result = (char*)malloc(unescaped_len + 1);
    if (!result) {
        return start;
    }

    char* out_ptr = result;
    while (p < scan) {
        if (*p == '\\') {
            switch (p[1]) {
                case '"':  *out_ptr++ = '"';  break;
                case '\\': *out_ptr++ = '\\'; break;
                case '/':  *out_ptr++ = '/';  break;
                case 'n':  *out_ptr++ = '\n'; break;
                case 'r':  *out_ptr++ = '\r'; break;
                case 't':  *out_ptr++ = '\t'; break;
                case 'b':  *out_ptr++ = '\b'; break;
                case 'f':  *out_ptr++ = '\f'; break;
                case 'u': {
                    unsigned int codepoint = 0;
                    size_t consumed = 0;
                    if (openai_json_decode_unicode_escape(p, &codepoint, &consumed) != 0) {
                        free(result);
                        return start;
                    }
                    out_ptr = openai_json_write_utf8(out_ptr, codepoint);
                    p += consumed;
                    continue;
                }
                default:
                    free(result);
                    return start;
            }
            p += 2;
        } else {
            *out_ptr++ = *p++;
        }
    }
    *out_ptr = '\0';
    *out = result;
    if (out_length) *out_length = unescaped_len;
    return scan + 1;
}

static const char* openai_json_parse_number(const char* p, double* out) {
    const char* start = p;
    if (*p == '-') p++;

    if (*p == '0') {
        p++;
        if (isdigit((unsigned char)*p)) return start;
    } else if (*p >= '1' && *p <= '9') {
        do { p++; } while (isdigit((unsigned char)*p));
    } else {
        return start;
    }

    if (*p == '.') {
        p++;
        if (!isdigit((unsigned char)*p)) return start;
        do { p++; } while (isdigit((unsigned char)*p));
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!isdigit((unsigned char)*p)) return start;
        do { p++; } while (isdigit((unsigned char)*p));
    }

    errno = 0;
    char* end = NULL;
    double value = strtod(start, &end);
    if (end != p || !isfinite(value)) return start;
    *out = value;
    return p;
}

static const char* openai_json_parse_value(const char* p, OpenAI_JSONNode* node);

static const char* openai_json_parse_object(const char* p, OpenAI_JSONNode* parent) {
    const char* start = p;
    if (!parent || *p != '{') return p;
    p++;
    p = openai_json_skip_space(p);

    if (*p == '}') return p + 1;

    OpenAI_JSONNode* prev = NULL;
    while (*p) {
        OpenAI_JSONNode* child = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
        if (!child) return start;
        p = openai_json_skip_space(p);
        const char* before_key = p;
        p = openai_json_parse_string(p, &child->key, &child->key_length);
        if (p == before_key || !child->key) {
            openai_json_free(child);
            return start;
        }
        p = openai_json_skip_space(p);
        if (*p == ':') p++;
        else {
            openai_json_free(child);
            return start;
        }
        p = openai_json_skip_space(p);
        const char* before_value = p;
        p = openai_json_parse_value(p, child);
        if (p == before_value) {
            openai_json_free(child);
            return start;
        }

        if (!parent->children) {
            parent->children = child;
        } else {
            prev->next = child;
        }
        prev = child;
        parent->child_count++;

        p = openai_json_skip_space(p);
        if (*p == '}') return p + 1;
        if (*p != ',') return start;
        p = openai_json_skip_space(p + 1);
        if (*p == '}' || *p == '\0') return start;
    }
    return start;
}

static const char* openai_json_parse_array(const char* p, OpenAI_JSONNode* parent) {
    const char* start = p;
    if (!parent || *p != '[') return p;
    p++;
    p = openai_json_skip_space(p);
    parent->is_array = 1;

    if (*p == ']') return p + 1;

    OpenAI_JSONNode* prev = NULL;
    while (*p) {
        OpenAI_JSONNode* child = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
        if (!child) return start;
        p = openai_json_skip_space(p);
        const char* before_value = p;
        p = openai_json_parse_value(p, child);
        if (p == before_value) {
            openai_json_free(child);
            return start;
        }

        if (!parent->children) {
            parent->children = child;
        } else {
            prev->next = child;
        }
        prev = child;
        parent->child_count++;

        p = openai_json_skip_space(p);
        if (*p == ']') return p + 1;
        if (*p != ',') return start;
        p = openai_json_skip_space(p + 1);
        if (*p == ']' || *p == '\0') return start;
    }
    return start;
}

static const char* openai_json_parse_value(const char* p, OpenAI_JSONNode* node) {
    p = openai_json_skip_space(p);

    if (*p == '{') {
        node->is_object = 1;
        return openai_json_parse_object(p, node);
    } else if (*p == '[') {
        return openai_json_parse_array(p, node);
    } else if (*p == '"') {
        const char* start = p;
        p = openai_json_parse_string(p, &node->string_value, &node->string_length);
        return node->string_value ? p : start;
    } else if (isdigit((unsigned char)*p) || *p == '-') {
        node->is_number = 1;
        return openai_json_parse_number(p, &node->number_value);
    } else if (strncmp(p, "true", 4) == 0) {
        node->is_boolean = 1;
        node->boolean_value = 1;
        return p + 4;
    } else if (strncmp(p, "false", 5) == 0) {
        node->is_boolean = 1;
        node->boolean_value = 0;
        return p + 5;
    } else if (strncmp(p, "null", 4) == 0) {
        node->is_null = 1;
        return p + 4;
    }
    return p;
}

OpenAI_JSONNode* openai_json_parse(const char* json_string) {
    if (!json_string) return NULL;

    /* Skip leading whitespace */
    const char* p = openai_json_skip_space(json_string);

    /* Valid JSON must start with { or [ */
    if (*p != '{' && *p != '[') return NULL;

    OpenAI_JSONNode* root = (OpenAI_JSONNode*)calloc(1, sizeof(OpenAI_JSONNode));
    if (!root) return NULL;
    const char* end = openai_json_parse_value(p, root);
    if (end == p) {
        openai_json_free(root);
        return NULL;
    }
    end = openai_json_skip_space(end);
    if (*end != '\0') {
        openai_json_free(root);
        return NULL;
    }
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

OpenAI_JSONNode* openai_json_array_first(OpenAI_JSONNode* parent) {
    if (!parent || !parent->is_array) return NULL;
    return (OpenAI_JSONNode*)parent->children;
}

OpenAI_JSONNode* openai_json_array_next(OpenAI_JSONNode* current) {
    if (!current) return NULL;
    return (OpenAI_JSONNode*)current->next;
}


static char* openai_json_escape_string_length(const char* str, size_t str_len) {
    size_t escaped_len = 0;
    for (size_t i = 0; i < str_len; i++) {
        unsigned char ch = (unsigned char)str[i];
        switch (ch) {
            case '"':
            case '\\':
                if (escaped_len > SIZE_MAX - 2) return NULL;
                escaped_len += 2;
                break;
            case '\n':
            case '\r':
            case '\t':
                if (escaped_len > SIZE_MAX - 2) return NULL;
                escaped_len += 2;
                break;
            case '\b':
            case '\f':
                if (escaped_len > SIZE_MAX - 2) return NULL;
                escaped_len += 2;
                break;
            default:
                if (ch < 0x20 || ch == 0x7F) {
                    /* Control character (RFC 8259: U+0000-U+001F and U+007F): \u00XX format */
                    if (escaped_len > SIZE_MAX - 6) return NULL;
                    escaped_len += 6;
                } else {
                    if (escaped_len == SIZE_MAX) return NULL;
                    escaped_len++;
                }
        }
    }

    char* result = (char*)malloc(escaped_len + 1);
    if (!result) return NULL;

    char* out = result;
    for (size_t i = 0; i < str_len; i++) {
        unsigned char ch = (unsigned char)str[i];
        switch (ch) {
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
                if (ch < 0x20 || ch == 0x7F) {
                    /* Control character (RFC 8259: U+0000-U+001F and U+007F): \u00XX format */
                    snprintf(out, 7, "\\u%04x", ch);
                    out += 6;
                } else {
                    *out++ = (char)ch;
                }
        }
    }
    *out = '\0';
    return result;
}

char* openai_json_escape_string(const char* str) {
    if (!str) return NULL;
    return openai_json_escape_string_length(str, strlen(str));
}

/* Helper for openai_json_serialize: append string to dynamic buffer */
static int serialize_append(char** buf, size_t* buf_size, size_t* offset, const char* str, size_t len) {
    if (*offset > SIZE_MAX - len - 1) return -1;
    size_t required = *offset + len + 1;
    while (required > *buf_size) {
        if (*buf_size > SIZE_MAX / 2) return -1;
        size_t new_size = *buf_size * 2;
        char* new_buf = (char*)realloc(*buf, new_size);
        if (!new_buf) return -1;
        *buf = new_buf;
        *buf_size = new_size;
    }
    memcpy(*buf + *offset, str, len);
    *offset += len;
    (*buf)[*offset] = '\0';
    return 0;
}

static int serialize_node(OpenAI_JSONNode* node, char** buf, size_t* buf_size, size_t* offset, int depth);

char* openai_json_serialize(OpenAI_JSONNode* node) {
    if (!node) return NULL;

    size_t buf_size = 256;
    size_t offset = 0;
    char* buf = (char*)malloc(buf_size);
    if (!buf) return NULL;
    buf[0] = '\0';

    if (serialize_node(node, &buf, &buf_size, &offset, 0) != 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

static int serialize_node(OpenAI_JSONNode* node, char** buf, size_t* buf_size, size_t* offset, int depth) {
    if (!node) return 0;

    if (depth > OPENAI_JSON_MAX_DEPTH) return -1;

    if (node->is_object) {
        if (serialize_append(buf, buf_size, offset, "{", 1) != 0) return -1;
        int first = 1;
        for (OpenAI_JSONNode* child = (OpenAI_JSONNode*)node->children; child; child = (OpenAI_JSONNode*)child->next) {
            if (!first) {
                if (serialize_append(buf, buf_size, offset, ",", 1) != 0) return -1;
            }
            first = 0;
            /* Key */
            if (!child->key) return -1;
            size_t raw_key_len = child->key_length;
            if (raw_key_len == 0 && child->key[0] != '\0') raw_key_len = strlen(child->key);
            char* escaped_key = openai_json_escape_string_length(child->key, raw_key_len);
            if (!escaped_key) return -1;
            size_t klen = strlen(escaped_key);
            if (serialize_append(buf, buf_size, offset, "\"", 1) != 0) { free(escaped_key); return -1; }
            if (serialize_append(buf, buf_size, offset, escaped_key, klen) != 0) { free(escaped_key); return -1; }
            if (serialize_append(buf, buf_size, offset, "\":", 2) != 0) { free(escaped_key); return -1; }
            free(escaped_key);
            /* Value */
            if (serialize_node(child, buf, buf_size, offset, depth + 1) != 0) return -1;
        }
        if (serialize_append(buf, buf_size, offset, "}", 1) != 0) return -1;
    } else if (node->is_array) {
        if (serialize_append(buf, buf_size, offset, "[", 1) != 0) return -1;
        int first = 1;
        for (OpenAI_JSONNode* child = (OpenAI_JSONNode*)node->children; child; child = (OpenAI_JSONNode*)child->next) {
            if (!first) {
                if (serialize_append(buf, buf_size, offset, ",", 1) != 0) return -1;
            }
            first = 0;
            if (serialize_node(child, buf, buf_size, offset, depth + 1) != 0) return -1;
        }
        if (serialize_append(buf, buf_size, offset, "]", 1) != 0) return -1;
    } else if (node->string_value) {
        size_t raw_string_len = node->string_length;
        if (raw_string_len == 0 && node->string_value[0] != '\0') raw_string_len = strlen(node->string_value);
        char* escaped = openai_json_escape_string_length(node->string_value, raw_string_len);
        if (!escaped) return -1;
        size_t slen = strlen(escaped);
        if (serialize_append(buf, buf_size, offset, "\"", 1) != 0) { free(escaped); return -1; }
        if (serialize_append(buf, buf_size, offset, escaped, slen) != 0) { free(escaped); return -1; }
        if (serialize_append(buf, buf_size, offset, "\"", 1) != 0) { free(escaped); return -1; }
        free(escaped);
    } else if (node->is_number) {
        char num_buf[64];
        if (!isfinite(node->number_value)) return -1;
        int nlen = snprintf(num_buf, sizeof(num_buf), "%.17g", node->number_value);
        if (nlen < 0 || (size_t)nlen >= sizeof(num_buf)) return -1;
        if (serialize_append(buf, buf_size, offset, num_buf, (size_t)nlen) != 0) return -1;
    } else if (node->is_boolean) {
        const char* value = node->boolean_value ? "true" : "false";
        if (serialize_append(buf, buf_size, offset, value, strlen(value)) != 0) return -1;
    } else if (node->is_null) {
        if (serialize_append(buf, buf_size, offset, "null", 4) != 0) return -1;
    } else {
        return -1;
    }
    return 0;
}
