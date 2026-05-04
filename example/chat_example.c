#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openai.h"

static void example_chat(OpenAI_Client* client) {
    printf("\n=== Chat Example ===\n");

    OpenAI_ChatRequest req = {0};
    req.model = "gpt-3.5-turbo";
    req.messages = (OpenAI_Message*)malloc(sizeof(OpenAI_Message) * 1);
    if (!req.messages) {
        fprintf(stderr, "Failed to allocate messages\n");
        return;
    }
    req.messages[0].role = "user";
    req.messages[0].content = "Hello, how are you?";
    req.message_count = 1;
    req.temperature = 0.7f;

    OpenAI_ChatResponse* resp = openai_chat_create(client, &req);
    free(req.messages);

    if (resp) {
        printf("ID: %s\n", resp->id ? resp->id : "N/A");
        printf("Model: %s\n", resp->model ? resp->model : "N/A");

        for (size_t i = 0; i < resp->choice_count; i++) {
            printf("Choice %zu: %s\n", i,
                resp->choices[i].content ? resp->choices[i].content : "N/A");
        }

        if (resp->usage) {
            printf("Usage: %s\n", resp->usage);
        }

        openai_chat_response_free(resp);
    } else {
        printf("Chat request failed\n");
    }
}

static void example_streaming(OpenAI_Client* client) {
    printf("\n=== Streaming Example ===\n");

    OpenAI_ChatRequest req = {0};
    req.model = "gpt-3.5-turbo";
    req.messages = (OpenAI_Message*)malloc(sizeof(OpenAI_Message) * 1);
    if (!req.messages) {
        fprintf(stderr, "Failed to allocate messages\n");
        return;
    }
    req.messages[0].role = "user";
    req.messages[0].content = "Write a short story about a robot:";
    req.message_count = 1;

    printf("Requesting streaming response...\n");

    void* stream = openai_chat_create_stream(client, &req);
    free(req.messages);

    if (!stream) {
        printf("Failed to create stream\n");
        return;
    }

    printf("Response: ");
    fflush(stdout);

    OpenAI_StreamEvent event;
    int chunk_count = 0;

    while (openai_stream_read(stream, &event) == 0) {
        if (event.content) {
            printf("%s", event.content);
            fflush(stdout);
            free(event.content);
            chunk_count++;
        }
    }

    printf("\nReceived %d chunks\n", chunk_count);
    openai_stream_close(stream);
}

static void example_embeddings(OpenAI_Client* client) {
    printf("\n=== Embeddings Example ===\n");

    OpenAI_EmbeddingRequest req = {0};
    req.model = "text-embedding-3-small";
    req.input = "Hello world";

    printf("Creating embedding for: \"%s\"\n", req.input);

    OpenAI_EmbeddingResponse* resp = openai_embeddings_create(client, &req);

    if (resp) {
        printf("Embedding dimension: %zu\n", resp->embedding_dim);
        if (resp->embedding_dim > 0) {
            printf("First 5 values: ");
            for (size_t i = 0; i < resp->embedding_dim && i < 5; i++) {
                printf("%.4f ", resp->embedding[i]);
            }
            printf("\n");
        }
        openai_embedding_response_free(resp);
    } else {
        printf("Embedding request failed\n");
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const char* api_key = getenv("OPENAI_API_KEY");
    if (!api_key) {
        fprintf(stderr, "Please set OPENAI_API_KEY environment variable\n");
        return 1;
    }

    printf("Initializing OpenAI client...\n");
    OpenAI_Client* client = openai_client_new(api_key);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    example_chat(client);
    example_streaming(client);
    example_embeddings(client);

    openai_client_free(client);
    printf("\nDone!\n");
    return 0;
}