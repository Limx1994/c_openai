#include "openai_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/errno.h>

static const unsigned char* s_response;
static size_t s_response_size;
static size_t s_response_offset;
static size_t s_max_fragment;
static int s_timeout_on_read;
static int s_connect_fails;
static int s_recv_calls;
static int s_close_calls;
static int s_failures;

static void reset_transport(const char* response, size_t max_fragment) {
    s_response = (const unsigned char*)response;
    s_response_size = response ? strlen(response) : 0;
    s_response_offset = 0;
    s_max_fragment = max_fragment;
    s_timeout_on_read = 0;
    s_connect_fails = 0;
    s_recv_calls = 0;
    s_close_calls = 0;
    errno = 0;
}

static void check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        s_failures++;
    }
}

int lwip_socket(int domain, int type, int protocol) {
    (void)domain;
    (void)type;
    (void)protocol;
    return 0;
}

int lwip_connect(int socket_fd, const struct sockaddr* address, socklen_t address_len) {
    (void)socket_fd;
    (void)address;
    (void)address_len;
    if (s_connect_fails) {
        errno = ECONNREFUSED;
        return -1;
    }
    return 0;
}

int lwip_close(int socket_fd) {
    check(socket_fd == 0, "fd 0 must be preserved through close");
    s_close_calls++;
    return 0;
}

struct hostent* lwip_gethostbyname(const char* name) {
    static char host_name[] = "example.test";
    static unsigned char address[4] = {127, 0, 0, 1};
    static char* address_list[] = {(char*)address, NULL};
    static struct hostent host;
    (void)name;
    host.h_name = host_name;
    host.h_aliases = NULL;
    host.h_addrtype = AF_INET;
    host.h_length = 4;
    host.h_addr_list = address_list;
    return &host;
}

ssize_t lwip_send(int socket_fd, const void* buffer, size_t size, int flags) {
    (void)socket_fd;
    (void)buffer;
    (void)flags;
    return (ssize_t)size;
}

ssize_t lwip_recv(int socket_fd, void* buffer, size_t size, int flags) {
    (void)socket_fd;
    (void)flags;
    s_recv_calls++;
    if (s_response_offset >= s_response_size) {
        errno = EIO;
        return -1;
    }
    size_t remaining = s_response_size - s_response_offset;
    size_t count = remaining < size ? remaining : size;
    if (s_max_fragment > 0 && count > s_max_fragment) count = s_max_fragment;
    memcpy(buffer, s_response + s_response_offset, count);
    s_response_offset += count;
    return (ssize_t)count;
}

int lwip_poll(struct pollfd* descriptors, nfds_t count, int timeout) {
    (void)timeout;
    check(count == 1, "transport must poll exactly one socket");
    if (s_timeout_on_read && (descriptors[0].events & POLLIN)) return 0;
    descriptors[0].revents = descriptors[0].events;
    return 1;
}

u16_t lwip_htons(u16_t value) {
    return (u16_t)((value << 8) | (value >> 8));
}

static OpenAI_HTTPResponse* request(void) {
    static const char body[] = "{}";
    OpenAI_HTTPRequest req = {0};
    req.url = "http://example.test/v1/test";
    req.method = "POST";
    req.body = body;
    req.body_size = sizeof(body) - 1;
    return openai_http_request(&req);
}

static void test_chunked_one_byte_fragments(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n\r\n"
        "4\r\nWiki\r\n"
        "5\r\npedia\r\n"
        "0\r\n\r\n";
    reset_transport(response, 1);
    OpenAI_HTTPResponse* resp = request();
    check(resp != NULL, "one-byte chunk fragments must complete");
    if (resp) {
        check(resp->status_code == 200, "status code must be parsed");
        check(resp->body_size == 9, "decoded body size must exclude framing");
        check(strcmp(resp->body, "Wikipedia") == 0, "decoded chunk payload must be exact");
        openai_http_response_free(resp);
    }
    check(s_response_offset == s_response_size,
          "receiver must stop exactly after the terminal chunk");
    check(s_close_calls == 1, "successful fd 0 request must close exactly once");
    check(openai_http_get_last_error() == OPENAI_HTTP_ERROR_NONE,
          "successful request must clear transport error");
}

static void test_chunk_extensions_and_trailers(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "3;name=value\r\nabc\r\n"
        "0\r\n"
        "X-Test: yes\r\n"
        "Another: value\r\n\r\n";
    reset_transport(response, 1);
    OpenAI_HTTPResponse* resp = request();
    check(resp != NULL, "chunk extensions and trailers must complete");
    if (resp) {
        check(resp->body_size == 3, "trailer bytes must not enter decoded body");
        check(strcmp(resp->body, "abc") == 0, "extension chunk payload must be exact");
        openai_http_response_free(resp);
    }
    check(s_response_offset == s_response_size,
          "trailer parser must consume the final empty line");
    check(s_close_calls == 1, "trailer response must close fd 0 exactly once");
}

static void test_receive_timeout(void) {
    reset_transport(NULL, 0);
    s_timeout_on_read = 1;
    OpenAI_HTTPResponse* resp = request();
    check(resp == NULL, "receive timeout must fail the request");
    check(openai_http_get_last_error() == OPENAI_HTTP_ERROR_TIMEOUT,
          "receive timeout must be distinguishable from network failure");
    check(s_close_calls == 1, "timeout must close fd 0 exactly once");
}

static void test_connect_failure_closes_fd_zero(void) {
    reset_transport(NULL, 0);
    s_connect_fails = 1;
    OpenAI_HTTPResponse* resp = request();
    check(resp == NULL, "connect failure must fail the request");
    check(openai_http_get_last_error() == OPENAI_HTTP_ERROR_NETWORK,
          "connection refusal must remain a network error");
    check(s_close_calls == 1, "connect failure must close fd 0 exactly once");
}

int main(void) {
    test_chunked_one_byte_fragments();
    test_chunk_extensions_and_trailers();
    test_receive_timeout();
    test_connect_failure_closes_fd_zero();
    if (s_failures != 0) {
        fprintf(stderr, "%d lwIP transport test(s) failed\n", s_failures);
        return 1;
    }
    puts("lwIP transport tests passed");
    return 0;
}
