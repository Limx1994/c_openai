# C-OpenAI

A cross-platform OpenAI API client written in C, designed for MCU (Microcontroller Unit) and POSIX-compliant systems.

## Features

- **Cross-platform**: Works on Windows, Linux, macOS, RTOS, and embedded systems
- **Dual HTTP backends**:
  - `libcurl`: Full-featured for PC and servers
  - `lwIP`: Lightweight for resource-constrained MCUs (STM32, ESP32, etc.)
- **Configurable memory**: Dynamic allocation or static buffer mode (compile-time option)
- **Core API support**:
  - Chat Completions
  - Streaming responses (SSE)
  - Embeddings

## Project Structure

```
c_openai/
├── include/              # Public headers
│   ├── openai.h          # Main API
│   ├── openai_config.h   # Compile-time configuration
│   ├── openai_types.h    # Type definitions
│   ├── openai_error.h    # Error codes
│   ├── openai_http.h     # HTTP interface
│   └── openai_json.h     # JSON interface
├── src/                  # Implementation
│   ├── openai_client.c   # Core client
│   ├── openai_http_curl.c    # libcurl backend
│   ├── openai_http_lwip.c     # lwIP backend
│   ├── openai_json.c     # JSON utilities
│   └── openai_error.c    # Error handling
├── cJSON/                # JSON parser (cJSON library)
├── example/              # Example code
│   └── chat_example.c
├── CMakeLists.txt        # Build configuration
├── README.md             # English documentation
├── README_zh.md          # Chinese documentation
└── CLAUDE.md             # Project specification
```

## Build

### Prerequisites

- CMake 3.10 or higher
- For libcurl backend: libcurl development files
- For lwIP backend: lwIP socket API

### Build with libcurl (default, for PC/Server)

```bash
mkdir build && cd build
cmake ..
make
```

### Build with lwIP (for embedded/MCU)

```bash
cmake .. -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP
make
```

### Build with static memory (no malloc, for bare-metal)

```bash
cmake .. -DOPENAI_USE_MALLOC=0
make
```

### Combined options (e.g., lwIP + static memory)

```bash
cmake .. -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP -DOPENAI_USE_MALLOC=0
make
```

## Configuration

Edit `include/openai_config.h` to customize:

```c
#define OPENAI_HTTP_BACKEND OPENAI_BACKEND_CURL  // or OPENAI_BACKEND_LWIP
#define OPENAI_USE_MALLOC 1    // 0 for static buffer mode
#define OPENAI_BUFFER_SIZE 4096
#define OPENAI_MAX_RETRIES 3
#define OPENAI_TIMEOUT 30
```

## Usage

```c
#include "openai.h"

// Create client with API key
OpenAI_Client* client = openai_client_new("sk-...");

// Prepare chat request
OpenAI_ChatRequest req = {0};
req.model = "gpt-3.5-turbo";
req.messages = malloc(sizeof(OpenAI_Message) * 2);
req.messages[0].role = "system";
req.messages[0].content = "You are a helpful assistant.";
req.messages[1].role = "user";
req.messages[1].content = "Hello!";
req.message_count = 2;
req.temperature = 0.7f;

// Send request and get response
OpenAI_ChatResponse* resp = openai_chat_create(client, &req);

if (resp && resp->choices[0].content) {
    printf("Response: %s\n", resp->choices[0].content);
}

// Cleanup
openai_chat_response_free(resp);
free(req.messages);
openai_client_free(client);
```

### Environment Variables

Set your API key via environment:

```bash
export OPENAI_API_KEY=sk-your-key-here
```

Or pass it directly in code:

```c
OpenAI_Client* client = openai_client_new(getenv("OPENAI_API_KEY"));
```

## API Reference

### Client Lifecycle

| Function | Description |
|----------|-------------|
| `openai_client_new(api_key)` | Create new client |
| `openai_client_free(client)` | Free client and resources |
| `openai_version()` | Get library version |

### Chat Completions

| Function | Description |
|----------|-------------|
| `openai_chat_create(client, req)` | Send chat request, returns response |
| `openai_chat_response_free(resp)` | Free response |

### Embeddings

| Function | Description |
|----------|-------------|
| `openai_embeddings_create(client, req)` | Get embeddings for text |
| `openai_embedding_response_free(resp)` | Free response |

### Streaming (planned)

Streaming API is defined but not fully implemented yet.

## Error Handling

```c
const char* err_str = openai_error_str(OPENAI_ERR_NETWORK);
// Returns: "Network error"
```

Error codes:
- `OPENAI_OK` - Success
- `OPENAI_ERR_INVALID_PARAM` - Invalid parameter
- `OPENAI_ERR_MEMORY` - Memory allocation failed
- `OPENAI_ERR_NETWORK` - Network error
- `OPENAI_ERR_TIMEOUT` - Request timeout
- `OPENAI_ERR_PARSE` - JSON parse error
- `OPENAI_ERR_API` - API returned error
- `OPENAI_ERR_AUTH` - Authentication failed
- `OPENAI_ERR_RATE_LIMIT` - Rate limit exceeded
- `OPENAI_ERR_SERVER` - Server error

## Platform-Specific Notes

### Linux/Raspberry Pi

Install libcurl development package:
```bash
sudo apt-get install libcurl4-openssl-dev
```

### Windows (MSYS2/MinGW)

```bash
pacman -S mingw-w64-x86_64-curl
```

### STM32/Embedded

1. Add lwIP to your project
2. Configure `OPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP`
3. Ensure `OPENAI_USE_MALLOC=0` for no-malloc mode
4. Port the cJSON library to your platform

## License

MIT License