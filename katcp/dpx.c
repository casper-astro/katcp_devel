#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
  struct katcp_flat *f;
  struct katcp_shared *s;

  s = d->d_shared;

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
