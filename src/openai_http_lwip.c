/*
 * lwIP HTTP backend for embedded systems
 * Requires lwIP socket API (include lwip/sockets.h)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* lwIP includes - adjust paths as needed for your platform */
#ifdef OPENAI_USE_LWIP
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/tls.h>
#endif

#include "openai_http.h"
#include "openai_config.h"

/* TLS context for secure connections */
typedef struct {
    void* conn;          /* altcp_pcb* for TLS, or int sock for plain TCP */
    int is_https;
} OpenAI_TLSContext;

static int tls_initialized = 0;

int openai_http_init(void) {
#ifdef OPENAI_USE_LWIP
    /* lwIP initialization if needed */
    tls_initialized = 1;
#endif
    return 0;
}

void openai_http_cleanup(void) {
#ifdef OPENAI_USE_LWIP
    tls_initialized = 0;
#endif
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
#ifdef OPENAI_USE_LWIP
#ifdef LWIP_ALTCP_TLS
    if (is_https && OPENAI_USE_TLS) {
        /* Use ALTCP + mbedTLS for HTTPS */
        struct altcp_tls_config* tls_config = altcp_tls_create_config_client(NULL, 0);
        if (!tls_config) return NULL;

        struct altcp_pcb* pcb = altcp_tls_new(tls_config, IPADDR_TYPE_V4);
        if (!pcb) {
            altcp_tls_free_config(tls_config);
            return NULL;
        }

        /* Store tls_config in pcb's state for later cleanup */
        pcb->state = (void*)tls_config;

        return (void*)pcb;
    }
#else
    (void)is_https;
#endif

    /* Plain TCP connection */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    struct hostent* server = gethostbyname(host);
    if (!server) {
        closesocket(sock);
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        closesocket(sock);
        return NULL;
    }

    return (void*)(intptr_t)sock;
#else
    (void)host;
    (void)port;
    (void)is_https;
    return NULL;
#endif
}

OpenAI_HTTPResponse* openai_http_request(OpenAI_HTTPRequest* req) {
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
        struct altcp_tls_config* tls_config = (struct altcp_tls_config*)pcb->state;

        /* DNS resolution and connect */
        ip_addr_t ipaddr;
        struct hostent* server = gethostbyname(host);
        if (!server) {
            altcp_tls_free_config(tls_config);
            altcp_close(pcb);
            return NULL;
        }
        memcpy(&ipaddr, server->h_addr, server->h_length);

        err_t err = altcp_connect(pcb, &ipaddr, port, NULL);
        if (err != ERR_OK) {
            altcp_tls_free_config(tls_config);
            altcp_close(pcb);
            return NULL;
        }

        /* Build HTTP request */
        size_t header_size = 512 + strlen(req->auth_header ? req->auth_header : "");
        char* header = (char*)malloc(header_size);
        if (!header) {
            altcp_tls_free_config(tls_config);
            altcp_close(pcb);
            return NULL;
        }

        int header_len = snprintf(header, header_size,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Authorization: Bearer %s\r\n"
            "Content-Type: application/json\r\n"
            "Accept: application/json\r\n"
            "Content-Length: %zu\r\n"
            "\r\n",
            path, host, req->auth_header ? req->auth_header : "",
            req->body ? req->body_size : 0);

        /* Send header */
        int total_sent = 0;
        while (total_sent < header_len) {
            int sent = altcp_write(pcb, header + total_sent, header_len - total_sent, 0);
            if (sent < 0) {
                free(header);
                altcp_tls_free_config(tls_config);
                altcp_close(pcb);
                return NULL;
            }
            total_sent += sent;
        }
        free(header);

        /* Send body if present */
        if (req->body && req->body_size > 0) {
            total_sent = 0;
            while (total_sent < (int)req->body_size) {
                int sent = altcp_write(pcb, req->body + total_sent, req->body_size - total_sent, 0);
                if (sent < 0) {
                    altcp_tls_free_config(tls_config);
                    altcp_close(pcb);
                    return NULL;
                }
                total_sent += sent;
            }
        }

        /* Read response */
        size_t buf_capacity = 4096;
        size_t buf_size = 0;
        char* buf = (char*)malloc(buf_capacity);
        if (!buf) {
            altcp_tls_free_config(tls_config);
            altcp_close(pcb);
            return NULL;
        }

        while (1) {
            if (buf_size >= buf_capacity - 1) {
                buf_capacity *= 2;
                char* new_buf = (char*)realloc(buf, buf_capacity);
                if (!new_buf) {
                    free(buf);
                    altcp_tls_free_config(tls_config);
                    altcp_close(pcb);
                    return NULL;
                }
                buf = new_buf;
            }

            int received = altcp_read(pcb, buf + buf_size, buf_capacity - buf_size - 1);
            if (received <= 0) break;
            buf_size += received;
            buf[buf_size] = '\0';

            char* header_end = strstr(buf, "\r\n\r\n");
            if (header_end) break;
        }

        altcp_tls_free_config(tls_config);
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
        return resp;
    }
#endif

    /* Plain HTTP branch */
    int sock = (int)(intptr_t)conn;

    /* Build HTTP request */
    size_t header_size = 512 + strlen(req->auth_header ? req->auth_header : "");
    char* header = (char*)malloc(header_size);
    if (!header) {
#ifdef OPENAI_USE_LWIP
        closesocket(sock);
#endif
        return NULL;
    }

    int header_len = snprintf(header, header_size,
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Accept: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        path, host, req->auth_header ? req->auth_header : "",
        req->body ? req->body_size : 0);

    /* Send request */
    int total_sent = 0;
    while (total_sent < header_len) {
        int sent = send(sock, header + total_sent, header_len - total_sent, 0);
        if (sent < 0) {
            free(header);
#ifdef OPENAI_USE_LWIP
            closesocket(sock);
#endif
            return NULL;
        }
        total_sent += sent;
    }

    /* Send body if present */
    if (req->body && req->body_size > 0) {
        total_sent = 0;
        while (total_sent < (int)req->body_size) {
            int sent = send(sock, req->body + total_sent, req->body_size - total_sent, 0);
            if (sent < 0) {
                free(header);
#ifdef OPENAI_USE_LWIP
                closesocket(sock);
#endif
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
#ifdef OPENAI_USE_LWIP
        closesocket(sock);
#endif
        return NULL;
    }

    while (1) {
        if (buf_size >= buf_capacity - 1) {
            buf_capacity *= 2;
            char* new_buf = (char*)realloc(buf, buf_capacity);
            if (!new_buf) {
                free(buf);
#ifdef OPENAI_USE_LWIP
                closesocket(sock);
#endif
                return NULL;
            }
            buf = new_buf;
        }

        int received = recv(sock, buf + buf_size, buf_capacity - buf_size - 1, 0);
        if (received <= 0) break;
        buf_size += received;
        buf[buf_size] = '\0';

        /* Check for end of headers */
        char* header_end = strstr(buf, "\r\n\r\n");
        if (header_end) break;
    }

#ifdef OPENAI_USE_LWIP
    closesocket(sock);
#endif

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

void openai_http_response_free(OpenAI_HTTPResponse* resp) {
    if (resp) {
        if (resp->body) free(resp->body);
        if (resp->headers) free(resp->headers);
        free(resp);
    }
}