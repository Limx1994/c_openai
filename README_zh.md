# C-OpenAI

跨平台的 OpenAI API C 语言客户端，专为 MCU（微控制器）和 POSIX 系统设计。

## 特性

- **跨平台**：支持 Windows、Linux、macOS、RTOS 及嵌入式系统
- **双 HTTP 后端**：
  - `libcurl`：功能完整，适用于 PC 和服务器
  - `lwIP`：轻量级，适用于资源受限的 MCU（STM32、ESP32 等）
- **可配置内存**：动态分配或静态缓冲区模式（编译时选项）
- **核心 API 支持**：
  - Chat Completions（聊天补全）
  - Streaming（流式响应，SSE）
  - Embeddings（文本嵌入）

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
│   ├── openai_http_lwip.c     # lwIP 后端
│   ├── openai_json.c     # JSON 工具
│   └── openai_error.c    # 错误处理
├── cJSON/                # JSON 解析器（cJSON 库）
├── third_party/          # 第三方库
│   ├── libcurl/          # libcurl HTTP 库（git submodule）
│   ├── lwip/             # lwIP TCP/IP 协议栈（git submodule）
│   ├── mbedtls/          # mbedTLS 加密库（git submodule）
│   └── CMakeLists.txt    # 第三方库构建配置
├── example/              # 示例代码
│   └── chat_example.c
├── CMakeLists.txt        # 构建配置
├── README.md             # 英文文档
├── README_zh.md          # 中文文档
└── CLAUDE.md             # 项目规范
```

## 构建

### 前置条件

- CMake 3.10 或更高
- Git（支持子模块）
- 所有依赖库均通过 git submodule 集成（libcurl、lwIP、mbedtls）

### 使用 libcurl 构建（默认，用于 PC/服务器）

```bash
mkdir build && cd build
cmake ..
make
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
#define OPENAI_TLS_CERT_VERIFY 0  // 0=跳过证书验证（测试用）
```

### 使用 lwIP 构建（用于嵌入式/MCU）

```bash
cmake .. -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP
make
```

### 使用静态内存构建（无 malloc，用于裸机）

```bash
cmake .. -DOPENAI_USE_MALLOC=0
make
```

### 组合选项（例如 lwIP + 静态内存）

```bash
cmake .. -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP -DOPENAI_USE_MALLOC=0
make
```

## 配置

编辑 `include/openai_config.h` 进行自定义：

```c
#define OPENAI_HTTP_BACKEND OPENAI_BACKEND_CURL  // 或 OPENAI_BACKEND_LWIP
#define OPENAI_USE_MALLOC 1    // 0 表示静态缓冲区模式
#define OPENAI_BUFFER_SIZE 4096
#define OPENAI_MAX_RETRIES 3
#define OPENAI_TIMEOUT 30
#define OPENAI_USE_TLS 1       // lwIP HTTPS（需要 mbedTLS）
#define OPENAI_TLS_CERT_VERIFY 0
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
req.temperature = 0.7f;

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

### 环境变量

通过环境变量设置 API key：

```bash
export OPENAI_API_KEY=sk-your-key-here
```

或在代码中直接传递：

```c
OpenAI_Client* client = openai_client_new(getenv("OPENAI_API_KEY"));
```

## API 参考

### 客户端生命周期

| 函数 | 描述 |
|------|------|
| `openai_client_new(api_key)` | 创建新客户端 |
| `openai_client_free(client)` | 释放客户端和资源 |
| `openai_version()` | 获取库版本 |

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
| `openai_stream_close(stream)` | 关闭流并释放资源 |

## 错误处理

```c
const char* err_str = openai_error_str(OPENAI_ERR_NETWORK);
// 返回: "Network error"
```

错误码：
- `OPENAI_OK` - 成功
- `OPENAI_ERR_INVALID_PARAM` - 无效参数
- `OPENAI_ERR_MEMORY` - 内存分配失败
- `OPENAI_ERR_NETWORK` - 网络错误
- `OPENAI_ERR_TIMEOUT` - 请求超时
- `OPENAI_ERR_PARSE` - JSON 解析错误
- `OPENAI_ERR_API` - API 返回错误
- `OPENAI_ERR_AUTH` - 认证失败
- `OPENAI_ERR_RATE_LIMIT` - 超出速率限制
- `OPENAI_ERR_SERVER` - 服务器错误
- `OPENAI_ERR_BUFFER_EMPTY` - 缓冲区为空（流式响应）
- `OPENAI_ERR_EOF` - 流结束（流式响应）

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
4. 将 cJSON 库移植到目标平台
5. 如需 HTTPS 支持，在 `lwipopts.h` 中启用 ALTCP + mbedTLS：
   ```c
   #define LWIP_ALTCP             1
   #define LWIP_ALTCP_TLS         1
   #define LWIP_ALTCP_TLS_MBEDTLS 1
   ```

## 许可证

MIT License