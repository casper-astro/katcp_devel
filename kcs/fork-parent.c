/* released to the public domain, no warranties, 
 * no liability, use at your own risk, batteries not included 
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <sysexits.h>

#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include "fork-parent.h"

#define READ_BUFFER 1024

int fork_parent()
{
  int p[2];
  pid_t pid;
  char buffer[READ_BUFFER];
  int rr, status, result;

  if (pipe(p)) {
    return -1;
  }

  fflush(stderr);

  pid = fork();

  if(pid < 0){
    return -1;
  }

  if(pid == 0){ /* in child - make pipe stderr and detach from terminal */
    close(p[0]);
    if (p[1] != STDERR_FILENO) {
      if (dup2(p[1], STDERR_FILENO) != STDERR_FILENO) {
	exit(EX_OSERR);
      }
      close(p[1]);
    }

    close(STDOUT_FILENO);
    close(STDIN_FILENO);
    /* TODO: could point STDIN and STDOUT at /dev/null */

    setsid();

    return 0;
  }

  /* in parent - read from pipe, exit when pipe closes */
  close(p[1]);

  do {
    rr = read(p[0], buffer, READ_BUFFER);
    switch (rr) {
      case -1:
        switch (errno) {
          case EAGAIN:
          case EINTR:
            rr = 1;
            break;
          default:
            fprintf(stderr, "unable to read error messages: %s\n", strerror(errno));
            fflush(stderr);
            break;
        }
        break;
      case 0:
        /* eof */
        break;
      default:
        write(STDERR_FILENO, buffer, rr);
        /* don't care if write fails, can't do anything about it */
        break;
    }
  } while (rr > 0);

  sched_yield();
  result = 0;

  if (waitpid(pid, &status, WNOHANG) > 0) {	/* got a child */
    if (WIFEXITED(status)) {
      result = WEXITSTATUS(status);
      fprintf(stderr, "exited with code %d\n", result);
    } else if (WIFSIGNALED(status)) {
      fprintf(stderr, "killed by signal %d\n", WTERMSIG(status));
      fflush(stderr);
      result = EX_SOFTWARE;
#if 0
      /* mimic child - problematic, it may ovewrite the child core file */
      result = WTERMSIG(status);
      raise(result);
#endif
#if 0
    } else if (WIFSTOPPED(status)) {	/* too clever by half */
      result = WSTOPSIG(status);
      fprintf(stderr, "stopped by signal %d, restarting with %d\n", result, SIGCONT);
      kill(pid, SIGCONT);
      result = 0;
#endif
    }
  }

  /* else child probably ok */
  exit(result);
  /* not reached */
  return -1;
}
