/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <katpriv.h>
#include <katcl.h>
#include <katcp.h>
#include <netc.h>
#include "kcs.h"

void replace_argv(struct katcp_dispatch *d, char *str)
{
  struct kcs_basic *kb;
  int i, argc;
  char **argv;

  kb = get_mode_katcp(d, KCS_MODE_BASIC);

  if (kb == NULL)
    return;

  argc = kb->b_argc;
  argv = kb->b_argv;
  
  for (i=0; i<argc; i++){
    bzero(argv[i],strlen(argv[i]));
  }

  sprintf(argv[0], "%s", str);

}

struct katcp_job * run_child_process_kcs(struct katcp_dispatch *d, struct katcp_url *url, int (*call)(struct katcl_line *, void *), void *data, struct katcp_notice *n) 
{
  int fds[2];
  pid_t pid;
  int i;
  struct katcp_job *j;
  struct katcl_line *xl;

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) 
    return NULL;
  
  pid = fork();
  if (pid < 0){
    close(fds[0]);
    close(fds[1]);
    return NULL;
  }

  if (pid > 0){
    close(fds[0]);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#ifdef DEBUG
    fprintf(stderr,"subprocess: in parent child has pid: %d\n",pid);
#endif
    j = create_job_katcp(d, url, pid, fds[1], 0, n);
    if (j == NULL){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to allocate job logic so terminating child process");
      kill(pid, SIGTERM);
      close(fds[1]);
      destroy_kurl_katcp(url);
    }
    return j;
  }
#ifdef DEBUG
  fprintf(stderr,"subprocess: in child: %d\n",getpid());
#endif
  /*in child use exit not return*/
  xl = create_katcl(fds[0]);
  close(fds[1]);
  
  for (i=0; i<1024; i++)
    if (i != fds[0])
      close(i);

  replace_argv(d, url->u_str);

#if 0 
  copies = 0;
  if (fds[0] != STDOUT_FILENO){
    if (dup2(fds[0], STDOUT_FILENO) != STDOUT_FILENO) {
      sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to set up standard output for child process %u (%s)", getpid(), strerror(errno)); 
      exit(EX_OSERR);
    }
    copies++;
  }
  if(fds[0] != STDIN_FILENO){
    if(dup2(fds[0], STDIN_FILENO) != STDIN_FILENO){
      sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to set up standard input for child process %u (%s)", getpid(), strerror(errno)); 
      exit(EX_OSERR);
    }
    copies++;
  }
  if(copies >= 2){
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  }
#endif

  if ((*call)(xl,data) < 0)
    sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "run child process kcs fail"); 
  
  exit(EX_OK);
  return NULL;
}


int xport_sync_connect_and_start_subprocess_kcs(struct katcl_line *l, void *data)
{
#define ARRAYSIZE 5
  struct katcp_url *url;
  int fd, wb;
  unsigned char onstring[ARRAYSIZE] = { 0002, 0201, 0002, 0377, 0377 };

  url = data;
  if (url == NULL)
    return -1;

  fd = net_connect(url->u_host, url->u_port, 0);

  if (fd < 0)
    return -1;
  
  wb = write(fd, onstring, ARRAYSIZE);
  
  sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "poweron %s wrote %d bytes", url->u_str ,wb); 

  close(fd);
  return 0;
#undef ARRAYSIZE
}

int xport_sync_connect_and_stop_subprocess_kcs(struct katcl_line *l, void *data)
{
#define ARRAYSIZE 5
  struct katcp_url *url;
  int fd, wb;
  unsigned char onstring[ARRAYSIZE] = { 0002, 0202, 0002, 0377, 0377 };

  url = data;
  if (url == NULL)
    return -1;

  fd = net_connect(url->u_host, url->u_port, 0);

  if (fd < 0)
    return -1;
  
  wb = write(fd, onstring, ARRAYSIZE);
  
  sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "poweroff %s wrote %d bytes", url->u_str ,wb); 
  
  close(fd);
  return 0;
#undef ARRAYSIZE
}

int xport_sync_connect_and_soft_restart_subprocess_kcs(struct katcl_line *l, void *data)
{
#define ARRAYSIZE 5
  struct katcp_url *url;
  int fd, wb;
  unsigned char onstring[ARRAYSIZE] = { 0002, 0202, 0002, 00, 00 };
  
  url = data;
  if (url == NULL)
    return -1;

  fd = net_connect(url->u_host, url->u_port, 0);

  if (fd < 0)
    return -1;
  
  wb = write(fd, onstring, ARRAYSIZE);
  
  sync_message_katcl(l, KATCP_LEVEL_INFO, NULL, "powersoft %s wrote %d bytes", url->u_str ,wb); 
  
  close(fd);
  return 0;
#undef ARRAYSIZE
}

#if 0
void process(void *data) {
#ifdef DEBUG
  fprintf(stdout,"subprocess: This is a sub process with data: %p\n",data);
#endif
}

int main(int argc, char **argv) {
  
  int rtn;
  char data = "hello world";


  rtn = run_child_process_kcs(NULL, NULL, &process, data, NULL) 

  if (rtn < 0){
#ifdef DEBUG
    fprintf(stderr,"subprocess: fail\n");
#endif
  } else {
#ifdef DEBUG
    fprintf(stderr,"subprocess: ok\n");
#endif
  }


  return 0;
}
#endif
