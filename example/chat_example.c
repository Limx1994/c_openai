#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openai.h"

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

    printf("Creating chat request...\n");
    OpenAI_ChatRequest req = {0};
    req.model = "gpt-3.5-turbo";
    req.messages = (OpenAI_Message*)malloc(sizeof(OpenAI_Message) * 1);
    req.messages[0].role = "user";
    req.messages[0].content = "Hello, how are you?";
    req.message_count = 1;
    req.temperature = 0.7f;

    printf("Sending chat request...\n");
    OpenAI_ChatResponse* resp = openai_chat_create(client, &req);

    if (resp) {
        printf("\n=== Response ===\n");
        printf("ID: %s\n", resp->id ? resp->id : "N/A");
        printf("Model: %s\n", resp->model ? resp->model : "N/A");

        for (size_t i = 0; i < resp->choice_count; i++) {
            printf("\nChoice %zu:\n", i);
            printf("  Role: %s\n", resp->choices[i].role ? resp->choices[i].role : "N/A");
            printf("  Content: %s\n", resp->choices[i].content ? resp->choices[i].content : "N/A");
        }

        openai_chat_response_free(resp);
    } else {
        printf("Failed to get response\n");
    }

    free(req.messages);
    openai_client_free(client);

    printf("\nDone!\n");
    return 0;
}