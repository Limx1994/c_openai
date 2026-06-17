# C-OpenAI

跨平台的 OpenAI/Anthropic API C 语言客户端，专为 MCU（微控制器）和 POSIX 系统设计。

## 特性

- **跨平台**：支持 Windows、Linux、macOS、RTOS 及嵌入式系统
- **双 HTTP 后端**：
  - `libcurl`：功能完整，适用于 PC 和服务器
  - `lwIP`：轻量级，适用于资源受限的 MCU（STM32、ESP32 等）
- **多提供商支持**：
  - OpenAI API（Chat Completions、Streaming、Embeddings）
  - Anthropic Messages API（Chat、Streaming）
- **可配置内存**：动态分配或静态缓冲区模式（编译时选项）
- **安全性**：JSON 注入防护、内存安全检查、安全的认证头处理

## 项目结构

```
c_openai/
├── include/              # 公共头文件
│   ├── openai.h          # 主 API
│   ├── openai_config.h   # 编译时配置
│   ├── openai_types.h    # 类型定义
│   ├── openai_error.h    # 错误码
│   ├── openai_http.h     # HTTP 接口
│   └── openai_json.h     # JSON 接口
├── src/                  # 实现
│   ├── openai_client.c   # 核心客户端
│   ├── openai_http_curl.c    # libcurl 后端
│   ├── openai_http_lwip.c    # lwIP 后端
│   └── openai_json.c     # JSON 解析器（自定义实现）
├── third_party/          # 第三方库（git 子模块）
│   ├── libcurl/          # libcurl HTTP 库
│   ├── lwip/             # lwIP TCP/IP 协议栈
│   ├── mbedtls/          # mbedTLS 加密库
│   └── CMakeLists.txt    # 第三方库构建配置
├── example/              # 示例代码
│   ├── chat_example.c    # OpenAI Chat Completions
│   └── anthropic_example.c  # Anthropic Messages API
├── build.sh              # libcurl 构建脚本（MinGW/MSYS2）
├── build_lwip.sh         # lwIP 构建脚本（ARM GCC）
├── CMakeLists.txt        # CMake 构建配置
├── README.md             # 英文文档
├── README_zh.md          # 中文文档
└── CLAUDE.md             # 项目规范
```

## 构建

### 前置条件

- CMake 3.18 或更高
- Git（支持子模块）
- 所有依赖库均通过 git submodule 集成（libcurl、lwIP、mbedtls）

### 使用 libcurl 构建（默认，用于 PC/服务器）

```bash
mkdir build && cd build
cmake ..
make
```

或使用构建脚本（MinGW/MSYS2）：
```bash
bash build.sh
```

### lwIP 后端（用于嵌入式/MCU）

lwIP 后端同时支持 HTTP 和 HTTPS：
- **HTTP**：纯 TCP socket，无 TLS 开销
- **HTTPS**：使用 ALTCP + mbedTLS 实现加密连接

**重要**：lwIP 和 mbedTLS 通过 git submodule 集成，克隆时需：
```bash
git clone --recursive https://github.com/Limx1994/c_openai.git
# 或如果已经克隆：
git submodule update --init --recursive
```

使用 HTTPS 时，需在 `lwipopts.h` 中启用：
```c
#define LWIP_ALTCP             1
#define LWIP_ALTCP_TLS         1
#define LWIP_ALTCP_TLS_MBEDTLS 1
```

并在 `openai_config.h` 中配置：
```c
#define OPENAI_USE_TLS 1          // 启用 TLS
```

### 使用 lwIP 构建（用于嵌入式/MCU）

使用构建脚本（推荐，需要 STM32CubeIDE 中的 ARM GCC）：
```bash
bash build_lwip.sh
```

或使用 CMake：
```bash
cmake .. -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP
make
```

**注意**：ARM Cortex-M 平台需要在 `third_party/mbedtls/tf-psa-crypto/include/psa/crypto_config.h` 中禁用 `MBEDTLS_HAVE_ASM`（寄存器不足）。

### 构建选项

```bash
# libcurl 后端（默认）
cmake ..
make

# lwIP 后端
cmake .. -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP
make
```

## 配置

编辑 `include/openai_config.h` 进行自定义：

```c
#define OPENAI_HTTP_BACKEND OPENAI_BACKEND_CURL  // 或 OPENAI_BACKEND_LWIP
#define OPENAI_TIMEOUT 30          // HTTP 超时秒数
#define OPENAI_USE_TLS 1           // lwIP HTTPS（需要 mbedTLS）
```

## 使用方法

```c
#include "openai.h"

// 使用 API key 创建客户端
OpenAI_Client* client = openai_client_new("sk-...");

// 准备聊天请求
OpenAI_ChatRequest req = {0};
req.model = "gpt-3.5-turbo";
req.messages = malloc(sizeof(OpenAI_Message) * 2);
req.messages[0].role = "system";
req.messages[0].content = "你是一个有帮助的助手。";
req.messages[1].role = "user";
req.messages[1].content = "你好！";
req.message_count = 2;
req.temperature = 0.7f;  // 0.0-2.0，-1.0 表示使用 API 默认值

// 发送请求并获取响应
OpenAI_ChatResponse* resp = openai_chat_create(client, &req);

if (resp && resp->choices[0].content) {
    printf("响应: %s\n", resp->choices[0].content);
}

// 清理
openai_chat_response_free(resp);
free(req.messages);
openai_client_free(client);
```

### Anthropic 使用方法

```c
#include "openai.h"

// 使用 Anthropic API key 创建客户端
OpenAI_Client* client = openai_client_new("sk-ant-...");
openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC);
// base_url 自动设为 https://api.anthropic.com/v1

// 与 OpenAI 相同的 API - system prompt 会自动提取
OpenAI_ChatRequest req = {0};
req.model = "claude-3-opus-20240229";
req.messages = malloc(sizeof(OpenAI_Message) * 2);
req.messages[0].role = "system";
req.messages[0].content = "你是一个有帮助的助手。";
req.messages[1].role = "user";
req.messages[1].content = "你好！";
req.message_count = 2;
req.temperature = 0.7f;  // 0.0-2.0，-1.0 表示使用 API 默认值
req.max_tokens = 1024;  // Anthropic 必填

OpenAI_ChatResponse* resp = openai_chat_create(client, &req);
// ... 与 OpenAI 相同的响应处理 ...

openai_chat_response_free(resp);
free(req.messages);
openai_client_free(client);
```

### 环境变量

通过环境变量设置 API key：

```bash
export OPENAI_API_KEY=sk-your-key-here
export ANTHROPIC_API_KEY=sk-ant-your-key-here
```

或在代码中直接传递：

```c
// OpenAI
OpenAI_Client* client = openai_client_new(getenv("OPENAI_API_KEY"));

// Anthropic
OpenAI_Client* client = openai_client_new(getenv("ANTHROPIC_API_KEY"));
openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC);
```

### 自定义 API 基础 URL

用于 Azure OpenAI、代理服务器或自定义端点：

```c
OpenAI_Client* client = openai_client_new("sk-your-key-here");
openai_client_set_base_url(client, "https://your-proxy.com/v1");
// 所有后续 API 调用将使用此基础 URL
```

## API 参考

### 客户端生命周期

| 函数 | 描述 |
|------|------|
| `openai_client_new(api_key)` | 创建新客户端 |
| `openai_client_free(client)` | 释放客户端和资源 |
| `openai_client_set_base_url(client, url)` | 设置自定义 API 基础 URL（用于 Azure、代理服务器等） |
| `openai_client_set_provider(client, provider)` | 设置 API 提供商（OPENAI_PROVIDER_OPENAI 或 OPENAI_PROVIDER_ANTHROPIC） |
| `openai_client_get_last_error(client)` | 获取最后一次错误代码（用于区分 NULL 返回的具体原因） |

### 聊天补全

| 函数 | 描述 |
|------|------|
| `openai_chat_create(client, req)` | 发送聊天请求，返回响应 |
| `openai_chat_response_free(resp)` | 释放响应 |

### Embeddings

| 函数 | 描述 |
|------|------|
| `openai_embeddings_create(client, req)` | 获取文本的嵌入向量 |
| `openai_embedding_response_free(resp)` | 释放响应 |

### 流式响应

| 函数 | 描述 |
|------|------|
| `openai_chat_create_stream(client, req)` | 创建流式聊天请求 |
| `openai_stream_read(stream, event)` | 从流中读取下一个事件 |
| `openai_stream_event_free(event)` | 释放事件的 content/role/stop_reason 字段 |
| `openai_stream_close(stream)` | 关闭流并释放资源 |

### JSON 工具

| 函数 | 描述 |
|------|------|
| `openai_json_parse(json_string)` | 解析 JSON 字符串为 DOM |
| `openai_json_free(node)` | 释放 JSON DOM |
| `openai_json_escape_string(str)` | 转义 JSON 字符串（防止注入） |
| `openai_json_get_string(parent, key)` | 从对象获取字符串值 |
| `openai_json_get_number(parent, key)` | 从对象获取数字值 |
| `openai_json_get_object(parent, key)` | 按键获取子对象 |
| `openai_json_array_first(parent)` | 获取数组首元素（迭代器模式） |
| `openai_json_array_next(current)` | 获取数组下一元素（迭代器模式） |

## 错误处理

错误码：
- `OPENAI_OK` - 成功
- `OPENAI_ERR_INVALID_PARAM` - 无效参数
- `OPENAI_ERR_MEMORY` - 内存分配失败
- `OPENAI_ERR_NETWORK` - 网络错误
- `OPENAI_ERR_TIMEOUT` - 请求超时
- `OPENAI_ERR_PARSE` - JSON 解析错误
- `OPENAI_ERR_API` - API 返回错误
- `OPENAI_ERR_AUTH` - 认证失败（HTTP 401）
- `OPENAI_ERR_RATE_LIMIT` - 超出速率限制（HTTP 429）
- `OPENAI_ERR_SERVER` - 服务器错误（HTTP 5xx）
- `OPENAI_ERR_BUFFER_EMPTY` - 缓冲区为空（流式响应）
- `OPENAI_ERR_EOF` - 流结束（流式响应）

### 错误查询

当 `openai_chat_create`、`openai_chat_create_stream` 或 `openai_embeddings_create` 返回 NULL 时，可通过 `openai_client_get_last_error()` 获取具体错误类型：

```c
OpenAI_ChatResponse* resp = openai_chat_create(client, &req);
if (!resp) {
    int err = openai_client_get_last_error(client);
    switch (err) {
        case OPENAI_ERR_AUTH:
            printf("认证失败，请检查 API Key\n");
            break;
        case OPENAI_ERR_RATE_LIMIT:
            printf("超出速率限制，请稍后重试\n");
            break;
        case OPENAI_ERR_SERVER:
            printf("服务器错误，请稍后重试\n");
            break;
        case OPENAI_ERR_NETWORK:
            printf("网络错误，请检查连接\n");
            break;
        default:
            printf("未知错误：%d\n", err);
    }
}
```

## 安全特性

本库包含以下安全增强：

1. **JSON 注入防护**：`openai_json_escape_string()` 函数转义特殊字符（`"`、`\`、`/`、`\n`、`\r`、`\t`），在嵌入用户内容时防止 JSON 注入攻击。适用于 Chat Completions 和 Embeddings 请求中的 model、role 和 content 字段。

2. **内存安全**：所有 `malloc()`、`calloc()` 和 `realloc()` 调用都包含 NULL 指针检查，防止分配失败时崩溃。

3. **安全的认证头处理**：仅在存在有效 API key 时才添加 Authorization 头，防止 "Bearer (null)" 头。

4. **缓冲区溢出保护**：动态缓冲区扩展在写入前进行正确的大小检查，`snprintf` 偏移量计算也经过验证。

5. **lwIP 后端流式请求支持**：libcurl 和 lwIP 后端现在都支持流式（SSE）请求，可在嵌入式设备上实现实时响应处理。

## 平台说明

### Linux / 树莓派

所有依赖库均通过 git submodule 集成。克隆时需：
```bash
git clone --recursive https://github.com/Limx1994/c_openai.git
# 或如果已经克隆：
git submodule update --init --recursive
```

### Windows (MSYS2/MinGW)

```bash
git clone --recursive https://github.com/Limx1994/c_openai.git
cd c_openai
mkdir build && cd build
cmake .. -G "MSYS Makefiles"
make
```

### STM32 / 嵌入式

1. 将 lwIP 添加到项目中
2. 配置 `OPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP`
3. 确保 `OPENAI_USE_MALLOC=0` 以启用无 malloc 模式
4. 如需 HTTPS 支持，在 `lwipopts.h` 中启用 ALTCP + mbedTLS：
   ```c
   #define LWIP_ALTCP             1
   #define LWIP_ALTCP_TLS         1
   #define LWIP_ALTCP_TLS_MBEDTLS 1
   ```

## 许可证

CC BY-NC 4.0（Creative Commons 署名-非商业性使用 4.0 国际许可证）

本项目采用 Creative Commons 署名-非商业性使用 4.0 国际许可证授权。
您可以将本软件用于**非商业目的**。

**允许**：
- ✅ 复制和分享本作品
- ✅ 修改和演绎本作品

**要求**：
- 📋 署名：必须注明原作者

**禁止**：
- ❌ 任何形式的商业使用

如需商业使用，请联系作者获取单独授权。

完整许可证内容请参阅 [LICENSE](LICENSE) 文件或访问：
https://creativecommons.org/licenses/by-nc/4.0/deed.zh-hans
