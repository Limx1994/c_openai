# C-OpenAI 嵌入式移植指南

本文档面向需要将 C-OpenAI v0.0.3 移植到嵌入式平台（STM32、ESP32、RP2040 等 MCU）的开发者。

验证边界：Windows/MinGW 下的静态库构建、模拟传输测试和第 7.1 节公开头文件配置编译已验证；ARM 工具链、目标板运行及真实 lwIP TLS 网络未验证。平台示例需要结合目标 SDK 调整。

---

## 目录

1. [项目概览](#1-项目概览)
2. [依赖分析](#2-依赖分析)
3. [移植前准备](#3-移植前准备)
4. [步骤一：初始化子模块](#4-步骤一初始化子模块)
5. [步骤二：配置 lwIP（lwipopts.h）](#5-步骤二配置-lwiplwipoptsh)
6. [步骤三：配置平台适配层](#6-步骤三配置平台适配层)
7. [步骤四：配置 mbedTLS](#7-步骤四配置-mbedtls)
8. [步骤五：配置 openai_config.h](#8-步骤五配置-openai_configh)
9. [步骤六：集成到项目构建系统](#9-步骤六集成到项目构建系统)
10. [步骤七：网络初始化](#10-步骤七网络初始化)
11. [步骤八：使用 API](#11-步骤八使用-api)
12. [不同平台移植参考](#12-不同平台移植参考)
13. [常见问题排查](#13-常见问题排查)
14. [内存需求估算](#14-内存需求估算)
15. [已知限制](#15-已知限制)

---

## 1. 项目概览

C-OpenAI 是一个 C 语言实现的 OpenAI/Anthropic API 客户端库，支持以下功能：

- **Chat Completions** — 同步聊天请求
- **Streaming** — 增量读取并解析 SSE 事件流
- **Embeddings** — 向量嵌入请求
- **双后端** — libcurl（PC/服务器）和 lwIP（嵌入式）
- **双 Provider** — OpenAI 和 Anthropic

嵌入式平台使用 **lwIP 后端**，通过 TCP Socket + ALTCP/mbedTLS 实现 HTTP/HTTPS 请求。

### 架构简图

```text
┌─────────────────────────────────────────┐
│            你的应用代码                    │
│         (chat_example.c 等)              │
├─────────────────────────────────────────┤
│            openai.h (公共 API)            │
├──────────┬──────────┬───────────────────┤
│ 客户端    │ JSON 解析  │ HTTP 后端          │
│ openai_  │ openai_   │ openai_http_      │
│ client.c │ json.c    │ lwip.c            │
├──────────┴──────────┴───────────────────┤
│          lwIP TCP/IP 协议栈               │
│        (third_party/lwip)                │
├─────────────────────────────────────────┤
│          mbedTLS 加密库                   │
│        (third_party/mbedtls)             │
├─────────────────────────────────────────┤
│        你的网络驱动 / 硬件                  │
│   (以太网/WiFi/LTE + MAC/PHY)            │
└─────────────────────────────────────────┘
```

> **流式支持说明**：`openai_chat_create_stream()` 在响应头到达后返回，`openai_stream_read()` 按需驱动后端并增量解析 SSE 事件。传输缓冲区有固定上限，单条未完成 SSE 行最大为 64 KiB。

---

## 2. 依赖分析

### 必需依赖

| 组件 | 来源 | 用途 | 大小估算 |
| ------- | --------------------------- | ------------------------------------------------------------------------------------------- | ----------- |
| lwIP | `third_party/lwip` (子模块) | TCP/IP 协议栈、Socket API、DNS | ~100-200 KB |
| mbedTLS | `third_party/mbedtls` (子模块) | TLS/SSL 加密（HTTPS） | ~200-400 KB |
| 标准库 | 你的工具链提供 | `malloc`, `calloc`, `realloc`, `free`, `snprintf`, `strtod`, `memset`, `memcpy`, `strcmp` 等 | — |

### 不需要的依赖

- **libcurl** — 嵌入式不使用此后端
- **JSON 库** — 项目自带手写 JSON 解析器（`openai_json.c`），零外部依赖

### 标准库依赖清单

嵌入式环境需要提供以下标准库函数：

```c
// 内存管理
void* malloc(size_t size);
void* calloc(size_t nmemb, size_t size);
void* realloc(void *ptr, size_t size);
void  free(void *ptr);

// 字符串操作
size_t strlen(const char *s);
char*  strcpy(char *dst, const char *src);
char*  strncpy(char *dst, const char *src, size_t n);
int    strcmp(const char *s1, const char *s2);
char*  strstr(const char *haystack, const char *needle);

// 格式化输出
int snprintf(char *str, size_t size, const char *format, ...);

// 字符处理
int isspace(int c);
int isdigit(int c);

// 数学
double strtod(const char *str, char **endptr);
```

> **提示**：lwIP 后端使用内部的 `openai_strcasestr()`，不依赖平台提供 `strcasestr`。

---

## 3. 移植前准备

确认以下条件：

1. **C 编译器** — 支持 C11 标准（`-std=gnu11` 或 `-std=c11`）
2. **网络硬件** — 以太网、WiFi 或蜂窝模块已可工作（ lwIP 需要底层网络驱动）
3. **lwIP 已集成** — 如果你的项目已使用 lwIP，可复用现有配置；否则需按本指南配置
4. **mbedTLS 已集成**（可选）— 如需 HTTPS 支持；如仅用 HTTP 可跳过
5. **堆内存** — 按 60-100 KB 可用堆作初步规划，实际需求须测量（详见[内存需求估算](#14-内存需求估算)）

---

## 4. 步骤一：初始化子模块

```bash
cd c_openai
git submodule update --init --recursive
```

这会拉取三个子模块：

- `third_party/lwip` — lwIP 协议栈
- `third_party/mbedtls` — mbedTLS 加密库
- `third_party/libcurl` — 仅 PC 后端使用，嵌入式可跳过（不拉取也不影响编译）

确认子模块目录存在：

```text
third_party/lwip/src/core/       ← 核心协议代码
third_party/lwip/src/include/    ← lwIP 头文件
third_party/mbedtls/include/     ← mbedTLS 头文件
third_party/mbedtls/library/     ← mbedTLS 库代码
```

---

## 5. 步骤二：配置 lwIP（lwipopts.h）

`lwipopts.h` 是 lwIP 的核心配置文件，**必须**提供。你需要在项目中创建此文件，或在编译时通过 `-include` 引入。

### 最小配置

```c
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/* ======================== 基础配置 ======================== */
#define NO_SYS 0                    /* 必须：当前 Socket backend 需要 RTOS */
#define LWIP_SOCKET 1               /* 必须启用 Socket API */
#define LWIP_NETCONN 0              /* 可选：Netconn API */
#define LWIP_NETIF_API 0
#define LWIP_PROVIDE_ERRNO 1        /* 提供 errno 定义 */
#define LWIP_TIMEVAL_PRIVATE 1      /* 使用 lwIP 的 timeval */

/* ======================== 协议配置 ======================== */
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 1                  /* 必须：HTTP 基于 TCP */
#define LWIP_UDP 1                  /* 必须：DNS 查询使用 UDP */
#define LWIP_DNS 1                  /* 必须：域名解析 */

/* ======================== 内存配置 ======================== */
/* 总堆大小 — 根据 MCU RAM 调整 */
#define MEM_SIZE (16 * 1024)        /* 16KB 最小，建议 24-32KB */

/* TCP 相关内存池 */
#define MEMP_NUM_PBUF 16
#define MEMP_NUM_TCP_PCB 8          /* 并发 TCP 连接数，单请求=1 足够 */
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_TCP_SEG 16
#define MEMP_NUM_SYS_TIMEOUT 10

/* ======================== TCP 参数 ======================== */
#define TCP_MSS 1460                /* 最大段大小，以太网=1460，WiFi 可能需降低 */
#define TCP_SND_BUF (4 * TCP_MSS)  /* 发送缓冲区 */
#define TCP_SND_QUEUELEN (2 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND (4 * TCP_MSS)      /* 接收窗口 */

/* ======================== TLS/ALTCP 配置（必须）======================== */
#define LWIP_ALTCP 1                /* 启用 ALTCP 抽象层 */
#define LWIP_ALTCP_TLS 1            /* 启用 TLS 支持 */
#define LWIP_ALTCP_TLS_MBEDTLS 1    /* 使用 mbedTLS 作为 TLS 后端 */
#define MEMP_NUM_ALTCP_PCB 4
#define MEMP_NUM_ALTCP_TLS_PCB 4

/* ======================== 硬件校验和（按平台配置）======================== */
/* STM32F4 有硬件校验和，设为 0 关闭软件计算 */
#define CHECKSUM_GEN_IP 0
#define CHECKSUM_GEN_UDP 0
#define CHECKSUM_GEN_TCP 0
#define CHECKSUM_CHECK_IP 0
#define CHECKSUM_CHECK_UDP 0
#define CHECKSUM_CHECK_TCP 0
/* 如果 MCU 无硬件校验和，全部设为 1 */

/* ======================== 调试（可选）======================== */
#define LWIP_STATS 0
#define LWIP_DEBUG 0

#endif /* LWIP_LWIPOPTS_H */
```

### 各配置项说明

| 配置项 | 推荐值 | 说明 |
| ------------------ | ----------- | --------------------------------------------- |
| `NO_SYS` | `0` | **必须**。当前 backend 使用 lwIP Socket API，需要真实 RTOS 系统层 |
| `LWIP_SOCKET` | `1` | **必须**。本库使用 POSIX Socket API |
| `MEM_SIZE` | 16384~32768 | lwIP 总堆内存（字节），取决于并发连接数和缓冲区需求 |
| `MEMP_NUM_TCP_PCB` | `4` | 最大并发 TCP 连接控制块。API 调用为单请求，`1` 即可 |
| `TCP_MSS` | `1460` | 以太网标准值。WiFi 可能需要 `536` 或更小 |
| `TCP_WND` | `4*TCP_MSS` | 接收窗口大小，影响下载速度 |
| `CHECKSUM_*` | `0` 或 `1` | `0`=使用硬件校验和（STM32 等），`1`=软件计算 |

---

## 6. 步骤三：配置平台适配层

需要提供 3 个文件（或在已有 lwIP 配置中复用）：

### 6.1 arch/cc.h — 类型定义

```c
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <errno.h>
#include <stdint.h>
#include <limits.h>

/* 平台整数类型 — 使用 stdint.h */
typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;

/* 结构体打包 */
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define PACK_STRUCT_STRUCT __attribute__((packed))

/* 调试输出（可为空） */
#define LWIP_PLATFORM_DIAG(x)
#define LWIP_PLATFORM_ASSERT(x)

#endif /* LWIP_ARCH_CC_H */
```

> **注意**：如果你的项目已有 lwIP 集成，`arch/cc.h` 通常已经存在，直接复用即可。

### 6.2 arch/sys_arch.h — 系统抽象类型

当前 `openai_http_lwip.c` 使用 Socket API，因此只支持 `NO_SYS=0`。`sys_sem_t`、
`sys_mutex_t`、`sys_mbox_t` 和 `sys_thread_t` 必须映射到真实 RTOS 对象；空指针或
空操作桩不能驱动 lwIP 的 `tcpip_thread`。

#### FreeRTOS 示例

```c
#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

typedef uint32_t sys_prot_t;
typedef SemaphoreHandle_t sys_sem_t;
typedef SemaphoreHandle_t sys_mutex_t;
typedef QueueHandle_t     sys_mbox_t;
typedef TaskHandle_t      sys_thread_t;

#endif
```

### 6.3 sys_arch.c — 系统抽象实现

> **不支持 `NO_SYS=1` 裸机模式**：`NO_SYS=1` 禁用 Socket/Netconn API，不能用于
> 当前 backend。不要用“始终成功”的 semaphore/mailbox 或返回 `NULL` 的
> `sys_thread_new()` 桩函数绕过编译；它们不会创建 `tcpip_thread`，请求会永久阻塞。
> 真正的裸机移植需要另写基于 lwIP raw API 的 HTTP backend。

`NO_SYS=0` 的平台层必须至少提供可工作的临界区、semaphore、mutex、mailbox、
thread 和毫秒时钟。`sys_thread_new()` 必须真正创建任务，等待函数必须遵守 timeout
语义，`sys_now()`/`sys_jiffies()` 必须单调返回毫秒值。

#### FreeRTOS 实现

```c
#include "lwip/sys.h"
#include "arch/cc.h"
#include "arch/sys_arch.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

sys_prot_t sys_arch_protect(void) {
    taskENTER_CRITICAL();
    return 0;
}
void sys_arch_unprotect(sys_prot_t pval) {
    (void)pval;
    taskEXIT_CRITICAL();
}

err_t sys_sem_new(sys_sem_t *sem, u8_t count) {
    *sem = xSemaphoreCreateCounting(0xFFFF, count);
    return (*sem == NULL) ? ERR_MEM : ERR_OK;
}
void sys_sem_free(sys_sem_t *sem) { vSemaphoreDelete(*sem); }
void sys_sem_signal(sys_sem_t *sem) { xSemaphoreGive(*sem); }
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout) {
    TickType_t ticks = (timeout == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    return (xSemaphoreTake(*sem, ticks) == pdTRUE) ? 0 : SYS_ARCH_TIMEOUT;
}

err_t sys_mutex_new(sys_mutex_t *mutex) {
    *mutex = xSemaphoreCreateMutex();
    return (*mutex == NULL) ? ERR_MEM : ERR_OK;
}
void sys_mutex_free(sys_mutex_t *mutex) { vSemaphoreDelete(*mutex); }
void sys_mutex_lock(sys_mutex_t *mutex) { xSemaphoreTake(*mutex, portMAX_DELAY); }
void sys_mutex_unlock(sys_mutex_t *mutex) { xSemaphoreGive(*mutex); }

err_t sys_mbox_new(sys_mbox_t *mbox, int size) {
    *mbox = xQueueCreate(size > 0 ? size : 10, sizeof(void*));
    return (*mbox == NULL) ? ERR_MEM : ERR_OK;
}
void sys_mbox_free(sys_mbox_t *mbox) { vQueueDelete(*mbox); }
void sys_mbox_post(sys_mbox_t *mbox, void *msg) {
    xQueueSend(*mbox, &msg, portMAX_DELAY);
}
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg) {
    return (xQueueSend(*mbox, &msg, 0) == pdTRUE) ? ERR_OK : ERR_MEM;
}
u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout) {
    TickType_t ticks = (timeout == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    return (xQueueReceive(*mbox, msg, ticks) == pdTRUE) ? 0 : SYS_ARCH_TIMEOUT;
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread,
    void *arg, int stacksize, int prio) {
    TaskHandle_t handle;
    xTaskCreate((TaskFunction_t)thread, name, stacksize / sizeof(StackType_t),
                arg, prio, &handle);
    return handle;
}

u32_t sys_jiffies(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }
u32_t sys_now(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }
```

---

## 7. 步骤四：配置 mbedTLS

如果你需要 HTTPS 支持（推荐），需要配置 mbedTLS。

### 7.1 创建 mbedtls_config.h

当前捆绑的 mbedTLS 4.x 将 TLS/X.509 配置与 TF-PSA-Crypto 配置分开。先使用当前依赖提供的默认配置验证集成，再按目标平台裁剪。在你的项目中创建 `mbedtls_config.h`：

```c
#ifndef OPENAI_MBEDTLS_CONFIG_H
#define OPENAI_MBEDTLS_CONFIG_H

/* 复用当前捆绑版本的 TLS/X.509 默认配置。 */
#include "mbedtls/mbedtls_config.h"

#endif /* OPENAI_MBEDTLS_CONFIG_H */
```

编译 mbedTLS、lwIP TLS 适配层和应用时，统一定义 `MBEDTLS_CONFIG_FILE="mbedtls_config.h"`，并将此文件所在目录加入头文件搜索路径。默认配置包含 TLS 客户端、TLS 1.2、证书解析及 `MBEDTLS_SSL_SERVER_NAME_INDICATION`，这是集成起点，不是最小 MCU 配置。

加密算法、随机数及平台内存配置位于 `third_party/mbedtls/tf-psa-crypto/include/psa/crypto_config.h`。需要自定义时，使用 `TF_PSA_CRYPTO_CONFIG_FILE` 指向独立配置，并确保库与应用采用相同配置；下一节的硬件熵源选项也在该配置中启用。头文件搜索路径至少包含 `third_party/mbedtls/include`、`third_party/mbedtls/tf-psa-crypto/include` 和 `third_party/mbedtls/tf-psa-crypto/drivers/builtin/include`。

不要手动包含 `mbedtls/check_config.h`：当前版本不提供此公开头文件。应用应通过 `mbedtls/ssl.h` 等公开头文件加载配置；裁剪后的配置需重新编译库并验证目标平台运行。

### 7.2 提供随机数源

当前 TF-PSA-Crypto 使用 `MBEDTLS_PSA_BUILTIN_GET_ENTROPY` 选择内置系统熵源；需要自定义硬件熵源时，在独立 PSA 配置中启用 `MBEDTLS_PSA_DRIVER_GET_ENTROPY` 并实现下列回调：

```c
#include "mbedtls/platform.h"

int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags,
                                 size_t *estimate_bits,
                                 unsigned char *output, size_t output_size);
```

这是接口声明，具体实现由目标平台提供。当前依赖要求填满 output，成功时将 estimate_bits 设为 `8 * output_size`；不支持的 flags 返回 `PSA_ERROR_NOT_SUPPORTED`，熵不可用时返回 `PSA_ERROR_INSUFFICIENT_ENTROPY`，不能用默认数据掩盖失败。

旧的 `mbedtls_hardware_poll()`、`MBEDTLS_ENTROPY_HARDWARE_ALT` 和 `MBEDTLS_NO_PLATFORM_ENTROPY` 已移除；不要包含已不存在的公开头 `mbedtls/entropy.h`。详细接口与配置限制见捆绑依赖的 [TF-PSA-Crypto 迁移指南](third_party/mbedtls/tf-psa-crypto/docs/1.0-migration-guide.md#custom-entropy-collector)。本项目未验证目标板硬件熵源实现。

---

## 8. 步骤五：配置 openai_config.h

修改 `include/openai_config.h` 以适应你的平台：

### 嵌入式推荐配置

```c
#ifndef OPENAI_CONFIG_H
#define OPENAI_CONFIG_H

/* ======================== 连接设置 ======================== */
#ifndef OPENAI_TIMEOUT
#define OPENAI_TIMEOUT 30          /* HTTP 请求超时（秒）。
                                      嵌入式网络较慢，建议 30-60 */
#endif

/* ======================== TLS 设置（lwIP 后端）======================== */
#ifndef OPENAI_USE_TLS
#define OPENAI_USE_TLS 1           /* 1=HTTPS（推荐）, 0=HTTP（不安全） */
#endif

/* ======================== API Base URL ======================== */
/* 如需使用反向代理或私有端点，在此修改 */
#if OPENAI_USE_TLS
#define OPENAI_API_BASE "https://api.openai.com/v1"
#define ANTHROPIC_API_BASE "https://api.anthropic.com/v1"
#else
#define OPENAI_API_BASE "http://api.openai.com/v1"
#define ANTHROPIC_API_BASE "http://api.anthropic.com/v1"
#endif

/* ======================== 日志（嵌入式建议关闭）======================== */
#ifndef OPENAI_LOG_ENABLED
#define OPENAI_LOG_ENABLED 0       /* 0=关闭所有日志，节省代码空间 */
#endif

#ifndef OPENAI_LOG_LEVEL
#define OPENAI_LOG_LEVEL 0         /* 仅在开启日志时有效：0=error, 1=warn, 2=info, 3=debug */
#endif

#if OPENAI_LOG_ENABLED
#include <stdio.h>
#define OPENAI_LOG_ERROR(fmt, ...) do { if (OPENAI_LOG_LEVEL >= 0) fprintf(stderr, "[OPENAI ERROR] " fmt "\n", ##__VA_ARGS__); } while(0)
#define OPENAI_LOG_WARN(fmt, ...)  do { if (OPENAI_LOG_LEVEL >= 1) fprintf(stderr, "[OPENAI WARN]  " fmt "\n", ##__VA_ARGS__); } while(0)
#define OPENAI_LOG_INFO(fmt, ...)  do { if (OPENAI_LOG_LEVEL >= 2) fprintf(stderr, "[OPENAI INFO]  " fmt "\n", ##__VA_ARGS__); } while(0)
#define OPENAI_LOG_DEBUG(fmt, ...) do { if (OPENAI_LOG_LEVEL >= 3) fprintf(stderr, "[OPENAI DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)
#else
#define OPENAI_LOG_ERROR(fmt, ...) do {} while(0)
#define OPENAI_LOG_WARN(fmt, ...)  do {} while(0)
#define OPENAI_LOG_INFO(fmt, ...)  do {} while(0)
#define OPENAI_LOG_DEBUG(fmt, ...) do {} while(0)
#endif

#endif /* OPENAI_CONFIG_H */
```

> **日志说明**：日志使用 `fprintf(stderr, ...)`。如果你的平台没有 `stderr`，将 `OPENAI_LOG_ENABLED` 设为 `0`。如需重定向日志，可修改宏定义为你的串口输出函数。

---

## 9. 步骤六：集成到项目构建系统

### 方法零：一键编译脚本（最简单）

项目提供 `build.py` 一键编译脚本，自动检测 ARM 工具链和依赖路径：

```bash
# 编译 lwIP 嵌入式后端（自动检测 STM32CubeIDE 中的 ARM GCC）
python build.py --backend lwip

# 编译全部后端
python build.py --jobs 12

# 自定义参数
python build.py --backend lwip --timeout 60 --log-level 3 --no-tls

# 清理构建目录
python build.py --clean
```

默认使用 `MinSizeRel`（`-Os`）并开启 10 个并行 worker；`--jobs` 小于 10 会直接
报错。`all` 模式要求 lwIP 和 curl 两个 backend 都成功，任何 backend 缺少工具链、
依赖或编译失败都会计入失败数并返回非零状态。

输出文件在 `build_output/` 目录：

- `libopenai_lwip.a` — lwIP 嵌入式后端库

### 方法一：独立编译（推荐 — 不链接第三方库）

项目支持独立编译 lwIP 后端静态库（`.a`），可直接合并到你的工程中：

```bash
# 1. 初始化子模块（获取 lwIP/mbedTLS 源码，仅需头文件）
git submodule update --init --recursive

# 2. 配置（指定 lwIP 后端和依赖头文件路径）
cmake -S . -B build -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP -DLWIP_INCLUDE_DIR="third_party/lwip/src/include" -DMBEDTLS_INCLUDE_DIR="third_party/mbedtls"

# 3. 编译
cmake --build build --parallel 12

# 4. 获取输出
#    libopenai_lwip.a  — lwIP 后端（嵌入式）
```

**可覆盖的编译选项：**

```bash
cmake -S . -B build -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP -DOPENAI_LOG_ENABLED=0 -DOPENAI_LOG_LEVEL=0 -DOPENAI_USE_TLS=1 -DOPENAI_TIMEOUT=30
```

`OPENAI_HTTP_BACKEND` 支持三个值：

- `OPENAI_BACKEND_CURL`（默认）— libcurl 后端
- `OPENAI_BACKEND_LWIP` — lwIP 后端
- `OPENAI_BACKEND_BOTH` — 同时编译两个后端

**合并到你的工程：**

```bash
# 使用 ar 合并多个 .a 文件
ar x libopenai_lwip.a              # 解包 openai 目标文件
ar x ../your_project/liblwip.a     # 解包你已有的 lwIP
ar x ../your_project/libmbedtls.a  # 解包你已有的 mbedTLS
ar rcs libcombined.a *.o           # 重新打包
```

或在你的链接命令中同时链接所有 `.a`：

```bash
arm-none-eabi-gcc main.o \
  -L/path/to/openai -lopenai_lwip \
  -L/path/to/your/project -llwip -lmbedtls \
  -lc -lm -lnosys -o firmware.elf
```

### 方法二：直接集成源码到你的 CMake 工程

如果你的项目使用 CMake，可以直接添加源文件：

```cmake
# 你的 CMakeLists.txt
set(OPENAI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/path/to/c_openai")

add_library(openai STATIC
    ${OPENAI_DIR}/src/openai_client.c
    ${OPENAI_DIR}/src/openai_json.c
    ${OPENAI_DIR}/src/openai_http_lwip.c
)
target_include_directories(openai PRIVATE
    ${OPENAI_DIR}/include
    ${LWIP_INCLUDE_DIR}          # 你的 lwIP 头文件路径
    ${MBEDTLS_INCLUDE_DIR}/include
    ${MBEDTLS_INCLUDE_DIR}/tf-psa-crypto/include
    ${MBEDTLS_INCLUDE_DIR}/tf-psa-crypto/drivers/builtin/include
    ${MBEDTLS_INCLUDE_DIR}/library
    ${CMAKE_CURRENT_SOURCE_DIR}/lwip_config  # lwipopts.h 所在目录
    ${CMAKE_CURRENT_SOURCE_DIR}/arch         # arch/cc.h 所在目录
)
target_compile_definitions(openai PRIVATE
    OPENAI_USE_LWIP=1
    LWIP_ALTCP=1
    LWIP_ALTCP_TLS=1
    LWIP_ALTCP_TLS_MBEDTLS=1
    OPENAI_LOG_ENABLED=0
    OPENAI_USE_TLS=1
)
target_compile_options(openai PRIVATE -w -std=gnu11)

# 链接到你的 lwIP 和 mbedTLS 目标
target_link_libraries(openai PRIVATE lwip mbedtls)
```

### 方法三：手动编译（Makefile / IDE 工程）

将以下源文件添加到你的工程：

```text
源文件（必须）：
  c_openai/src/openai_client.c
  c_openai/src/openai_json.c
  c_openai/src/openai_http_lwip.c

头文件路径（必须）：
  c_openai/include/
  c_openai/third_party/lwip/src/include/
  c_openai/third_party/mbedtls/include/
  c_openai/third_party/mbedtls/tf-psa-crypto/include/
  c_openai/third_party/mbedtls/tf-psa-crypto/drivers/builtin/include/
  c_openai/third_party/mbedtls/tf-psa-crypto/core/
  c_openai/third_party/mbedtls/library/
  <你的 lwipopts.h 目录>
  <你的 arch 目录>

编译定义（必须）：
  OPENAI_USE_LWIP=1
  LWIP_ALTCP=1
  LWIP_ALTCP_TLS=1
  LWIP_ALTCP_TLS_MBEDTLS=1
  LWIP_IPV4=1
  LWIP_SOCKET=1
  LWIP_TCP=1
  LWIP_UDP=1
  LWIP_DNS=1
  OPENAI_LOG_ENABLED=0

编译选项（推荐）：
  -w -std=gnu11

链接库（必须）：
  libopenai.a
  liblwip.a（你自己编译的 lwIP 库）
  libmbedtls.a（你自己编译的 mbedTLS 库）
  libc.a（标准 C 库）
  libm.a（数学库）
```

### 方法四：使用项目自带的构建脚本（STM32 示例）

项目提供了 `build_lwip.sh`，用于交叉编译本项目的 lwIP backend archive：

```bash
# 查看脚本了解编译细节
cat build_lwip.sh
```

该脚本会：

- 从脚本自身位置解析项目根目录，并允许通过环境变量覆盖 toolchain
- 使用至少 10 个并行任务构建 `libopenai_lwip.a`
- 严格检查项目源文件、固定子模块头文件和 archive 成员
- 仅在调用方提供真实平台 link object 与 response file 时执行最终 ELF 链接验证

脚本不会生成假的 `sys_arch.c`，也不会把 archive 构建成功描述为 firmware 可用。
lwIP、mbedTLS、RTOS port、网络驱动和 linker script 仍由目标工程提供。

---

## 10. 步骤七：网络初始化

**在调用任何 openai API 之前**，你的 lwIP 网络栈必须已经初始化完成：

```c
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/sys.h"
#include "netif/etharp.h"

/* 全局网络接口 */
struct netif gnetif;

static void tcpip_init_done(void *arg)
{
    sys_sem_signal((sys_sem_t *)arg);
}

void network_init(void)
{
    /* 1. 创建并初始化 lwIP tcpip_thread */
    sys_sem_t init_sem;
    if (sys_sem_new(&init_sem, 0) != ERR_OK) return;
    tcpip_init(tcpip_init_done, &init_sem);
    sys_arch_sem_wait(&init_sem, 0);
    sys_sem_free(&init_sem);

    /* 2. 初始化网络接口（以太网示例） */
    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr,  192, 168, 1, 100);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw,      192, 168, 1, 1);

    netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL,
              ethernetif_init, tcpip_input);
    netif_set_default(&gnetif);
    netif_set_up(&gnetif);

    /* 3. 启动 DHCP（可选） */
    dhcp_start(&gnetif);

    /* 4. 等待网络就绪 */
    while (!netif_is_up(&gnetif)) {
        sys_msleep(1);
    }
}

```

> **关键点**：
>
> - `tcpip_init()` 必须成功创建 `tcpip_thread`，并应等待完成回调后再配置 netif
> - 网卡接收路径应把数据交给 `tcpip_input`，不要从应用线程直接驱动 lwIP core
> - 网络接口必须 `netif_set_up()` 后才能发送数据
> - DNS 解析需要 UDP 支持（`LWIP_UDP=1`）

---

## 11. 步骤八：使用 API

### 11.1 同步聊天请求

```c
#include "openai.h"
#include <string.h>
#include <stdio.h>

void chat_example(void)
{
    /* API Key — 生产环境应从安全存储读取 */
    const char *api_key = "sk-your-api-key-here";

    /* 创建客户端 */
    OpenAI_Client *client = openai_client_new(api_key);
    if (!client) {
        printf("Failed to create client\n");
        return;
    }

    /* （可选）设置自定义端点，如反向代理 */
    // openai_client_set_base_url(client, "http://192.168.1.50:8080/v1");

    /* 构造消息 */
    OpenAI_Message messages[2];
    messages[0].role = "system";
    messages[0].content = "You are a helpful assistant.";
    messages[1].role = "user";
    messages[1].content = "What is the capital of France?";

    /* 构造请求 */
    OpenAI_ChatRequest req = {0};    /* 零初始化很重要 */
    req.model = "gpt-4o-mini";
    req.messages = messages;
    req.message_count = 2;
    req.max_tokens = 128;
    req.temperature = 0.7;
    req.stream = 0;                  /* 0=同步，1=流式 */

    /* 发送请求 */
    OpenAI_ChatResponse *resp = openai_chat_create(client, &req);

    if (resp) {
        if (resp->choices && resp->choice_count > 0) {
            printf("Response: %s\n", resp->choices[0].content);
            printf("Finish reason: %s\n",
                   resp->choices[0].finish_reason ? resp->choices[0].finish_reason : "N/A");
            printf("Model: %s\n", resp->model);
            if (resp->usage) {
                printf("Usage: %s\n", resp->usage);
            }
        } else {
            printf("No choices in response\n");
        }
        openai_chat_response_free(resp);
    } else {
        /* 使用 openai_client_get_last_error() 获取具体错误类型 */
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
                printf("请求失败，错误码：%d\n", err);
        }
    }

    openai_client_free(client);
}
```

### 11.2 Anthropic 模型

```c
#include "openai.h"
#include <string.h>
#include <stdio.h>

void anthropic_example(void)
{
    OpenAI_Client *client = openai_client_new("sk-ant-your-key");
    if (!client) return;

    /* 切换到 Anthropic Provider */
    openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC);
    /* base_url 会自动设为 https://api.anthropic.com/v1 */

    /* Anthropic 的 system prompt 作为 messages 中 role="system" 的消息 */
    OpenAI_Message messages[1];
    messages[0].role = "user";
    messages[0].content = "Hello, Claude!";

    OpenAI_ChatRequest req = {0};
    req.model = "claude-sonnet-4-20250514";
    req.messages = messages;
    req.message_count = 1;
    req.max_tokens = 256;    /* Anthropic 必填 */

    OpenAI_ChatResponse *resp = openai_chat_create(client, &req);
    if (resp && resp->choice_count > 0) {
        printf("Claude: %s\n", resp->choices[0].content);
        openai_chat_response_free(resp);
    }

    openai_client_free(client);
}
```

### 11.3 流式请求

```c
void streaming_example(void)
{
    OpenAI_Client *client = openai_client_new("sk-your-key");
    if (!client) return;

    OpenAI_Message messages[1];
    messages[0].role = "user";
    messages[0].content = "Write a haiku about coding";

    OpenAI_ChatRequest req = {0};
    req.model = "gpt-4o-mini";
    req.messages = messages;
    req.message_count = 1;
    req.max_tokens = 100;
    req.stream = 1;

    /* 创建流式句柄 */
    void *stream = openai_chat_create_stream(client, &req);
    if (!stream) {
        openai_client_free(client);
        return;
    }

    /* 逐条读取事件 */
    OpenAI_StreamEvent event;
    int chunk_count = 0;

    while (openai_stream_read(stream, &event) == OPENAI_OK) {
        if (event.content) {
            printf("%s", event.content);   /* 打印每个 token */
            chunk_count++;
        }
        openai_stream_event_free(&event);
    }

    printf("\n\nReceived %d chunks\n", chunk_count);
    openai_stream_close(stream);
    openai_client_free(client);
}
```

> **流式注意事项**：当前实现会增量读取并解析事件。这意味着：
>
> - `openai_chat_create_stream()` 在收到 HTTP 响应头后返回
> - `openai_stream_read()` 会等待下一条完整 SSE 事件或流结束
> - 空闲传输使用 `OPENAI_TIMEOUT` 超时，单条未完成 SSE 行最大为 64 KiB

### 11.4 Extended Thinking（扩展思考）

Anthropic 的 Extended Thinking 功能让 Claude 在回答前进行深度思考。启用后，响应中会包含 `thinking` 内容。

```c
void extended_thinking_example(void)
{
    OpenAI_Client *client = openai_client_new("sk-ant-your-key");
    if (!client) return;

    openai_client_set_provider(client, OPENAI_PROVIDER_ANTHROPIC);

    OpenAI_Message messages[1];
    messages[0].role = "user";
    messages[0].content = "请详细分析快速排序算法的时间复杂度，并给出证明。";

    OpenAI_ChatRequest req = {0};
    req.model = "claude-sonnet-4-20250514";
    req.messages = messages;
    req.message_count = 1;
    req.max_tokens = 16000;
    req.thinking_enabled = 1;     /* 启用 Extended Thinking */
    req.thinking_budget = 10000;  /* thinking token 预算 */

    OpenAI_ChatResponse *resp = openai_chat_create(client, &req);
    if (resp && resp->choice_count > 0) {
        /* 思考过程 */
        if (resp->choices[0].thinking) {
            printf("=== 思考过程 ===\n%s\n", resp->choices[0].thinking);
        }
        /* 最终答案 */
        if (resp->choices[0].content) {
            printf("=== 最终答案 ===\n%s\n", resp->choices[0].content);
        }
        /* 结束原因 */
        if (resp->choices[0].finish_reason) {
            printf("结束原因: %s\n", resp->choices[0].finish_reason);
        }
        openai_chat_response_free(resp);
    }

    openai_client_free(client);
}
```

> **Extended Thinking 注意事项**：
>
> - `thinking_budget` 必须小于 `max_tokens`
> - thinking 内容可能很长，注意内存限制
> - 启用 thinking 后响应时间会增加
> - `thinking_enabled=0`（默认）时不发送 thinking 参数，行为与普通请求一致

---

## 12. 不同平台移植参考

### STM32F4（Cortex-M4 + RTOS）

- 已有 `build_lwip.sh` 参考脚本
- MCU 标志：`-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16`
- `sys_now()` 使用 `HAL_GetTick()`
- 硬件校验和：`CHECKSUM_*` 全部设为 `0`
- 推荐 RAM：128KB+（lwIP 16KB + mbedTLS 50KB+ + openai 20KB+）

当前 Socket backend 不支持 STM32F4 `NO_SYS=1` 裸机配置。请使用 FreeRTOS/CMSIS-RTOS
等真实系统层并调用 `tcpip_init()`；纯裸机项目需要实现独立的 lwIP raw API backend。

### STM32H7（Cortex-M7，FreeRTOS）

```c
/* sys_now() 改为 FreeRTOS 时钟 */
u32_t sys_now(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}
```

- Cortex-M7 性能充足，`TCP_MSS` 可用 `1460`
- mbedTLS 可启用 `MBEDTLS_HAVE_ASM`
- 推荐 RAM：256KB+

### ESP32

ESP-IDF 自带 lwIP 和 mbedTLS，**无需使用项目子模块**：

```c
/* 在 ESP-IDF 项目中 */
/* 使用 ESP-IDF 自带的 lwIP */
#include "lwip/sockets.h"
#include "lwip/netdb.h"

/* 使用 ESP-IDF 自带的 mbedTLS */
#include "mbedtls/ssl.h"
```

- 只需复制 `openai_client.c`、`openai_json.c`、`openai_http_lwip.c` 和 `include/` 目录
- 调整 `lwipopts.h` 匹配 ESP-IDF 的配置
- ESP32 有硬件加速，HTTPS 性能良好

### RP2040（Raspberry Pi Pico）

- 使用 lwIP + Pico SDK 的网络驱动
- `sys_now()` 使用 `time_us_32() / 1000`
- RAM 有限（264KB），建议 `MEM_SIZE = 8192`
- 如 RAM 不足，可将 `OPENAI_USE_TLS` 设为 `0`（HTTP）节省 mbedTLS 的 ~200KB

### STM32 + 4G 模块（AT 指令）

如果使用外部 4G 模块（如 SIM7600）通过 AT 指令联网：

- 不需要 lwIP
- 需要实现 `openai_http.c` 适配层，通过串口发送 AT 指令建立 TCP 连接
- 参考 `openai_http.h` 中的接口定义自行实现

---

## 13. 常见问题排查

### 编译错误

| 错误信息 | 原因 | 解决方案 |
| ------------------------------------------------------ | ------------------ | ----------------------------------------------------------------- |
| `fatal error: lwip/sockets.h: No such file` | lwIP 头文件路径未添加 | 添加 `third_party/lwip/src/include` 到包含路径 |
| `fatal error: mbedtls/ssl.h: No such file` | mbedTLS 头文件路径未添加 | 添加 `third_party/mbedtls/include` 到包含路径 |
| `error: 'LWIP_ALTCP_TLS' undeclared` | 编译定义缺失 | 添加 `-DLWIP_ALTCP=1 -DLWIP_ALTCP_TLS=1 -DLWIP_ALTCP_TLS_MBEDTLS=1` |
| `error: implicit declaration of function 'altcp_read'` | lwIP 2.2.x 已移除此函数 | 确保使用项目自带的 `openai_http_lwip.c`（已内置兼容层） |
| `undefined reference to 'sys_now'` | 未实现系统时钟 | 在 `sys_arch.c` 中实现 `sys_now()` 返回毫秒计时 |
| `undefined reference to 'gethostbyname'` | lwIP DNS 未启用 | 确认 `LWIP_DNS=1`，且 `lwip_netdb.c` 已编译 |
| `error: strtod undeclared` | 嵌入式工具链未提供 `strtod` | 需要提供 `strtod` 实现（newlib-nano 默认包含） |

### 运行时问题

| 现象 | 原因 | 解决方案 |
| ----------------------------- | --------------------- | ---------------------------------- |
| `gethostbyname()` 返回空 | DNS 未配置或网络未就绪 | 确认 DNS 服务器 IP 已设置，网络接口已 up |
| `connect()` 返回 -1 | TCP 连接失败 | 检查防火墙、TLS 配置、端口 443 是否可达 |
| HTTPS 连接后无响应 | mbedTLS 握手失败 | 检查时间戳（TLS 证书验证需要正确时间） |
| 响应被截断 | `Content-Length` 解析错误 | 确认使用最新代码（已修复大小写敏感问题） |
| 内存不足（OOM） | RAM 不够 | 增大堆或减小 `MEM_SIZE`/`TCP_WND` |
| `openai_client_new()` 返回 NULL | 已存在一个客户端实例 | 当前限制为单客户端，先 `openai_client_free()` |

### 调试建议

1. **启用日志**：将 `OPENAI_LOG_ENABLED` 设为 `1`，`OPENAI_LOG_LEVEL` 设为 `3`
2. **启用 lwIP 调试**：在 `lwipopts.h` 中设置 `LWIP_DEBUG 1` 并启用 `TCP_DEBUG`、`DNS_DEBUG` 等
3. **串口输出**：确保 `fprintf(stderr, ...)` 重定向到你的串口
4. **Wireshark 抓包**：如果有以太网口，可以抓包查看 TCP/TLS 握手过程

---

## 14. 内存需求估算

本节数值为配置规划估算，未在目标板测量，不是最低可运行保证。实际 Flash、堆峰值与响应长度、编译选项和第三方库配置有关，需在目标工程测量。

### 静态内存（代码 + 常量）

| 组件 | Flash 占用估算 |
| ------------------ | --------------- |
| openai_client.c | ~15-20 KB |
| openai_json.c | ~5-8 KB |
| openai_http_lwip.c | ~5-8 KB |
| lwIP 核心 | ~50-80 KB |
| mbedTLS（TLS 客户端） | ~150-250 KB |
| **合计** | **~225-366 KB** |

> 如禁用 TLS（`OPENAI_USE_TLS=0`），可节省 ~150-250 KB Flash。

### 动态内存（堆）

| 分配项 | 堆用量估算 |
| ------------------ | --------------------- |
| lwIP 栈（`MEM_SIZE`） | 8-32 KB（可配） |
| mbedTLS 会话 | 30-60 KB（每连接） |
| HTTP 请求缓冲 | 1-4 KB |
| HTTP 响应缓冲 | 4-64 KB（流式缓冲有固定上限） |
| JSON 解析树 | 2-8 KB |
| openai 响应结构 | 1-4 KB |
| **合计（单次请求）** | **~50-120 KB** |

### RAM 配置估算

| 配置 | 起步估算 | 规划估算 |
| -------------- | ------ | ------ |
| HTTP（无 TLS） | 40 KB | 64 KB |
| HTTPS（mbedTLS） | 80 KB | 128 KB |
| HTTPS + RTOS 栈 | 100 KB | 160 KB |

---

## 15. 已知限制

1. **单客户端限制** — 同一时刻只能存在一个 `OpenAI_Client` 实例（全局计数器控制）
2. **非线程安全** — lwIP 后端使用静态全局变量，不支持多线程并发请求
3. **无连接复用** — 每次请求都新建 TCP 连接，无 HTTP Keep-Alive
4. **流式行长度限制** — 单条未结束的 SSE 行最大为 64 KiB，超过限制会返回错误
5. **无重试机制** — 请求失败不会自动重试
6. **无代理支持** — lwIP 后端不支持 HTTP 代理
7. **DNS 查询** — 每次连接调用解析接口；缓存行为由目标 lwIP 配置决定
8. **TLS 证书配置** — 当前后端调用 `altcp_tls_create_config_client(NULL, 0)`，未传入 CA；启用 TLS 本身不等于完成服务器证书验证，目标集成需确认实际行为

---

## 快速参考：最小移植清单

```text
方法一：独立编译（推荐）
□ 1. git submodule update --init --recursive
□ 2. cmake -S . -B build -DOPENAI_HTTP_BACKEND=OPENAI_BACKEND_LWIP -DLWIP_INCLUDE_DIR="..." -DMBEDTLS_INCLUDE_DIR="..."
□ 3. cmake --build build --parallel 12
□ 4. 获取 libopenai_lwip.a，合并到你的工程
□ 5. 确保网络已初始化（参考第 10 节）
□ 6. 链接 openai_lwip + lwip + mbedtls 静态库

方法二：直接集成源码
□ 1. git submodule update --init --recursive
□ 2. 使用 RTOS 和 NO_SYS=0，提供 lwipopts.h（参考第 5 节）
□ 3. 提供 arch/cc.h（参考第 6.1 节）
□ 4. 提供 arch/sys_arch.h（参考第 6.2 节）
□ 5. 提供 sys_arch.c（参考第 6.3 节）
□ 6. 配置 mbedTLS（参考第 7 节，HTTPS 必需）
□ 7. 将 3 个 .c 源文件加入工程编译
□ 8. 添加头文件路径和编译定义
□ 9. 调用 tcpip_init() 并确保网络已初始化（参考第 10 节）
□ 10. 链接 openai + lwip + mbedtls 静态库
□ 11. 编译测试
```
