#ifndef OPENAI_JSON_H
#define OPENAI_JSON_H

#include <stddef.h>

/* JSON node for DOM-style parsing */
typedef struct OpenAI_JSONNode OpenAI_JSONNode;

OpenAI_JSONNode* openai_json_parse(const char* json_string);
void openai_json_free(OpenAI_JSONNode* node);
char* openai_json_dump(OpenAI_JSONNode* node);

/* Query functions */
const char* openai_json_get_string(OpenAI_JSONNode* parent, const char* key);
double openai_json_get_number(OpenAI_JSONNode* parent, const char* key);
OpenAI_JSONNode* openai_json_get_object(OpenAI_JSONNode* parent, const char* key);
OpenAI_JSONNode* openai_json_get_array_item(OpenAI_JSONNode* parent, size_t index);
size_t openai_json_get_array_size(OpenAI_JSONNode* parent);

#endif /* OPENAI_JSON_H */