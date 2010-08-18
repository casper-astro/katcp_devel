#ifndef NETC_H_
#define NETC_H_

int net_connect(char *name, int port, int verbose);
int net_listen(char *name, int port, int verbose);

#endif
