/*
 * Minimal arch/cc.h for ARM embedded (compilation check only)
 * In real projects, use your platform's arch/cc.h
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Define platform byte order */
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* LWIP_PLATFORM_DIAG: diagnostics output */
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while(0)

/* LWIP_PLATFORM_ASSERT: assertion */
#define LWIP_PLATFORM_ASSERT(x) do { printf("Assertion \"%s\" failed at line %d in %s\n", x, __LINE__, __FILE__); } while(0)

/* Provide sys_now() prototype (user must implement) */
uint32_t sys_now(void);

/* Provide sys_check_timeouts() prototype (user must implement for NO_SYS=0) */
void sys_check_timeouts(void);

#endif /* LWIP_ARCH_CC_H */
