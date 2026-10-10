#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openai.h"
#include "openai_error.h"
#include "openai_http.h"
#include "openai_json.h"

struct OpenAI_HTTPStream {
    const char* data;
    size_t size;
    size_t offset;
};

static const char* mock_response_body = "{}";
static const char* mock_stream_body = "";
static int mock_status_code = 200;
static int mock_request_failure = 0;
static int mock_stream_open_failure = 0;
static int mock_stream_read_failure = 0;
static OpenAI_HTTPError mock_transport_error = OPENAI_HTTP_ERROR_NONE;
static char* captured_request_body = NULL;

static char* duplicate_string(const char* value) {
    size_t length = strlen(value);
    char* copy = (char*)malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, value, length + 1);
    return copy;
}

static char* make_embedding_response(size_t dimension) {
    if (dimension == 0 || dimension > (SIZE_MAX - 256) / 8) return NULL;
    size_t capacity = dimension * 8 + 256;
    char* json = (char*)malloc(capacity);
    if (!json) return NULL;

    int written = snprintf(json, capacity,
        "{\"object\":\"list\",\"data\":[{\"object\":\"embedding\","
        "\"index\":0,\"embedding\":[");
    if (written < 0 || (size_t)written >= capacity) {
        free(json);
        return NULL;
    }
    size_t offset = (size_t)written;
    for (size_t i = 0; i < dimension; i++) {
        written = snprintf(json + offset, capacity - offset,
            i == 0 ? "%zu" : ",%zu", i);
        if (written < 0 || (size_t)written >= capacity - offset) {
            free(json);
            return NULL;
        }
        offset += (size_t)written;
    }
    written = snprintf(json + offset, capacity - offset,
        "]}],\"model\":\"embed-test\"}");
    if (written < 0 || (size_t)written >= capacity - offset) {
        free(json);
        return NULL;
    }
    return json;
}

static void reset_mock(void) {
    free(captured_request_body);
    captured_request_body = NULL;
    mock_response_body = "{}";
    mock_status_code = 200;
    mock_request_failure = 0;
    mock_stream_read_failure = 0;
    mock_transport_error = OPENAI_HTTP_ERROR_NONE;
}

int openai_http_init(void) {
    return 0;
}

void openai_http_cleanup(void) {
}

OpenAI_HTTPError openai_http_get_last_error(void) {
    return mock_transport_error;
}

OpenAI_HTTPResponse* openai_http_request(OpenAI_HTTPRequest* req) {
    free(captured_request_body);
    captured_request_body = req && req->body ? duplicate_string(req->body) : NULL;
    if (req && req->body && !captured_request_body) return NULL;
    if (mock_request_failure) return NULL;

    OpenAI_HTTPResponse* response = (OpenAI_HTTPResponse*)calloc(1, sizeof(*response));
    if (!response) return NULL;
    response->status_code = mock_status_code;
    response->body = duplicate_string(mock_response_body);
    if (!response->body) {
        free(response);
        return NULL;
    }
    response->body_size = strlen(response->body);
    return response;
}

OpenAI_HTTPStream* openai_http_stream_open(OpenAI_HTTPRequest* req, int* status_code) {
    free(captured_request_body);
    captured_request_body = req && req->body ? duplicate_string(req->body) : NULL;
    if (req && req->body && !captured_request_body) return NULL;
    if (mock_stream_open_failure) return NULL;

    OpenAI_HTTPStream* stream = (OpenAI_HTTPStream*)calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->data = mock_stream_body;
    stream->size = strlen(mock_stream_body);
    if (status_code) *status_code = mock_status_code;
    return stream;
}

int openai_http_stream_read(OpenAI_HTTPStream* stream, void* buffer, size_t size) {
    if (mock_stream_read_failure) return -1;
    if (!stream || !buffer) return -1;
    size_t remaining = stream->size - stream->offset;
    if (remaining == 0) return 0;
    if (size > remaining) size = remaining;
    memcpy(buffer, stream->data + stream->offset, size);
    stream->offset += size;
    return (int)size;
}

void openai_http_stream_close(OpenAI_HTTPStream* stream) {
    free(stream);
}

void openai_http_response_free(OpenAI_HTTPResponse* response) {
    if (!response) return;
    free(response->body);
    free(response);
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __func__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int test_json_round_trip(void) {
    const char* source =
        "{\"t\":true,\"f\":false,\"n\":null,"
        "\"bmp\":\"\\u4f60\\u597d\","
        "\"astral\":\"\\ud83d\\ude00\","
        "\"zero\":\"a\\u0000b\"}";
    OpenAI_JSONNode* root = openai_json_parse(source);
    CHECK(root != NULL);

    OpenAI_JSONNode* true_node = openai_json_get_object(root, "t");
    OpenAI_JSONNode* false_node = openai_json_get_object(root, "f");
    OpenAI_JSONNode* null_node = openai_json_get_object(root, "n");
    CHECK(true_node && true_node->is_boolean && true_node->boolean_value == 1);
    CHECK(false_node && false_node->is_boolean && false_node->boolean_value == 0);
    CHECK(null_node && null_node->is_null);
    CHECK(strcmp(openai_json_get_string(root, "bmp"), "\xe4\xbd\xa0\xe5\xa5\xbd") == 0);
    CHECK(strcmp(openai_json_get_string(root, "astral"), "\xf0\x9f\x98\x80") == 0);

    char* serialized = openai_json_serialize(root);
    CHECK(serialized != NULL);
    CHECK(strstr(serialized, "\"t\":true") != NULL);
    CHECK(strstr(serialized, "\"f\":false") != NULL);
    CHECK(strstr(serialized, "\"n\":null") != NULL);
    CHECK(strstr(serialized, "\"zero\":\"a\\u0000b\"") != NULL);

    OpenAI_JSONNode* reparsed = openai_json_parse(serialized);
    CHECK(reparsed != NULL);
    openai_json_free(reparsed);
    free(serialized);
    openai_json_free(root);

    CHECK(openai_json_parse("{\"x\":\"\\ud800\"}") == NULL);
    CHECK(openai_json_parse("{\"x\":\"\\udc00\"}") == NULL);
    CHECK(openai_json_parse("{\"x\":\"\\q\"}") == NULL);
    CHECK(openai_json_parse("{\"x\":true,}") == NULL);
    return 0;
}

static int test_openai_request_parameters(void) {
    reset_mock();
    mock_response_body =
        "{\"id\":\"c1\",\"object\":\"chat.completion\",\"choices\":["
        "{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"answer\"},"
        "\"finish_reason\":\"stop\"}]}";
    OpenAI_Client* client = openai_client_new("test-key");
    CHECK(client != NULL);
    OpenAI_Message messages[] = { { "user", "hello" }, { "assistant", "prior" } };
    OpenAI_ChatRequest request = {0};
    request.messages = messages;
    request.message_count = 1;

    OpenAI_ChatResponse* response = openai_chat_create(client, &request);
    CHECK(response != NULL);
    OpenAI_JSONNode* root = openai_json_parse(captured_request_body);
    CHECK(root && root->is_object);
    OpenAI_JSONNode* message_array = openai_json_get_object(root, "messages");
    CHECK(message_array && message_array->is_array && message_array->child_count == 1);
    CHECK(openai_json_get_object(root, "temperature") != NULL);
    CHECK(openai_json_get_number(root, "temperature") == 0);
    CHECK(openai_json_get_object(root, "stream") == NULL);
    openai_json_free(root);
    openai_chat_response_free(response);

    request.message_count = 2;
    request.temperature = 0.7f;
    request.max_tokens = 32;
    request.top_p = 0.8f;
    request.stop = "\"done\"\n";
    response = openai_chat_create(client, &request);
    CHECK(response != NULL);
    root = openai_json_parse(captured_request_body);
    CHECK(root != NULL);
    message_array = openai_json_get_object(root, "messages");
    CHECK(message_array && message_array->is_array && message_array->child_count == 2);
    CHECK(fabs(openai_json_get_number(root, "temperature") - 0.7) < 0.00001);
    CHECK(openai_json_get_number(root, "max_tokens") == 32);
    CHECK(fabs(openai_json_get_number(root, "top_p") - 0.8) < 0.00001);
    CHECK(strcmp(openai_json_get_string(root, "stop"), request.stop) == 0);
    openai_json_free(root);
    openai_chat_response_free(response);

    void* stream = openai_chat_create_stream(client, &request);
    CHECK(stream != NULL);
    CHECK(request.stream == 0);
    root = openai_json_parse(captured_request_body);
    CHECK(root != NULL);
    message_array = openai_json_get_object(root, "messages");
    CHECK(message_array && message_array->is_array && message_array->child_count == 2);
    CHECK(openai_json_get_number(root, "max_tokens") == 32);
    OpenAI_JSONNode* stream_node = openai_json_get_object(root, "stream");
    CHECK(stream_node && stream_node->is_boolean && stream_node->boolean_value);
    openai_json_free(root);
    openai_stream_close(stream);
    openai_client_free(client);
    reset_mock();
    return 0;
}

static OpenAI_Client* new_anthropic_client(void) {
    OpenAI_Client* client = openai_client_new("test-key");
    if (!client) return NULL;
    if (openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC) != 0) {
        openai_client_free(client);
        return NULL;
    }
    return client;
}

static int test_anthropic_request_and_response(void) {
    reset_mock();
    mock_response_body =
        "{\"id\":\"m1\",\"type\":\"message\",\"role\":\"assistant\","
        "\"model\":\"claude-test\",\"content\":["
        "{\"type\":\"text\",\"text\":\"answer\"},"
        "{\"type\":\"thinking\",\"thinking\":\"reason\"}],"
        "\"stop_reason\":\"end_turn\","
        "\"usage\":{\"input_tokens\":3,\"output_tokens\":2}}";

    OpenAI_Client* client = new_anthropic_client();
    CHECK(client != NULL);
    OpenAI_Message messages[] = {
        { "system", "be helpful" },
        { "user", "question" },
        { "assistant", "prior" }
    };
    OpenAI_ChatRequest request = {0};
    request.model = "claude-test";
    request.messages = messages;
    request.message_count = sizeof(messages) / sizeof(messages[0]);
    request.temperature = -1.0f;
    request.max_tokens = 32;

    OpenAI_ChatResponse* response = openai_chat_create(client, &request);
    CHECK(response != NULL);
    CHECK(response->choice_count == 1);
    CHECK(response->choices != NULL);
    CHECK(strcmp(response->choices[0].content, "answer") == 0);
    CHECK(strcmp(response->choices[0].thinking, "reason") == 0);

    CHECK(captured_request_body != NULL);
    OpenAI_JSONNode* request_json = openai_json_parse(captured_request_body);
    CHECK(request_json != NULL);
    CHECK(strcmp(openai_json_get_string(request_json, "system"), "be helpful") == 0);
    OpenAI_JSONNode* sent_messages = openai_json_get_object(request_json, "messages");
    CHECK(sent_messages && sent_messages->is_array && sent_messages->child_count == 2);
    OpenAI_JSONNode* first = openai_json_array_first(sent_messages);
    OpenAI_JSONNode* second = openai_json_array_next(first);
    CHECK(first && strcmp(openai_json_get_string(first, "content"), "question") == 0);
    CHECK(second && strcmp(openai_json_get_string(second, "content"), "prior") == 0);

    openai_json_free(request_json);
    openai_chat_response_free(response);
    openai_client_free(client);
    reset_mock();
    return 0;
}

static int test_anthropic_text_and_thinking_stream(void) {
    reset_mock();
    mock_stream_body =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"hello\"}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"reason\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"signature_delta\",\"signature\":\"sig\"}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n";

    OpenAI_Client* client = new_anthropic_client();
    CHECK(client != NULL);
    OpenAI_Message message = { "user", "hello" };
    OpenAI_ChatRequest request = {0};
    request.model = "claude-test";
    request.messages = &message;
    request.message_count = 1;
    request.temperature = -1.0f;
    request.max_tokens = 32;

    void* stream = openai_chat_create_stream(client, &request);
    CHECK(stream != NULL);
    CHECK(captured_request_body && strstr(captured_request_body, "\"messages\":[{\"role\":\"user\"") != NULL);

    OpenAI_StreamEvent event;
    CHECK(openai_stream_read(stream, &event) == OPENAI_OK);
    CHECK(event.event_type && strcmp(event.event_type, OPENAI_EVENT_CHUNK) == 0);
    CHECK(event.index == 0 && event.content && strcmp(event.content, "hello") == 0);
    CHECK(event.role && strcmp(event.role, "assistant") == 0);
    openai_stream_event_free(&event);

    CHECK(openai_stream_read(stream, &event) == OPENAI_OK);
    CHECK(event.event_type && strcmp(event.event_type, OPENAI_EVENT_CHUNK) == 0);
    CHECK(event.index == 1 && event.thinking && strcmp(event.thinking, "reason") == 0);
    openai_stream_event_free(&event);

    CHECK(openai_stream_read(stream, &event) == OPENAI_OK);
    CHECK(event.event_type && strcmp(event.event_type, OPENAI_EVENT_THINKING_SIG) == 0);
    CHECK(event.signature && strcmp(event.signature, "sig") == 0);
    openai_stream_event_free(&event);
    CHECK(openai_stream_read(stream, &event) == OPENAI_ERR_EOF);

    openai_stream_close(stream);
    openai_client_free(client);
    reset_mock();
    return 0;
}

static int test_anthropic_tool_stream(void) {
    reset_mock();
    mock_stream_body =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"tool_use\",\"id\":\"tool-1\",\"name\":\"lookup\",\"input\":{}}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"q\\\":1}\"}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n";

    OpenAI_Client* client = new_anthropic_client();
    CHECK(client != NULL);
    OpenAI_Message message = { "user", "use a tool" };
    OpenAI_ChatRequest request = {0};
    request.model = "claude-test";
    request.messages = &message;
    request.message_count = 1;
    request.temperature = -1.0f;
    request.max_tokens = 32;
    void* stream = openai_chat_create_stream(client, &request);
    CHECK(stream != NULL);

    OpenAI_StreamEvent event;
    CHECK(openai_stream_read(stream, &event) == OPENAI_OK);
    CHECK(event.event_type && strcmp(event.event_type, OPENAI_EVENT_TOOL_USE_START) == 0);
    CHECK(event.content && strstr(event.content, "lookup") != NULL);
    openai_stream_event_free(&event);

    CHECK(openai_stream_read(stream, &event) == OPENAI_OK);
    CHECK(event.event_type && strcmp(event.event_type, OPENAI_EVENT_TOOL_USE_DELTA) == 0);
    CHECK(event.content && strcmp(event.content, "{\"q\":1}") == 0);
    openai_stream_event_free(&event);

    CHECK(openai_stream_read(stream, &event) == OPENAI_OK);
    CHECK(event.event_type && strcmp(event.event_type, OPENAI_EVENT_TOOL_USE_STOP) == 0);
    openai_stream_event_free(&event);
    CHECK(openai_stream_read(stream, &event) == OPENAI_ERR_EOF);

    openai_stream_close(stream);
    openai_client_free(client);
    reset_mock();
    return 0;
}

static int test_embeddings_parser(void) {
    reset_mock();
    OpenAI_Client* client = openai_client_new("test-key");
    CHECK(client != NULL);
    OpenAI_EmbeddingRequest request = { "embed-test", "input" };

    mock_response_body =
        "{\"object\":\"list\",\"data\":[{\"object\":\"embedding\",\"index\":0,"
        "\"embedding\":[0.25,-1.5,2e-2]}],\"model\":\"embed-test\","
        "\"usage\":{\"prompt_tokens\":1}}";
    OpenAI_EmbeddingResponse* response = openai_embeddings_create(client, &request);
    CHECK(response != NULL);
    CHECK(response->embedding_dim == 3);
    CHECK(fabsf(response->embedding[0] - 0.25f) < 0.00001f);
    CHECK(fabsf(response->embedding[1] + 1.5f) < 0.00001f);
    CHECK(fabsf(response->embedding[2] - 0.02f) < 0.00001f);
    CHECK(strcmp(response->object, "list") == 0);
    CHECK(strcmp(response->model, "embed-test") == 0);
    openai_embedding_response_free(response);

    char* large_response = make_embedding_response(1536);
    CHECK(large_response != NULL);
    mock_response_body = large_response;
    response = openai_embeddings_create(client, &request);
    CHECK(response != NULL);
    CHECK(response->embedding_dim == 1536);
    CHECK(response->embedding[0] == 0.0f);
    CHECK(response->embedding[1535] == 1535.0f);
    openai_embedding_response_free(response);
    free(large_response);

    const char* invalid_responses[] = {
        "{\"data\":[{\"embedding\":[]}]}",
        "{\"data\":[{\"embedding\":[1,\"bad\"]}]}",
        "{\"data\":[{\"embedding\":[1,]}]}",
        "{\"data\":[{\"embedding\":[1,2]}]",
        "{\"data\":[]}"
    };
    for (size_t i = 0; i < sizeof(invalid_responses) / sizeof(invalid_responses[0]); i++) {
        mock_response_body = invalid_responses[i];
        response = openai_embeddings_create(client, &request);
        CHECK(response == NULL);
        CHECK(openai_client_get_last_error(client) == OPENAI_ERR_PARSE);
    }

    openai_client_free(client);
    reset_mock();
    return 0;
}

static int test_timeout_mapping(void) {
    reset_mock();
    mock_transport_error = OPENAI_HTTP_ERROR_TIMEOUT;
    OpenAI_Client* client = openai_client_new("test-key");
    CHECK(client != NULL);

    OpenAI_Message message = { "user", "hello" };
    OpenAI_ChatRequest chat = {0};
    chat.messages = &message;
    chat.message_count = 1;
    chat.temperature = -1.0f;

    mock_request_failure = 1;
    CHECK(openai_chat_create(client, &chat) == NULL);
    CHECK(openai_client_get_last_error(client) == OPENAI_ERR_TIMEOUT);

    OpenAI_EmbeddingRequest embedding = { "embed-test", "input" };
    CHECK(openai_embeddings_create(client, &embedding) == NULL);
    CHECK(openai_client_get_last_error(client) == OPENAI_ERR_TIMEOUT);
    mock_request_failure = 0;

    mock_stream_open_failure = 1;
    CHECK(openai_chat_create_stream(client, &chat) == NULL);
    CHECK(openai_client_get_last_error(client) == OPENAI_ERR_TIMEOUT);
    mock_stream_open_failure = 0;

    mock_stream_body = "data: {}\n\n";
    void* stream = openai_chat_create_stream(client, &chat);
    CHECK(stream != NULL);
    mock_stream_read_failure = 1;
    OpenAI_StreamEvent event;
    CHECK(openai_stream_read(stream, &event) == OPENAI_ERR_TIMEOUT);
    openai_stream_close(stream);

    mock_stream_read_failure = 0;
    mock_request_failure = 1;
    mock_transport_error = OPENAI_HTTP_ERROR_NETWORK;
    CHECK(openai_chat_create(client, &chat) == NULL);
    CHECK(openai_client_get_last_error(client) == OPENAI_ERR_NETWORK);

    openai_client_free(client);
    reset_mock();
    return 0;
}

int main(void) {
    int failures = 0;
    failures += test_json_round_trip();
    failures += test_openai_request_parameters();
    failures += test_anthropic_request_and_response();
    failures += test_anthropic_text_and_thinking_stream();
    failures += test_anthropic_tool_stream();
    failures += test_embeddings_parser();
    failures += test_timeout_mapping();
    reset_mock();

    if (failures != 0) {
        fprintf(stderr, "core_api_tests: %d test group(s) failed\n", failures);
        return 1;
    }
    puts("core_api_tests: all checks passed");
    return 0;
}
