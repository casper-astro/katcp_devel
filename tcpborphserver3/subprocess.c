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

#include "tcpborphserver3.h"

void replace_argv(struct katcp_dispatch *d, char *str)
{
  struct tbs_raw *tr;
  int i, argc;
  char **argv;

  tr = get_mode_katcp(d, TBS_MODE_RAW);

  if (tr == NULL)
    return;

  argc = tr->r_argc;
  argv = tr->r_argv;
  
  for (i=0; i<argc; i++){
    bzero(argv[i], strlen(argv[i]));
  }

  sprintf(argv[0], "%s", str);

#ifdef DEBUG
  fprintf(stderr, "%s: process name changed to %s\n", __func__, str);
#endif
}

struct katcp_job *run_child_process_tbs(struct katcp_dispatch *d, struct katcp_url *url, int (*call)(struct katcl_line *, void *), void *data, struct katcp_notice *n) 
{
  int fds[2];
  pid_t pid;
  struct katcp_job *j;
  struct katcl_line *xl;
  int result;

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
    fprintf(stderr, "%s: in parent child has pid: %d\n", __func__, pid);
#endif

    j = create_job_katcp(d, url, pid, fds[1], 0, n);
    if (j == NULL){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to allocate job logic so terminating child process");
      kill(pid, SIGTERM);
      close(fds[1]);
      /* semantics are that on failure, parent cleans up */
      /* destroy_kurl_katcp(url); */
    }
    return j;
  }

  /* WARNING: now we are in child - use exit not return */
#ifdef DEBUG
  fprintf(stderr,"%s: in child: %d\n", __func__, getpid());
#endif

  xl = create_katcl(fds[0]);
  close(fds[1]);
 
#if 0
  for (i=0; i<1024; i++)
    if (i != fds[0])
      close(i);
#endif 

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

  result = ((*call)(xl, data));

  exit((result < 0) ? (result * -1) : result);

  return NULL;
}
