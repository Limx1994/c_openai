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

/**
 * @brief Set custom API base URL
 * @param client Client handle
 * @param base_url Custom base URL (e.g., "https://my-proxy.com/v1")
 * @return 0 on success, -1 on failure
 *
 * If not called, defaults to OPENAI_API_BASE.
 * Pass NULL to reset to default.
 */
int openai_client_set_base_url(OpenAI_Client* client, const char* base_url);

/**
 * @brief Set API provider
 * @param client Client handle
 * @param provider Provider type (OPENAI_PROVIDER_OPENAI or OPENAI_PROVIDER_ANTHROPIC)
 * @return 0 on success, -1 on failure
 *
 * Must be called before any API requests. Default is OPENAI_PROVIDER_OPENAI.
 */
int openai_client_set_provider(OpenAI_Client* client, int provider);

/**
 * @brief Get last error code from client
 * @param client Client handle
 * @return Last error code (OpenAI_ErrorCode), or OPENAI_OK if no error
 *
 * Call this after a NULL return from openai_chat_create, openai_chat_create_stream,
 * or openai_embeddings_create to get the specific error type.
 */
int openai_client_get_last_error(OpenAI_Client* client);

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
 * @param event Output event structure (caller must free event->content and event->role)
 * @return 0 on success, OPENAI_ERR_EOF on stream end, error code on failure
 */
int openai_stream_read(void* stream, OpenAI_StreamEvent* event);

/**
 * @brief Free stream event fields (content and role)
 * @param event Event from openai_stream_read()
 */
void openai_stream_event_free(OpenAI_StreamEvent* event);

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

#endif /* OPENAI_H */
