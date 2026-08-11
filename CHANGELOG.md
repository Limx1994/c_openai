# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.2] - 2026-08-11

### Fixed

#### CRITICAL (2 issues)
- **double-free in parse_anthropic_response**: Fixed double-free when `content_cleanup` label fell through to `parse_cleanup`, causing `openai_chat_response_free()` to free already-released `content` pointer.
- **use-after-free in DELTA_UNKNOWN**: Removed `DELTA_UNKNOWN` return value and its fall-through path in `parse_anthropic_delta_from_dom`. All unknown delta types now return `OPENAI_ERR_PARSE` uniformly.

#### HIGH (4 issues)
- **Memory leak in stream_fill_buffer failure path**: Added `free(line_buffer)` before returning on non-EOF errors.
- **Memory leak in DELTA_CONSUMED path**: Added `free(line_buffer)` to prevent leak when empty deltas are consumed.
- **s_tls_config leak in connect_error path**: Added `altcp_tls_free_config()` cleanup in lwIP `connect_error` label when `gethostbyname` fails.
- **Stream read int truncation**: `openai_http_stream_read()` now clamps returned byte count to `INT_MAX` in both curl and lwIP backends to prevent `size_t`-to-`int` truncation.

#### MEDIUM (6 issues)
- **Unreachable code in parse_sse_line**: Fixed missing `parse_pos` advancement when only `finish_reason` is present.
- **build_http_header double snprintf**: Replaced second `snprintf` with `memcpy` after realloc for better performance.
- **build_chat_request_body repeated snprintf**: Replaced intermediate buffer + `strlen` + `strcpy` pattern with direct `snprintf` to main buffer for simple parameters.
- **system_content O(n^2) realloc**: Changed linear growth strategy to doubling growth in `build_anthropic_request_body`.
- **openai_json_serialize redundant strlen**: Added `openai_json_escape_string_with_length()` returning escaped length via out parameter.
- **parse_sse_line full DOM parsing**: Added fast-path field extraction for OpenAI streaming (skips DOM build for known fields).

#### LOW (11 issues)
- **Removed find_system_gcc()**: Deleted unused function from build.py.
- **Removed OPENAI_ERR_BUFFER_EMPTY**: Deleted deprecated error code; use `OPENAI_ERR_TIMEOUT` instead.
- **Removed Openai_JSONNode typedef**: Deleted backward-compatibility typedef (unused).
- **Removed openai_http_request_stream**: Deleted unused HTTP stream function declaration and both backend implementations.
- **reset_mock redundant initialization**: Removed redundant initialization of already-initialized static variables.
- **validate_anthropic_request**: Added missing `messages` non-empty validation.
- **openai_http_parse_url**: Replaced unsafe `atoi` with `strtol` and added error checking for port parsing.
- **thinking snprintf return check**: Added return value validation for `snprintf` in thinking parameter serialization.
- **build.py --jobs minimum**: Kept default at 10 (minimal change; only removed unused find_system_gcc).
- **Fixed tool_use delta reuse**: Replaced `strdup(part_json)` with `openai_json_serialize` to avoid redundant allocation.
- **Fixed extra_headers dedup**: Removed duplicate `Content-Type` header when `extra_headers` contains it.
- **Fixed URL fragment handling**: Stripped fragment from URL before parsing in lwIP backend.
- **Fixed send_all_tcp types**: Corrected parameter types in lwIP `send_all_tcp` to `const void*`.
- **Fixed misleading comments**: Updated comments in `parse_anthropic_response` and `stream_fill_buffer`.
- **Fixed snprintf checks**: Added return value checks for all `snprintf` calls in request body builders.

### Security
- Added `escape_for_snprintf()` and `has_crlf()` functions in lwIP backend for HTTP header injection prevention.
- Both backends now validate `extra_headers` for bare CRLF before appending to request headers.

## [0.0.1] - 2026-08-01

- Initial release
- OpenAI Chat Completions + Streaming + Embeddings
- Anthropic Messages API + Streaming
- Dual HTTP backends (libcurl + lwIP)
- Custom JSON parser with DOM-style parsing
- Security: JSON injection prevention, memory safety, secure auth headers
