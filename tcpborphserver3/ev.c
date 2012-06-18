
#include <stdio.h>
#include <string.h>
#include <fnctl.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <katcp.h>

int open_evdev_tbs(katcp_dispatch *d, char *name)
{
#define BUFFER 64
  int i, fd;
  char buffer[BUFFER];

  for(i = 0; ;i++){
    snprintf(buffer, BUFFER - 1, "/dev/input/event%d", i);
    buffer[BUFFER - 1] = '\0';

    fd = open(buffer, O_RDWR | O_CLOEXEC);
    if(fd < 0){
      switch(errno){
        case ENODEV : 
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no device driver backing %s, giving up", buffer);
          return -1;
        case ENOENT : 
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, (i == 0) ? "unable to locate device file %s, try creating it" : "no further device file at %s to match %s, giving up", buffer, name);
          return -1;
        default : 
          log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "skipping device %s, error given: %s", buffer, strerror(errno));
          break;
      }
    }

    if(ioctl(fd, EVIOCGNAME(BUFFER - 1), buffer) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to acquire name for %s: %s", buffer, strerror(errno));
      close(fd);
      return 1;
    }

    buffer[BUFFER - 1] = '\0';

    if(strcmp(buffer, name)){
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "input device name %s does not match request %s", buffer, name);
      close(fd);
      continue;
    }

    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "found matching event%d for name %s", i, name);
    return fd;
  }
#undef BUFFER 
}

int run_chasis_tbs(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode)
{
  struct input_event event;
  int rr;

  if(mode & KATCP_ARB_READ){
    rr = read(fd, &event, sizeof(struct input_event));

  }


  return 0;
}


int chasis_init_tbs(struct katcp_dispatch *d, char *name)
{
  struct katcp_arb *a;
  int fd;

  a = find_arb_katcp(d, name);
  if(a){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "logic for %s already initialised", name);
    return 0;
  }

  fd = open_evdev_tbs(d, name);
  if(fd < 0){
    return -1;
  }

  a = create_arb_katcp(d, name, fd, KATCP_ARB_READ, &run_chasis_tbs, gs);
  if(a == NULL){
    close(fd);
    return -1;
  }

  return 0;
}
