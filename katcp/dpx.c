#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <netc.h>
#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

#define FLAT_MAGIC 0x49021faf

#define FLAT_STATE_GONE   0
#define FLAT_STATE_NEW    1
#define FLAT_STATE_UP     2
#define FLAT_STATE_DRAIN  3

/* was supposed to be called duplex, but flat is punnier */

struct katcp_flat{
  unsigned int f_magic;
  char *f_name;          /* locate the thing by name */

  int f_flags;           /* which directions can we do */

  int f_state;           /* up, shutting down, etc */
  int f_exit_code;       /* reported exit status */

  int f_log_level;       /* log level currently set */

  struct katcp_notice *f_halt;

  struct katcl_line *f_line;
  struct katcp_shared *f_shared;
};

/* */

#ifdef DEBUG
static void sane_flat_katcp(struct katcp_flat *f)
{
  if(f->f_magic != FLAT_MAGIC){
    fprintf(stderr, "flat: bad magic 0x%x, expected 0x%x\n", f->f_magic, FLAT_MAGIC);
    abort();
  }
}
#else 
#define sane_flat_katcp(f);
#endif

static void destroy_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *f)
{
  sane_flat_katcp(f);

  if(f->f_line){
    destroy_katcl(f->f_line, 1);
    f->f_line = NULL;
  }

  if(f->f_name){
    free(f->f_name);
    f->f_name = NULL;
  }

  if(f->f_halt){
    /* TODO: wake halt notice watching us */

    f->f_halt = NULL;
  }

  /* will probably have to decouple ourselves here */
  f->f_shared = NULL;
  f->f_magic = 0;

  free(f);
}

struct katcp_flat *create_flat_katcp(struct katcp_dispatch *d, int fd, char *name)
{
  /* TODO: what about cloning an existing one to preserve misc settings, including log level, etc */
  struct katcp_flat *f, **tmp;
  struct katcp_shared *s;

  s = d->d_shared;

  /* TODO: check if we have hit the ceiling */

  tmp = realloc(s->s_flats, sizeof(struct katcp_flat *) * (s->s_floors + 1));
  if(tmp == NULL){
    return NULL;
  }

  s->s_flats = tmp;

  f = malloc(sizeof(struct katcp_flat));
  if(f == NULL){
    return NULL;
  }

  f->f_magic = FLAT_MAGIC;
  f->f_name = NULL;

  f->f_flags = 0;

  f->f_state = FLAT_STATE_NEW;
  f->f_exit_code = 0; /* WARNING: should technically be a fail, in case we don't set it ourselves */

  f->f_log_level = s->s_default;

  f->f_halt = NULL;
  f->f_line = NULL;
  f->f_shared = NULL;

  if(name){
    f->f_name = strdup(name);
    if(f->f_name == NULL){
      destroy_flat_katcp(d, f);
      return NULL;
    }
  }

  f->f_line = create_katcl(fd);
  if(f->f_line == NULL){
    destroy_flat_katcp(d, f);
    return NULL;
  }

  f->f_shared = s;

  s->s_flats[s->s_floors] = f;
  s->s_floors++;

  return f;
}

int load_flat_katcp(struct katcp_dispatch *d)
{
  struct katcp_flat *f;
  struct katcp_shared *s;
  unsigned int i;
  int result, fd;

  sane_flat_katcp(f);

  s = d->d_shared;

  result = 0;

#ifdef DEBUG
  fprintf(stderr, "flat: loading %u units\n", s->s_floors);
#endif

  for(i = 0; i < s->s_floors; i++){

    f = s->s_flats[i];
    fd = fileno_katcl(f->f_line);

    switch(f->f_state){
      case FLAT_STATE_GONE :
#ifdef DEBUG
        fprintf(stderr, "flat: problem: state %u should have been removed already\n", f->f_state);
        abort();
#endif
        break;

      case FLAT_STATE_UP : 
        FD_SET(fd, &(s->s_read));
        if(fd > s->s_max){
          s->s_max = fd;
        }
        break;

    }

    if(flushing_katcl(f->f_line)){
      FD_SET(fd, &(s->s_write));
      if(fd > s->s_max){
        s->s_max = fd;
      }
    }
    /* TODO */
  }

  return result;
}

int run_flat_katcp(struct katcp_dispatch *d)
{
  struct katcp_flat *f;
  struct katcp_shared *s;
  unsigned int i;
  int fd, result;

  sane_flat_katcp(f);

  s = d->d_shared;

  result = 0;

#ifdef DEBUG
  fprintf(stderr, "flat: running %u units\n", s->s_floors);
#endif

  for(i = 0; i < s->s_floors; i++){
    f = s->s_flats[i];

    s->s_this = f;

    fd = fileno_katcl(f->f_line);

    if(FD_ISSET(fd, &(s->s_write))){
      if(write_katcl(f->f_line) < 0){
        /* FAIL somehow */
      }
    }

    if(FD_ISSET(fd, &(s->s_read))){
      if(read_katcl(f->f_line) < 0){
        /* FAIL somehow */
      }
    }

    /* TODO */
  }

  s->s_this = NULL;

  return result;
}

/****************************************************************/

int accept_flat_katcp(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode)
{
#define LABEL_BUFFER 32
  int fd, nfd;
  unsigned int len;
  struct sockaddr_in sa;
  struct katcp_flat *f;
  char label[LABEL_BUFFER];
  long opts;

  fd = fileno_arb_katcp(d, a);

  if(!(mode & KATCP_ARB_READ)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "ay caramba, accept handler expected to be called with the read flag");
    return 0;
  }

  len = sizeof(struct sockaddr_in);
  nfd = accept(fd, (struct sockaddr *) &sa, &len);

  if(nfd < 0){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "accept on %s failed: %s", name_arb_katcp(d, a), strerror(errno));
    return 0;
  }

  opts = fcntl(nfd, F_GETFL, NULL);
  if(opts >= 0){
    opts = fcntl(nfd, F_SETFL, opts | O_NONBLOCK);
  }

  snprintf(label, LABEL_BUFFER, "%s:%d", inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
  label[LABEL_BUFFER - 1] = '\0';

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "accepted new connection from %s via %s", label, name_arb_katcp(d, a));

  f = create_flat_katcp(d, nfd, label);
  if(f == NULL){
    close(nfd);
  }
  
  return 0;
#undef LABEL_BUFFER
}

struct katcp_arb *listen_flat_katcp(struct katcp_dispatch *d, char *name)
{
  int fd;
  struct katcp_arb *a;
  long opts;

  fd = net_listen(name, 0, 0);
  if(fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to listen on %s: %s", name, strerror(errno));
    return NULL;
  }

  opts = fcntl(fd, F_GETFL, NULL);
  if(opts >= 0){
    opts = fcntl(fd, F_SETFL, opts | O_NONBLOCK);
  }

  a = create_arb_katcp(d, name, fd, KATCP_ARB_READ, &accept_flat_katcp, NULL);
  if(a == NULL){
    close(fd);
    return NULL;
  }

  return a;
}

int listen_duplex_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name;

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  if(listen_flat_katcp(d, name) == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to listen on %s: %s", name, strerror(errno));
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int list_duplex_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  unsigned int i;
  struct katcp_shared *s;
  struct katcp_flat *fx;

  s = d->d_shared;

  for(i = 0; i < s->s_floors; i++){
    fx = s->s_flats[i];
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%p: %s", fx, fx->f_name);
  }

  return KATCP_RESULT_OK;
}
