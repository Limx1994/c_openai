/*
 * lwIP HTTP backend for embedded systems
 * Requires lwIP socket API (include lwip/sockets.h)
 *
 * This entire file is guarded by OPENAI_USE_LWIP.
 * When not defined, the file compiles to an empty translation unit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef OPENAI_USE_LWIP
/* Prevent system from redefining struct timeval/fd_set (lwIP provides them) */
#define _SYS__TIMEVAL_H_
#define _SYS_SELECT_H
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/ip_addr.h>
#include <lwip/altcp_tls.h>

/* ============================================================
 * altcp_read compatibility layer (removed in lwIP 2.2.x)
 * Uses altcp_recv callback + ring buffer for blocking reads
 * Only needed when ALTCP TLS is enabled.
 * ============================================================ */
#ifdef LWIP_ALTCP_TLS

#include "openai_config.h"
#include <lwip/sys.h>
#include <lwip/timeouts.h>

#define ALTCP_RX_BUF_SIZE 8192

typedef struct {
    uint8_t buf[ALTCP_RX_BUF_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint8_t data_ready;
} altcp_rx_ring_t;

/* Per-PCB receive ring buffer (single-request-at-a-time) */
static altcp_rx_ring_t s_rx_ring;
static struct altcp_pcb *s_rx_pcb = NULL;

static err_t altcp_recv_cb(void *arg, struct altcp_pcb *pcb,
                          struct pbuf *p, err_t err) {
    (void)arg; (void)pcb; (void)err;
    if (!p) {
        /* Connection closed */
        s_rx_ring.data_ready = 2; /* EOF signal */
        return ERR_OK;
    }
    /* Copy pbuf data into ring buffer */
    struct pbuf *q;
    for (q = p; q != NULL; q = q->next) {
        uint32_t h = s_rx_ring.head;
        uint32_t t = s_rx_ring.tail;
        uint32_t free_space = (t + ALTCP_RX_BUF_SIZE - h - 1) % ALTCP_RX_BUF_SIZE;
        uint32_t to_copy = q->len < free_space ? q->len : free_space;
        for (uint32_t i = 0; i < to_copy; i++) {
            s_rx_ring.buf[h] = ((uint8_t *)q->payload)[i];
            h = (h + 1) % ALTCP_RX_BUF_SIZE;
        }
        s_rx_ring.head = h;
    }
    s_rx_ring.data_ready = 1;
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void altcp_read_install_recv(struct altcp_pcb *pcb) {
    s_rx_pcb = pcb;
    s_rx_ring.head = 0;
    s_rx_ring.tail = 0;
    s_rx_ring.data_ready = 0;
    altcp_recv(pcb, altcp_recv_cb);
}

static int altcp_read(struct altcp_pcb *pcb, void *buf, size_t len) {
    (void)pcb;
    if (!s_rx_pcb) return -1;

    uint32_t t = s_rx_ring.tail;
    uint32_t h = s_rx_ring.head;

    /* Wait for data or EOF */
    uint32_t timeout_ms = OPENAI_TIMEOUT * 1000;
    uint32_t start = sys_now();
    while (t == h && s_rx_ring.data_ready != 2) {
        if ((sys_now() - start) >= timeout_ms) return 0; /* timeout */
#ifdef LWIP_ALTCP
        sys_check_timeouts();
#endif
    }

    if (s_rx_ring.data_ready == 2 && t == h) {
        return 0; /* EOF */
    }

    /* Read from ring buffer */
    size_t available = (h + ALTCP_RX_BUF_SIZE - t) % ALTCP_RX_BUF_SIZE;
    size_t to_read = len < available ? len : available;
    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < to_read; i++) {
        dst[i] = s_rx_ring.buf[t];
        t = (t + 1) % ALTCP_RX_BUF_SIZE;
    }
    s_rx_ring.tail = t;
    if (t == h) s_rx_ring.data_ready = 0;

    return (int)to_read;
}

#endif /* LWIP_ALTCP_TLS */

#include "openai_http.h"

/* Case-insensitive substring search (portable, no libc dependency) */
static char* openai_strcasestr(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return NULL;
    size_t needle_len = strlen(needle);
    for (; *haystack; haystack++) {
        size_t i;
        for (i = 0; i < needle_len && haystack[i]; i++) {
            if (tolower((unsigned char)haystack[i]) != tolower((unsigned char)needle[i])) break;
        }
        if (i == needle_len) return (char*)haystack;
    }
    return NULL;
}

/* TLS config storage - module-level static for single-request-at-a-time use */
#ifdef LWIP_ALTCP_TLS
static struct altcp_tls_config* s_tls_config = NULL;
#endif

int openai_http_init(void) {
    return 0;
}

void openai_http_cleanup(void) {
}

static int openai_http_parse_url(const char* url, char* host, size_t host_size,
                                  int* port, char* path, size_t path_size) {
    /* Simple URL parser: https://api.openai.com/v1/chat/completions */
    const char* p = url;

    /* Skip protocol */
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *port = 443;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        *port = 80;
    } else {
        OPENAI_LOG_ERROR("openai_http_parse_url: unsupported protocol in URL: %s", url);
        return -1;
    }

    /* Extract host */
    const char* end = p;
    while (*end && *end != ':' && *end != '/' && *end != '?') end++;

    size_t host_len = end - p;
    if (host_len >= host_size) return -1;
    memcpy(host, p, host_len);
    host[host_len] = '\0';

    /* Extract port if present */
    if (*end == ':') {
        end++;
        *port = atoi(end);
        while (isdigit(*end)) end++;
    }

    /* Extract path */
    const char* path_start = end;
    if (*path_start == '\0') {
        strcpy(path, "/");
    } else {
        size_t path_len = strlen(path_start);
        if (path_len >= path_size) return -1;
        strcpy(path, path_start);
    }

    return 0;
}

static void* openai_http_connect(const char* host, int port, int is_https) {
#ifdef LWIP_ALTCP_TLS
    if (is_https && OPENAI_USE_TLS) {
        /* Use ALTCP + mbedTLS for HTTPS */
        struct altcp_tls_config* tls_config = altcp_tls_create_config_client(NULL, 0);
        if (!tls_config) {
            OPENAI_LOG_ERROR("openai_http_connect: failed to create TLS config for %s", host);
            return NULL;
        }

        struct altcp_pcb* pcb = altcp_tls_new(tls_config, IPADDR_TYPE_V4);
        if (!pcb) {
            OPENAI_LOG_ERROR("openai_http_connect: failed to create ALTCP TLS pcb for %s", host);
            altcp_tls_free_config(tls_config);
            return NULL;
        }

        /* Store tls_config in module-level static (safe for single-request-at-a-time) */
        s_tls_config = tls_config;

        return (void*)pcb;
    }
#else
    (void)is_https;
#endif

    /* Plain TCP connection */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        OPENAI_LOG_ERROR("openai_http_connect: socket() failed");
        return NULL;
    }

    struct hostent* server = gethostbyname(host);
    if (!server) {
        OPENAI_LOG_ERROR("openai_http_connect: DNS resolution failed for host: %s", host);
        closesocket(sock);
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        OPENAI_LOG_ERROR("openai_http_connect: TCP connect failed to %s:%d", host, port);
        closesocket(sock);
        return NULL;
    }

    return (void*)(intptr_t)sock;
}

OpenAI_HTTPResponse* openai_http_request(OpenAI_HTTPRequest* req) {
    if (!req || !req->url) return NULL;

    /* TODO: HTTP status code to OpenAI_ErrorCode mapping
     *
     * The following HTTP status codes should eventually map to specific error codes:
     *   401 -> OPENAI_ERR_AUTH        (Authentication failed)
     *   429 -> OPENAI_ERR_RATE_LIMIT  (Rate limit exceeded)
     *   500, 502, 503, 504 -> OPENAI_ERR_SERVER (Server error)
     *
     * Current implementation returns status_code in the response struct for the
     * caller to map. Full integration requires changing the return type or adding
     * an error code output parameter to this function -- a larger refactor that
     * should be done separately.
     */

    char host[256] = {0};
    char path[512] = {0};
    int port = 443;
    int is_https = (strncmp(req->url, "https://", 8) == 0);

    if (openai_http_parse_url(req->url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        return NULL;
    }

    void* conn = openai_http_connect(host, port, is_https);
    if (!conn) return NULL;

#ifdef LWIP_ALTCP_TLS
    if (is_https && OPENAI_USE_TLS) {
        struct altcp_pcb* pcb = (struct altcp_pcb*)conn;

        /* DNS resolution and connect */
        ip_addr_t ipaddr;
        struct hostent* server = gethostbyname(host);
        if (!server) {
            OPENAI_LOG_ERROR("openai_http_request: DNS resolution failed for host: %s", host);
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            altcp_close(pcb);
            return NULL;
        }
        memcpy(&ipaddr, server->h_addr, server->h_length);

        err_t err = altcp_connect(pcb, &ipaddr, port, NULL);
        if (err != ERR_OK) {
            OPENAI_LOG_ERROR("openai_http_request: ALTCP connect failed to %s:%d (err=%d)", host, port, err);
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            altcp_close(pcb);
            return NULL;
        }

        /* Install recv callback for altcp_read compatibility */
        altcp_read_install_recv(pcb);

        /* Build HTTP request */
        size_t auth_len = req->auth_header ? strlen(req->auth_header) : 0;
        size_t extra_len = req->extra_headers ? strlen(req->extra_headers) : 0;
        size_t header_size = 512 + auth_len + extra_len;
        char* header = (char*)malloc(header_size);
        if (!header) {
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            altcp_close(pcb);
            return NULL;
        }

        int header_len;
        if (req->auth_header) {
            char auth_line[512];
            if (req->auth_mode == 1) {
                snprintf(auth_line, sizeof(auth_line), "x-api-key: %s\r\n", req->auth_header);
            } else {
                snprintf(auth_line, sizeof(auth_line), "Authorization: Bearer %s\r\n", req->auth_header);
            }
            header_len = snprintf(header, header_size,
                "POST %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "%s"
                "Content-Type: application/json\r\n"
                "Accept: application/json\r\n"
                "%s"
                "Content-Length: %zu\r\n"
                "\r\n",
                path, host, auth_line,
                req->extra_headers ? req->extra_headers : "",
                req->body ? req->body_size : 0);
        } else {
            header_len = snprintf(header, header_size,
                "POST %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Content-Type: application/json\r\n"
                "Accept: application/json\r\n"
                "%s"
                "Content-Length: %zu\r\n"
                "\r\n",
                path, host,
                req->extra_headers ? req->extra_headers : "",
                req->body ? req->body_size : 0);
        }

        /* Send header - altcp_write buffers data, single call is sufficient */
        err = altcp_write(pcb, header, header_len, 0);
        free(header);
        if (err != ERR_OK) {
            OPENAI_LOG_ERROR("openai_http_request: ALTCP write header failed (err=%d)", err);
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            altcp_close(pcb);
            return NULL;
        }

        /* Send body if present */
        if (req->body && req->body_size > 0) {
            err = altcp_write(pcb, req->body, req->body_size, 0);
            if (err != ERR_OK) {
                OPENAI_LOG_ERROR("openai_http_request: ALTCP write body failed (err=%d)", err);
                if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
                altcp_close(pcb);
                return NULL;
            }
        }

        /* Read response */
        size_t buf_capacity = 4096;
        size_t buf_size = 0;
        char* buf = (char*)malloc(buf_capacity);
        if (!buf) {
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            altcp_close(pcb);
            return NULL;
        }

        while (1) {
            /* Ensure we have space before reading */
            if (buf_size >= buf_capacity - 1) {
                buf_capacity *= 2;
                char* new_buf = (char*)realloc(buf, buf_capacity);
                if (!new_buf) {
                    free(buf);
                    if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
                    altcp_close(pcb);
                    return NULL;
                }
                buf = new_buf;
            }

            size_t to_read = (buf_capacity - buf_size - 1) < 4096 ? (buf_capacity - buf_size - 1) : 4096;
            int received = altcp_read(pcb, buf + buf_size, to_read);
            if (received <= 0) break;
            buf_size += received;
            buf[buf_size] = '\0';

            char* header_end = strstr(buf, "\r\n\r\n");
            if (header_end) {
                /* Parse Content-Length to read remaining body */
                char* cl_str = openai_strcasestr(buf, "content-length:");
                if (cl_str) {
                    cl_str += 15;
                    while (*cl_str == ' ') cl_str++;
                    int content_length = atoi(cl_str);
                    size_t body_received = buf_size - (size_t)(header_end + 4 - buf);
                    while ((int)body_received < content_length) {
                        if (buf_size >= buf_capacity - 1) {
                            buf_capacity *= 2;
                            char* new_buf2 = (char*)realloc(buf, buf_capacity);
                            if (!new_buf2) break;
                            buf = new_buf2;
                        }
                        size_t to_read2 = (buf_capacity - buf_size - 1) < 4096 ? (buf_capacity - buf_size - 1) : 4096;
                        if ((int)to_read2 > content_length - (int)body_received)
                            to_read2 = (size_t)(content_length - (int)body_received);
                        int received2 = altcp_read(pcb, buf + buf_size, to_read2);
                        if (received2 <= 0) break;
                        buf_size += received2;
                        buf[buf_size] = '\0';
                        body_received += received2;
                    }
                }
                break;
            }
        }

        if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
        altcp_close(pcb);

        OpenAI_HTTPResponse* resp = (OpenAI_HTTPResponse*)calloc(1, sizeof(OpenAI_HTTPResponse));
        if (!resp) {
            free(buf);
            return NULL;
        }

        char* body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            char* status_line = buf;
            char* line_end = strstr(buf, "\r\n");
            if (line_end) {
                *line_end = '\0';
                char* status = strchr(status_line, ' ');
                if (status) {
                    status++;
                    resp->status_code = atoi(status);
                }
            }
            size_t body_len = buf_size - (body_start - buf);
            resp->body = (char*)malloc(body_len + 1);
            if (resp->body) {
                memcpy(resp->body, body_start, body_len);
                resp->body[body_len] = '\0';
                resp->body_size = body_len;
            }
        } else {
            resp->status_code = 0;
        }
        free(buf);
        OPENAI_LOG_DEBUG("lwIP HTTPS response: status=%d, body_size=%zu", resp->status_code, resp->body_size);
        return resp;
    }
#endif

    /* Plain HTTP branch */
    int sock = (int)(intptr_t)conn;

    /* Build HTTP request */
    size_t auth_len = req->auth_header ? strlen(req->auth_header) : 0;
    size_t extra_len = req->extra_headers ? strlen(req->extra_headers) : 0;
    size_t header_size = 512 + auth_len + extra_len;
    char* header = (char*)malloc(header_size);
    if (!header) {
        closesocket(sock);
        return NULL;
    }

    int header_len;
    if (req->auth_header) {
        char auth_line[512];
        if (req->auth_mode == 1) {
            snprintf(auth_line, sizeof(auth_line), "x-api-key: %s\r\n", req->auth_header);
        } else {
            snprintf(auth_line, sizeof(auth_line), "Authorization: Bearer %s\r\n", req->auth_header);
        }
        header_len = snprintf(header, header_size,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "%s"
            "Content-Type: application/json\r\n"
            "Accept: application/json\r\n"
            "%s"
            "Content-Length: %zu\r\n"
            "\r\n",
            path, host, auth_line,
            req->extra_headers ? req->extra_headers : "",
            req->body ? req->body_size : 0);
    } else {
        header_len = snprintf(header, header_size,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Accept: application/json\r\n"
            "%s"
            "Content-Length: %zu\r\n"
            "\r\n",
            path, host,
            req->extra_headers ? req->extra_headers : "",
            req->body ? req->body_size : 0);
    }

    /* Send request */
    int total_sent = 0;
    while (total_sent < header_len) {
        int sent = send(sock, header + total_sent, header_len - total_sent, 0);
        if (sent <= 0) {
            OPENAI_LOG_ERROR("lwIP: send header failed");
            free(header);
            closesocket(sock);
            return NULL;
        }
        total_sent += sent;
    }

    /* Send body if present */
    if (req->body && req->body_size > 0) {
        total_sent = 0;
        while (total_sent < (int)req->body_size) {
            int sent = send(sock, req->body + total_sent, req->body_size - total_sent, 0);
            if (sent <= 0) {
                OPENAI_LOG_ERROR("lwIP: send body failed");
                free(header);
                closesocket(sock);
                return NULL;
            }
            total_sent += sent;
        }
    }

    free(header);

    /* Read response */
    size_t buf_capacity = 4096;
    size_t buf_size = 0;
    char* buf = (char*)malloc(buf_capacity);
    if (!buf) {
        closesocket(sock);
        return NULL;
    }

    while (1) {
        /* Ensure we have space before reading */
        if (buf_size >= buf_capacity - 1) {
            buf_capacity *= 2;
            char* new_buf = (char*)realloc(buf, buf_capacity);
            if (!new_buf) {
                free(buf);
                closesocket(sock);
                return NULL;
            }
            buf = new_buf;
        }

        int to_read = (int)(buf_capacity - buf_size - 1);
        int received = recv(sock, buf + buf_size, to_read, 0);
        if (received < 0) {
            OPENAI_LOG_WARN("lwIP: recv failed");
            break;
        }
        if (received == 0) break;
        buf_size += received;
        buf[buf_size] = '\0';

        /* Check for end of headers */
        char* header_end = strstr(buf, "\r\n\r\n");
        if (header_end) {
            /* Parse Content-Length to read remaining body (case-insensitive per RFC 7230) */
            char* cl_str = openai_strcasestr(buf, "content-length:");
            if (cl_str) {
                cl_str += 15;
                while (*cl_str == ' ') cl_str++;
                int content_length = atoi(cl_str);
                size_t body_received = buf_size - (size_t)(header_end + 4 - buf);
                while ((int)body_received < content_length) {
                    if (buf_size >= buf_capacity - 1) {
                        buf_capacity *= 2;
                        char* new_buf2 = (char*)realloc(buf, buf_capacity);
                        if (!new_buf2) break;
                        buf = new_buf2;
                    }
                    int to_read2 = (int)(buf_capacity - buf_size - 1);
                    if (to_read2 > content_length - (int)body_received)
                        to_read2 = content_length - (int)body_received;
                    int received2 = recv(sock, buf + buf_size, to_read2, 0);
                    if (received2 <= 0) break;
                    buf_size += received2;
                    buf[buf_size] = '\0';
                    body_received += received2;
                }
            }
            break;
        }
    }

    closesocket(sock);

    OpenAI_HTTPResponse* resp = (OpenAI_HTTPResponse*)calloc(1, sizeof(OpenAI_HTTPResponse));
    if (!resp) {
        free(buf);
        return NULL;
    }

    /* Parse HTTP response */
    char* body_start = strstr(buf, "\r\n\r\n");
    if (body_start) {
        body_start += 4;

        /* Parse status line */
        char* status_line = buf;
        char* line_end = strstr(buf, "\r\n");
        if (line_end) {
            *line_end = '\0';

            /* HTTP/1.1 200 OK */
            char* status = strchr(status_line, ' ');
            if (status) {
                status++;
                resp->status_code = atoi(status);
            }
        }

        size_t body_len = buf_size - (body_start - buf);
        resp->body = (char*)malloc(body_len + 1);
        if (resp->body) {
            memcpy(resp->body, body_start, body_len);
            resp->body[body_len] = '\0';
            resp->body_size = body_len;
        }
    } else {
        resp->status_code = 0;
        resp->body = NULL;
        resp->body_size = 0;
    }

    free(buf);
    OPENAI_LOG_DEBUG("lwIP HTTP response: status=%d, body_size=%zu", resp->status_code, resp->body_size);
    return resp;
}

void openai_http_response_free(OpenAI_HTTPResponse* resp) {
    if (resp) {
        if (resp->body) free(resp->body);
        free(resp);
    }
}

/* Streaming HTTP request - for SSE responses */
OpenAI_HTTPResponse* openai_http_request_stream(OpenAI_HTTPRequest* req) {
    if (!req || !req->url) return NULL;

    char host[256] = {0};
    char path[512] = {0};
    int port = 443;
    int is_https = (strncmp(req->url, "https://", 8) == 0);

    if (openai_http_parse_url(req->url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        return NULL;
    }

    void* conn = openai_http_connect(host, port, is_https);
    if (!conn) return NULL;

#ifdef LWIP_ALTCP_TLS
    if (is_https && OPENAI_USE_TLS) {
        struct altcp_pcb* pcb = (struct altcp_pcb*)conn;

        /* DNS resolution and connect */
        ip_addr_t ipaddr;
        struct hostent* server = gethostbyname(host);
        if (!server) {
            OPENAI_LOG_ERROR("openai_http_request_stream: DNS resolution failed for host: %s", host);
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            altcp_close(pcb);
            return NULL;
        }
        memcpy(&ipaddr, server->h_addr, server->h_length);

        err_t err = altcp_connect(pcb, &ipaddr, port, NULL);
        if (err != ERR_OK) {
            OPENAI_LOG_ERROR("openai_http_request_stream: ALTCP connect failed to %s:%d (err=%d)", host, port, err);
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            altcp_close(pcb);
            return NULL;
        }

        /* Install recv callback for altcp_read compatibility */
        altcp_read_install_recv(pcb);

        /* Build HTTP request for streaming */
        size_t auth_len = req->auth_header ? strlen(req->auth_header) : 0;
        size_t extra_len = req->extra_headers ? strlen(req->extra_headers) : 0;
        size_t header_size = 512 + auth_len + extra_len;
        char* header = (char*)malloc(header_size);
        if (!header) {
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            altcp_close(pcb);
            return NULL;
        }

        int header_len;
        if (req->auth_header) {
            char auth_line[512];
            if (req->auth_mode == 1) {
                snprintf(auth_line, sizeof(auth_line), "x-api-key: %s\r\n", req->auth_header);
            } else {
                snprintf(auth_line, sizeof(auth_line), "Authorization: Bearer %s\r\n", req->auth_header);
            }
            header_len = snprintf(header, header_size,
                "POST %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "%s"
                "Content-Type: application/json\r\n"
                "Accept: text/event-stream\r\n"
                "%s"
                "Content-Length: %zu\r\n"
                "\r\n",
                path, host, auth_line,
                req->extra_headers ? req->extra_headers : "",
                req->body ? req->body_size : 0);
        } else {
            header_len = snprintf(header, header_size,
                "POST %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Content-Type: application/json\r\n"
                "Accept: text/event-stream\r\n"
                "%s"
                "Content-Length: %zu\r\n"
                "\r\n",
                path, host,
                req->extra_headers ? req->extra_headers : "",
                req->body ? req->body_size : 0);
        }

        /* Send header - altcp_write buffers data, single call is sufficient */
        err = altcp_write(pcb, header, header_len, 0);
        free(header);
        if (err != ERR_OK) {
            OPENAI_LOG_ERROR("openai_http_request_stream: ALTCP write header failed (err=%d)", err);
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            altcp_close(pcb);
            return NULL;
        }

        /* Send body if present */
        if (req->body && req->body_size > 0) {
            err = altcp_write(pcb, req->body, req->body_size, 0);
            if (err != ERR_OK) {
                OPENAI_LOG_ERROR("openai_http_request_stream: ALTCP write body failed (err=%d)", err);
                if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
                altcp_close(pcb);
                return NULL;
            }
        }

        /* Read response */
        size_t buf_capacity = 4096;
        size_t buf_size = 0;
        char* buf = (char*)malloc(buf_capacity);
        if (!buf) {
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            altcp_close(pcb);
            return NULL;
        }

        while (1) {
            /* Ensure we have space before reading */
            if (buf_size >= buf_capacity - 1) {
                buf_capacity *= 2;
                char* new_buf = (char*)realloc(buf, buf_capacity);
                if (!new_buf) {
                    free(buf);
                    if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
                    altcp_close(pcb);
                    return NULL;
                }
                buf = new_buf;
            }

            size_t to_read = (buf_capacity - buf_size - 1) < 4096 ? (buf_capacity - buf_size - 1) : 4096;
            int received = altcp_read(pcb, buf + buf_size, to_read);
            if (received <= 0) break;
            buf_size += received;
            buf[buf_size] = '\0';

            char* header_end = strstr(buf, "\r\n\r\n");
            if (header_end) {
                /* Parse Content-Length to read remaining body */
                char* cl_str = openai_strcasestr(buf, "content-length:");
                if (cl_str) {
                    cl_str += 15;
                    while (*cl_str == ' ') cl_str++;
                    int content_length = atoi(cl_str);
                    size_t body_received = buf_size - (size_t)(header_end + 4 - buf);
                    while ((int)body_received < content_length) {
                        if (buf_size >= buf_capacity - 1) {
                            buf_capacity *= 2;
                            char* new_buf2 = (char*)realloc(buf, buf_capacity);
                            if (!new_buf2) break;
                            buf = new_buf2;
                        }
                        size_t to_read2 = (buf_capacity - buf_size - 1) < 4096 ? (buf_capacity - buf_size - 1) : 4096;
                        if ((int)to_read2 > content_length - (int)body_received)
                            to_read2 = (size_t)(content_length - (int)body_received);
                        int received2 = altcp_read(pcb, buf + buf_size, to_read2);
                        if (received2 <= 0) break;
                        buf_size += received2;
                        buf[buf_size] = '\0';
                        body_received += received2;
                    }
                }
                break;
            }
        }

        if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
        altcp_close(pcb);

        OpenAI_HTTPResponse* resp = (OpenAI_HTTPResponse*)calloc(1, sizeof(OpenAI_HTTPResponse));
        if (!resp) {
            free(buf);
            return NULL;
        }

        char* body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            char* status_line = buf;
            char* line_end = strstr(buf, "\r\n");
            if (line_end) {
                *line_end = '\0';
                char* status = strchr(status_line, ' ');
                if (status) {
                    status++;
                    resp->status_code = atoi(status);
                }
            }
            size_t body_len = buf_size - (body_start - buf);
            resp->body = (char*)malloc(body_len + 1);
            if (resp->body) {
                memcpy(resp->body, body_start, body_len);
                resp->body[body_len] = '\0';
                resp->body_size = body_len;
            }
        } else {
            resp->status_code = 0;
        }
        free(buf);
        OPENAI_LOG_DEBUG("lwIP HTTPS response: status=%d, body_size=%zu", resp->status_code, resp->body_size);
        return resp;
    }
#endif

    /* Plain HTTP branch */
    int sock = (int)(intptr_t)conn;

    /* Build HTTP request for streaming */
    size_t auth_len = req->auth_header ? strlen(req->auth_header) : 0;
    size_t extra_len = req->extra_headers ? strlen(req->extra_headers) : 0;
    size_t header_size = 512 + auth_len + extra_len;
    char* header = (char*)malloc(header_size);
    if (!header) {
        closesocket(sock);
        return NULL;
    }

    int header_len;
    if (req->auth_header) {
        char auth_line[512];
        if (req->auth_mode == 1) {
            snprintf(auth_line, sizeof(auth_line), "x-api-key: %s\r\n", req->auth_header);
        } else {
            snprintf(auth_line, sizeof(auth_line), "Authorization: Bearer %s\r\n", req->auth_header);
        }
        header_len = snprintf(header, header_size,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "%s"
            "Content-Type: application/json\r\n"
            "Accept: text/event-stream\r\n"
            "%s"
            "Content-Length: %zu\r\n"
            "\r\n",
            path, host, auth_line,
            req->extra_headers ? req->extra_headers : "",
            req->body ? req->body_size : 0);
    } else {
        header_len = snprintf(header, header_size,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Accept: text/event-stream\r\n"
            "%s"
            "Content-Length: %zu\r\n"
            "\r\n",
            path, host,
            req->extra_headers ? req->extra_headers : "",
            req->body ? req->body_size : 0);
    }

    /* Send request */
    int total_sent = 0;
    while (total_sent < header_len) {
        int sent = send(sock, header + total_sent, header_len - total_sent, 0);
        if (sent <= 0) {
            OPENAI_LOG_ERROR("lwIP: send header failed");
            free(header);
            closesocket(sock);
            return NULL;
        }
        total_sent += sent;
    }

    /* Send body if present */
    if (req->body && req->body_size > 0) {
        total_sent = 0;
        while (total_sent < (int)req->body_size) {
            int sent = send(sock, req->body + total_sent, req->body_size - total_sent, 0);
            if (sent <= 0) {
                OPENAI_LOG_ERROR("lwIP: send body failed");
                free(header);
                closesocket(sock);
                return NULL;
            }
            total_sent += sent;
        }
    }

    free(header);

    /* Read response */
    size_t buf_capacity = 4096;
    size_t buf_size = 0;
    char* buf = (char*)malloc(buf_capacity);
    if (!buf) {
        closesocket(sock);
        return NULL;
    }

    while (1) {
        /* Ensure we have space before reading */
        if (buf_size >= buf_capacity - 1) {
            buf_capacity *= 2;
            char* new_buf = (char*)realloc(buf, buf_capacity);
            if (!new_buf) {
                free(buf);
                closesocket(sock);
                return NULL;
            }
            buf = new_buf;
        }

        int to_read = (int)(buf_capacity - buf_size - 1);
        int received = recv(sock, buf + buf_size, to_read, 0);
        if (received < 0) {
            OPENAI_LOG_WARN("lwIP: recv failed");
            break;
        }
        if (received == 0) break;
        buf_size += received;
        buf[buf_size] = '\0';

        /* Check for end of headers */
        char* header_end = strstr(buf, "\r\n\r\n");
        if (header_end) {
            /* Parse Content-Length to read remaining body (case-insensitive per RFC 7230) */
            char* cl_str = openai_strcasestr(buf, "content-length:");
            if (cl_str) {
                cl_str += 15;
                while (*cl_str == ' ') cl_str++;
                int content_length = atoi(cl_str);
                size_t body_received = buf_size - (size_t)(header_end + 4 - buf);
                while ((int)body_received < content_length) {
                    if (buf_size >= buf_capacity - 1) {
                        buf_capacity *= 2;
                        char* new_buf2 = (char*)realloc(buf, buf_capacity);
                        if (!new_buf2) break;
                        buf = new_buf2;
                    }
                    int to_read2 = (int)(buf_capacity - buf_size - 1);
                    if (to_read2 > content_length - (int)body_received)
                        to_read2 = content_length - (int)body_received;
                    int received2 = recv(sock, buf + buf_size, to_read2, 0);
                    if (received2 <= 0) break;
                    buf_size += received2;
                    buf[buf_size] = '\0';
                    body_received += received2;
                }
            }
            break;
        }
    }

    closesocket(sock);

    OpenAI_HTTPResponse* resp = (OpenAI_HTTPResponse*)calloc(1, sizeof(OpenAI_HTTPResponse));
    if (!resp) {
        free(buf);
        return NULL;
    }

    /* Parse HTTP response */
    char* body_start = strstr(buf, "\r\n\r\n");
    if (body_start) {
        body_start += 4;

        /* Parse status line */
        char* status_line = buf;
        char* line_end = strstr(buf, "\r\n");
        if (line_end) {
            *line_end = '\0';

            /* HTTP/1.1 200 OK */
            char* status = strchr(status_line, ' ');
            if (status) {
                status++;
                resp->status_code = atoi(status);
            }
        }

        size_t body_len = buf_size - (body_start - buf);
        resp->body = (char*)malloc(body_len + 1);
        if (resp->body) {
            memcpy(resp->body, body_start, body_len);
            resp->body[body_len] = '\0';
            resp->body_size = body_len;
        }
    } else {
        resp->status_code = 0;
        resp->body = NULL;
        resp->body_size = 0;
    }

    free(buf);
    return resp;
}

#endif /* OPENAI_USE_LWIP */