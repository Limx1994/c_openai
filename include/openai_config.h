#ifndef OPENAI_CONFIG_H
#define OPENAI_CONFIG_H

/* HTTP backend selection */
#define OPENAI_BACKEND_CURL 0
#define OPENAI_BACKEND_LWIP 1

#ifndef OPENAI_HTTP_BACKEND
#define OPENAI_HTTP_BACKEND OPENAI_BACKEND_CURL
#endif

/* Memory management */
#ifndef OPENAI_USE_MALLOC
#define OPENAI_USE_MALLOC 1
#endif

/* Buffer sizes */
#ifndef OPENAI_BUFFER_SIZE
#define OPENAI_BUFFER_SIZE 4096
#endif

/* Connection settings */
#ifndef OPENAI_MAX_RETRIES
#define OPENAI_MAX_RETRIES 3
#endif

#ifndef OPENAI_TIMEOUT
#define OPENAI_TIMEOUT 30
#endif

/* API Base URL */
#define OPENAI_API_BASE "https://api.openai.com/v1"

/* TLS settings (for lwIP backend) */
#ifndef OPENAI_USE_TLS
#define OPENAI_USE_TLS 1
#endif

#ifndef OPENAI_TLS_CERT_VERIFY
#define OPENAI_TLS_CERT_VERIFY 0  /* 0=skip cert verify, 1=verify CA cert */
#endif

/* Version */
#define OPENAI_VERSION "1.0.0"

#endif /* OPENAI_CONFIG_H */