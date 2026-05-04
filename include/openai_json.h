/**
 * @file openai_json.h
 * @brief OpenAI JSON Interface
 */

#ifndef OPENAI_JSON_H
#define OPENAI_JSON_H

#include <stddef.h>

/* JSON node for DOM-style parsing */
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

/**
 * @brief Convert JSON DOM back to string
 * @param node Root node from openai_json_parse()
 * @return JSON string (caller must free), or NULL on failure
 */
char* openai_json_dump(OpenAI_JSONNode* node);

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
 * @brief Get array size
 * @param parent Parent array node
 * @return Number of items in array
 */
size_t openai_json_get_array_size(OpenAI_JSONNode* parent);

#endif /* OPENAI_JSON_H */
