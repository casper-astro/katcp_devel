#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include <fcntl.h>

#include <sys/socket.h>

#include <katcp.h>
#include <katcl.h>
#include <netc.h>

#define DEFAULT_SWITCH "switch" 
#define MAX_BUFFER      1000000 /* how many bytes we buffer of other party ... */

#define FLAG_DISCARD    0x1
#define FLAG_RELAX      0x2
#define FLAG_FALLBACK   0x4
#define FLAG_MASTER     0x8

#define FLAG_DEAD       0x80

#define FLAG_NET      0x1000
#define FLAG_EXEC     0x2000
#define FLAG_STREAM   0x4000  /* can not be re-opened ? */

struct mpx_input
{
  struct katcl_line *i_line;
  unsigned int i_flags;
  char *i_label;
  char *i_actual;
};

struct mpx_state
{
  struct mpx_input **s_vector;
  unsigned int s_count;

  char *s_switch;
  int s_symbolic;

  int s_this;
  int s_select;
  int s_fall;
};

/***********************************************************/

void destroy_input(struct mpx_state *s, struct mpx_input *mi);
void destroy_state(struct mpx_state *s);

struct mpx_state *create_state();
int add_input(struct mpx_state *s, int fd, unsigned int flags, char *label, char *actual);

/***********************************************************/

void destroy_state(struct mpx_state *s)
{
  unsigned int i;

  if(s == NULL){
    return;
  }

  if(s->s_vector){
    for(i = 0; i < s->s_count; i++){
      destroy_input(s, s->s_vector[i]);
      s->s_vector[i] = NULL;
    }

    free(s->s_vector);
    s->s_vector = NULL;
  }

  s->s_count = 0; 

  s->s_this = (-1);
  s->s_select = (-1);
  s->s_fall = (-1);

  if(s->s_switch){
    free(s->s_switch);
    s->s_switch = NULL;
  }
  s->s_symbolic = (-1);

  free(s);
}

struct mpx_state *create_state()
{
  struct mpx_state *s;

  s = malloc(sizeof(struct mpx_state));
  if(s == NULL){
    return NULL;
  }

  s->s_vector = NULL;
  s->s_count = 0;

  s->s_this = (-1);
  s->s_select = (-1);
  s->s_fall = (-1);

  s->s_switch = NULL;
  s->s_symbolic = 1;

  return s;
}

int set_change(struct mpx_state *ms, char *cmd, int symbolic)
{
  char *ptr;
  unsigned int len;

  ptr = (cmd == NULL) ? DEFAULT_SWITCH : cmd;
  len = strlen(ptr) + 2;

  if(ms->s_switch){
    free(ms->s_switch);
    ms->s_switch = NULL;
  }

  ms->s_switch = malloc(sizeof(char) * len);
  if(ms->s_switch == NULL){
    return -1;
  }

  ms->s_switch[0] = KATCP_REPLY;
  strcpy(ms->s_switch + 1, ptr);

  ms->s_symbolic = symbolic;

  return 0;
}

/***********************************************************/

void destroy_input(struct mpx_state *s, struct mpx_input *mi)
{
  if(mi == NULL){
    return;
  }

  if(mi->i_line){
    destroy_katcl(mi->i_line, 1);
    mi->i_line = NULL;
  }

  if(mi->i_label){
    free(mi->i_label);
    mi->i_label = NULL;
  }

  if(mi->i_actual){
    free(mi->i_actual);
    mi->i_actual = NULL;
  }

  free(mi);
}

int resurrect_input(struct mpx_state *s, struct mpx_input *mi)
{
  int fd;

  if((mi->i_flags & FLAG_DEAD) == 0){ 
    return 0;
  }

  if(mi->i_actual == NULL){
    return -1;
  }

  if(mi->i_line){
    destroy_katcl(mi->i_line, 1);
    mi->i_line = NULL;
  }

  if(mi->i_flags & FLAG_NET){
    fd = net_connect(mi->i_actual, 0, 0);
    if(fd < 0){
      return -1;
    }

    mi->i_line = create_katcl(fd);
    if(mi->i_line == NULL){
      return -1;
    }

    mi->i_flags = mi->i_flags & (~FLAG_DEAD);

    return 0;
  }

  /* TODO: for FLAG_EXEC attempt to restart process - requires strexec or similar ? */

  /* else for FLAG_STREAM unsupported way of restarting things */

  return -1;
}

int add_input(struct mpx_state *s, int fd, unsigned int flags, char *label, char *actual)
{
  struct mpx_input **tmp, *mi;
  struct katcl_line *l;

  if(s == NULL){
    return -1;
  }

  if(fd < 0){
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "%s should have flags 0x%x\n", label, flags);
#endif

  if(flags & FLAG_MASTER){
    if(flags & (FLAG_RELAX | FLAG_FALLBACK)){
      fprintf(stderr, "bad option permutation - the master can not set fallback or nonessential\n");
      return -1;
    }
  }

  tmp = realloc(s->s_vector, sizeof(struct mpx_input *) * (s->s_count + 1));
  if(tmp == NULL){
    return -1;
  }

  s->s_vector = tmp;

  mi = malloc(sizeof(struct mpx_input));
  if(mi == NULL){
    return -1;
  }

  mi->i_line = NULL;
  mi->i_flags = FLAG_DEAD;
  mi->i_label = NULL;
  mi->i_actual = NULL;

  l = create_katcl(fd);
  if(l == NULL){
    destroy_input(s, mi);
    return -1;
  }

  if(label){
    mi->i_label = strdup(label);
    if(mi->i_label == NULL){
      destroy_input(s, mi);
      return -1;
    }
  }

  if(actual){
    mi->i_actual = strdup(actual);
    if(mi->i_actual == NULL){
      destroy_input(s, mi);
      return -1;
    }
  }

  mi->i_line = l;
  mi->i_flags = flags;

  s->s_vector[s->s_count] = mi;

  s->s_count++;

  return 0;
}

int request_change_input(struct mpx_state *ms, char *arg)
{
  struct mpx_input *mt;
  int i, target;
  char *end;

  if(arg == NULL){
    return -1;
  }

  target = (-1);

  if(ms->s_symbolic){
    for(i = 0; (i < ms->s_count) && strcmp(arg, ms->s_vector[i]->i_label); i++);
    target = i;
  } else {
    target = strtoul(arg, &end, 10);
    if(end[0] != '\0'){
      return -1;
    }
  }

  if(target >= ms->s_count){ /* out of range or not found */
    return -1;
  }

  if(target == ms->s_this){ /* talking to ourselves again ... */
    return -1; 
  }

  mt = ms->s_vector[target];

  if(resurrect_input(ms, mt) < 0){
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "change: request %s maps to index %d (current %d)\n", arg, target, ms->s_select);
#endif

  return target;
}

/***********************************************************/

int exec_pipe(char **args)
{
  int fds[2], count;
  pid_t pid;

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0){
    return -1;
  }

  pid = fork();
  if(pid < 0){
    close(fds[0]);
    close(fds[1]);
    return -1;
  }

  if(pid > 0){ /* parent */
    close(fds[0]);

    return fds[1];
  }

  /* in child */

  close(fds[1]);
  count = 0;

  if(fds[0] != STDIN_FILENO){
    if(dup2(fds[0], STDIN_FILENO) != STDIN_FILENO){
      exit(EX_OSERR);
    }
    count++;
  }
  if(fds[0] != STDOUT_FILENO){
    if(dup2(fds[0], STDOUT_FILENO) != STDOUT_FILENO){
      exit(EX_OSERR);
    }
    count++;
  }

  if(count >= 2){
    close(fds[0]);
  }

  execvp(args[0], args);

  exit(EX_UNAVAILABLE);

  return -1;
}

/***********************************************************/

int send_disconnect(struct mpx_state *ms, char *reason)
{
  struct mpx_input *mt;
  int result;

  mt = ms->s_vector[ms->s_this];

  append_string_katcl(mt->i_line, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#disconnect");
  append_string_katcl(mt->i_line, KATCP_FLAG_LAST | KATCP_FLAG_STRING, reason);

  while((result = write_katcl(mt->i_line)) == 0);

  return result;
}

int handle_io_failure(struct mpx_state *ms, struct mpx_input *mi, char *reason)
{
  struct mpx_input *mt, *mf;
  unsigned int flags;

  if(ms->s_fall < 0){
    /* no fallback */
#ifdef DEBUG
    fprintf(stderr, "no fallback defined, giving up\n");
#endif
    return -1;
  }

  mt = ms->s_vector[ms->s_this];
  mf = ms->s_vector[ms->s_fall];

  flags = mi->i_flags;
  mi->i_flags |= FLAG_DEAD;

  if(mi->i_line){
    destroy_katcl(mi->i_line, 1);
    mi->i_line = NULL;
  }

  if((flags & FLAG_RELAX) == 0){
    /* this client is important, io failure here is fatal */
    if(mt != mi){
      send_disconnect(ms, reason);
    }
#ifdef DEBUG
    fprintf(stderr, "client failure not survivable, failing\n");
#endif
    return -1;
  }

  if(mf->i_flags & FLAG_DEAD){
    /* fallback dead */
#ifdef DEBUG
    fprintf(stderr, "fallback client also gone, giving up\n");
#endif
    return -1;
  }

  if(flags & FLAG_DEAD){
    /* already dead ? */
    return 0;
  }

  /* if we have already selected fallback, don't mention it again - bit weird ... */
  if(ms->s_fall != ms->s_select){

    ms->s_switch[0] = KATCP_INFORM;

    append_string_katcl(mt->i_line, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, ms->s_switch);
    append_string_katcl(mt->i_line, KATCP_FLAG_LAST | KATCP_FLAG_STRING, mf->i_label);

    ms->s_select = ms->s_fall;
  }

  return 0;
}

int fixup_checks(struct mpx_state *ms)
{
  unsigned int i;
  int relax, fallback, backup;
  struct mpx_input *mi;

  relax = (-1);
  fallback = (-1);
  backup = (-1);

  if(ms->s_count <= 1){
    fprintf(stderr, "not enough parties to multiplex\n");
    return -1;
  }

  for(i = 0; i < ms->s_count; i++){
    mi = ms->s_vector[i];
    if(mi->i_flags & FLAG_RELAX){
      relax = i;
    }
    if(mi->i_flags & FLAG_FALLBACK){
      fallback = i;
    }
    if(mi->i_flags & FLAG_MASTER){
      if(ms->s_this < 0){
        ms->s_this = i;
      }
    } else {
      if((mi->i_flags & (FLAG_FALLBACK | FLAG_RELAX)) == 0){
        if(backup < 0){
          backup = i;
        }
      }
    }
  }

  if(ms->s_this < 0){
    if(backup < 0){
      fprintf(stderr, "unable to guess which connection ought to be master\n");
      return -1;
    }
    ms->s_this = backup;
  }

  if(ms->s_this >= ms->s_count){
    fprintf(stderr, "controlling connection out of range\n");
    return -1;
  }

  mi = ms->s_vector[ms->s_this];
  mi->i_flags &= ~(FLAG_RELAX | FLAG_FALLBACK);

  if(relax >= 0){
    if(fallback < 0){
      fallback = (ms->s_this > 0) ? 0 : 1;
    }
    ms->s_fall = fallback;
#ifdef DEBUG
    fprintf(stderr, "fallback client is at index %d\n", ms->s_fall);
#endif
  }

  ms->s_select = (ms->s_this > 0) ? 0 : 1;

#ifdef DEBUG
  fprintf(stderr, "have %d parties, master at %d, fallback %d, selected %d\n", ms->s_count, ms->s_this, ms->s_fall, ms->s_select);
#endif

  return 0;
}

int run_state(struct mpx_state *ms)
{
  int run, mfd, fd, i, result, change, size;
  struct mpx_input *mi, *mt;
  fd_set fsr, fsw;
  char *cmd, *arg;

  for(run = 1; run > 0;){

    mfd = (-1);

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    for(i = 0; i < ms->s_count; i++){
      mi = ms->s_vector[i];

#if DEBUG > 1
      fprintf(stderr, "run[%d]: flags=0x%x\n", i, mi->i_flags);
#endif

      if((mi->i_flags & FLAG_DEAD) == 0){ /* what if everybody is dead, huh ? */
        fd = fileno_katcl(mi->i_line);
#if DEBUG > 1
        fprintf(stderr, "run[%d]: fd=%d\n", i, fd);
#endif
        if(fd >= 0){

          FD_SET(fd, &fsr);

          size = flushing_katcl(mi->i_line);
          if(size > 0){
            FD_SET(fd, &fsw);
          }

          if(fd > mfd){
            mfd = fd;
          }
        }
      }

    }

    result = select(mfd + 1, &fsr, &fsw, NULL, NULL);

    for(i = 0; i < ms->s_count; i++){
      mi = ms->s_vector[i];

      if((mi->i_flags & FLAG_DEAD) == 0){

        fd = fileno_katcl(mi->i_line);
        if(fd >= 0){

          if(FD_ISSET(fd, &fsr)){
            result = read_katcl(mi->i_line);

            if(result){
#ifdef DEBUG
              fprintf(stderr, "read[%d] returns %d\n", i, result);
#endif
              if(handle_io_failure(ms, mi, (result < 0) ? "read failure" : "end of stream") < 0){
                run = (result < 0) ? (-1) : 0;
              }
              /* WARNING: s_select could be changed here */
            } else {
              if(awaiting_katcl(mi->i_line) > MAX_BUFFER){
                discard_katcl(mi->i_line);
              }
            }
          }

          if(FD_ISSET(fd, &fsw)){
            result = write_katcl(mi->i_line);
            if(result < 0){
#ifdef DEBUG
              fprintf(stderr, "write[%d] returns %d\n", i, result);
#endif
              if(handle_io_failure(ms, mi, "write failure") < 0){
                run = (result < 0) ? (-1) : 0;
              }
              /* WARNING: s_select could be changed here */
            }
          }

        }
      }
    }

#ifdef DEBUG
#endif

    mt = ms->s_vector[ms->s_this];
    mi = ms->s_vector[ms->s_select];

#ifdef DEBUG
    fprintf(stderr, "relay from master\n");
#endif

    if((mt->i_flags & FLAG_DEAD) || (mi->i_flags & FLAG_DEAD)){
      /* WARNING: io errors should be fixed up earlier ... */
      break;
    }

    while(have_katcl(mt->i_line) > 0){
      cmd = arg_string_katcl(mt->i_line, 0);
      if(cmd){
        if((cmd[0] == KATCP_REQUEST) && (strcmp(cmd + 1, ms->s_switch + 1) == 0)){
#ifdef DEBUG
          fprintf(stderr, "encountered switch request\n");
#endif
          arg = arg_string_katcl(mt->i_line, 1);
          change = request_change_input(ms, arg);

          ms->s_switch[0] = KATCP_REPLY;
          append_string_katcl(mt->i_line, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, ms->s_switch);

          if(change < 0){
            append_string_katcl(mt->i_line, KATCP_FLAG_LAST | KATCP_FLAG_STRING, KATCP_FAIL);
          } else {
            ms->s_select = change;
            mi = ms->s_vector[ms->s_select];
            append_string_katcl(mt->i_line, KATCP_FLAG_LAST | KATCP_FLAG_STRING, KATCP_OK);
          }
        } else {
#ifdef DEBUG
          fprintf(stderr, "doing relay of %s to %d\n", cmd, ms->s_select);
#endif
          if(relay_katcl(mt->i_line, mi->i_line) < 0){
#ifdef DEBUG
            fprintf(stderr, "relay from master of %s failed\n", cmd);
#endif
            send_disconnect(ms, "relay failure");
            run = (-1);
          }
        }
      }
    }

#ifdef DEBUG
    fprintf(stderr, "relay to master\n");
#endif

    while(have_katcl(mi->i_line) > 0){
#ifdef DEBUG
      cmd = arg_string_katcl(mi->i_line, 0);
      if(cmd){
        fprintf(stderr, "selected said %s\n", cmd);
      }
#endif
      if(relay_katcl(mi->i_line, mt->i_line) < 0){
#ifdef DEBUG
        fprintf(stderr, "relay from selected of %s failed\n", cmd);
#endif
        run = (-1);
      }
    }

  }

  return run;
}

/***********************************************************/

void usage(char *app)
{
  printf("usage: %s [options] -e command [args]\n", app);
  printf("-h                 this help\n");
  printf("-v                 increase verbosity\n");
  printf("-q                 run quietly\n");
#if 0
  /* not yet implemented */
  printf("-b                 buffer command\n");
#endif
  printf("-r                 io failure in next party is not fatal\n");
  printf("-k                 next party is the one to switch to in case of io failure\n");
  printf("-m                 next party is master\n");
  printf("-d                 go into background\n");
  printf("-f                 remain in foreground\n");
  printf("-i                 indexed rather than symbolic selection\n");
  printf("-s switch          command used to switch (default %s)\n", DEFAULT_SWITCH);
  printf("-n host:port       remote party to contact\n");
  printf("-l label           set symbolic name for next peer\n");
  printf("-e command [args]  subprocess to launch\n");

}


/***********************************************************/

#define TYPE_CLIENT    0
#define TYPE_EXEC      1

int main(int argc, char **argv)
{
  int i, j, c, verbose, detach, type, offset, fd, result, symbolic;
  unsigned int flags, initial;
  char *app, *remote, *change, *label;
  struct mpx_state *ms;

  i = j = 1;
  app = argv[0];
  detach = 0;

  verbose = 0;
  offset = 0;
  change = NULL;
  symbolic = 1;
  label = NULL;

  flags = 0;

  flags = FLAG_DISCARD;
  initial = flags;

  type = TYPE_CLIENT;

  ms = create_state();
  if(ms == NULL){
    fprintf(stderr, "%s: unable to allocate state\n", app);
    return EX_OSERR;
  }

  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(app);
          return EX_OK;

        case 'b' : 
          flags &= ~FLAG_DISCARD;     
          j++;
          break;

        case 'r' : 
          flags |= FLAG_RELAX;     
          j++;
          break;

        case 'k' : 
          flags |= FLAG_FALLBACK;     
          j++;
          break;

        case 'm' : 
          flags |= FLAG_MASTER;     
          j++;
          break;

        case 'v' : 
          verbose++;
          j++;
          break;
        case 'q' : 
          verbose = 0;
          j++;
          break;

        case 'd' : 
          detach = 1;
          j++;
          break;

        case 'f' : 
          detach = 0;
          j++;
          break;

        case 'e' : 
          type = TYPE_EXEC;
          j++;
          break;

        case 'n' :
          type = TYPE_CLIENT;
          j++;
          break;

        case 'i' :
          symbolic = 0;
          j++;
          break;


        case 's' :
        case 'l' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            fprintf(stderr, "%s: usage: argument needs a parameter\n", app);
            return EX_USAGE;
          }

          switch(c){
            case 's' :
              change = argv[i] + j;
              break;
            case 'l' :
              label = argv[i] + j;
              break;
          }

          i++;
          j = 1;
          break;

        case '-' :
          j++;
          break;

        case '\0':
          if(j == 1){
            sleep(1);

            if(add_input(ms, STDIN_FILENO, flags | FLAG_STREAM, label ? label : "-", NULL) < 0){
              fprintf(stderr, "%s: unable to add standard stream\n", app);
              return EX_UNAVAILABLE;
            }

            flags = initial;
            label = NULL;

          }
          j = 1;
          i++;
          break;
        default:
          fprintf(stderr, "%s: usage: unknown option -%c\n", app, argv[i][j]);
          return EX_USAGE;
      }
    } else {

      switch(type){
        case TYPE_CLIENT :

          remote = argv[i];

          fd = net_connect(remote, 0, NETC_VERBOSE_ERRORS);
          if(fd < 0){
            fprintf(stderr, "%s: unable to connect to %s\n", app, remote);
            return EX_UNAVAILABLE;
          }

          if(add_input(ms, fd, flags | FLAG_NET, label ? label : remote, remote) < 0){
            fprintf(stderr, "%s: unable to add %s\n", app, remote);
            return EX_UNAVAILABLE;
          }

          flags = initial;
          label = NULL;

          break;
        case TYPE_EXEC :
          if(offset > 0){
            fprintf(stderr, "%s: usage: multiple exec specified", app);
          }

          fd = exec_pipe(&(argv[i]));
          if(fd < 0){
            fprintf(stderr, "%s: unable to launch to %s\n", app, argv[i]);
            return EX_UNAVAILABLE;
          }

          if(add_input(ms, fd, flags | FLAG_EXEC, label ? label : argv[i], NULL) < 0){
            fprintf(stderr, "%s: unable to add %s\n", app, argv[i]);
            return EX_UNAVAILABLE;
          }

          flags = initial;
          label = NULL;

          /* TODO: this could be changed ... */
          if(ms->s_count <= 0){
            fprintf(stderr, "%s: internal logic problem\n", app);
            return EX_UNAVAILABLE;
          }

          ms->s_this = ms->s_count - 1;

          offset = i;
          i = argc;
          break;
        default :
          fprintf(stderr, "%s: internal problem: bad connection type %d\n", app, type);
          return EX_SOFTWARE;
      }

      /* j = 0; */

      i++;
    }
  }

  if(set_change(ms, change, symbolic) < 0){
    fprintf(stderr, "%s: unable to configure settings\n", app);
    return EX_SOFTWARE;
  }

  if(fixup_checks(ms) < 0){
    fprintf(stderr, "%s: sanity checks failed\n", app);
    return EX_SOFTWARE;
  }

  result = run_state(ms);

#ifdef DEBUG
  fprintf(stderr, "%s: run ended with code %d\n", app, result);
#endif

  if(result < 0){
    return EX_SOFTWARE;
  }

  return EX_OK;
#undef BUFFER
}

