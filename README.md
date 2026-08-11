# C-OpenAI

A cross-platform OpenAI/Anthropic API client written in C, designed for MCU (Microcontroller Unit) and POSIX-compliant systems.

## Features

- **Cross-platform**: Works on Windows, Linux, macOS, RTOS, and embedded systems
- **Dual HTTP backends**:
  - `libcurl`: Full-featured for PC and servers
  - `lwIP`: Lightweight for resource-constrained MCUs (STM32, ESP32, etc.)
- **Multi-provider support**:
  - OpenAI API (Chat Completions, Streaming, Embeddings)
  - Anthropic Messages API (Chat, Streaming)
- **Dynamic memory**: Uses heap allocation with explicit ownership and free APIs
- **Security**: JSON injection prevention, memory safety checks, secure auth header handling

## Project Structure

```text
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
│   ├── openai_http_lwip.c    # lwIP backend
│   └── openai_json.c     # JSON parser (custom implementation)
├── third_party/          # Third-party libraries (git submodules)
│   ├── libcurl/          # libcurl HTTP library
│   ├── lwip/             # lwIP TCP/IP stack
│   ├── mbedtls/          # mbedTLS encryption library
│   ├── lwip_port/        # Minimal lwIP platform headers (for build check)
│   └── CMakeLists.txt    # Third-party build config
├── example/              # Example code
│   ├── chat_example.c    # OpenAI Chat Completions
│   └── anthropic_example.c  # Anthropic Messages API
├── build.py              # One-click build script (lwIP/curl backends)
├── build.sh              # libcurl build script (MinGW/MSYS2)
├── build_lwip.sh         # lwIP build script (ARM GCC)
├── CMakeLists.txt        # CMake build configuration
├── README.md             # English documentation
├── README_zh.md          # Chinese documentation
├── CHANGELOG.md          # Release history
└── CLAUDE.md             # Project specification
```

## Build

### Prerequisites

- CMake 3.18 or higher
- Git (with submodule support)
- All dependencies included as git submodules (libcurl, lwIP, mbedtls)

### One-Click Build (Recommended)

```bash
# Build lwIP backend (auto-detects ARM toolchain from STM32CubeIDE)
python build.py --backend lwip

# Build all backends
python build.py

# Clean build directory
python build.py --clean

# Verbose mode (show compile commands)
python build.py -v

# Custom options
python build.py --backend lwip --timeout 60 --log-level 3 --no-tls
```

### Build with libcurl (for PC/Server)

```bash
cmake -S . -B build
cmake --build build --parallel 10
```

Or use the build script (MinGW/MSYS2):

```bash
bash build.sh
```

### lwIP Backend (for embedded/MCU)

The lwIP backend supports both HTTP and HTTPS:

- **HTTP**: Plain TCP socket, no TLS overhead
- **HTTPS**: Uses ALTCP + mbedTLS for encrypted connections

**Important**: lwIP and mbedTLS are included as git submodules. Clone with:

```bash
git clone --recursive https://github.com/Limx1994/c_openai.git
# Or if already cloned:
git submodule update --init --recursive
```

For HTTPS with lwIP, ensure these are enabled in your `lwipopts.h`:

```c
#define LWIP_ALTCP             1
#define LWIP_ALTCP_TLS         1
#define LWIP_ALTCP_TLS_MBEDTLS 1
```

Then configure in `openai_config.h`:

```c
#define OPENAI_USE_TLS 1          // Enable TLS
```

### Build with lwIP (for embedded/MCU)

Using the build script (recommended, requires ARM GCC from STM32CubeIDE):

```bash
bash build_lwip.sh
```

This script builds only `libopenai_lwip.a`. Your target project must provide
the real RTOS/lwIP/mbedTLS port and network driver; the script reports the
final firmware link as unverified unless platform link inputs are supplied.

Or with CMake:

```bash
cmake -S . -B build -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP
cmake --build build --parallel 10
```

**Note**: For ARM Cortex-M targets, `MBEDTLS_HAVE_ASM` must be disabled in `third_party/mbedtls/tf-psa-crypto/include/psa/crypto_config.h` due to register constraints.

### Build options

```bash
# libcurl backend (default)
cmake -S . -B build
cmake --build build --parallel 10

# lwIP backend
cmake -S . -B build -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP
cmake --build build --parallel 10

# build both backend libraries
cmake -S . -B build -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_BOTH
cmake --build build --parallel 10

# build examples with the bundled libcurl
cmake -S . -B build -DOPENAI_BUILD_EXAMPLES=ON
cmake --build build --parallel 10
```

## Configuration

Configure CMake and edit `include/openai_config.h` to customize:

```c
#define OPENAI_TIMEOUT 30          // HTTP timeout in seconds
#define OPENAI_USE_TLS 1           // for lwIP HTTPS (requires mbedTLS)
```

`OPENAI_HTTP_BACKEND` is a CMake option. Supported values are
`OPENAI_BACKEND_CURL` (default), `OPENAI_BACKEND_BOTH`, and
`OPENAI_BACKEND_LWIP`.

Logging is controlled by compile-time macros:

```c
#define OPENAI_LOG_ENABLED 1       // 0=disable all logging (recommended for embedded)
#define OPENAI_LOG_LEVEL 3         // 0=error, 1=warn, 2=info, 3=debug
```

## Usage

```c
#include "openai.h"

// Create client with API key
OpenAI_Client* client = openai_client_new("sk-...");

// Prepare chat request
OpenAI_ChatRequest req = {0};
req.model = "gpt-4o-mini";
req.messages = malloc(sizeof(OpenAI_Message) * 2);
req.messages[0].role = "system";
req.messages[0].content = "You are a helpful assistant.";
req.messages[1].role = "user";
req.messages[1].content = "Hello!";
req.message_count = 2;
req.temperature = 0.7f;  // 0.0-2.0, -1.0 for API default

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

### Anthropic Usage

```c
#include "openai.h"

// Create client with Anthropic API key
OpenAI_Client* client = openai_client_new("sk-ant-...");
openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC);
// base_url automatically set to https://api.anthropic.com/v1

// Same API as OpenAI - system prompt is extracted automatically
OpenAI_ChatRequest req = {0};
req.model = "claude-sonnet-4-20250514";
req.messages = malloc(sizeof(OpenAI_Message) * 2);
req.messages[0].role = "system";
req.messages[0].content = "You are a helpful assistant.";
req.messages[1].role = "user";
req.messages[1].content = "Hello!";
req.message_count = 2;
req.temperature = 0.7f;  // 0.0-2.0, -1.0 for API default
req.max_tokens = 1024;  // Required for Anthropic

OpenAI_ChatResponse* resp = openai_chat_create(client, &req);
// ... same response handling as OpenAI ...

openai_chat_response_free(resp);
free(req.messages);
openai_client_free(client);
```

### Anthropic Extended Thinking

```c
#include "openai.h"

OpenAI_Client* client = openai_client_new("sk-ant-...");
openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC);

OpenAI_ChatRequest req = {0};
req.model = "claude-sonnet-4-20250514";
req.messages = malloc(sizeof(OpenAI_Message) * 1);
req.messages[0].role = "user";
req.messages[0].content = "Solve this complex math problem...";
req.message_count = 1;
req.max_tokens = 16000;
req.thinking_enabled = 1;     // Enable Extended Thinking
req.thinking_budget = 10000;  // Token budget for thinking

OpenAI_ChatResponse* resp = openai_chat_create(client, &req);
if (resp && resp->choice_count > 0) {
    // Access thinking content
    if (resp->choices[0].thinking) {
        printf("Thinking: %s\n", resp->choices[0].thinking);
    }
    // Access final answer
    if (resp->choices[0].content) {
        printf("Answer: %s\n", resp->choices[0].content);
    }
}
openai_chat_response_free(resp);
free(req.messages);
openai_client_free(client);
```

### Environment Variables

Set your API key via environment:

```bash
export OPENAI_API_KEY=sk-your-key-here
export ANTHROPIC_API_KEY=sk-ant-your-key-here
```

Or pass it directly in code:

```c
// OpenAI
OpenAI_Client* client = openai_client_new(getenv("OPENAI_API_KEY"));

// Anthropic
OpenAI_Client* client = openai_client_new(getenv("ANTHROPIC_API_KEY"));
openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC);
```

### Custom API Base URL

For Azure OpenAI, proxy servers, or custom endpoints:

```c
OpenAI_Client* client = openai_client_new("sk-your-key-here");
openai_client_set_base_url(client, "https://your-proxy.com/v1");
// All subsequent API calls will use this base URL
```

## API Reference

### Client Lifecycle

| Function                                       | Description                                                            |
| ---------------------------------------------- | ---------------------------------------------------------------------- |
| `openai_client_new(api_key)`                   | Create new client                                                      |
| `openai_client_free(client)`                   | Free client and resources                                              |
| `openai_client_set_base_url(client, url)`      | Set custom API base URL (e.g., for Azure, proxies)                     |
| `openai_client_set_provider(client, provider)` | Set API provider (OPENAI_PROVIDER_OPENAI or OPENAI_PROVIDER_ANTHROPIC) |
| `openai_client_get_last_error(client)`         | Get last error code (use after NULL return to determine error type)    |

### Chat Completions

| Function                          | Description                         |
| --------------------------------- | ----------------------------------- |
| `openai_chat_create(client, req)` | Send chat request, returns response |
| `openai_chat_response_free(resp)` | Free response                       |

### Embeddings

| Function                                | Description             |
| --------------------------------------- | ----------------------- |
| `openai_embeddings_create(client, req)` | Get embeddings for text |
| `openai_embedding_response_free(resp)`  | Free response           |

### Streaming

| Function                                 | Description                                |
| ---------------------------------------- | ------------------------------------------ |
| `openai_chat_create_stream(client, req)` | Create streaming chat request              |
| `openai_stream_read(stream, event)`      | Read next event from stream                |
| `openai_stream_event_free(event)`        | Free event content/role/stop_reason/thinking/signature/usage fields |
| `openai_stream_close(stream)`            | Close stream and free resources            |

The `event_type` field distinguishes event categories during streaming:

| Constant                           | Description                                      |
| ---------------------------------- | ------------------------------------------------ |
| `OPENAI_EVENT_CHUNK`              | Text delta (both OpenAI and Anthropic)           |
| `OPENAI_EVENT_TOOL_CALL_DELTA`    | OpenAI `tool_calls` delta                        |
| `OPENAI_EVENT_TOOL_USE_START`     | Anthropic `tool_use` block start                 |
| `OPENAI_EVENT_TOOL_USE_DELTA`     | Anthropic `tool_use` delta                       |
| `OPENAI_EVENT_TOOL_USE_STOP`      | Anthropic `tool_use` block end                   |
| `OPENAI_EVENT_THINKING_SIG`       | Anthropic Extended Thinking signature delta      |

Check `event->event_type` to switch between text deltas, OpenAI tool_call deltas, and Anthropic tool_use events. `content` holds text or `partial_json` for tool_use blocks.

Streaming responses are delivered incrementally. `openai_chat_create_stream()`
returns after the response headers arrive, and `openai_stream_read()` waits for
and parses the next SSE event. The transport applies `OPENAI_TIMEOUT` as an idle
timeout. A single incomplete SSE line is limited to 64 KiB.

### JSON Utilities

| Function                              | Description                                 |
| ------------------------------------- | ------------------------------------------- |
| `openai_json_parse(json_string)`      | Parse JSON string into DOM                  |
| `openai_json_free(node)`              | Free JSON DOM                               |
| `openai_json_escape_string(str)`      | Escape string for JSON (prevents injection) |
| `openai_json_serialize(node)`         | Serialize JSON node tree to string (caller free) |
| `openai_json_get_string(parent, key)` | Get string value from object                |
| `openai_json_get_number(parent, key)` | Get number value from object                |
| `openai_json_get_object(parent, key)` | Get child object by key                     |
| `openai_json_array_first(parent)`     | Get first array item (for iterator pattern) |
| `openai_json_array_next(current)`     | Get next array item (for iterator pattern)  |

## Error Handling

Error codes:

- `OPENAI_OK` - Success
- `OPENAI_ERR_INVALID_PARAM` - Invalid parameter
- `OPENAI_ERR_MEMORY` - Memory allocation failed
- `OPENAI_ERR_NETWORK` - Network error
- `OPENAI_ERR_TIMEOUT` - Request timeout
- `OPENAI_ERR_PARSE` - JSON parse error
- `OPENAI_ERR_API` - API returned error
- `OPENAI_ERR_AUTH` - Authentication failed (HTTP 401)
- `OPENAI_ERR_RATE_LIMIT` - Rate limit exceeded (HTTP 429)
- `OPENAI_ERR_SERVER` - Server error (HTTP 5xx)
- `OPENAI_ERR_EOF` - End of stream (streaming)
- `OPENAI_ERR_UNKNOWN` - Unknown error

### Error Query

When `openai_chat_create`, `openai_chat_create_stream`, or `openai_embeddings_create` returns NULL, use `openai_client_get_last_error()` to get the specific error type:

```c
OpenAI_ChatResponse* resp = openai_chat_create(client, &req);
if (!resp) {
    int err = openai_client_get_last_error(client);
    switch (err) {
        case OPENAI_ERR_AUTH:
            printf("Authentication failed, check API key\n");
            break;
        case OPENAI_ERR_RATE_LIMIT:
            printf("Rate limit exceeded, retry later\n");
            break;
        case OPENAI_ERR_SERVER:
            printf("Server error, retry later\n");
            break;
        case OPENAI_ERR_NETWORK:
            printf("Network error, check connection\n");
            break;
        default:
            printf("Unknown error: %d\n", err);
    }
}
```

## Security Features

This library includes several security enhancements:

1. **JSON Injection Prevention**: The `openai_json_escape_string()` function escapes special characters (`"`, `\`, `\n`, `\r`, `\t`) to prevent JSON injection attacks when embedding user content. This applies to model names, roles, and message content in Chat Completions and Embeddings requests.

2. **Memory Safety**: All `malloc()`, `calloc()`, and `realloc()` calls include NULL pointer checks to prevent crashes from allocation failures.

3. **Safe Auth Header Handling**: Authorization headers are only added when a valid API key is present, preventing "Bearer (null)" headers.

4. **Buffer Overflow Protection**: Dynamic buffer expansion includes proper size checks before writing, with `snprintf` return value validation (negative returns trigger cleanup and NULL return).

5. **Incremental SSE Parsing**: Both backends deliver SSE data incrementally. Transport buffers are bounded, incomplete SSE lines are capped at 64 KiB, and idle streams terminate with a network error after `OPENAI_TIMEOUT`.

6. **lwIP Overflow Protection**: `build_http_header()` uses two-pass `snprintf` with truncation detection and SIZE_MAX overflow checks. `recv_response()` guards buffer doubling against SIZE_MAX/2 overflow. `build_anthropic_request_body()` checks SIZE_MAX before accumulating system content.

7. **HTTP Header Injection Prevention**: The lwIP backend uses `escape_for_snprintf()` and `has_crlf()` to validate `extra_headers`, rejecting any headers containing bare CRLF to prevent HTTP header injection. The libcurl backend uses `append_extra_headers()` with the same CRLF validation.

8. **Stream Read Size Protection**: `openai_http_stream_read()` clamps the returned byte count to `INT_MAX` to prevent `size_t`-to-`int` truncation issues in the SSE parser.

## Platform-Specific Notes

### Linux/Raspberry Pi

All dependencies are included as git submodules. Clone with:

```bash
git clone --recursive https://github.com/Limx1994/c_openai.git
# Or if already cloned:
git submodule update --init --recursive
```

### Windows (MSYS2/MinGW)

```bash
git clone --recursive https://github.com/Limx1994/c_openai.git
cd c_openai
cmake -S . -B build -G "MSYS Makefiles"
cmake --build build --parallel 10
```

### STM32/Embedded

1. Add lwIP to your project
2. Configure CMake with `OPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP`
3. For HTTPS support, enable ALTCP + mbedTLS in `lwipopts.h`:

   ```c
   #define LWIP_ALTCP             1
   #define LWIP_ALTCP_TLS         1
   #define LWIP_ALTCP_TLS_MBEDTLS 1
   ```

## License

CC BY-NC 4.0 (Creative Commons Attribution-NonCommercial 4.0 International)

This project is licensed under the Creative Commons Attribution-NonCommercial
4.0 International License. You may use, modify, and share this software for
**non-commercial purposes only**.

**Allowed**:

- ✅ Copy and redistribute the material
- ✅ Remix, transform, and build upon the material

**Required**:

- 📋 Attribution: You must give appropriate credit

**Prohibited**:

- ❌ Commercial use of any kind

For commercial use, please contact the author for a separate license.

See [LICENSE](LICENSE) for full details or visit:
<https://creativecommons.org/licenses/by-nc/4.0/>
