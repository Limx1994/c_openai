# C-OpenAI: Cross-platform OpenAI/Anthropic API Client for MCU/POSIX

## Context

用户希望在本地目录创建 Python openai 包的 C 语言实现版本，同时支持 Anthropic Messages API：

- **HTTP 库**：libcurl（跨平台，功能完善）+ lwIP（嵌入式）
- **内存管理**：动态分配模式，调用方通过对应 free API 释放资源
- **功能范围**：核心功能集（Chat Completions + Streaming + Embeddings）
- **API 提供商**：OpenAI（默认）+ Anthropic（通过 `openai_client_set_provider` 切换）
- **文档要求**：必须同时提供英文（README.md）和中文（README_zh.md）文档

## 项目结构

```text
c_openai/
├── include/
│   ├── openai.h           # 主头文件，OpenAI_Client 定义
│   ├── openai_error.h     # 错误码定义
│   ├── openai_types.h     # 类型定义（消息、请求、响应、Provider 枚举）
│   ├── openai_http.h      # HTTP 接口（支持 auth_mode、extra_headers）
│   ├── openai_json.h      # JSON 接口（含 openai_json_serialize）
│   └── openai_config.h    # 编译配置（后端选择、内存模式、API Base URL）
├── src/
│   ├── openai_client.c    # 客户端核心实现（OpenAI + Anthropic）
│   ├── openai_http_curl.c # libcurl HTTP 封装
│   ├── openai_http_lwip.c # lwIP HTTP 封装
│   └── openai_json.c      # JSON 解析封装（含 openai_json_serialize）
├── third_party/
│   ├── libcurl/           # libcurl HTTP 库（git submodule）
│   ├── lwip/              # lwIP TCP/IP 协议栈（git submodule）
│   ├── mbedtls/           # mbedTLS 加密库（git submodule）
│   ├── lwip_port/         # 最小 lwIP 平台适配头文件（编译检查用）
│   └── CMakeLists.txt     # 第三方库构建配置
├── example/
│   ├── chat_example.c     # OpenAI Chat Completions 示例
│   ├── anthropic_example.c # Anthropic Messages API 示例
│   └── CMakeLists.txt     # 示例构建配置
├── build.py               # 一键编译脚本（支持 lwIP/curl 后端）
├── CMakeLists.txt         # 根构建配置
├── README.md              # 英文文档
├── README_zh.md           # 中文文档
└── AGENTS.md              # 项目规范
```

## HTTP 后端实现

### libcurl 后端（默认）

- 路径：`src/openai_http_curl.c`
- 功能：完整 HTTPS 支持，代理，证书验证
- 适用：PC、服务器、树莓派

### lwIP 后端（嵌入式）

- 路径：`src/openai_http_lwip.c`
- 功能：TCP/HTTP/HTTPS 实现，支持 ALTCP + mbedTLS
- 依赖：lwIP + mbedTLS（通过 git submodule 集成在 `third_party/` 目录）
- 适用：STM32、ESP32、裸机环境
- **重要**：使用 lwIP 后端需初始化子模块：`git submodule update --init --recursive`

**所有后端**（libcurl、lwIP）均已通过 git submodule 集成，无需手动安装系统库。

### lwIP HTTPS/TLS 配置

lwIP 后端使用 ALTCP + mbedTLS 实现 HTTPS。需在 `lwipopts.h` 中启用：

```c
#define LWIP_ALTCP             1
#define LWIP_ALTCP_TLS         1
#define LWIP_ALTCP_TLS_MBEDTLS 1
#define MEMP_NUM_ALTCP_PCB     4
#define MEMP_NUM_ALTCP_TLS_PCB 4
```

openai_config.h 中可配置：

```c
#define OPENAI_USE_TLS 1          // 0=禁用 TLS
```

### 后端切换

后端通过 CMake 变量 `OPENAI_HTTP_BACKEND` 选择，支持三个值：

- `OPENAI_BACKEND_CURL`（默认）— libcurl 后端
- `OPENAI_BACKEND_LWIP` — lwIP 后端
- `OPENAI_BACKEND_BOTH` — 同时编译两个后端

在 CMakeLists.txt 中配置，或在命令行中指定：

## API 接口设计

```c
// 初始化
OpenAI_Client* openai_client_new(const char* api_key);
void openai_client_free(OpenAI_Client* client);
int openai_client_set_base_url(OpenAI_Client* client, const char* base_url);
int openai_client_set_provider(OpenAI_Client* client, int provider);
int openai_client_get_last_error(OpenAI_Client* client);  // 获取最后一次错误代码

// Chat Completions（支持 OpenAI 和 Anthropic）
OpenAI_ChatResponse* openai_chat_create(OpenAI_Client* client, OpenAI_ChatRequest* req);
void openai_chat_response_free(OpenAI_ChatResponse* resp);

// Streaming（支持 OpenAI 和 Anthropic）
void* openai_chat_create_stream(OpenAI_Client* client, OpenAI_ChatRequest* req);
int openai_stream_read(void* stream, OpenAI_StreamEvent* event);
void openai_stream_event_free(OpenAI_StreamEvent* event);
void openai_stream_close(void* stream);

// Embeddings
OpenAI_EmbeddingResponse* openai_embeddings_create(OpenAI_Client* client, OpenAI_EmbeddingRequest* req);
void openai_embedding_response_free(OpenAI_EmbeddingResponse* resp);

// JSON 工具
char* openai_json_escape_string(const char* str);  // JSON 字符串转义（防注入）
char* openai_json_serialize(OpenAI_JSONNode* node); // JSON 节点树序列化为字符串（调用方 free）
OpenAI_JSONNode* openai_json_array_first(OpenAI_JSONNode* parent);  // 数组迭代器：首元素
OpenAI_JSONNode* openai_json_array_next(OpenAI_JSONNode* current);  // 数组迭代器：下一元素
```

## 配置选项（编译时）

```c
// openai_config.h
#define OPENAI_TIMEOUT 30          // 超时秒数
#define OPENAI_USE_TLS 1           // lwIP HTTPS（需启用 LWIP_ALTCP_TLS_MBEDTLS）

// 在包含 openai_json.h 前定义可覆写 JSON 序列化深度限制
#define OPENAI_JSON_MAX_DEPTH 64   // 默认 64，嵌入式可降低（如 32）以节省栈空间
```

日志通过编译宏控制，默认开启（`OPENAI_LOG_ENABLED=1`），级别 0-3：

```c
// openai_config.h
#define OPENAI_LOG_ENABLED 1       // 0=关闭所有日志，嵌入式建议设为 0
#define OPENAI_LOG_LEVEL 3         // 0=error, 1=warn, 2=info, 3=debug
```

## Anthropic Messages API 支持

库现在支持 Anthropic Messages API，与 OpenAI 使用相同的接口。

### 使用方法

```c
#include "openai.h"

// 创建客户端并设置 Anthropic 提供商
OpenAI_Client* client = openai_client_new("sk-ant-...");
openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC);
// base_url 自动设为 https://api.anthropic.com/v1

// 使用方式与 OpenAI 相同
OpenAI_ChatRequest req = {0};
req.model = "claude-sonnet-4-20250514";
req.messages = ...;
req.max_tokens = 1024;  // Anthropic 必填

OpenAI_ChatResponse* resp = openai_chat_create(client, &req);
```

### 关键差异

| 方面 | OpenAI | Anthropic |
|------|--------|-----------|
| 认证 | `Authorization: Bearer {key}` | `x-api-key: {key}` + `anthropic-version` |
| 端点 | `/v1/chat/completions` | `/v1/messages` |
| system prompt | messages 中 `role: "system"` | 顶层 `"system"` 字段 |
| max_tokens | 可选 | 必填（默认 4096） |
| stop | `"stop": "value"` | `"stop_sequences": ["value"]` |
| 响应 content | `choices[0].message.content` | `content` 数组拼接 |
| streaming | `data: {...}` + `[DONE]` | `event:` + `data:` + `message_stop` |
| Extended Thinking | 不支持 | `thinking_enabled=1` + `thinking_budget` |

### 默认 Base URL

- OpenAI: `https://api.openai.com/v1`（OPENAI_USE_TLS=1 时）
- Anthropic: `https://api.anthropic.com/v1`（OPENAI_USE_TLS=1 时）

可通过 `openai_client_set_base_url()` 覆盖。

## 实现步骤

1. **创建基础结构** - CMakeLists.txt, include/openai.h
2. **实现 HTTP 层** - libcurl 封装，处理 HTTPS 请求
3. **实现 JSON 层** - 自定义 JSON 解析器（openai_json.c），解析/构建 JSON
4. **实现 Chat Completions** - 核心聊天 API
5. **实现 Streaming** - SSE 事件流解析
6. **实现 Embeddings** - 向量嵌入 API
7. **示例程序** - 演示用法

## 文档要求

- **README.md** - 英文文档（English）
- **README_zh.md** - 中文文档（中文）

## 许可证

本项目采用 **CC BY-NC 4.0**（Creative Commons 署名-非商业性使用 4.0 国际许可证）授权。

**关键条款**：

- ✅ 允许：复制、分享、修改本作品
- 📋 要求：必须注明原作者
- 🚫 禁止：商业使用

完整许可证内容请参阅 [LICENSE](LICENSE) 文件。

## 验证方法

1. **libcurl 编译**：`bash build.sh`（MinGW/MSYS2）
2. **lwIP 编译**：`bash build_lwip.sh`（ARM GCC，自动检测 STM32CubeIDE 工具链）
3. **CMake 编译**：`cmake -S . -B build && cmake --build build --parallel 10`
4. **功能测试**：运行示例程序调用真实 API
5. **嵌入式测试**：在 STM32/Raspberry Pi Pico 等平台编译验证

## 已知问题

以下问题已在代码审查和修复过程中解决：

- CMake 版本冲突（已修复为 3.18）
- libcurl 链接缺失（已添加）
- JSON 注入漏洞（已转义处理）
- 内存 NULL 检查缺失（已添加）
- lwIP 后端流式请求缺失（已实现）

当前已知限制包括：单条未完成 SSE 行最大为 64 KiB，lwIP 后端不支持并发请求，且静态缓冲区内存模式尚未实现。

## 已修复问题

以下问题已在代码审查后修复：

1. **JSON 注入漏洞** - `openai_client.c` 中 `build_chat_request_body` 函数直接插入用户内容，未转义特殊字符。现已通过 `openai_json_escape_string()` 转义处理。
2. **缓冲区溢出风险** - `snprintf` 偏移量计算可能在长字符串后超出边界。现已改为提前计算所需空间并在使用 realloc 前进行 NULL 检查。
3. **curl 回调内存泄漏** - `openai_http_curl.c` 中 `write_callback` 函数在 realloc 失败时未释放旧缓冲区导致泄漏。现已修复。
4. **NULL auth_header 处理** - `openai_http_curl.c` 和 `openai_http_lwip.c` 中在 api_key 为 NULL 时生成无效的 "Bearer (null)" 头。现已修复，仅在 auth_header 有效时添加 Authorization 头。
5. **CMake 版本冲突** - 根 CMakeLists.txt 要求 3.10，但 libcurl 子模块要求 3.18。已提升版本要求至 3.18。
6. **libcurl 链接缺失** - `src/CMakeLists.txt` 在 libcurl 后端未链接 curl 库。已添加 `target_link_libraries(... PUBLIC curl)`。
7. **错误的头文件路径** - `openai_http_lwip.c` 包含 `<lwip/tls.h>`，应为 `<lwip/altcp_tls.h>`。已修复。
8. **LWIP_ALTCP_TLS 未定义** - `third_party/CMakeLists.txt` 未定义 LWIP_ALTCP_TLS=1 和 LWIP_ALTCP_TLS_MBEDTLS=1，导致 TLS 功能实际被禁用。已修复。
9. **libcurl 目标名称错误** - `example/CMakeLists.txt` 使用 `CURL::libcurl`（无效），应为 `curl`。已修复。
10. **calloc NULL 检查缺失** - `openai_json.c` 中 `openai_json_parse_object` 和 `openai_json_parse_array` 的 calloc 结果未检查即使用。已添加 `if (!child) return p;` 检查。
11. **malloc NULL 检查缺失** - `openai_http_curl.c` 中 `malloc(1)` 失败会崩溃。已添加 NULL 检查。
12. **lwIP 后端流式请求缺失** - `openai_http_lwip.c` 缺失 `openai_http_request_stream()` 函数实现，导致 Streaming API 无法在 lwIP 后端使用。已实现。
13. **curl headers 追加逻辑错误** - `openai_http_curl.c` 中当 auth_header 为 NULL 时，curl_slist_append(NULL, ...) 会失败。已修复，将 Content-Type 和 Accept 的追加放入 `if (headers)` 条件内。
14. **lwIP 缓冲区边界检查** - `openai_http_lwip.c` 中缓冲区边界检查在读取之后，可能导致缓冲区溢出。已修复，改为先计算 to_read 再读取。
15. **JSON 解析 parent NULL 检查** - `openai_json.c` 中 parse_object 和 parse_array 函数在 parent 为 NULL 时访问 parent->children 导致崩溃。已添加 `if (!parent) return p;` 检查。
16. **third_party CMake 版本冲突** - `third_party/CMakeLists.txt` 要求 3.10，与根 CMakeLists.txt 的 3.18 冲突。已提升至 3.18。
17. **Content-Length 大小写敏感** - `openai_http_lwip.c` 中 Content-Length 解析仅检查两种大小写，部分服务器发送的非标准大小写（如 `CONTENT-LENGTH`）会导致响应体截断。已改用大小写不敏感比较。
18. **JSON 解析无效输入返回非 NULL** - `openai_json.c` 中 `openai_json_parse` 对空字符串或无效 JSON 仍返回全零 root 节点，调用方无法区分解析失败和空对象。现已添加首字符校验，无效输入返回 NULL。
19. **lwIP HTTPS TLS 配置内存泄漏** - `openai_http_lwip.c` 中 `openai_http_request()` 和 `openai_http_request_stream()` 的 ALTCP_TLS 分支正常完成后只设 `s_tls_config = NULL` 未调用 `altcp_tls_free_config()` 释放内存。已修复，在两处正常完成路径添加 `altcp_tls_free_config()` 调用。
20. **openai_config.h 缺少 include guard** - 文件末尾有 `#endif` 但开头缺少 `#ifndef OPENAI_CONFIG_H` / `#define OPENAI_CONFIG_H`，导致编译错误（`#endif without #if`）。已在 c19d858 提交格式化时补回。
21. **Anthropic 响应解析 NULL 指针崩溃** - `openai_client.c` 中 `parse_anthropic_response()` 在 `calloc()` 失败时 `choice_count` 已被设为 1 但 `choices` 为 NULL，导致 `openai_chat_response_free()` 解引用 NULL 指针崩溃。已修复，在 calloc 失败时将 `choice_count` 重置为 0。
22. **Anthropic 请求体 JSON 格式错误（Critical）** - `openai_client.c` 中 `build_anthropic_request_body` 和 `build_chat_request_body` 的消息分隔符逻辑使用数组索引 `i` 而非已写入消息计数，当 system 消息排在首位时生成前导逗号的无效 JSON。已修复，改用 `first_msg` / `first_non_system` 标志位。
23. **OpenAI SSE 流式解析转义序列处理** - `openai_client.c` 中 `parse_sse_line` 遇到 `\n` 输出字面 `n` 而非换行符，与 `parse_anthropic_sse_line` 不一致。已修复，添加与 Anthropic 版本一致的 switch-case 转义处理。
24. **Anthropic 流式事件 role/index/stop_reason 为空** - `openai_client.c` 中 Anthropic SSE 解析路径不提取 `role`、`index` 和 `stop_reason`。已修复，增加对 `content_block_start` 和 `message_delta` 事件的处理。
25. **流式请求 CURLOPT_TIMEOUT 中断长流** - `openai_http_curl.c` 中 `openai_http_request_stream` 设置 `CURLOPT_TIMEOUT`（总传输时间限制），长时间流式响应会被中断。已修复，改为仅设置 `CURLOPT_CONNECTTIMEOUT`。
26. **curl write_callback O(n²) 缓冲区增长** - `openai_http_curl.c` 中 `write_callback` 每次精确增长 `realsize` 字节，对比 `stream_write_callback` 已使用几何倍增。已修复，为 `memory_chunk` 添加 `capacity` 字段，采用倍增策略。
27. **流式解析固定缓冲区限制** - `openai_client.c` 中 `line_buffer[1024]` 和 `content[512]` 导致长内容截断。已修复，增大为 `line_buffer[4096]` 和 `content[2048]`。
28. **stop 参数固定缓冲区溢出风险** - `openai_client.c` 中 `stop_buf[512]` 对长转义字符串可能截断导致 JSON 格式错误。已修复，改用动态分配。
29. **JSON 数组迭代 O(n²)** - `openai_client.c` 中循环调用 `openai_json_get_array_item(arr, i)` 导致 O(n²) 复杂度。已修复，添加 `openai_json_array_first` / `openai_json_array_next` 迭代器接口。
30. **README 文档记录不存在的配置选项** - README 中记录了 `OPENAI_USE_MALLOC`、`OPENAI_BUFFER_SIZE`、`OPENAI_MAX_RETRIES`、`OPENAI_TLS_CERT_VERIFY` 等未实现的配置项。已修复，从文档中移除。
31. **README API 参考缺少函数** - `openai_client_set_provider` 和 `openai_stream_event_free` 未在 API 参考表中。已修复，补充到文档。
32. **lwIP altcp_read 超时机制无效** - `openai_http_lwip.c` 中 `altcp_read` 的超时使用 `waited++` 无实际延时。已修复，使用 `sys_now()` 计时并添加 `sys_check_timeouts()` 调用。
33. **lwIP HTTPS Content-Length 大小写不一致** - `openai_http_lwip.c` 中 ALTCP TLS 路径使用 `strstr` 仅匹配两种大小写。已修复，改为 `openai_strcasestr`。
34. **lwIP send() 返回 0 导致无限循环** - `openai_http_lwip.c` 中 `send` 返回 0（对端关闭）时循环不退出。已修复，将 `sent < 0` 改为 `sent <= 0`。
35. **lwIP 条件编译死代码清理** - `openai_http_lwip.c` 中 altcp 兼容层、s_tls_config、冗余 `#ifdef` 守卫等条件编译死代码。已修复，移入正确的 `#ifdef` 块或移除冗余守卫。
36. **OPENAI_TIMEOUT 对 lwIP 无效** - `openai_http_lwip.c` 中硬编码 `timeout_ms = 30000` 未使用 `OPENAI_TIMEOUT` 宏。已修复。
37. **OpenAI_StreamEvent 缺少 stop_reason 字段** - Anthropic 流式响应的 `message_delta` 事件包含 `stop_reason`，但结构体中没有对应字段。已修复，添加 `stop_reason` 字段并更新 `openai_stream_event_free`。
38. **temperature 哨兵值约定** - 零初始化请求会发送 `temperature:0`；调用方必须显式设置 `-1.0f` 才使用 API 默认值。当前结构无法区分“未设置”和显式 `0`。
39. **Anthropic 流式 stop_reason 无法传递给调用方** - `openai_client.c` 中 `message_delta` 事件提取了 `stop_reason` 但不返回 0。已修复，在提取到 `stop_reason` 时返回事件。
40. **OpenAI 流式响应从未解析 finish_reason** - `openai_client.c` 中 `parse_sse_line` 仅提取 `content`，包含 `finish_reason` 的最终 chunk 被丢弃。已修复，增加对 `finish_reason` 的解析。
41. **OpenAI 非流式响应缺少 finish_reason 字段** - `openai_types.h` 中 `OpenAI_Choice` 结构体没有 `finish_reason` 成员。已修复，添加 `finish_reason` 字段并在响应解析中提取。
42. **Anthropic 非流式响应忽略 tool_use 内容块** - `openai_client.c` 中 `parse_anthropic_response` 仅处理 `type: "text"` 的块。已修复，将 `tool_use` 块序列化为 JSON 字符串拼接到 content。
43. **Anthropic 流式 content_block_start 事件丢失** - `openai_client.c` 中 `content_block_start` 的 role/index 在跨调用时丢失。已修复，在 `OpenAI_StreamHandle` 中维护当前 block 的 role/index 状态。
44. **API 函数错误信息全部丢失** - `openai_client.c` 中 `openai_chat_create` 等函数返回 NULL 时无法区分错误类型。已修复，添加 `last_error` 字段和 `openai_client_get_last_error()` API。
45. **lwIP 环形缓冲区逐字节复制** - `openai_http_lwip.c` 中 `altcp_recv_cb` 使用逐字节循环复制 pbuf 数据。已修复，改用 `memcpy` 批量复制。
46. **openai_strcasestr 每次比较调用 tolower** - `openai_http_lwip.c` 中 `openai_strcasestr` 对每个字符调用 `tolower()`。已修复，改用 ASCII 位运算。
47. **#ifdef LWIP_ALTCP 冗余条件守卫** - `openai_http_lwip.c` 中 `altcp_read` 函数内部对 `sys_check_timeouts()` 的调用有冗余的 `#ifdef LWIP_ALTCP` 守卫。已修复，移除冗余守卫。
48. **memory_chunk 与 OpenAI_StreamBuffer 重复定义** - `openai_http_curl.c` 中两个结构体字段完全相同。已修复，统一为 `memory_chunk`。
49. **openai_json_get_array_item 已被迭代器 API 取代** - `openai_json.c` 中该函数从未被调用。已删除函数定义和头文件声明。
50. **OpenAI_Message.name 字段从未使用** - `openai_types.h` 中 `name` 字段在所有源文件中从未被读取或写入。已删除该字段。
51. **openai_json_parse 中冗余的空白跳过逻辑** - `openai_json.c` 中使用手动字符比较而非调用 `openai_json_skip_space` 函数。已修复，改用 `openai_json_skip_space`。
52. **lwIP 流式后端 Content-Length 依赖导致 SSE 响应为空（Critical）** - `openai_http_lwip.c` 中 `openai_http_request_stream()` 的流式路径盲目复制了非流式路径的 Content-Length 读取逻辑。SSE 流式响应使用 `Transfer-Encoding: chunked`，不包含 `Content-Length` 头，导致代码在解析到 `\r\n\r\n` 后立即 `break`，返回空响应体。已修复，重写整个 lwIP 后端，提取 `recv_response()` 函数，流式模式下不依赖 Content-Length，持续读取直到 EOF。
53. **lwIP 后端 ~800 行重复代码（Critical）** - `openai_http_lwip.c` 中 `openai_http_request()` 和 `openai_http_request_stream()` 几乎完全相同（连接建立、头解析、响应读取），仅 Accept 头不同。已修复，合并为单一 `do_http_request(req, is_stream)` 函数。
54. **curl 后端 ~200 行重复代码** - `openai_http_curl.c` 中两个请求函数高度重复。已修复，合并为单一 `do_curl_request(req, is_stream)` 函数。
55. **Anthropic tool_use 缺少 input 字段** - `openai_client.c` 中 `parse_anthropic_response()` 的 tool_use 块只序列化 `id` 和 `name`，丢弃 `input` 对象（工具调用参数）。已修复，添加 `openai_json_serialize()` 函数，序列化 input JSON 子树。
56. **OpenAI 非流式响应忽略 tool_calls** - `openai_client.c` 中 OpenAI 响应解析只提取 `message.content` 和 `message.role`，忽略 `message.tool_calls` 数组。已修复，添加 tool_calls 解析，序列化为 JSON 拼接到 content。
57. **Anthropic 流式 role 从错误事件提取** - `openai_client.c` 中代码从 `content_block_start` 事件提取 role，但 Anthropic API 的 role 仅在 `message_start` 事件中。已修复，添加 `message_start` 事件处理。
58. **示例代码内存泄漏** - `chat_example.c` 使用 `free(event.content)` 而非 `openai_stream_event_free()`，导致 `event.role` 和 `event.stop_reason` 泄漏。`anthropic_example.c` 中 `openai_stream_event_free()` 仅在 `event.content` 非空时调用，导致纯 `stop_reason` 事件泄漏。已修复。
59. **altcp_read 逐字节复制未落地** - AGENTS.md 第 45 条声称 `altcp_read` 已改用 `memcpy`，但代码中仍是逐字节循环。已在 lwIP 后端重写中修复，改用两段 `memcpy`。
60. **HTTP 状态码映射重复 3 次** - `openai_client.c` 中 `openai_chat_create`、`openai_chat_create_stream`、`openai_embeddings_create` 三处完全相同的 401/429/500+ 映射逻辑。已修复，提取为 `set_http_error()` 辅助函数。
61. **non_system_count 未使用变量** - `openai_client.c` 中 `build_anthropic_request_body` 的 `non_system_count` 变量被计算但从未读取。已删除。
62. **StreamHandle.client 字段从未使用** - `openai_client.c` 中 `OpenAI_StreamHandle` 的 `client` 字段从未赋值或读取。已删除。
63. **usage 字符串解析死代码分支** - `openai_client.c` 中 OpenAI usage 解析先尝试 `openai_json_get_string`（永远返回 NULL），再用 `openai_json_get_number`。已修复，删除死代码分支。
64. **TODO 注释已过时** - `openai_http_curl.c` 和 `openai_http_lwip.c` 中的 TODO 注释声称 HTTP 状态码映射未实现，但已在 `openai_client.c` 中实现。已在后端重写中清除。
65. **Anthropic Extended Thinking 支持** - 新增 `thinking_enabled` 和 `thinking_budget` 请求参数，支持 Anthropic Extended Thinking 功能。响应中 `OpenAI_Choice.thinking` 字段包含 thinking 内容，流式事件中 `OpenAI_StreamEvent.thinking` 字段包含 thinking delta。同时修复非流式响应未提取 `stop_reason` 的问题。
66. **畸形 Unicode escape 导致堆越界写** - `openai_json_parse_string()` 的长度计算与复制逻辑对不完整 `\uXXXX` 处理不一致。已统一两遍扫描规则，严格验证四位十六进制字符并安全拒绝畸形输入。
67. **lwIP HTTPS 在握手完成前发送请求** - ALTCP 连接使用空 callback 并立即调用 `altcp_write()`。已注册连接和错误 callback，等待 TCP/TLS ready 后再发送请求。
68. **Streaming 先缓冲完整响应** - curl 和 lwIP 后端此前等待完整响应后才创建 stream handle。已新增增量 HTTP stream 接口，按需读取 SSE，传输缓冲区有界，单条未完成 SSE 行限制为 64 KiB。
69. **lwIP 接收 busy-spin** - RTOS 配置下等待数据时没有让出执行权。已在连接和接收等待循环中加入 `sys_msleep(1)`，同时保留超时与 `sys_check_timeouts()` 驱动。
70. **snprintf 负返回值未检查（MEDIUM）** - `build_chat_request_body` 和 `build_anthropic_request_body` 中 `snprintf` 返回值未检查，编码错误时返回 -1 导致 `offset` 变为负值，后续 `snprintf(buf + offset, ...)` 向缓冲区前部越界写入。已修复，对所有 `snprintf` 调用检查返回值，为负时释放已分配资源并返回 NULL。
71. **calloc 失败时已分配字段泄漏（MEDIUM）** - `parse_anthropic_response` 中按顺序分配 `id`/`model`/`object`/`choices`，`calloc(choices)` 失败时前三个字段已分配但无法释放。已修复，在 `calloc` 失败路径清理所有已分配字段并将 `choice_count` 置 0。
72. **tool_call/tool_use 流式事件类型常量** - 新增 4 个流式事件类型常量（`OPENAI_EVENT_TOOL_CALL_DELTA`、`OPENAI_EVENT_TOOL_USE_START`、`OPENAI_EVENT_TOOL_USE_DELTA`、`OPENAI_EVENT_TOOL_USE_STOP`），支持 OpenAI tool_calls delta 和 Anthropic tool_use 三阶段事件（start/delta/stop）。已定义于 `openai_types.h`。
73. **JSON 序列化深度限制** - `serialize_node()` 新增 `depth` 参数，超过 `OPENAI_JSON_MAX_DEPTH`（默认 64）时返回 -1，防止恶意深度嵌套 JSON 导致栈溢出。已定义于 `openai_json.h` 为可覆写宏。
74. **SSE 解析改用 JSON DOM** - `parse_sse_line()` 完全重写为基于 JSON DOM 的解析，提取 `finish_reason`、`role`、`choice_index` 和 `tool_calls` 数组，取代此前的手动字符串扫描。OpenAI 非流式响应现在也提取 `message.tool_calls` 并序列化为 JSON 拼接到 content。
75. **lwIP HTTP 头截断检测和 SIZE_MAX 溢出保护** - `build_http_header()` 改为两遍 `snprintf`，检测返回值与所需大小，SIZE_MAX 溢出检查保护缓冲区大小计算。`recv_response()` 在缓冲区倍增前检查 `SIZE_MAX/2` 防止整数溢出，内部循环增加 `realloc` NULL 检查。
76. **Anthropic realloc SIZE_MAX 溢出保护** - `build_anthropic_request_body()` 的 system_content 累积在 `realloc` 前检查 SIZE_MAX 溢出，防止超长 system prompt 导致整数回绕。所有 `snprintf` 返回值统一检查负编码错误。
