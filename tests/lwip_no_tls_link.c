#include "openai_http.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"

int lwip_socket(int domain, int type, int protocol)
{
    (void)domain;
    (void)type;
    (void)protocol;
    return -1;
}

int lwip_connect(int socket_fd, const struct sockaddr *address,
                 socklen_t address_len)
{
    (void)socket_fd;
    (void)address;
    (void)address_len;
    return -1;
}

int lwip_close(int socket_fd)
{
    (void)socket_fd;
    return 0;
}

struct hostent *lwip_gethostbyname(const char *name)
{
    (void)name;
    return NULL;
}

ssize_t lwip_send(int socket_fd, const void *buffer, size_t size, int flags)
{
    (void)socket_fd;
    (void)buffer;
    (void)size;
    (void)flags;
    return -1;
}

ssize_t lwip_recv(int socket_fd, void *buffer, size_t size, int flags)
{
    (void)socket_fd;
    (void)buffer;
    (void)size;
    (void)flags;
    return -1;
}

int lwip_setsockopt(int socket_fd, int level, int option,
                    const void *value, socklen_t value_len)
{
    (void)socket_fd;
    (void)level;
    (void)option;
    (void)value;
    (void)value_len;
    return 0;
}

int lwip_poll(struct pollfd *descriptors, nfds_t count, int timeout)
{
    (void)descriptors;
    (void)count;
    (void)timeout;
    return -1;
}

u16_t lwip_htons(u16_t value)
{
    return (unsigned short)((value << 8) | (value >> 8));
}

void sys_check_timeouts(void)
{
}

void sys_msleep(u32_t milliseconds)
{
    (void)milliseconds;
}

u32_t sys_now(void)
{
    return 0;
}

int main(void)
{
    if (openai_http_init() != 0) {
        return 1;
    }
    openai_http_cleanup();
    return 0;
}
