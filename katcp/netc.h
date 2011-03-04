#ifndef NETC_H_
#define NETC_H_

#define NETC_VERBOSE_ERRORS  0x1
#define NETC_VERBOSE_STATS   0x2
#define NETC_ASYNC           0x4

int net_connect(char *name, int port, int flags);
int net_listen(char *name, int port, int flags);

#endif
