# C-OpenAI: Cross-platform OpenAI API Client for MCU/POSIX

## Context

用户希望在本地目录创建 Python openai 包的 C 语言实现版本：
- **HTTP 库**：libcurl（跨平台，功能完善）+ lwIP（嵌入式）
- **内存管理**：混合模式（支持动态分配和静态缓冲区，可配置）
- **功能范围**：核心功能集（Chat Completions + Streaming + Embeddings）
- **文档要求**：必须同时提供英文（README.md）和中文（README_zh.md）文档

## 项目结构

```
c_openai/
├── include/
│   ├── openai.h           # 主头文件，OpenAI_Client 定义
│   ├── openai_error.h     # 错误码定义
│   ├── openai_types.h     # 类型定义（消息、请求、响应）
│   ├── openai_http.h      # HTTP 接口
│   ├── openai_json.h      # JSON 接口
│   └── openai_config.h    # 编译配置（后端选择、内存模式）
├── src/
│   ├── openai_client.c    # 客户端核心实现
│   ├── openai_http_curl.c # libcurl HTTP 封装
│   ├── openai_http_lwip.c  # lwIP HTTP 封装
│   ├── openai_json.c      # JSON 解析封装
│   ├── openai_error.c     # 错误处理
│   └── CMakeLists.txt     # 库构建配置
├── example/
│   ├── chat_example.c     # Chat Completions 示例
│   └── CMakeLists.txt     # 示例构建配置
├── cJSON/
│   └── cJSON.c/h          # cJSON 库（内联）
├── CMakeLists.txt         # 根构建配置
├── README.md              # 英文文档
├── README_zh.md          # 中文文档
└── CLAUDE.md             # 项目规范
```

## HTTP 后端实现

### libcurl 后端（默认）
- 路径：`src/openai_http_curl.c`
- 功能：完整 HTTPS 支持，代理，证书验证
- 适用：PC、服务器、树莓派

### lwIP 后端（嵌入式）
- 路径：`src/openai_http_lwip.c`
- 功能：轻量级 TCP/HTTP 实现，适用于资源受限 MCU
- 依赖：lwIP 套接字 API
- 适用：STM32、ESP32、裸机环境

### 后端切换

```c
// openai_config.h
#define OPENAI_HTTP_BACKEND OPENAI_BACKEND_CURL  // libcurl
// 或
#define OPENAI_HTTP_BACKEND OPENAI_BACKEND_LWIP  // lwIP
```

## API 接口设计

```c
// 初始化
OpenAI_Client* openai_client_new(const char* api_key);
void openai_client_free(OpenAI_Client* client);

// Chat Completions
OpenAI_Response* openai_chat_create(OpenAI_Client* client, OpenAI_ChatRequest* req);

// Streaming
OpenAI_Stream* openai_chat_create_stream(OpenAI_Client* client, OpenAI_ChatRequest* req);
int openai_stream_read(OpenAI_Stream* stream, OpenAI_StreamEvent* event);

// Embeddings
OpenAI_EmbeddingResponse* openai_embeddings_create(OpenAI_Client* client, const char* input);

// 错误处理
const char* openai_error_str(OpenAI_ErrorCode code);
```

## 配置选项（编译时）

```c
// openai_config.h
#define OPENAI_USE_MALLOC 1      // 0=静态缓冲区
#define OPENAI_BUFFER_SIZE 4096   // 默认缓冲区大小
#define OPENAI_MAX_RETRIES 3      // 重试次数
#define OPENAI_TIMEOUT 30          // 超时秒数
#define OPENAI_HTTP_BACKEND OPENAI_BACKEND_CURL  // OPENAI_BACKEND_CURL 或 OPENAI_BACKEND_LWIP
```

## 实现步骤

1. **创建基础结构** - CMakeLists.txt, include/openai.h
2. **实现 HTTP 层** - libcurl 封装，处理 HTTPS 请求
3. **实现 JSON 层** - cJSON 集成，解析/构建 JSON
4. **实现 Chat Completions** - 核心聊天 API
5. **实现 Streaming** - SSE 事件流解析
6. **实现 Embeddings** - 向量嵌入 API
7. **示例程序** - 演示用法

## 文档要求

- **README.md** - 英文文档（English）
- **README_zh.md** - 中文文档（中文）

## 验证方法

1. **编译测试**：`mkdir build && cd build && cmake .. && make`
2. **功能测试**：运行示例程序调用真实 API
3. **嵌入式测试**：在 STM32/Raspberry Pi Pico 等平台编译验证

## 已知问题

- Streaming API 已定义接口但尚未完全实现
- lwIP 后端需要用户自行提供 lwIP 库和头文件