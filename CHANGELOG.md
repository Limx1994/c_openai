# 发布记录

记录本项目可核实的版本变更。格式参考 [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)，版本采用 [Semantic Versioning](https://semver.org/spec/v2.0.0.html)。历史条目保留原发布日期；它们不是本版本全库审查或运行验证的证明。

## [0.0.3] - 2026-10-10

### 修复

- OpenAI 请求构造在写入顶层可选参数前闭合 messages 数组，覆盖普通与流式请求。
- curl 和 lwIP 接受单个附加头末尾的 CRLF，修复 Anthropic 版本头被拒绝；仍拒绝嵌入 CRLF。
- lwIP TLS 等待及读取刷新接收索引，数据读取完毕后保留 EOF 标记。
- JSON 控制字符的 Unicode 转义使用足够容量，避免六字符转义被截断。
- Anthropic text/thinking 块结束时继续消费事件，不再返回空成功事件；tool 块结束仍交付对应事件。
- 移植指南第 7.1 节复用捆绑 mbedTLS 4.x 的 TLS/X.509 配置，分离 PSA 配置入口，移除不存在的公开检查头引用。

### 文档与版本

- 同步 README、移植指南、开发规范和审查流程，清除已删除文档的结构引用。
- 移植指南第 7.2 节按捆绑 TF-PSA-Crypto 的迁移说明更新熵源回调声明，移除旧配置及头文件示例。
- 明确单客户端、串行使用、Anthropic 正数 max_tokens、自定义 URL 与目标板验证边界。
- CMake 项目版本、公共头文件版本注释与本地 release 标签统一为 v0.0.3；公共 API 签名未变化。
- 文档构建命令使用 12 并行；build.py 默认值保持不变。

### 验证范围

- Windows/MinGW：curl CTest 1/1、lwIP 无 TLS 3/3、BOTH + TLS 1/1；两个 curl 示例构建与链接。
- 本机模拟服务验证 Anthropic 普通/流式请求；TLS 接收采用模拟时钟和到达事件验证；第 7.1 节配置通过公开头文件编译检查。
- 未验证真实远端 API、ARM 工具链、目标板或真实 lwIP TLS 网络；未完成第三方全库人工深审。

## [0.0.2] - 2026-08-11

### 历史修复

#### 历史严重问题

- **parse_anthropic_response 重复释放**：修复 content_cleanup 落入 parse_cleanup 后，openai_chat_response_free() 再次释放 content 的路径。
- **DELTA_UNKNOWN 释放后使用**：移除该返回值及 parse_anthropic_delta_from_dom 中的穿透路径，未知 delta 类型统一返回 OPENAI_ERR_PARSE。

#### 历史高优先级问题

- **stream_fill_buffer 失败路径泄漏**：非 EOF 错误返回前释放 line_buffer。
- **DELTA_CONSUMED 路径泄漏**：消费空 delta 后释放 line_buffer。
- **connect_error 路径 s_tls_config 泄漏**：lwIP 的 gethostbyname 失败时调用 altcp_tls_free_config()。
- **流读取整数截断**：两个后端将 openai_http_stream_read() 返回字节数限制在 INT_MAX 以内。

#### 历史中优先级问题

- **parse_sse_line 分支处理**：仅有 finish_reason 时推进 parse_pos。
- **build_http_header 重复格式化**：realloc 后以 memcpy 替换第二次 snprintf。
- **build_chat_request_body 重复格式化**：简单参数直接写入主缓冲区，移除中间缓冲区、strlen 和 strcpy。
- **system_content 扩容**：build_anthropic_request_body 由线性增长改为倍增。
- **openai_json_serialize 重复计长**：增加 openai_json_escape_string_with_length()，通过输出参数返回长度。
- **parse_sse_line DOM 解析**：为已知 OpenAI 流式字段增加快速提取路径。

#### 历史低优先级问题

- **移除 find_system_gcc()**：删除 build.py 中未使用的函数。
- **移除 OPENAI_ERR_BUFFER_EMPTY**：删除废弃错误码，使用 OPENAI_ERR_TIMEOUT。
- **移除 Openai_JSONNode typedef**：删除未使用的兼容类型别名。
- **移除 openai_http_request_stream**：删除未使用的声明及两个后端实现。
- **reset_mock 冗余初始化**：移除已初始化静态变量的重复初始化。
- **validate_anthropic_request**：补充 messages 非空校验。
- **openai_http_parse_url**：端口解析使用 strtol 替换 atoi，并检查错误。
- **thinking 序列化**：检查 snprintf 返回值。
- **build.py --jobs**：默认值保留为 10，仅删除未使用的 find_system_gcc。
- **tool_use delta 处理**：使用 openai_json_serialize 替换 strdup(part_json)。
- **extra_headers 去重**：附加头包含 Content-Type 时不重复添加。
- **URL fragment 处理**：lwIP 后端解析前移除 fragment。
- **send_all_tcp 参数**：lwIP 后端参数改为 const void*。
- **注释修正**：更新 parse_anthropic_response 和 stream_fill_buffer 注释。
- **snprintf 检查**：请求体构造增加返回值检查。

### 请求头处理

- lwIP 后端增加 escape_for_snprintf() 和 has_crlf() 处理请求头输入。
- 两个后端在添加 extra_headers 前检查换行；末尾 CRLF 的兼容修复见 v0.0.3。

## [0.0.1] - 2026-08-01

- 首次发布。
- OpenAI Chat Completions、Streaming 和 Embeddings。
- Anthropic Messages API 和 Streaming。
- libcurl 和 lwIP 双 HTTP 后端。
- 自定义 DOM 风格 JSON 解析器。
- JSON 字符串转义、内存检查及认证头处理。
