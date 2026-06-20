/*
 * Minimal lwipopts.h for C-OpenAI compilation check
 * In real projects, use your platform's lwipopts.h
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* System */
#define NO_SYS                     0
#define LWIP_SOCKET               1
#define LWIP_NETCONN              1

/* TCP */
#define LWIP_TCP                   1
#define TCP_MSS                   1460
#define TCP_SND_BUF              (4 * TCP_MSS)

/* ALTCP / TLS */
#define LWIP_ALTCP                1
#define LWIP_ALTCP_TLS            1
#define LWIP_ALTCP_TLS_MBEDTLS    1
#define MEMP_NUM_ALTCP_PCB        4
#define MEMP_NUM_ALTCP_TLS_PCB    4

/* Memory */
#define MEM_ALIGNMENT              4
#define MEM_SIZE                  8192
#define MEMP_NUM_PBUF             16
#define MEMP_NUM_TCP_PCB          5
#define MEMP_NUM_TCP_PCB_LISTEN   5
#define MEMP_NUM_TCP_SEG          16
#define PBUF_POOL_SIZE            16

/* DHCP */
#define LWIP_DHCP                  1

/* DNS */
#define LWIP_DNS                   1

#endif /* LWIPOPTS_H */
