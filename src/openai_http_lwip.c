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
    volatile uint8_t overflow;
} altcp_rx_ring_t;

/* Per-PCB receive ring buffer (single-request-at-a-time) */
static altcp_rx_ring_t s_rx_ring;
static struct altcp_pcb *s_rx_pcb = NULL;
static volatile int s_altcp_connect_state = 0;
static volatile err_t s_altcp_connect_error = ERR_OK;

static err_t altcp_connected_cb(void *arg, struct altcp_pcb *pcb, err_t err) {
    (void)arg;
    (void)pcb;
    s_altcp_connect_error = err;
    s_altcp_connect_state = (err == ERR_OK) ? 1 : -1;
    return ERR_OK;
}

static void altcp_error_cb(void *arg, err_t err) {
    (void)arg;
    s_altcp_connect_error = err;
    s_altcp_connect_state = -1;
    s_rx_pcb = NULL;
}

static int altcp_wait_connected(void) {
    uint32_t timeout_ms = OPENAI_TIMEOUT * 1000;
    uint32_t start = sys_now();
    while (s_altcp_connect_state == 0) {
        if ((sys_now() - start) >= timeout_ms) {
            s_altcp_connect_error = ERR_TIMEOUT;
            return -1;
        }
        sys_check_timeouts();
#if !NO_SYS
        sys_msleep(1);
#endif
    }
    return s_altcp_connect_state == 1 ? 0 : -1;
}

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
    u16_t copied_total = 0;
    for (q = p; q != NULL; q = q->next) {
        uint32_t h = s_rx_ring.head;
        uint32_t t = s_rx_ring.tail;
        uint32_t free_space = (t + ALTCP_RX_BUF_SIZE - h - 1) % ALTCP_RX_BUF_SIZE;
        uint32_t to_copy = q->len < free_space ? q->len : free_space;
        if (to_copy < q->len) {
            s_rx_ring.overflow = 1;
            s_rx_ring.data_ready = 3;
            break;
        }
        uint32_t chunk1 = ALTCP_RX_BUF_SIZE - h;
        if (chunk1 > to_copy) chunk1 = to_copy;
        memcpy(s_rx_ring.buf + h, q->payload, chunk1);
        if (chunk1 < to_copy) {
            memcpy(s_rx_ring.buf, (uint8_t *)q->payload + chunk1, to_copy - chunk1);
        }
        s_rx_ring.head = (h + to_copy) % ALTCP_RX_BUF_SIZE;
        copied_total = (u16_t)(copied_total + to_copy);
    }
    if (!s_rx_ring.overflow) {
        s_rx_ring.data_ready = 1;
    }
    if (copied_total > 0) {
        altcp_recved(pcb, copied_total);
    }
    pbuf_free(p);
    return s_rx_ring.overflow ? ERR_MEM : ERR_OK;
}

static void altcp_read_install_recv(struct altcp_pcb *pcb) {
    s_rx_pcb = pcb;
    s_rx_ring.head = 0;
    s_rx_ring.tail = 0;
    s_rx_ring.data_ready = 0;
    s_rx_ring.overflow = 0;
    altcp_recv(pcb, altcp_recv_cb);
}

static int altcp_read(struct altcp_pcb *pcb, void *buf, size_t len) {
    (void)pcb;
    if (!s_rx_pcb) return -1;

    uint32_t t = s_rx_ring.tail;
    uint32_t h = s_rx_ring.head;

    if (s_rx_ring.overflow) {
        return -1;
    }

    /* Wait for data or EOF */
    uint32_t timeout_ms = OPENAI_TIMEOUT * 1000;
    uint32_t start = sys_now();
    while (t == h && s_rx_ring.data_ready != 2) {
        if ((sys_now() - start) >= timeout_ms) {
            OPENAI_LOG_ERROR("lwIP ALTCP receive timed out");
            return -1;
        }
        sys_check_timeouts();
#if !NO_SYS
        sys_msleep(1);
#endif
        if (s_rx_ring.overflow) return -1;
    }

    if (s_rx_ring.data_ready == 2 && t == h) {
        return 0; /* EOF */
    }

    /* Read from ring buffer using memcpy (not byte-by-byte) */
    size_t available = (h + ALTCP_RX_BUF_SIZE - t) % ALTCP_RX_BUF_SIZE;
    size_t to_read = len < available ? len : available;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t chunk1 = ALTCP_RX_BUF_SIZE - t;
    if (chunk1 > to_read) chunk1 = (uint32_t)to_read;
    memcpy(dst, s_rx_ring.buf + t, chunk1);
    if (chunk1 < to_read) {
        memcpy(dst + chunk1, s_rx_ring.buf, to_read - chunk1);
    }
    t = (t + (uint32_t)to_read) % ALTCP_RX_BUF_SIZE;
    s_rx_ring.tail = t;
    if (t == h) s_rx_ring.data_ready = 0;

    return (int)to_read;
}

#endif /* LWIP_ALTCP_TLS */

#include "openai_http.h"

/* Case-insensitive substring search (ASCII-only, no locale dependency) */
static char* openai_strcasestr(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return NULL;
    size_t needle_len = strlen(needle);
    for (; *haystack; haystack++) {
        size_t i;
        for (i = 0; i < needle_len && haystack[i]; i++) {
            char a = haystack[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
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

/* ============================================================
 * Helper functions shared by request and request_stream
 * ============================================================ */

/* Reliable send over plain TCP socket */
static int send_all_tcp(int sock, const char* data, size_t len) {
    int total_sent = 0;
    while (total_sent < (int)len) {
        int sent = send(sock, data + total_sent, len - total_sent, 0);
        if (sent <= 0) {
            OPENAI_LOG_ERROR("lwIP: send failed");
            return -1;
        }
        total_sent += sent;
    }
    return 0;
}

/* Build HTTP request header string. Caller must free(). */
static char* build_http_header(OpenAI_HTTPRequest* req, const char* host,
                                const char* path, const char* accept_type) {
    size_t host_len = strlen(host);
    size_t path_len = strlen(path);
    size_t auth_len = req->auth_header ? strlen(req->auth_header) : 0;

    /* Initial capacity overflow protection */
    if (host_len > SIZE_MAX - path_len ||
        host_len + path_len > SIZE_MAX - 64) {
        return NULL;
    }

    size_t extra_len = host_len + path_len + 64;

    if (auth_len > SIZE_MAX - extra_len ||
        auth_len + extra_len > SIZE_MAX - 512) {
        return NULL;
    }

    size_t header_size = 512 + auth_len + extra_len;
    char* header = (char*)malloc(header_size);
    if (!header) return NULL;

    int ret;
    size_t needed;

    if (req->auth_header) {
        char auth_line[512];
        if (req->auth_mode == 1) {
            snprintf(auth_line, sizeof(auth_line), "x-api-key: %s\r\n", req->auth_header);
        } else {
            snprintf(auth_line, sizeof(auth_line), "Authorization: Bearer %s\r\n", req->auth_header);
        }
        ret = snprintf(header, header_size,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "%s"
            "Content-Type: application/json\r\n"
            "Accept: %s\r\n"
            "%s"
            "Content-Length: %zu\r\n"
            "\r\n",
            path, host, auth_line, accept_type,
            req->extra_headers ? req->extra_headers : "",
            req->body ? req->body_size : 0);
    } else {
        ret = snprintf(header, header_size,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Accept: %s\r\n"
            "%s"
            "Content-Length: %zu\r\n"
            "\r\n",
            path, host, accept_type,
            req->extra_headers ? req->extra_headers : "",
            req->body ? req->body_size : 0);
    }

    if (ret < 0) {
        free(header);
        return NULL;  /* Encoding error */
    }
    if ((uintmax_t)ret >= (uintmax_t)SIZE_MAX) {
        free(header);
        return NULL;  /* Extreme integer overflow */
    }
    needed = (size_t)ret + 1;
    if (needed > header_size) {
        char* new_header = (char*)realloc(header, needed);
        if (!new_header) { free(header); return NULL; }
        header = new_header;

        if (req->auth_header) {
            char auth_line[512];
            if (req->auth_mode == 1) {
                snprintf(auth_line, sizeof(auth_line), "x-api-key: %s\r\n", req->auth_header);
            } else {
                snprintf(auth_line, sizeof(auth_line), "Authorization: Bearer %s\r\n", req->auth_header);
            }
            ret = snprintf(header, needed,
                "POST %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "%s"
                "Content-Type: application/json\r\n"
                "Accept: %s\r\n"
                "%s"
                "Content-Length: %zu\r\n"
                "\r\n",
                path, host, auth_line, accept_type,
                req->extra_headers ? req->extra_headers : "",
                req->body ? req->body_size : 0);
        } else {
            ret = snprintf(header, needed,
                "POST %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Content-Type: application/json\r\n"
                "Accept: %s\r\n"
                "%s"
                "Content-Length: %zu\r\n"
                "\r\n",
                path, host, accept_type,
                req->extra_headers ? req->extra_headers : "",
                req->body ? req->body_size : 0);
        }
        if (ret < 0 || (size_t)ret >= needed) {
            free(header);
            return NULL;
        }
    }
    return header;
}

static char* decode_chunked_body(const char* body, size_t body_len, size_t* out_len) {
    char* decoded = (char*)malloc(body_len + 1);
    if (!decoded) return NULL;

    size_t in_pos = 0;
    size_t out_pos = 0;
    while (in_pos < body_len) {
        char* endptr = NULL;
        unsigned long chunk_size = strtoul(body + in_pos, &endptr, 16);
        if (endptr == body + in_pos) {
            free(decoded);
            return NULL;
        }
        while ((size_t)(endptr - body) < body_len && *endptr != '\n') endptr++;
        if ((size_t)(endptr - body) >= body_len) {
            free(decoded);
            return NULL;
        }
        in_pos = (size_t)(endptr - body) + 1;
        if (chunk_size == 0) {
            decoded[out_pos] = '\0';
            *out_len = out_pos;
            return decoded;
        }
        if (chunk_size > body_len - in_pos) {
            free(decoded);
            return NULL;
        }
        memcpy(decoded + out_pos, body + in_pos, chunk_size);
        out_pos += chunk_size;
        in_pos += chunk_size;
        if (in_pos < body_len && body[in_pos] == '\r') in_pos++;
        if (in_pos < body_len && body[in_pos] == '\n') in_pos++;
    }

    free(decoded);
    return NULL;
}

/* Parse HTTP response from raw buffer into OpenAI_HTTPResponse */
static OpenAI_HTTPResponse* parse_http_response(char* buf, size_t buf_size) {
    OpenAI_HTTPResponse* resp = (OpenAI_HTTPResponse*)calloc(1, sizeof(OpenAI_HTTPResponse));
    if (!resp) {
        return NULL;
    }

    char* body_start = strstr(buf, "\r\n\r\n");
    if (body_start) {
        int is_chunked = 0;
        char* transfer_encoding = openai_strcasestr(buf, "transfer-encoding:");
        if (transfer_encoding && transfer_encoding < body_start &&
            openai_strcasestr(transfer_encoding, "chunked") &&
            openai_strcasestr(transfer_encoding, "chunked") < body_start) {
            is_chunked = 1;
        }
        body_start += 4;

        /* Parse status line */
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

        size_t body_len = buf_size - (size_t)(body_start - buf);
        if (is_chunked) {
            resp->body = decode_chunked_body(body_start, body_len, &resp->body_size);
        } else {
            resp->body = (char*)malloc(body_len + 1);
            if (resp->body) {
                memcpy(resp->body, body_start, body_len);
                resp->body[body_len] = '\0';
                resp->body_size = body_len;
            }
        }
    } else {
        resp->status_code = 0;
        resp->body = NULL;
        resp->body_size = 0;
    }

    return resp;
}

/*
 * Read response into buffer. Handles both Content-Length and streaming (no CL).
 * For non-streaming (is_stream=0): reads Content-Length bytes then stops.
 * For streaming (is_stream=1): reads until connection close or timeout.
 *
 * recv_fn: function to read data (recv for plain TCP, altcp_read for TLS)
 * recv_arg: socket fd or altcp_pcb pointer
 */
typedef int (*recv_fn_t)(void* arg, void* buf, size_t len);

static char* recv_response(recv_fn_t recv_fn, void* recv_arg, int is_stream,
                            size_t* out_size) {
    size_t buf_capacity = 4096;
    size_t buf_size = 0;
    char* buf = (char*)malloc(buf_capacity);
    if (!buf) return NULL;

    int header_found = 0;
    int content_length = -1; /* -1 = not found */

    while (1) {
        /* Ensure space before reading */
        if (buf_size >= buf_capacity - 1) {
            if (buf_capacity > SIZE_MAX / 2) {
                free(buf);
                return NULL;
            }
            buf_capacity *= 2;
            char* new_buf = (char*)realloc(buf, buf_capacity);
            if (!new_buf) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }

        size_t to_read = (buf_capacity - buf_size - 1) < 4096 ?
                         (buf_capacity - buf_size - 1) : 4096;
        int received = recv_fn(recv_arg, buf + buf_size, to_read);
        if (received < 0) {
            free(buf);
            return NULL;
        }
        if (received == 0) break;
        buf_size += received;
        buf[buf_size] = '\0';

        /* Parse headers if not yet done */
        if (!header_found) {
            char* header_end = strstr(buf, "\r\n\r\n");
            if (header_end) {
                header_found = 1;
                char* cl_str = openai_strcasestr(buf, "content-length:");
                if (cl_str) {
                    cl_str += 15;
                    while (*cl_str == ' ') cl_str++;
                    content_length = atoi(cl_str);
                }

                if (!is_stream && content_length >= 0) {
                    /* Non-streaming with Content-Length: read exact body */
                    size_t body_received = buf_size - (size_t)(header_end + 4 - buf);
                    while ((int)body_received < content_length) {
                        if (buf_size >= buf_capacity - 1) {
                            buf_capacity *= 2;
                            char* new_buf2 = (char*)realloc(buf, buf_capacity);
                            if (!new_buf2) break;
                            buf = new_buf2;
                        }
                        size_t to_read2 = (buf_capacity - buf_size - 1) < 4096 ?
                                          (buf_capacity - buf_size - 1) : 4096;
                        if ((int)to_read2 > content_length - (int)body_received)
                            to_read2 = (size_t)(content_length - (int)body_received);
                        int received2 = recv_fn(recv_arg, buf + buf_size, to_read2);
                        if (received2 < 0) {
                            free(buf);
                            return NULL;
                        }
                        if (received2 == 0) break;
                        buf_size += received2;
                        buf[buf_size] = '\0';
                        body_received += received2;
                    }
                    break; /* Done reading non-streaming response */
                }
                /* For streaming or no Content-Length: continue reading until EOF */
            }
        }
        /* For streaming with headers found: keep reading until recv returns 0 */
    }

    *out_size = buf_size;
    return buf;
}

/* Wrappers to match recv_fn_t signature */
static int recv_tcp_wrapper(void* arg, void* buf, size_t len) {
    return recv((int)(intptr_t)arg, buf, len, 0);
}

#ifdef LWIP_ALTCP_TLS
static int altcp_read_wrapper(void* arg, void* buf, size_t len) {
    return altcp_read((struct altcp_pcb*)arg, buf, len);
}
#endif

/*
 * Core HTTP request function.
 * is_stream=0: standard request (reads Content-Length body)
 * is_stream=1: streaming request (reads until connection close)
 */
static OpenAI_HTTPResponse* do_http_request(OpenAI_HTTPRequest* req, int is_stream) {
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

    const char* accept_type = is_stream ? "text/event-stream" : "application/json";
    char* header = build_http_header(req, host, path, accept_type);
    if (!header) {
#ifdef LWIP_ALTCP_TLS
        if (is_https && OPENAI_USE_TLS) {
            altcp_close((struct altcp_pcb*)conn);
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
        } else
#endif
        {
            closesocket((int)(intptr_t)conn);
        }
        return NULL;
    }

    char* buf = NULL;
    size_t buf_size = 0;

#ifdef LWIP_ALTCP_TLS
    if (is_https && OPENAI_USE_TLS) {
        struct altcp_pcb* pcb = (struct altcp_pcb*)conn;

        /* DNS resolution and connect */
        ip_addr_t ipaddr;
        struct hostent* server = gethostbyname(host);
        if (!server) {
            OPENAI_LOG_ERROR("do_http_request: DNS resolution failed for host: %s", host);
            free(header);
            altcp_close(pcb);
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            return NULL;
        }
        memcpy(&ipaddr, server->h_addr, server->h_length);

        s_altcp_connect_state = 0;
        s_altcp_connect_error = ERR_OK;
        altcp_err(pcb, altcp_error_cb);
        err_t err = altcp_connect(pcb, &ipaddr, port, altcp_connected_cb);
        if (err != ERR_OK) {
            OPENAI_LOG_ERROR("do_http_request: ALTCP connect failed to %s:%d (err=%d)", host, port, err);
            free(header);
            altcp_err(pcb, NULL);
            altcp_close(pcb);
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            return NULL;
        }

        if (altcp_wait_connected() != 0) {
            OPENAI_LOG_ERROR("do_http_request: ALTCP connect/TLS handshake failed for %s:%d (err=%d)",
                             host, port, s_altcp_connect_error);
            free(header);
            if (s_altcp_connect_state != -1) {
                altcp_err(pcb, NULL);
                altcp_close(pcb);
            }
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            return NULL;
        }

        /* Install recv callback for altcp_read compatibility */
        altcp_read_install_recv(pcb);

        /* Send header */
        size_t header_len = strlen(header);
        err = altcp_write(pcb, header, header_len, TCP_WRITE_FLAG_COPY);
        free(header);
        if (err != ERR_OK) {
            OPENAI_LOG_ERROR("do_http_request: ALTCP write header failed (err=%d)", err);
            if (s_altcp_connect_state != -1) {
                altcp_err(pcb, NULL);
                altcp_close(pcb);
            }
            if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
            return NULL;
        }

        /* Send body if present */
        if (req->body && req->body_size > 0) {
            err = altcp_write(pcb, req->body, req->body_size, TCP_WRITE_FLAG_COPY);
            if (err != ERR_OK) {
                OPENAI_LOG_ERROR("do_http_request: ALTCP write body failed (err=%d)", err);
                if (s_altcp_connect_state != -1) {
                    altcp_err(pcb, NULL);
                    altcp_close(pcb);
                }
                if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
                return NULL;
            }
        }

        /* Read response */
        buf = recv_response(altcp_read_wrapper, (void*)pcb, is_stream, &buf_size);

        if (s_altcp_connect_state != -1) {
            altcp_err(pcb, NULL);
            altcp_recv(pcb, NULL);
            altcp_close(pcb);
        }
        if (s_tls_config) { altcp_tls_free_config(s_tls_config); s_tls_config = NULL; }
        s_rx_pcb = NULL;
    } else
#endif
    {
        /* Plain TCP branch */
        int sock = (int)(intptr_t)conn;

        /* Send header */
        size_t header_len = strlen(header);
        if (send_all_tcp(sock, header, header_len) != 0) {
            free(header);
            closesocket(sock);
            return NULL;
        }
        free(header);

        /* Send body if present */
        if (req->body && req->body_size > 0) {
            if (send_all_tcp(sock, req->body, req->body_size) != 0) {
                closesocket(sock);
                return NULL;
            }
        }

        /* Read response */
        buf = recv_response(recv_tcp_wrapper, (void*)(intptr_t)sock, is_stream, &buf_size);

        closesocket(sock);
    }

    if (!buf) {
        return NULL;
    }

    /* Parse HTTP response */
    OpenAI_HTTPResponse* resp = parse_http_response(buf, buf_size);
    free(buf);

    OPENAI_LOG_DEBUG("lwIP HTTP %s response: status=%d, body_size=%zu",
        is_stream ? "stream" : "", resp ? resp->status_code : 0,
        resp ? resp->body_size : 0);
    return resp;
}

OpenAI_HTTPResponse* openai_http_request(OpenAI_HTTPRequest* req) {
    return do_http_request(req, 0);
}

OpenAI_HTTPResponse* openai_http_request_stream(OpenAI_HTTPRequest* req) {
    return do_http_request(req, 1);
}

#define OPENAI_STREAM_HEADER_SIZE 8192
#define OPENAI_STREAM_RAW_SIZE 4096

struct OpenAI_HTTPStream {
    int is_tls;
    int sock;
#ifdef LWIP_ALTCP_TLS
    struct altcp_pcb* pcb;
    struct altcp_tls_config* tls_config;
#endif
    int chunked;
    long content_remaining;
    size_t chunk_remaining;
    int consume_chunk_crlf;
    int eof;
    unsigned char raw[OPENAI_STREAM_RAW_SIZE];
    size_t raw_pos;
    size_t raw_size;
};

static int lwip_stream_transport_read(OpenAI_HTTPStream* stream, void* buffer, size_t size) {
#ifdef LWIP_ALTCP_TLS
    if (stream->is_tls) return altcp_read(stream->pcb, buffer, size);
#endif
    return recv(stream->sock, buffer, size, 0);
}

static int lwip_stream_fill_raw(OpenAI_HTTPStream* stream) {
    if (stream->raw_pos < stream->raw_size) return 1;
    stream->raw_pos = 0;
    stream->raw_size = 0;
    int received = lwip_stream_transport_read(stream, stream->raw, sizeof(stream->raw));
    if (received <= 0) {
        if (received < 0) return -1;
        stream->eof = 1;
        return 0;
    }
    stream->raw_size = (size_t)received;
    return 1;
}

static int lwip_stream_read_chunk_size(OpenAI_HTTPStream* stream) {
    char line[32];
    size_t length = 0;
    while (1) {
        int ready = lwip_stream_fill_raw(stream);
        if (ready <= 0) return -1;
        unsigned char c = stream->raw[stream->raw_pos++];
        if (c == '\n') break;
        if (c != '\r') {
            if (length + 1 >= sizeof(line)) return -1;
            line[length++] = (char)c;
        }
    }
    line[length] = '\0';
    char* extension = strchr(line, ';');
    if (extension) *extension = '\0';
    char* end = NULL;
    unsigned long chunk_size = strtoul(line, &end, 16);
    if (end == line || *end != '\0') return -1;
    stream->chunk_remaining = (size_t)chunk_size;
    if (chunk_size == 0) stream->eof = 1;
    return 0;
}

static int lwip_stream_consume_crlf(OpenAI_HTTPStream* stream) {
    int saw_lf = 0;
    while (!saw_lf) {
        int ready = lwip_stream_fill_raw(stream);
        if (ready <= 0) return -1;
        unsigned char c = stream->raw[stream->raw_pos++];
        if (c == '\n') saw_lf = 1;
        else if (c != '\r') return -1;
    }
    stream->consume_chunk_crlf = 0;
    return 0;
}

static int lwip_stream_send_request(OpenAI_HTTPStream* stream,
                                    const char* header, const OpenAI_HTTPRequest* req) {
#ifdef LWIP_ALTCP_TLS
    if (stream->is_tls) {
        err_t err = altcp_write(stream->pcb, header, strlen(header), TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK && req->body && req->body_size > 0) {
            err = altcp_write(stream->pcb, req->body, req->body_size, TCP_WRITE_FLAG_COPY);
        }
        if (err == ERR_OK) err = altcp_output(stream->pcb);
        return err == ERR_OK ? 0 : -1;
    }
#endif
    if (send_all_tcp(stream->sock, header, strlen(header)) != 0) return -1;
    if (req->body && req->body_size > 0 &&
        send_all_tcp(stream->sock, req->body, req->body_size) != 0) return -1;
    return 0;
}

OpenAI_HTTPStream* openai_http_stream_open(OpenAI_HTTPRequest* req, int* status_code) {
    if (!req || !req->url || !status_code) return NULL;

    char host[256] = {0};
    char path[512] = {0};
    int port = 443;
    int is_https = (strncmp(req->url, "https://", 8) == 0);
    if (openai_http_parse_url(req->url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        return NULL;
    }

    void* conn = openai_http_connect(host, port, is_https);
    if (!conn) return NULL;
    OpenAI_HTTPStream* stream = (OpenAI_HTTPStream*)calloc(1, sizeof(OpenAI_HTTPStream));
    if (!stream) {
#ifdef LWIP_ALTCP_TLS
        if (is_https && OPENAI_USE_TLS) altcp_close((struct altcp_pcb*)conn);
        else
#endif
        closesocket((int)(intptr_t)conn);
        return NULL;
    }
    stream->sock = -1;
    stream->content_remaining = -1;

#ifdef LWIP_ALTCP_TLS
    if (is_https && OPENAI_USE_TLS) {
        stream->is_tls = 1;
        stream->pcb = (struct altcp_pcb*)conn;
        stream->tls_config = s_tls_config;
        s_tls_config = NULL;

        ip_addr_t ipaddr;
        struct hostent* server = gethostbyname(host);
        if (!server) {
            openai_http_stream_close(stream);
            return NULL;
        }
        memcpy(&ipaddr, server->h_addr, server->h_length);
        s_altcp_connect_state = 0;
        s_altcp_connect_error = ERR_OK;
        altcp_err(stream->pcb, altcp_error_cb);
        err_t err = altcp_connect(stream->pcb, &ipaddr, port, altcp_connected_cb);
        if (err != ERR_OK || altcp_wait_connected() != 0) {
            openai_http_stream_close(stream);
            return NULL;
        }
        altcp_read_install_recv(stream->pcb);
    } else
#endif
    {
        stream->sock = (int)(intptr_t)conn;
        struct timeval timeout;
        timeout.tv_sec = OPENAI_TIMEOUT;
        timeout.tv_usec = 0;
        setsockopt(stream->sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }

    char* header = build_http_header(req, host, path, "text/event-stream");
    if (!header || lwip_stream_send_request(stream, header ? header : "", req) != 0) {
        free(header);
        openai_http_stream_close(stream);
        return NULL;
    }
    free(header);

    char response_header[OPENAI_STREAM_HEADER_SIZE + 1];
    size_t header_size = 0;
    size_t header_end = 0;
    while (header_end == 0) {
        unsigned char incoming[1024];
        int received = lwip_stream_transport_read(stream, incoming, sizeof(incoming));
        if (received <= 0 || header_size + (size_t)received > OPENAI_STREAM_HEADER_SIZE) {
            openai_http_stream_close(stream);
            return NULL;
        }
        memcpy(response_header + header_size, incoming, (size_t)received);
        header_size += (size_t)received;
        response_header[header_size] = '\0';
        char* end = strstr(response_header, "\r\n\r\n");
        if (end) {
            header_end = (size_t)(end - response_header) + 4;
            size_t body_size = header_size - header_end;
            if (body_size > sizeof(stream->raw)) {
                openai_http_stream_close(stream);
                return NULL;
            }
            if (body_size > 0) memcpy(stream->raw, response_header + header_end, body_size);
            stream->raw_size = body_size;
        }
    }

    char* status = strchr(response_header, ' ');
    *status_code = status ? atoi(status + 1) : 0;
    char* transfer = openai_strcasestr(response_header, "transfer-encoding:");
    char* chunked = transfer ? openai_strcasestr(transfer, "chunked") : NULL;
    stream->chunked = transfer && transfer < response_header + header_end &&
                      chunked && chunked < response_header + header_end;
    char* content_length = openai_strcasestr(response_header, "content-length:");
    if (content_length && content_length < response_header + header_end) {
        stream->content_remaining = strtol(content_length + 15, NULL, 10);
    }
    return stream;
}

int openai_http_stream_read(OpenAI_HTTPStream* stream, void* buffer, size_t size) {
    if (!stream || !buffer || size == 0) return -1;
    unsigned char* output = (unsigned char*)buffer;

    if (!stream->chunked) {
        if (stream->content_remaining == 0) return 0;
        int ready = lwip_stream_fill_raw(stream);
        if (ready <= 0) return ready;
        size_t available = stream->raw_size - stream->raw_pos;
        size_t count = size < available ? size : available;
        if (stream->content_remaining >= 0 && count > (size_t)stream->content_remaining) {
            count = (size_t)stream->content_remaining;
        }
        memcpy(output, stream->raw + stream->raw_pos, count);
        stream->raw_pos += count;
        if (stream->content_remaining >= 0) stream->content_remaining -= (long)count;
        return (int)count;
    }

    while (!stream->eof) {
        if (stream->consume_chunk_crlf && lwip_stream_consume_crlf(stream) != 0) return -1;
        if (stream->chunk_remaining == 0) {
            if (lwip_stream_read_chunk_size(stream) != 0) return -1;
            if (stream->eof) return 0;
        }
        int ready = lwip_stream_fill_raw(stream);
        if (ready <= 0) return -1;
        size_t available = stream->raw_size - stream->raw_pos;
        size_t count = size < available ? size : available;
        if (count > stream->chunk_remaining) count = stream->chunk_remaining;
        memcpy(output, stream->raw + stream->raw_pos, count);
        stream->raw_pos += count;
        stream->chunk_remaining -= count;
        if (stream->chunk_remaining == 0) stream->consume_chunk_crlf = 1;
        return (int)count;
    }
    return 0;
}

void openai_http_stream_close(OpenAI_HTTPStream* stream) {
    if (!stream) return;
#ifdef LWIP_ALTCP_TLS
    if (stream->is_tls) {
        if (stream->pcb && s_altcp_connect_state != -1) {
            altcp_err(stream->pcb, NULL);
            altcp_recv(stream->pcb, NULL);
            altcp_close(stream->pcb);
        }
        if (stream->tls_config) altcp_tls_free_config(stream->tls_config);
        if (s_rx_pcb == stream->pcb) s_rx_pcb = NULL;
    } else
#endif
    if (stream->sock >= 0) closesocket(stream->sock);
    free(stream);
}

void openai_http_response_free(OpenAI_HTTPResponse* resp) {
    if (resp) {
        if (resp->body) free(resp->body);
        free(resp);
    }
}

#endif /* OPENAI_USE_LWIP */
