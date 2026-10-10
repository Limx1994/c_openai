# C-OpenAI

一个跨平台的 OpenAI/Anthropic API C 语言客户端，专为 MCU（微控制器）和 POSIX 系统设计。

当前版本：**v0.0.3**。发布记录见 [CHANGELOG.md](CHANGELOG.md)，嵌入式集成见 [移植指南](PORTING_GUIDE_zh.md)。

## 特性

- **跨平台设计**：提供 PC 和嵌入式后端；本版本验证环境为 Windows/MinGW，其他平台需在目标环境验证
- **双 HTTP 后端**：
  - `libcurl`：功能完整，适用于 PC 和服务器
  - `lwIP`：轻量级，适用于资源受限的 MCU（STM32、ESP32 等）
- **多提供商支持**：
  - OpenAI API（Chat Completions、Streaming、Embeddings）
  - Anthropic Messages API（Chat、Streaming）
- **动态内存**：使用堆分配，并提供明确的资源释放 API
- **输入处理**：JSON 字符串转义、请求头校验和缓冲区大小检查

## 项目结构

```text
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
│   ├── lwip_port/        # 最小 lwIP 平台适配头文件（编译检查用）
│   └── CMakeLists.txt    # 第三方库构建配置
├── example/              # 示例代码
│   ├── chat_example.c    # OpenAI Chat Completions
│   ├── anthropic_example.c  # Anthropic Messages API
│   └── CMakeLists.txt    # 示例构建配置
├── build.py              # 一键编译脚本（支持 lwIP/curl 后端）
├── CMakeLists.txt        # CMake 构建配置
├── README.md             # 中文使用说明
├── PORTING_GUIDE_zh.md   # 嵌入式移植指南
├── CHANGELOG.md          # 发布历史
└── CLAUDE.md             # 项目规范
```

## 构建

### 前置条件

- CMake 3.18 或更高
- Git（支持子模块）
- 所有依赖库均通过 git submodule 集成（libcurl、lwIP、mbedtls）

### 一键编译（推荐）

```bash
# 编译 lwIP 嵌入式后端（自动检测 STM32CubeIDE 中的 ARM 工具链）
python build.py --backend lwip

# 编译全部后端
python build.py

# 清理构建目录
python build.py --clean

# 详细模式（显示编译命令）
python build.py -v

# 自定义参数
python build.py --backend lwip --timeout 60 --log-level 3 --no-tls
```

### 使用 libcurl 构建（用于 PC/服务器）

```bash
cmake -S . -B build
cmake --build build --parallel 12
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

该脚本只构建 `libopenai_lwip.a`。目标工程仍须提供真实的 RTOS/lwIP/mbedTLS
平台层和网络驱动；未提供平台链接输入时，脚本会明确报告最终 firmware 链接未验证。

或使用 CMake：

```bash
cmake -S . -B build -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP
cmake --build build --parallel 12
```

**注意**：ARM Cortex-M 平台需要在 `third_party/mbedtls/tf-psa-crypto/include/psa/crypto_config.h` 中禁用 `MBEDTLS_HAVE_ASM`（寄存器不足）。

### 构建选项

```bash
# libcurl 后端（默认）
cmake -S . -B build
cmake --build build --parallel 12

# lwIP 后端
cmake -S . -B build -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP
cmake --build build --parallel 12

# 同时编译两个后端
cmake -S . -B build -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_BOTH
cmake --build build --parallel 12

# 编译示例程序
cmake -S . -B build -DOPENAI_BUILD_EXAMPLES=ON
cmake --build build --parallel 12
```

## 配置

编辑 `include/openai_config.h` 进行自定义：

```c
#define OPENAI_TIMEOUT 30          // HTTP 超时秒数
#define OPENAI_USE_TLS 1           // lwIP HTTPS（需要 mbedTLS）
```

日志通过编译宏控制：

```c
#define OPENAI_LOG_ENABLED 1       // 0=关闭所有日志（嵌入式建议设为 0）
#define OPENAI_LOG_LEVEL 3         // 0=error, 1=warn, 2=info, 3=debug
```

## 使用方法

```c
#include "openai.h"

// 使用 API key 创建客户端
OpenAI_Client* client = openai_client_new("sk-...");

// 准备聊天请求
OpenAI_ChatRequest req = {0};
req.model = "gpt-4o-mini";
req.messages = malloc(sizeof(OpenAI_Message) * 2);
req.messages[0].role = "system";
req.messages[0].content = "你是一个有帮助的助手。";
req.messages[1].role = "user";
req.messages[1].content = "你好！";
req.message_count = 2;
req.temperature = 0.7f;  // 0.0-2.0，-1.0 表示使用 API 默认值

// 发送请求并获取响应
OpenAI_ChatResponse* resp = openai_chat_create(client, &req);

if (resp && resp->choice_count > 0 && resp->choices[0].content) {
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
req.model = "claude-sonnet-4-20250514";
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

### Anthropic Extended Thinking（扩展思考）

```c
#include "openai.h"

OpenAI_Client* client = openai_client_new("sk-ant-...");
openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC);

OpenAI_ChatRequest req = {0};
req.model = "claude-sonnet-4-20250514";
req.messages = malloc(sizeof(OpenAI_Message) * 1);
req.messages[0].role = "user";
req.messages[0].content = "解决这个复杂的数学问题...";
req.message_count = 1;
req.max_tokens = 16000;
req.thinking_enabled = 1;     // 启用 Extended Thinking
req.thinking_budget = 10000;  // thinking token 预算

OpenAI_ChatResponse* resp = openai_chat_create(client, &req);
if (resp && resp->choice_count > 0) {
    // 访问 thinking 内容
    if (resp->choices[0].thinking) {
        printf("思考过程: %s\n", resp->choices[0].thinking);
    }
    // 访问最终答案
    if (resp->choices[0].content) {
        printf("答案: %s\n", resp->choices[0].content);
    }
}
openai_chat_response_free(resp);
free(req.messages);
openai_client_free(client);
```

### 环境变量

通过环境变量设置 API key：

```powershell
$env:OPENAI_API_KEY = "<从现有凭证管理取得的值>"
$env:ANTHROPIC_API_KEY = "<从现有凭证管理取得的值>"
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

用于兼容当前请求路径与认证方式的代理服务器或自定义端点。此接口仅修改基础 URL，不提供 Azure 专用认证、部署路径或 API 版本参数适配：

```c
OpenAI_Client* client = openai_client_new("sk-your-key-here");
openai_client_set_base_url(client, "https://your-proxy.com/v1");
// 所有后续 API 调用将使用此基础 URL
```

## API 参考

### 客户端生命周期

| 函数 | 描述 |
| ------ | ------ |
| `openai_client_new(api_key)` | 创建客户端；已有实例未释放时返回 NULL |
| `openai_client_free(client)` | 释放客户端和资源 |
| `openai_client_set_base_url(client, url)` | 设置兼容端点的基础 URL；传 NULL 恢复提供商默认值 |
| `openai_client_set_provider(client, provider)` | 设置 API 提供商（OPENAI_PROVIDER_OPENAI 或 OPENAI_PROVIDER_ANTHROPIC） |
| `openai_client_get_last_error(client)` | 获取最后一次错误代码（用于区分 NULL 返回的具体原因） |

### 聊天补全

客户端计数和 lwIP 后端状态使用全局变量，调用方须串行使用，先关闭流、释放响应，再释放客户端。切换提供商不会清除自定义基础 URL；需要恢复默认值时调用 `openai_client_set_base_url(client, NULL)`。

Anthropic 请求必须提供非空 model、非空 messages 和正数 `max_tokens`，否则返回参数错误。开启 thinking 时，有效预算必须大于 0 且小于 `max_tokens`；未提供正数预算时使用 `max_tokens / 2`。

| 函数 | 描述 |
| ------ | ------ |
| `openai_chat_create(client, req)` | 发送聊天请求，返回响应 |
| `openai_chat_response_free(resp)` | 释放响应 |

### Embeddings

| 函数 | 描述 |
| ------ | ------ |
| `openai_embeddings_create(client, req)` | 获取文本的嵌入向量 |
| `openai_embedding_response_free(resp)` | 释放响应 |

### 流式响应

| 函数 | 描述 |
| ------ | ------ |
| `openai_chat_create_stream(client, req)` | 创建流式聊天请求 |
| `openai_stream_read(stream, event)` | 从流中读取下一个事件 |
| `openai_stream_event_free(event)` | 释放事件的 content/role/stop_reason/thinking/signature/usage 字段 |
| `openai_stream_close(stream)` | 关闭流并释放资源 |

`event_type` 字段用于区分流式事件类型：

| 常量 | 含义 |
| ------ | ------ |
| `OPENAI_EVENT_CHUNK` | 文本 delta（OpenAI 和 Anthropic 通用） |
| `OPENAI_EVENT_TOOL_CALL_DELTA` | OpenAI `tool_calls` delta |
| `OPENAI_EVENT_TOOL_USE_START` | Anthropic `tool_use` 块开始 |
| `OPENAI_EVENT_TOOL_USE_DELTA` | Anthropic `tool_use` delta |
| `OPENAI_EVENT_TOOL_USE_STOP` | Anthropic `tool_use` 块结束 |
| `OPENAI_EVENT_THINKING_SIG` | Anthropic Extended Thinking signature delta |

检查 `event->event_type` 来区分文本 delta、OpenAI tool_call delta 和 Anthropic tool_use 事件。`content` 对 tool_use 块保存 `partial_json`。

流式响应会增量交付。`openai_chat_create_stream()` 在收到响应头后返回，`openai_stream_read()` 等待并解析下一条 SSE 事件。传输层使用 `OPENAI_TIMEOUT` 作为空闲超时，单条未完成 SSE 行限制为 64 KiB。

### JSON 工具

| 函数 | 描述 |
| ------ | ------ |
| `openai_json_parse(json_string)` | 解析 JSON 字符串为 DOM |
| `openai_json_free(node)` | 释放 JSON DOM |
| `openai_json_escape_string(str)` | 转义 JSON 字符串（防止注入） |
| `openai_json_serialize(node)` | JSON 节点树序列化为字符串（调用方 free） |
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
- `OPENAI_ERR_EOF` - 流结束（流式响应）
- `OPENAI_ERR_UNKNOWN` - 未知错误

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

## 输入处理与运行限制

- JSON 字符串转义覆盖引号、反斜杠和控制字符；v0.0.3 修复了 Unicode 控制字符转义截断。
- 两个 HTTP 后端允许单个附加头以 CRLF 结尾，拒绝嵌入 CRLF；curl 在加入头列表前移除结尾 CRLF。
- 流读取字节数限制在 `INT_MAX` 以内，单条未完成 SSE 行限制为 64 KiB；超时通过错误返回报告。
- 静态缓冲区内存模式尚未实现；请求失败不会自动重试。
- lwIP 需要目标工程提供真实 RTOS、网络驱动和 TLS 集成；构建静态库不等于目标板可运行。

## 验证

使用项目现有 CMake 测试；以下为 Windows/MinGW 的 curl 配置示例：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DOPENAI_BUILD_TESTS=ON
cmake --build build --parallel 12
ctest --test-dir build --output-on-failure
```

v0.0.3 的复验范围包括 curl 核心测试、lwIP 无 TLS 测试、BOTH + TLS 构建测试、示例链接，以及本机模拟服务的 Anthropic 普通/流式请求。TLS 接收使用模拟到达事件验证，移植指南配置使用公开头文件编译验证。未验证真实远端 API、ARM 工具链或目标板运行，也未完成第三方全库人工审查。

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
cmake -S . -B build -G "MSYS Makefiles"
cmake --build build --parallel 12
```

### STM32 / 嵌入式

1. 将 lwIP 添加到项目中
2. 配置 `OPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP`
3. 如需 HTTPS 支持，在 `lwipopts.h` 中启用 ALTCP + mbedTLS：

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
<https://creativecommons.org/licenses/by-nc/4.0/deed.zh-hans>
