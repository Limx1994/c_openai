/**
 * @file openai_config.h
 * @brief OpenAI Compile-Time Configuration
 *
 * Configure the library at compile time by setting these macros
 * before including openai.h, or define them in your build system.
 */

/* HTTP backend selection */
#define OPENAI_BACKEND_CURL 0  /**< Use libcurl backend (default, for PC/server) */
#define OPENAI_BACKEND_LWIP 1   /**< Use lwIP backend (for embedded/MCU) */

#ifndef OPENAI_HTTP_BACKEND
#define OPENAI_HTTP_BACKEND OPENAI_BACKEND_CURL  /**< Select HTTP backend */
#endif

/* Connection settings */
#ifndef OPENAI_TIMEOUT
#define OPENAI_TIMEOUT 30  /**< Request timeout in seconds */
#endif

/* TLS settings (for lwIP backend only) */
#ifndef OPENAI_USE_TLS
#define OPENAI_USE_TLS 1  /**< 1=enable TLS, 0=disable (plain HTTP) */
#endif

/* API Base URL - conditional on TLS setting */
#if OPENAI_USE_TLS
#define OPENAI_API_BASE "https://api.openai.com/v1"
#define ANTHROPIC_API_BASE "https://api.anthropic.com/v1"
#else
#define OPENAI_API_BASE "http://api.openai.com/v1"
#define ANTHROPIC_API_BASE "http://api.anthropic.com/v1"
#endif

/* Version */
#define OPENAI_VERSION "1.0.0"  /**< Library version */

/* Logging */
#ifndef OPENAI_LOG_ENABLED
#define OPENAI_LOG_ENABLED 1   /**< 0=disable all logging, 1=enable */
#endif

#ifndef OPENAI_LOG_LEVEL
#define OPENAI_LOG_LEVEL 3     /**< 0=error, 1=warn, 2=info, 3=debug */
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
