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

/* Memory management */
#ifndef OPENAI_USE_MALLOC
#define OPENAI_USE_MALLOC 1  /**< 1=dynamic allocation, 0=static buffer mode */
#endif

/* Buffer sizes */
#ifndef OPENAI_BUFFER_SIZE
#define OPENAI_BUFFER_SIZE 4096  /**< Default buffer size for static mode */
#endif

/* Connection settings */
#ifndef OPENAI_MAX_RETRIES
#define OPENAI_MAX_RETRIES 3  /**< Maximum retry attempts */
#endif

#ifndef OPENAI_TIMEOUT
#define OPENAI_TIMEOUT 30  /**< Request timeout in seconds */
#endif

/* API Base URL */
#define OPENAI_API_BASE "https://api.openai.com/v1"  /**< OpenAI API base URL */

/* TLS settings (for lwIP backend only) */
#ifndef OPENAI_USE_TLS
#define OPENAI_USE_TLS 1  /**< 1=enable TLS, 0=disable (plain HTTP) */
#endif

#ifndef OPENAI_TLS_CERT_VERIFY
#define OPENAI_TLS_CERT_VERIFY 0  /**< 0=skip cert verify, 1=verify CA cert */
#endif

/* Version */
#define OPENAI_VERSION "1.0.0"  /**< Library version */

#endif /* OPENAI_CONFIG_H */
