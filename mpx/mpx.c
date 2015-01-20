
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

#define DEBUG

#define DEFAULT_SWITCH "switch" 

struct mpx_input
{
  struct katcl_line *i_line;
  unsigned int i_discard;
  char *i_name;
};

struct mpx_state
{
  struct mpx_input **s_vector;
  unsigned int s_count;

  char *s_switch;
  int s_symbolic;

  int s_this;
  int s_select;
};

/***********************************************************/

void destroy_input(struct mpx_state *s, struct mpx_input *mi);
void destroy_state(struct mpx_state *s);

struct mpx_state *create_state();
int add_input(struct mpx_state *s, int fd, int discard, char *label);

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

  if(mi->i_name){
    free(mi->i_name);
  }

  free(mi);
}

int add_input(struct mpx_state *s, int fd, int discard, char *label)
{
  struct mpx_input **tmp, *mi;
  struct katcl_line *l;

  if(s == NULL){
    return -1;
  }

  if(fd < 0){
    return -1;
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
  mi->i_discard = 0;
  mi->i_name = NULL;

  l = create_katcl(fd);
  if(l == NULL){
    destroy_input(s, mi);
    return -1;
  }

  if(label){
    mi->i_name = strdup(label);
    if(mi->i_name == NULL){
      destroy_input(s, mi);
      return -1;
    }
  }

  mi->i_line = l;
  mi->i_discard = discard;

  s->s_vector[s->s_count] = mi;

  s->s_count++;

  return 0;
}

int change_input(struct mpx_state *ms, char *arg)
{
  int i, target;
  char *end;

  if(arg == NULL){
    return -1;
  }

  target = (-1);

  if(ms->s_symbolic){
    for(i = 0; (i < ms->s_count) && strcmp(arg, ms->s_vector[i]->i_name); i++);
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
  struct mpx_input *mi;
  int result;

  mi = ms->s_vector[ms->s_this];


  append_string_katcl(mi->i_line, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#disconnect");
  append_string_katcl(mi->i_line, KATCP_FLAG_LAST | KATCP_FLAG_STRING, reason);

  while((result = write_katcl(mi->i_line)) == 0);

  return result;
}

int run_state(struct mpx_state *ms)
{
  int run, mfd, fd, i, result, change;
  struct mpx_input *mi, *ni;
  fd_set fsr, fsw;
  char *cmd, *arg;

  if(ms->s_this < 0){
    return -1;
  }

  if(ms->s_count <= 1){
    return -1;
  }

  ms->s_select = (ms->s_this > 0) ? 0 : 1;

  for(run = 1; run > 0;){

    mfd = (-1);

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    for(i = 0; i < ms->s_count; i++){
      mi = ms->s_vector[i];

      fd = fileno_katcl(mi->i_line);
      if(fd >= 0){

        FD_SET(fd, &fsr);

        if(flushing_katcl(mi->i_line)){
          FD_SET(fd, &fsw);
        }

        if(fd > mfd){
          mfd = fd;
        }
      }
    }

    result = select(mfd + 1, &fsr, &fsw, NULL, NULL);

    for(i = 0; i < ms->s_count; i++){
      mi = ms->s_vector[i];

      fd = fileno_katcl(mi->i_line);
      if(fd >= 0){

        if(FD_ISSET(fd, &fsr)){
          result = read_katcl(mi->i_line);

          if(result){
#ifdef DEBUG
            fprintf(stderr, "read[%d] returns %d\n", i, result);
#endif

            if(ms->s_this == i){
              run = (result < 0) ? (-1) : 0;
            } else {
              if(result < 0){
                send_disconnect(ms, "read failure");
              } else {
                send_disconnect(ms, "end of stream");
              }
              run = (-1);
            }
          }
        }

        if(FD_ISSET(fd, &fsw)){
          result = write_katcl(mi->i_line);
          if(result < 0){
#ifdef DEBUG
            fprintf(stderr, "write[%d] returns %d\n", i, result);
#endif
            run = (-1);
            if(ms->s_this != i){
              send_disconnect(ms, "write failure");
            }
          }
        }

      }
    }

    mi = ms->s_vector[ms->s_this];
    ni = ms->s_vector[ms->s_select];

    while(have_katcl(mi->i_line) > 0){
      cmd = arg_string_katcl(mi->i_line, 0);
      if(cmd){
        if((cmd[0] == KATCP_REQUEST) && (strcmp(cmd + 1, ms->s_switch + 1) == 0)){
          arg = arg_string_katcl(mi->i_line, 1);
          change = change_input(ms, arg);

          append_string_katcl(mi->i_line, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, ms->s_switch);

          if(change < 0){
            append_string_katcl(mi->i_line, KATCP_FLAG_LAST | KATCP_FLAG_STRING, KATCP_FAIL);
          } else {
            ms->s_select = change;
            ni = ms->s_vector[ms->s_select];
            append_string_katcl(mi->i_line, KATCP_FLAG_LAST | KATCP_FLAG_STRING, KATCP_OK);
          }
        } else {
#ifdef DEBUG
          fprintf(stderr, "doing relay of %s to %d\n", cmd, ms->s_select);
#endif
          if(relay_katcl(mi->i_line, ni->i_line) < 0){
#ifdef DEBUG
            fprintf(stderr, "relay from master of %s failed\n", cmd);
#endif
            send_disconnect(ms, "relay failure");
            run = (-1);
          }
        }
      }
    }

    while(have_katcl(ni->i_line) > 0){
#ifdef DEBUG
      cmd = arg_string_katcl(ni->i_line, 0);
      if(cmd){
        fprintf(stderr, "selected said %s\n", cmd);
      }
#endif
      if(relay_katcl(ni->i_line, mi->i_line) < 0){
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
  printf("-b                 buffer command\n");
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
  int i, j, c, verbose, detach, type, offset, discarding, fd, result, symbolic;

  char *app, *remote, *change, *label;
  struct mpx_state *ms;

  i = j = 1;
  app = argv[0];
  detach = 0;

  verbose = 0;
  offset = 0;
  discarding = 1;
  change = NULL;
  symbolic = 1;
  label = NULL;

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
          discarding = 0;
          j++;
          break;

        case 'v' : 
          verbose++;
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
            if(add_input(ms, STDIN_FILENO, discarding, label ? label : "-") < 0){
              fprintf(stderr, "%s: unable to add standard stream\n", app);
              return EX_UNAVAILABLE;
            }
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

          if(add_input(ms, fd, discarding, label ? label : remote) < 0){
            fprintf(stderr, "%s: unable to add %s\n", app, remote);
            return EX_UNAVAILABLE;
          }
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

          if(add_input(ms, fd, discarding, label ? label : argv[i]) < 0){
            fprintf(stderr, "%s: unable to add %s\n", app, argv[i]);
            return EX_UNAVAILABLE;
          }
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

