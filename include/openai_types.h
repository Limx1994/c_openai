/**
 * @file openai_types.h
 * @brief OpenAI API Type Definitions
 */

#ifndef OPENAI_TYPES_H
#define OPENAI_TYPES_H

#include <stddef.h>

/* Message roles */
#define OPENAI_ROLE_SYSTEM "system"
#define OPENAI_ROLE_USER "user"
#define OPENAI_ROLE_ASSISTANT "assistant"
#define OPENAI_ROLE_FUNCTION "function"

/**
 * @brief Chat message structure
 */
typedef struct {
    char* role;      /**< Message role (system/user/assistant) */
    char* content;    /**< Message content */
    char* name;      /**< Optional speaker name */
} OpenAI_Message;

/**
 * @brief Chat completion request structure
 */
typedef struct {
    char* model;          /**< Model name (e.g., "gpt-3.5-turbo") */
    OpenAI_Message* messages; /**< Array of messages */
    size_t message_count;   /**< Number of messages */
    float temperature;     /**< Sampling temperature (0.0-2.0) */
    int max_tokens;        /**< Maximum tokens in response (0=default) */
    float top_p;           /**< Nucleus sampling parameter */
    int stream;            /**< Stream flag (set automatically for streaming) */
    char* stop;            /**< Stop sequences */
} OpenAI_ChatRequest;

/**
 * @brief Chat response choice
 */
typedef struct {
    char* content;    /**< Response content */
    char* role;       /**< Message role */
    int index;        /**< Choice index */
} OpenAI_Choice;

/**
 * @brief Chat completion response
 */
typedef struct {
    char* id;              /**< Response ID */
    char* model;            /**< Model used */
    char* object;           /**< Response object type */
    long created;           /**< Timestamp */
    OpenAI_Choice* choices; /**< Array of choices */
    size_t choice_count;     /**< Number of choices */
    char* usage;            /**< Usage information string */
} OpenAI_ChatResponse;

/**
 * @brief Embeddings request structure
 */
typedef struct {
    char* model;   /**< Embedding model (e.g., "text-embedding-3-small") */
    char* input;    /**< Input text to embed */
} OpenAI_EmbeddingRequest;

/**
 * @brief Embeddings response structure
 */
typedef struct {
    char* object;           /**< Object type */
    char* model;            /**< Model used */
    float* embedding;        /**< Embedding vector */
    size_t embedding_dim;    /**< Embedding vector dimension */
} OpenAI_EmbeddingResponse;

/* Stream event types */
#define OPENAI_EVENT_CHUNK "chat.completion.chunk"
#define OPENAI_EVENT_DONE "done"

/**
 * @brief Stream event structure
 */
typedef struct {
    char* event_type;   /**< Event type (e.g., "chat.completion.chunk") */
    char* content;      /**< Delta content (caller must free) */
    char* role;         /**< Optional role */
    int index;          /**< Choice index */
} OpenAI_StreamEvent;

/* Client handle - forward declaration */
struct OpenAI_Client;
typedef struct OpenAI_Client OpenAI_Client;

#endif /* OPENAI_TYPES_H */
