# C-OpenAI 项目开发规范

## 项目背景

用户希望在本地目录创建 Python openai 包的 C 语言实现版本，同时支持 Anthropic Messages API：

- **HTTP 库**：libcurl（跨平台，功能完善）+ lwIP（嵌入式）
- **内存管理**：动态分配模式，调用方通过对应 free API 释放资源
- **功能范围**：核心功能集（Chat Completions + Streaming + Embeddings）
- **API 提供商**：OpenAI（默认）+ Anthropic（通过 `openai_client_set_provider` 切换）
- **文档要求**：维护现有简体中文文档，不恢复已删除的 README_zh.md，不新增重复文档

## 项目结构

```text
c_openai/
├── include/
│   ├── openai.h           # 主 API，OpenAI_Client 不透明句柄
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
├── README.md              # 中文使用说明
├── PORTING_GUIDE_zh.md     # 嵌入式移植指南
├── CHANGELOG.md            # 发布记录
└── CLAUDE.md              # 项目规范
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
- 集成参考：STM32、ESP32 等带真实 RTOS/lwIP 系统层的平台；当前 Socket 后端不支持 NO_SYS=1 裸机模式
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

## API 接口设计

当前版本为 v0.0.3。同一时刻只允许一个客户端实例，重复创建返回 NULL；全局状态不支持并发调用。切换提供商保留自定义 base_url，传 NULL 才恢复默认 URL。自定义 URL 不提供 Azure 专用协议适配。

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
```

日志通过编译宏控制：

```c
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
| ------ | -------- | ----------- |
| 认证 | `Authorization: Bearer {key}` | `x-api-key: {key}` + `anthropic-version` |
| 端点 | `/v1/chat/completions` | `/v1/messages` |
| system prompt | messages 中 `role: "system"` | 顶层 `"system"` 字段 |
| max_tokens | 可选 | 必须大于 0，无自动默认值 |
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

- **README.md**：简体中文使用说明。
- **PORTING_GUIDE_zh.md**：嵌入式集成步骤与验证边界。
- **CHANGELOG.md**：版本记录，保留历史日期，仅记录已核实变更。
- **CLAUDE.md** 与 **.claude/workflows/REVIEW_PLAN.md**：开发约束与审查流程。
- 修改前阅读目标文件和直接相关调用、类型、配置，搜索已有实现；只做任务必需的最小修改。

## 许可证

本项目采用 **PolyForm Noncommercial License 1.0.0 及 LICENSE 中的附加条款**，包含非商业限制和黑名单禁用要求。完整条款见 [LICENSE](LICENSE)，不得在文档中简化为标准 PolyForm 授权或继续使用旧 CC BY-NC 说明。

第三方库继续遵循各自许可证，不随本项目许可证替换而改变。

## 验证方法

优先使用 PowerShell 和现有脚本。修改问题前检查项目日志，执行前定义成功标准；构建支持并行时使用 `--parallel 12`。`build.py` 的默认并行数仍为 10，需要 12 时显式传入 `--jobs 12`。

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DOPENAI_BUILD_TESTS=ON
cmake --build build --parallel 12
ctest --test-dir build --output-on-failure
```

文档检查复用 `.markdownlint-cli2.jsonc`，明确指定维护文档，另运行 `git diff --check`。按配置分别验证 curl、lwIP 无 TLS 和 BOTH + TLS，并构建示例。

2026-10-10 的六项修复日志显示：curl CTest 1/1、lwIP 无 TLS 3/3、BOTH + TLS 1/1 通过；示例链接、本机 Anthropic 普通/流式请求、TLS 接收模拟及指南配置编译通过。修改后必须重新验证，旧日志不能代替新结果。复验入口及日志见本地 `.review/FIX_REPORT.md`（审查产物，不纳入 release）。

真实远端 API、ARM 工具链和目标板运行未验证；第三方全库人工深审未完成。输出结果必须区分通过、失败、未执行；不将静态库编译或模拟测试表述为板上运行成功。

## 已知限制

1. 单条未完成 SSE 行最大为 64 KiB
2. lwIP 后端不支持并发请求
3. 静态缓冲区内存模式尚未实现
4. 仅允许单客户端，客户端生命周期计数不是线程安全的
5. Anthropic 开启 thinking 时预算必须大于 0 且小于 max_tokens；未设置正数预算时使用 max_tokens / 2
