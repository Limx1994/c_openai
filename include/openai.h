#ifndef OPENAI_H
#define OPENAI_H

#include "openai_config.h"
#include "openai_error.h"
#include "openai_types.h"

/* Client lifecycle */
OpenAI_Client* openai_client_new(const char* api_key);
void openai_client_free(OpenAI_Client* client);

/* Chat Completions API */
OpenAI_ChatResponse* openai_chat_create(OpenAI_Client* client, OpenAI_ChatRequest* req);
void openai_chat_response_free(OpenAI_ChatResponse* resp);

/* Streaming API */
void* openai_chat_create_stream(OpenAI_Client* client, OpenAI_ChatRequest* req);
int openai_stream_read(void* stream, OpenAI_StreamEvent* event);
void openai_stream_close(void* stream);

/* Embeddings API */
OpenAI_EmbeddingResponse* openai_embeddings_create(OpenAI_Client* client, OpenAI_EmbeddingRequest* req);
void openai_embedding_response_free(OpenAI_EmbeddingResponse* resp);

/* Utility */
const char* openai_version(void);

#endif /* OPENAI_H */