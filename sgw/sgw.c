/* (c) 2012 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "netc.h"
#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

struct speed_item{
  int x_speed;
  int x_code;
};

static struct speed_item speed_table[] = {
  { 2400,    B2400 },
  { 9600,    B9600 },  
  { 19200,   B19200 }, 
  { 19200,   B19200 }, 
  { 38400,   B38400 }, 
  { 57600,   B57600 },  
  { 115200,  B115200 }, 
  { 0,       B0 } 
};

static int start_serial(char *device, int speed)
{
  struct termios tio;
  int i, code;
  int fd;

  fd = open(device, O_RDWR | O_NONBLOCK | O_NOCTTY);
  if(fd < 0){
    return -1;
  }

  if(tcgetattr(fd, &tio)){
    close(fd);
    return -1;
  }

  tio.c_cc[VMIN] = 1;
  tio.c_cc[VTIME] = 0;

  tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IXOFF | IXON);
  tio.c_iflag |= (IGNCR | ICRNL);

#if 0
  tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR);
  tio.c_iflag |= (IGNCR | ICRNL | IXOFF | IXON);
#endif

  tio.c_oflag &= ~OPOST;

  tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

#ifdef CRTSCTS
  tio.c_cflag &= ~(CRTSCTS);
#endif

  tio.c_cflag &= ~(CSIZE | PARENB);
  tio.c_cflag |=  (CS8 | CLOCAL);

  if(speed){
    for(i = 0; (speed_table[i].x_speed > 0) && (speed_table[i].x_speed != speed); i++);

    if(speed_table[i].x_speed != speed){
      close(fd);
      return -1;
    }

    code = speed_table[i].x_code;
#ifdef DEBUG
    fprintf(stderr, "serial: setting serial speed to %d\n", speed);
#endif
    cfsetispeed(&tio, code);
    cfsetospeed(&tio, code);
  }

  if(tcsetattr(fd, TCSANOW, &tio)){
    close(fd);
    return -1;
  }

  return fd;
}

void usage(char *label, struct katcl_line *k)
{
  sync_message_katcl(k, KATCP_LEVEL_INFO, label, "serial-device [port [serial-speed]]");
}

int main(int argc, char **argv)
{
  char *net, *serial, *label;
  int i, j, c, lfd, run, fd, mfd, result, count, speed;
  struct katcl_line *sk, *nk, *k;
  struct katcl_parse *p;
  fd_set fsr, fsw;
  
  label = "katcp-serial-gateway";

  k = create_katcl(STDOUT_FILENO);
  if(k == NULL){
    fprintf(stderr, "%s: unable to create katcp message logic\n", label);
    return 4;
  }
  sk = NULL;
  nk = NULL;

  speed = 0;
  serial = NULL;
  net = NULL;
  count = 0;

  i = j = 1;
  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(label, k);
          return 0;

        case 'b' :
        case 's' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "argument needs a parameter");
            return 2;
          }

          switch(c){
            case 'p' :
              net = argv[i] + j;
              break;
            case 'b' :
              speed = atoi(argv[i] + j);
              if(speed < 0){
                sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "speed %s unreasonable", argv[i] + j);
                return 2;
              }
              break;
            case 's' :
              serial = argv[i] + j;
              break;
          }

        case '-' :
          j++;
          break;
        case '\0':
          j = 1;
          i++;
          break;
        default:
          sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "unknown option -%c", c);
          return 2;
      }
    } else {
      if(serial == NULL){
        serial = argv[i];
      } else if(net == NULL){
        net = argv[i];
      } else if(speed == 0){
        speed = atoi(argv[i]);
      } else {
        sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "excess argument %s", argv[i]);
        return 2;
      }
      i++;
    }

  }

  if(speed <= 0){
    speed = 9600;
  }

  if(serial == NULL){
    sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "need a serial port to relay");
    usage(label, k);
    return 2;
  }

#if 0
  fd = open(serial, O_RDWR | O_NOCTTY);
#endif
  fd = start_serial(serial, speed);
  if(fd < 0){
    sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "unable to open serial device %s: %s", serial, strerror(errno));
    return 3;
  }
  sk = create_katcl(fd);
  if(sk == NULL){
    sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "unable to run katcp wrapper on serial file descriptor");
    return 4;
  }

  lfd = net_listen(net, 0, 0);
  if(lfd < 0){
    sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "unknown option -%c", argv[i][j]);
    return 3;
  }

  for(run = 1; run; ){
    mfd = 0;
    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    if(k){
      if(flushing_katcl(k)){
        fd = fileno_katcl(k);
        FD_SET(fd, &fsw);
        mfd = fd;
      }
    }

    if(nk){
      fd = fileno_katcl(nk);
      if(flushing_katcl(nk)){
        FD_SET(fd, &fsw);
      }
      FD_SET(fd, &fsr);
      if(mfd < fd){
        mfd = fd;
      }
    } else {
      FD_SET(lfd, &fsr);
      if(mfd < lfd){
        mfd = lfd;
      }
    }

    if(sk){
      fd = fileno_katcl(sk);
      if(flushing_katcl(sk)){
        FD_SET(fd, &fsw);
      }
      FD_SET(fd, &fsr);
      if(mfd < fd){
        mfd = fd;
      }
    }

    result = select(mfd + 1, &fsr, &fsw, NULL, NULL);
    switch(result){
      case -1 :
        switch(errno){
          case EAGAIN :
          case EINTR  :
            continue; /* WARNING */
          default  :
            sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "select failed: %s", strerror(errno));
            return 4;
        }
        break;
#if 0
      case  0 :
        sync_message_katcl(k, KATCP_LEVEL_ERROR, label, "requests timed out after %dms", timeout);
        /* could terminate cleanly here, but ... */
        return 3;
#endif
    }

    if(k){
      fd = fileno_katcl(k);
      if(FD_ISSET(fd, &fsw)){
        write_katcl(k); /* WARNING: ignores write failures - unable to do much about it */
      }
    }

    if(nk){
      fd = fileno_katcl(nk);

      if(FD_ISSET(fd, &fsw)){ /* flushing things */
        result = write_katcl(nk);
        if(result < 0){
          log_message_katcl(k, KATCP_LEVEL_ERROR, label, "unable to write to network client: %s", strerror(error_katcl(nk)));
          destroy_katcl(nk, 1);
          nk = NULL;
          continue;
        }
      }

      if(FD_ISSET(fd, &fsr)){ /* reading */
        result = read_katcl(nk);
        if(result){
          if(result < 0){
            log_message_katcl(k, KATCP_LEVEL_ERROR, label, "network read failed: %s", strerror(error_katcl(nk)));
          } else {
            log_message_katcl(k, KATCP_LEVEL_WARN, label, "network disconnected");
          }
          destroy_katcl(nk, 1);
          nk = NULL;
          continue;
        }

        while(have_katcl(nk) > 0){
          p = ready_katcl(nk);
          if(p){
            append_parse_katcl(sk, p);
          }
        }
      }
    } else {
      /* TODO: should report an address */
      fd = accept(lfd, NULL, NULL);
      if(fd < 0){
        log_message_katcl(k, KATCP_LEVEL_ERROR, label, "network accept failed: %s", strerror(error_katcl(nk)));
      }
      nk = create_katcl(fd);
      if(nk == NULL){
        log_message_katcl(k, KATCP_LEVEL_ERROR, label, "unable to encapsulate network file descriptor");
        close(fd);
      }
      if(count > 0){
        log_message_katcl(nk, KATCP_LEVEL_WARN, label, "discarded %u messages while no client was connected", count);
        count = 0;
      }
      log_message_katcl(nk, KATCP_LEVEL_DEBUG, label, "connected to %s at speed %d", serial, speed);
    }

    if(sk){
      fd = fileno_katcl(sk);

      if(FD_ISSET(fd, &fsw)){ /* flushing things */
        result = write_katcl(sk);
        if(result < 0){
          log_message_katcl(k, KATCP_LEVEL_ERROR, label, "unable to write to serial port %s: %s", serial, strerror(error_katcl(sk)));
          destroy_katcl(sk, 1);
          sk = NULL;
          run = 0;
          /* WARNING: unsatisfactory, may starve flushing logic */
          continue;
        }
      }

      if(FD_ISSET(fd, &fsr)){ /* reading */
        result = read_katcl(sk);
        if(result){
          if(result < 0){
            log_message_katcl(k, KATCP_LEVEL_ERROR, label, "serial read from %s failed: %s", serial, strerror(error_katcl(sk)));
          } else {
            log_message_katcl(k, KATCP_LEVEL_WARN, label, "serial port hung up");
          }
          destroy_katcl(sk, 1);
          sk = NULL;
          run = 0;
        }
        while(have_katcl(sk) > 0){
          p = ready_katcl(sk);
          if(p){
            if(nk){
              append_parse_katcl(nk, p);
            } else {
              count++;
            }
          }
        }
        continue;
      }
    }

  }

  if(nk){
    destroy_katcl(nk, 1);
    nk = NULL;
  }

  if(sk){
    destroy_katcl(sk, 1);
    sk = NULL;
  }

  if(lfd >= 0){
    close(lfd);
  }

  while(write_katcl(k) == 0);
  destroy_katcl(k, 0);

  return 0;
}
