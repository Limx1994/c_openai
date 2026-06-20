#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openai.h"

static void example_anthropic_chat(OpenAI_Client* client) {
    printf("\n=== Anthropic Chat Example ===\n");

    OpenAI_ChatRequest req = {0};
    req.model = "claude-3-opus-20240229";
    req.messages = (OpenAI_Message*)malloc(sizeof(OpenAI_Message) * 2);
    if (!req.messages) return;

    req.messages[0].role = "system";
    req.messages[0].content = "You are a helpful assistant.";
    req.messages[1].role = "user";
    req.messages[1].content = "Hello, how are you?";
    req.message_count = 2;
    req.temperature = 0.7f;
    req.max_tokens = 1024;

    OpenAI_ChatResponse* resp = openai_chat_create(client, &req);
    free(req.messages);

    if (resp) {
        printf("ID: %s\n", resp->id ? resp->id : "N/A");
        printf("Model: %s\n", resp->model ? resp->model : "N/A");
        for (size_t i = 0; i < resp->choice_count; i++) {
            printf("Choice %zu: %s\n", i,
                resp->choices[i].content ? resp->choices[i].content : "N/A");
        }
        if (resp->usage) printf("Usage: %s\n", resp->usage);
        openai_chat_response_free(resp);
    }
}

static void example_anthropic_stream(OpenAI_Client* client) {
    printf("\n=== Anthropic Streaming Example ===\n");

    OpenAI_ChatRequest req = {0};
    req.model = "claude-3-opus-20240229";
    req.messages = (OpenAI_Message*)malloc(sizeof(OpenAI_Message) * 1);
    if (!req.messages) return;

    req.messages[0].role = "user";
    req.messages[0].content = "Write a short poem about the ocean:";
    req.message_count = 1;
    req.max_tokens = 512;

    void* stream = openai_chat_create_stream(client, &req);
    free(req.messages);

    if (!stream) {
        printf("Stream creation failed\n");
        return;
    }

    printf("Response: ");
    fflush(stdout);
    OpenAI_StreamEvent event;
    while (openai_stream_read(stream, &event) == 0) {
        if (event.content) {
            printf("%s", event.content);
            fflush(stdout);
        }
        openai_stream_event_free(&event);
    }
    printf("\n");
    openai_stream_close(stream);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const char* api_key = getenv("ANTHROPIC_API_KEY");
    if (!api_key) {
        fprintf(stderr, "Please set ANTHROPIC_API_KEY environment variable\n");
        return 1;
    }

    OpenAI_Client* client = openai_client_new(api_key);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC);

    example_anthropic_chat(client);
    example_anthropic_stream(client);

    openai_client_free(client);
    return 0;
}
