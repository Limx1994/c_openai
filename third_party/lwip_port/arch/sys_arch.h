/*
 * Minimal sys_arch.h for C-OpenAI compilation check
 * In real projects, use your platform's sys_arch.h
 */
#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "arch/cc.h"

/* For NO_SYS=0, provide minimal definitions */
typedef uint32_t sys_sem_t;
typedef uint32_t sys_mutex_t;
typedef void* sys_mbox_t;
typedef void* sys_thread_t;
typedef uint32_t sys_prot_t;

#define SYS_SEM_NULL  0
#define SYS_MBOX_NULL NULL
#define SYS_MRTEX_NULL 0

#endif /* LWIP_ARCH_SYS_ARCH_H */
