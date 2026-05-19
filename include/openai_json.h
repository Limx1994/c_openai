/**
 * @file openai_json.h
 * @brief OpenAI JSON Interface
 */

#ifndef OPENAI_JSON_H
#define OPENAI_JSON_H

#include <stddef.h>

/* JSON node for DOM-style parsing */
struct OpenAI_JSONNode {
    char* key;
    char* string_value;
    double number_value;
    int is_number;
    int is_array;
    int is_object;
    void* children;  /* OpenAI_JSONNode* array */
    void* next;
    int child_count;
};
typedef struct OpenAI_JSONNode OpenAI_JSONNode;

/**
 * @brief Parse JSON string into DOM
 * @param json_string JSON string to parse
 * @return Root node of parsed JSON, or NULL on failure
 */
OpenAI_JSONNode* openai_json_parse(const char* json_string);

/**
 * @brief Free JSON DOM
 * @param node Root node from openai_json_parse()
 */
void openai_json_free(OpenAI_JSONNode* node);

/* Query functions */

/**
 * @brief Get string value from object
 * @param parent Parent object node
 * @param key Key name
 * @return String value, or NULL if not found
 */
const char* openai_json_get_string(OpenAI_JSONNode* parent, const char* key);

/**
 * @brief Get number value from object
 * @param parent Parent object node
 * @param key Key name
 * @return Number value, or 0 if not found
 */
double openai_json_get_number(OpenAI_JSONNode* parent, const char* key);

/**
 * @brief Get child object by key
 * @param parent Parent object node
 * @param key Key name
 * @return Child object node, or NULL if not found
 */
OpenAI_JSONNode* openai_json_get_object(OpenAI_JSONNode* parent, const char* key);

/**
 * @brief Get array item by index
 * @param parent Parent array node
 * @param index Item index (0-based)
 * @return Array item node, or NULL if not found
 */
OpenAI_JSONNode* openai_json_get_array_item(OpenAI_JSONNode* parent, size_t index);

/**
 * @brief Escape string for JSON
 * @param str Input string to escape
 * @return Escaped JSON string (caller must free), or NULL on failure
 */
char* openai_json_escape_string(const char* str);

#endif /* OPENAI_JSON_H */
