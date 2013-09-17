#ifndef NETC_H_
#define NETC_H_

#ifdef __cplusplus
extern "C" {
#endif

#define NETC_VERBOSE_ERRORS  0x1
#define NETC_VERBOSE_STATS   0x2
#define NETC_ASYNC           0x4
#define NETC_TCP_KEEP_ALIVE  0x8

#define NETC_DEFAULT_PORT   7147

int net_connect(char *name, int port, int flags);
int net_listen(char *name, int port, int flags);

#ifdef __cplusplus
}
#endif

#endif
