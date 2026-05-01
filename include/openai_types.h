#ifndef OPENAI_TYPES_H
#define OPENAI_TYPES_H

#include <stddef.h>

/* Message roles */
#define OPENAI_ROLE_SYSTEM "system"
#define OPENAI_ROLE_USER "user"
#define OPENAI_ROLE_ASSISTANT "assistant"
#define OPENAI_ROLE_FUNCTION "function"

/* Chat message */
typedef struct {
    char* role;
    char* content;
    char* name;
} OpenAI_Message;

/* Chat request */
typedef struct {
    char* model;
    OpenAI_Message* messages;
    size_t message_count;
    float temperature;
    int max_tokens;
    float top_p;
    int stream;
    char* stop;
} OpenAI_ChatRequest;

/* Chat response choice */
typedef struct {
    char* content;
    char* role;
    int index;
} OpenAI_Choice;

/* Chat response */
typedef struct {
    char* id;
    char* model;
    char* object;
    long created;
    OpenAI_Choice* choices;
    size_t choice_count;
    char* usage;
} OpenAI_ChatResponse;

/* Embeddings request */
typedef struct {
    char* model;
    char* input;
} OpenAI_EmbeddingRequest;

/* Embeddings response */
typedef struct {
    char* object;
    char* model;
    float* embedding;
    size_t embedding_dim;
} OpenAI_EmbeddingResponse;

/* Stream event types */
#define OPENAI_EVENT_CHUNK "chat.completion.chunk"
#define OPENAI_EVENT_DONE "done"

typedef struct {
    char* event_type;
    char* content;
    char* role;
    int index;
} OpenAI_StreamEvent;

/* Client handle - forward declaration */
struct OpenAI_Client;
typedef struct OpenAI_Client OpenAI_Client;

#endif /* OPENAI_TYPES_H */