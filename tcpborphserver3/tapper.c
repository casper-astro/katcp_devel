#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <linux/if.h>
#include <linux/if_tun.h>

int tap_open(char *name)
{
#if 0
  return open("/dev/tap0", O_RDWR);
#else
  int fd;
  struct ifreq ifr;

  fd = open("/dev/net/tun", O_RDWR);
  if(fd < 0){
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));

  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  /* ifr.ifr_flags = IFF_TAP; */

  if(name){
    strncpy(ifr.ifr_name, name, IFNAMSIZ);
  }

  if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
    return -1;
  }

  return fd;
#endif
}

int tap_close(int fd)
{
  return close(fd);
}
