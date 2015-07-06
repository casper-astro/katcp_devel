#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <errno.h>

#include <fcntl.h>
#include <time.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>

#include <katcp.h>
#include <katcl.h>

#define DEFAULT_LEVEL "info"
#define IO_INITIAL     1024

struct totalstate{
  int t_verbose;
  int t_infer;
  char *t_system;
};

struct iostate{
  int i_fd;
  unsigned char *i_buffer;
  unsigned int i_size;
  unsigned int i_have;
  unsigned int i_done;
  int i_level;
};

void destroy_iostate(struct iostate *io)
{
  if(io == NULL){
    return;
  }

  if(io->i_buffer){
    free(io->i_buffer);
    io->i_buffer = NULL;
  }

  if(io->i_fd >= 0){
    close(io->i_fd);
    io->i_fd = (-1);
  }

  free(io);
}

struct iostate *create_iostate(int fd, int level)
{
  struct iostate *io;

  io = malloc(sizeof(struct iostate));
  if(io == NULL){
    return NULL;
  }

  io->i_fd = (-1);
  io->i_buffer = NULL;

  io->i_buffer = malloc(sizeof(unsigned char) * IO_INITIAL);
  if(io->i_buffer == NULL){
    destroy_iostate(io);
    return NULL;
  }

  io->i_fd = fd;

  io->i_size = IO_INITIAL;
  io->i_have = 0;
  io->i_done = 0;
  io->i_level = level;

  return io;
}

int run_iostate(struct totalstate *ts, struct iostate *io, struct katcl_line *k)
{
  int rr, end, fd, sw;
  unsigned char *tmp;
  unsigned int may, i;

  if(io->i_have >= io->i_size){
    tmp = realloc(io->i_buffer, sizeof(unsigned char) * io->i_size * 2);
    if(tmp == NULL){
      return -1;
    }

    io->i_buffer = tmp;
    io->i_size *= 2;
  }

  may = io->i_size - io->i_have;
  end = 0;

  rr = read(io->i_fd, io->i_buffer, may);
  if(rr <= 0){
    if(rr < 0){
      switch(errno){
        case EAGAIN :
        case EINTR  :
          return 1;
        default :
          /* what about logging this ... */
          return -1;
      }
    } else {
      io->i_buffer[io->i_have] = '\n';
      io->i_have++;
      end = 1;
    } 
  }

  io->i_have += rr;

  for(i = io->i_done; i < io->i_have; i++){
    switch(io->i_buffer[i]){
      case '\r' :
      case '\n' :
        io->i_buffer[i] = '\0';
        if(io->i_done < i){
#ifdef DEBUG
          fprintf(stderr, "considering <%s> (infer=%d)\n", io->i_buffer + io->i_done, ts->t_infer);
#endif
          if((ts->t_infer > 0) && (strncmp(io->i_buffer + io->i_done, KATCP_LOG_INFORM " ", 5) == 0)){
            fd = fileno_katcl(k);
            io->i_buffer[i] = '\n';
            sw = i - io->i_done + 1;
            write(fd, io->i_buffer + io->i_done, sw);
#ifdef DEBUG
            fprintf(stderr, "attempting raw relay to fd=%d of %d bytes\n", fd, sw);
#endif
          } else {
            log_message_katcl(k, io->i_level, ts->t_system, "%s", io->i_buffer + io->i_done);
          }
        }
        io->i_done = i + 1;
        break;
      default :
        /* do nothing ... */
        break;
    }
  }
  
  if(io->i_done < io->i_have){
    if(io->i_done > 0){
      memmove(io->i_buffer, io->i_buffer + io->i_done, io->i_have - io->i_done);
      io->i_have -= io->i_done;
      io->i_done = 0;
    }
  } else {
    io->i_have = 0;
    io->i_done = 0;
  }

  return end ? 1 : 0;
}

void usage(char *app)
{
  printf("kcprun: a wrapper to katcp-ify program output\n\n");
  printf("usage: %s [options] command ...\n", app);
  printf("-h                 this help\n");
  printf("-q                 run quietly\n");
  printf("-v                 increase verbosity\n");
  printf("-e level           specify the level for messages from standard error\n");
  printf("-o level           specify the level for messages from standard output\n");
  printf("-s subsystem       specify the subsystem (overrides KATCP_LABEL)\n");
  printf("-i                 inhibit termination of subprocess on eof on standard input\n");
  printf("-r                 forward raw #log messages unchanged\n");
  printf("-x                 relay child exit codes\n");
}

static int tweaklevel(int verbose, int level)
{
  int result;

  result = level + (verbose - 1);
  if(result < KATCP_LEVEL_TRACE){
    return KATCP_LEVEL_TRACE;
  } else if(result > KATCP_LEVEL_FATAL){
    return KATCP_LEVEL_FATAL;
  }

  return result;
}

static int collect_child(struct katcl_line *k, char *task, char *system, int verbose, pid_t pid)
{
  int status, code, sig, level;
  pid_t result;

  result = waitpid(pid, &status, WNOHANG);

  if(result == 0){
    /* child still running */
    log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_DEBUG), system, "task %s still running", task);
    return -1;
  }

  if(result < 0){
    switch(errno){
      case ENOENT :
      case ECHILD :
        /* no child */
        return 0;
      default :
        /* other problem */
        log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_WARN), system, "unable to collect task %s: %s", task, strerror(errno));
        return 1;
    }
  }

  if(result != pid){
    log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_ERROR), system, "major logic problem: expected pid %u but received %u", pid, result);
    return 1;
  }

  /* now result == pid */

  /* child has gone, collect the return code */
  if(WIFEXITED(status)){
    code = WEXITSTATUS(status);
    if(code > 0){
      log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_WARN), system, "task %s exited with nonzero exit code", task);
    } else {
      log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_INFO), system, "task %s exited normally", task);
    }
  
    return (code > 0) ? code: 0;
  } 
  
  if(WIFSIGNALED(status)){
    sig = WTERMSIG(status);
    switch(sig){
      case SIGTERM :
      case SIGKILL :
        level = KATCP_LEVEL_INFO;
        break;
      case SIGPIPE :
        level = KATCP_LEVEL_WARN;
        break;
      default :
        level = KATCP_LEVEL_ERROR;
        break;
    }
    log_message_katcl(k, tweaklevel(verbose, level), system, "task %s exited with signal %d", task, sig);

    return 1;
  } 
  
  
  /* process stopped ? continued ? other weirdness */

  log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_WARN), system, "task %s in rather unusual status 0x%x", task, status);
  return 1;
}

int main(int argc, char **argv)
{
#define BUFFER 128
  int terminate, code, childgone, parentgone, exitrelay;
  int levels[2], index, efds[2], ofds[2];
  int i, j, c, offset, result, rr;
  struct katcl_line *k;
  char *app;
  char *tmp;
  pid_t pid;
  struct totalstate total, *ts;
  struct iostate *erp, *orp;
  fd_set fsr, fsw;
  int mfd, fd;
  unsigned char buffer[BUFFER];

  
  ts = &total;

  terminate = 1;
  offset = (-1);
  exitrelay = 0;

  levels[0] = KATCP_LEVEL_INFO;
  levels[1] = KATCP_LEVEL_ERROR;

  i = j = 1;

  ts->t_system = getenv("KATCP_LABEL");
  ts->t_verbose = 1;
  ts->t_infer = 0;

  if(ts->t_system == NULL){
    ts->t_system = "run";
  }

  app = argv[0];

  k = create_katcl(STDOUT_FILENO);
  if(k == NULL){
    fprintf(stderr, "%s: unable to create katcp message logic\n", app);
    return EX_OSERR;
  }

  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(app);
          return 0;

        case 'x' : 
          exitrelay = 1;
          j++;
          break;

        case 'i' : 
          terminate = 0;
          j++;
          break;

        case 'r' : 
          ts->t_infer = 1;
          j++;
          break;

        case 'q' : 
          ts->t_verbose = 0;
          j++;
          break;

        case 'v' : 
          ts->t_verbose++;
          j++;
          break;

        case 'e' :
        case 'o' :
        case 's' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }

          if (i >= argc) {
            sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "option -%c needs a parameter");
            return EX_USAGE;
          }

          index = 0;

          switch(c){
            case 'e' :
              index++;
              /* fall */
            case 'o' :
              tmp = argv[i] + j;
#ifdef KATCP_CONSISTENCY_CHECKS
              if((index < 0) || (index > 1)){
                fprintf(stderr, "logic problem: index value %d not correct\n", index);
                abort();
              }
#endif
              levels[index] = log_to_code_katcl(tmp);
              if(levels[index] < 0){
                sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "unknown log level %s", tmp);
                levels[index] = KATCP_LEVEL_FATAL;
              }
              break;
            case 's' :
              ts->t_system = argv[i] + j;
              break;
          }

          i++;
          j = 1;
          break;

        case '-' :
          j++;
          break;
        case '\0':
          j = 1;
          i++;
          break;
        default:
          fprintf(stderr, "%s: unknown option -%c\n", app, argv[i][j]);
          return 2;
      }
    } else {
      if(offset < 0){
        offset = i;
      }
      i = argc;
    }
  }

  if(offset < 0){
    sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "need something to run");
    return EX_USAGE;
  }

  if(pipe(efds) < 0){
    sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to create error pipe: %s", strerror(errno));
    return EX_OSERR;
  }

  if(pipe(ofds) < 0){
    sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to create output pipe: %s", strerror(errno));
    return EX_OSERR;
  }

  pid = fork();
  if(pid < 0){
    sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to fork: %s", strerror(errno));
    return EX_OSERR;
  }

  if(pid == 0){
    /* in child */

    if(terminate){
      /* child not going to listen to stdin anyway, though maybe use /dev/zero instead, just in case ? */
      fd = open("/dev/null", O_RDONLY);
      if(fd < 0){
        sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_WARN), ts->t_system, "unable to open /dev/null: %s", strerror(errno));
      }

      if(dup2(fd, STDIN_FILENO) != STDIN_FILENO){
        sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_WARN), ts->t_system, "unable to replace standard input filedescriptor: %s", strerror(errno));
      } else {
        close(fd);
      }
    }

    if(dup2(efds[1], STDERR_FILENO) != STDERR_FILENO){
      sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to replace standard error filedescriptor: %s", strerror(errno));
      return EX_OSERR;
    }

    if(dup2(ofds[1], STDOUT_FILENO) != STDOUT_FILENO){
      sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to replace standard output filedescriptor: %s", strerror(errno));
      return EX_OSERR;
    }

    close(ofds[0]);
    close(efds[0]);

    execvp(argv[offset], &(argv[offset]));

    return EX_OSERR;
  }

  log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_DEBUG), ts->t_system, "launched %s as process %d", argv[offset], pid);

  close(ofds[1]);
  close(efds[1]);

  orp = create_iostate(ofds[0], levels[0]);
  erp = create_iostate(efds[0], levels[1]);

  ofds[0] = (-1);
  efds[0] = (-1);

  if((orp == NULL) || (erp == NULL)){
    sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to allocate state for io handlers");
    return EX_OSERR;
  }

  childgone = 0;
  parentgone = 0;

  do{
    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    mfd = 0;

    fd = STDIN_FILENO;
    FD_SET(fd, &fsr);
    if(fd > mfd){
      mfd = fd;
    }

    FD_SET(orp->i_fd, &fsr);
    if(orp->i_fd > mfd){
      mfd = orp->i_fd;
    }

    FD_SET(erp->i_fd, &fsr);
    if(erp->i_fd > mfd){
      mfd = erp->i_fd;
    }

    if(flushing_katcl(k)){
#ifdef DEBUG
      fprintf(stderr, "need to flush\n");
#endif
      fd = fileno_katcl(k);
      FD_SET(fd, &fsw);
    }

    result = select(mfd + 1, &fsr, &fsw, NULL, NULL);
    if(result < 0){
      switch(errno){
        case EAGAIN :
        case EINTR  :
          result = 0;
          break;
        default :
          log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "select failed: %s", strerror(errno));
          break;
      }
    } else {

      fd = fileno_katcl(k);
      if(FD_ISSET(fd, &fsw)){
#ifdef DEBUG
        fprintf(stderr, "invoking flush logic\n");
#endif
        write_katcl(k);
      }

      if(FD_ISSET(STDIN_FILENO, &fsr)){
        rr = read(STDIN_FILENO, buffer, BUFFER);
        if(rr <= 0){
          if(rr < 0){
            switch(errno){
              case EAGAIN :
              case EINTR  :
                break;
              default :
                parentgone = 1;
                result = (-1);
                break;
            }
          } else {
            parentgone = 1;
            result = (-1);
          }
        }
      }

      if(FD_ISSET(orp->i_fd, &fsr)){
        if(run_iostate(ts, orp, k)){
          childgone = 1;
          result = (-1);
        }
      }

      if(FD_ISSET(erp->i_fd, &fsr)){
        if(run_iostate(ts, erp, k)){
          childgone = 1;
          result = (-1);
        }
      }

    }

  } while(result >= 0);

  code = collect_child(k, argv[offset], ts->t_system, ts->t_verbose, pid);

  if(code < 0){
    if(terminate){
      log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_INFO), ts->t_system, "terminating task %s", argv[offset]);
      if(kill(pid, SIGTERM) < 0){
        switch(errno){
          case ESRCH  :
          case ENOENT :
            break;
          default :
            log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_INFO), ts->t_system, "unable to terminate task %s", argv[offset]);
            break;
        }
      } else {
        sleep(1);
        if(collect_child(k, argv[offset], ts->t_system, ts->t_verbose, pid) < 0){
          log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_INFO), ts->t_system, "killing task %s", argv[offset]);
          if(kill(pid, SIGKILL) < 0){
            switch(errno){
              case ESRCH  :
              case ENOENT :
                break;
              default :
                log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_INFO), ts->t_system, "unable to terminate task %s", argv[offset]);
                break;
            }
          } else {
            sleep(1);
            collect_child(k, argv[offset], ts->t_system, ts->t_verbose, pid);
          }
        }
      }
    }
  }

  if(exitrelay == 0){
    code = 0;
  } 

  while((result = write_katcl(k)) == 0);

  destroy_iostate(orp);
  destroy_iostate(erp);

  destroy_katcl(k, 0);

  return code;
#undef BUFFER
}
