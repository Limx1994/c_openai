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
 * @brief Get first item in array (for iterator pattern)
 * @param parent Parent array node
 * @return First array item node, or NULL if empty
 */
OpenAI_JSONNode* openai_json_array_first(OpenAI_JSONNode* parent);

/**
 * @brief Get next item in array (for iterator pattern)
 * @param current Current array item node
 * @return Next array item node, or NULL if at end
 */
OpenAI_JSONNode* openai_json_array_next(OpenAI_JSONNode* current);

/**
 * @brief Escape string for JSON
 * @param str Input string to escape
 * @return Escaped JSON string (caller must free), or NULL on failure
 */
char* openai_json_escape_string(const char* str);

/**
 * @brief Serialize a JSON node tree back to a JSON string
 * @param node Root node to serialize
 * @return JSON string (caller must free), or NULL on failure
 *
 * Recursively serializes the node and all its children into a valid JSON string.
 * Useful for serializing sub-trees (e.g., tool_use input objects).
 */
char* openai_json_serialize(OpenAI_JSONNode* node);

#endif /* OPENAI_JSON_H */
