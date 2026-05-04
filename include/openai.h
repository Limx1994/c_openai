/**
 * @file openai.h
 * @brief OpenAI API Client Main Header
 *
 * A cross-platform OpenAI API client implementation in C, supporting:
 * - Chat Completions
 * - Streaming responses (SSE)
 * - Embeddings
 *
 * @version 1.0.0
 */

#ifndef OPENAI_H
#define OPENAI_H

#include "openai_config.h"
#include "openai_error.h"
#include "openai_types.h"

/* Client lifecycle */

/**
 * @brief Create a new OpenAI client
 * @param api_key Your OpenAI API key (e.g., "sk-...")
 * @return Client handle, or NULL on failure
 */
OpenAI_Client* openai_client_new(const char* api_key);

/**
 * @brief Free client and all resources
 * @param client Client handle from openai_client_new()
 */
void openai_client_free(OpenAI_Client* client);

/* Chat Completions API */

/**
 * @brief Send a chat completion request
 * @param client Client handle
 * @param req Chat request parameters
 * @return Response handle, or NULL on failure
 */
OpenAI_ChatResponse* openai_chat_create(OpenAI_Client* client, OpenAI_ChatRequest* req);

/**
 * @brief Free chat response
 * @param resp Response handle from openai_chat_create()
 */
void openai_chat_response_free(OpenAI_ChatResponse* resp);

/* Streaming API */

/**
 * @brief Create a streaming chat request
 * @param client Client handle
 * @param req Chat request parameters (req->stream will be set automatically)
 * @return Stream handle, or NULL on failure
 */
void* openai_chat_create_stream(OpenAI_Client* client, OpenAI_ChatRequest* req);

/**
 * @brief Read next event from stream
 * @param stream Stream handle from openai_chat_create_stream()
 * @param event Output event structure (caller must free event->content)
 * @return 0 on success, OPENAI_ERR_EOF on stream end, error code on failure
 */
int openai_stream_read(void* stream, OpenAI_StreamEvent* event);

/**
 * @brief Close stream and free resources
 * @param stream Stream handle from openai_chat_create_stream()
 */
void openai_stream_close(void* stream);

/* Embeddings API */

/**
 * @brief Create embeddings for text input
 * @param client Client handle
 * @param req Embedding request parameters
 * @return Response handle with embedding vector, or NULL on failure
 */
OpenAI_EmbeddingResponse* openai_embeddings_create(OpenAI_Client* client, OpenAI_EmbeddingRequest* req);

/**
 * @brief Free embedding response
 * @param resp Response handle from openai_embeddings_create()
 */
void openai_embedding_response_free(OpenAI_EmbeddingResponse* resp);

/* Utility */

/**
 * @brief Get library version string
 * @return Version string (e.g., "1.0.0")
 */
const char* openai_version(void);

#endif /* OPENAI_H */
