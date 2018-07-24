#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "katpriv.h"
#include "katcp.h"

#define ARB_REAP_LIVE   0
#define ARB_REAP_FADE   1
#define ARB_REAP_GONE   2
#define ARB_REAP_FREE   3

static void destroy_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a);

struct katcp_arb *create_type_arb_katcp(struct katcp_dispatch *d, char *name, unsigned int type, int fd, unsigned int mode, int (*run)(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode), void *data)
{
  struct katcp_arb *a, **tmp;
  struct katcp_shared *s;

  s = d->d_shared;
  if(s == NULL){
    return NULL;
  }

  tmp = realloc(s->s_extras, sizeof(struct katcp_arb *) * (s->s_total + 1));
  if(tmp == NULL){
    return NULL;
  }

  s->s_extras = tmp;

  a = malloc(sizeof(struct katcp_arb));
  if(a == NULL){
    return NULL;
  }

  if(name){
    a->a_name = strdup(name);
    if(a->a_name == NULL){
      free(a);
      return NULL;
    }
  } else {
    a->a_name = NULL;
  }

  a->a_fd = fd;

  a->a_type = type;

  a->a_mode = mode & (KATCP_ARB_READ | KATCP_ARB_WRITE | KATCP_ARB_STOP);
  a->a_reap = ARB_REAP_LIVE;
  a->a_run = run;
  a->a_data = data;

  s->s_extras[s->s_total] = a;
  s->s_total++;

  return a;
}

struct katcp_arb *create_arb_katcp(struct katcp_dispatch *d, char *name, int fd, unsigned int mode, int (*run)(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode), void *data)
{
  return create_type_arb_katcp(d, name, 0, fd, mode, run, data);
}

void destroy_arbs_katcp(struct katcp_dispatch *d)
{
  struct katcp_arb *a;
  unsigned int i;
  struct katcp_shared *s;

  s = d->d_shared;
  if(s == NULL){
    return;
  }

  for(i = 0; i < s->s_total; i++){
    a = s->s_extras[i];

    switch(a->a_reap){
      case ARB_REAP_LIVE :
      case ARB_REAP_FADE :
        if((a->a_mode & KATCP_ARB_STOP) && a->a_run){
          (*(a->a_run))(d, a, KATCP_ARB_STOP);
        }
        a->a_reap = ARB_REAP_GONE;
        break;
      case ARB_REAP_GONE :
        break;
      default :
#ifdef KATCP_CONSISTENCY_CHECKS
        fprintf(stderr, "logic problem: invalid arb state %d for %p while deleting %u of all\n", a->a_reap, a, i);
#endif
        break;
    }

    s->s_extras[i] = NULL;

    destroy_arb_katcp(d, a);
  }

  s->s_total = 0;

  if(s->s_extras){
    free(s->s_extras);
    s->s_extras = NULL;
  }
}

static void destroy_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a)
{
  if(a == NULL){
    return;
  } 

#ifdef KATCP_CONSISTENCY_CHECKS
  if(a->a_reap != ARB_REAP_GONE){
    fprintf(stderr, "logic problem: arb destroy expects gone state, not %d in %p\n", a->a_reap, a);
    abort();
  }
#endif

#ifdef DEBUG
  fprintf(stderr, "arb[%p]: destroy with mode=%u, reap=%u, fd=%d, name=%s\n", a, a->a_mode, a->a_reap, a->a_fd, a->a_name);
#endif

  if(a->a_name){
    free(a->a_name);
    a->a_name = NULL;
  }

  if(a->a_fd >= 0){
    close(a->a_fd);
    a->a_fd = (-1);
  }

  a->a_type = 0;
  a->a_mode = 0;
  a->a_reap = ARB_REAP_FREE;
  a->a_run = NULL;
  a->a_data = NULL;

  free(a);
}

int unlink_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a)
{
  switch(a->a_reap){
    case ARB_REAP_LIVE :
      
      if(a->a_fd >= 0){ /* release fd sooner */
        close(a->a_fd);
        a->a_fd = (-1);
      }

      if(a->a_mode & KATCP_ARB_STOP){
        a->a_reap = ARB_REAP_FADE;
      } else {
        a->a_reap = ARB_REAP_GONE;
      }
      break;
    case ARB_REAP_FADE :
    case ARB_REAP_GONE  :
      /* all sorted */
      break;
    default :
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "logic problem: bad arb state %d for %p while unlinking\n", a->a_reap, a);
      abort();
#endif
      break;
  }

  return 0;
}

void mode_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode)
{
  if(a == NULL){
    return;
  }

  a->a_mode = mode & (KATCP_ARB_READ | KATCP_ARB_WRITE | KATCP_ARB_STOP);
}

char *name_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a)
{
  if(a == NULL){
    return NULL;
  }

  return a->a_name;
}

unsigned int type_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a)
{
  if(a == NULL){
    return 0;
  }

  return a->a_type;
}

void *data_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a)
{
  if(a == NULL){
    return NULL;
  }

  return a->a_data;
}

int fileno_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a)
{
  if(a == NULL){
    return -1;
  }

  return a->a_fd;
}

int foreach_arb_katcp(struct katcp_dispatch *d, unsigned int type, int (*call)(struct katcp_dispatch *d, struct katcp_arb *a, void *data), void *data)
{
  unsigned int i;
  int count;
  struct katcp_shared *s;
  struct katcp_arb *a;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  count = 0;
  
  for(i = 0; i < s->s_total; i++){
    a = s->s_extras[i];

    if(a && (a->a_reap == ARB_REAP_LIVE)){ 
      if((type == 0) || (type == a->a_type)){
        if((*(call))(d, a, data) == 0){
          count++;
        }
      }
    }
  }

  return count;
}

struct katcp_arb *find_type_arb_katcp(struct katcp_dispatch *d, char *name, unsigned int type)
{
  unsigned int i;
  struct katcp_shared *s;
  struct katcp_arb *a;

  s = d->d_shared;
  if(s == NULL){
    return NULL;
  }
  
  for(i = 0; i < s->s_total; i++){
    a = s->s_extras[i];

    if((type == 0) || (a->a_type == type)){
      if(a->a_name && (strcmp(a->a_name, name) == 0)){
        return a;
      }
    }
  }

  return NULL;
}

struct katcp_arb *find_arb_katcp(struct katcp_dispatch *d, char *name)
{
  return find_type_arb_katcp(d, name, 0);
}

void load_arb_katcp(struct katcp_dispatch *d)
{
  unsigned int i;
  struct katcp_shared *s;
  struct katcp_arb *a;

  s = d->d_shared;
  if(s == NULL){
    return;
  }

  i = 0;
  while(i < s->s_total){
  
    a = s->s_extras[i];

#ifdef DEBUG
    fprintf(stderr, "arb[%d]=%p: mode=%u, reap=%u, fd=%d, name=%s\n", i, a, a->a_mode, a->a_reap, a->a_fd, a->a_name);
#endif

    switch(a->a_reap){
      case ARB_REAP_LIVE : 
        if(a->a_fd >= 0){
          if(a->a_mode & KATCP_ARB_READ){
            FD_SET(a->a_fd, &(s->s_read));
          } 
          if(a->a_mode & KATCP_ARB_WRITE){
            FD_SET(a->a_fd, &(s->s_write));
          }

          if(a->a_mode & (KATCP_ARB_WRITE | KATCP_ARB_READ)){
            if(a->a_fd > s->s_max){ 
              s->s_max = a->a_fd;
            }
          }
        }
        i++;
        break;
      case ARB_REAP_FADE : 
        mark_busy_katcp(d);
        i++;
        break;
      case ARB_REAP_GONE : 
        s->s_total--;
        if(s->s_total > i){
          s->s_extras[i] = s->s_extras[s->s_total];
        }
        destroy_arb_katcp(d, a);
        break;
      default :
#ifdef KATCP_CONSISTENCY_CHECKS
        fprintf(stderr, "logic problem: bad arb state %d for %p while loading\n", a->a_reap, a);
        abort();
#endif
        i++;
        break;
    }

  }
}

int run_arb_katcp(struct katcp_dispatch *d)
{
  unsigned int i, mode;
  struct katcp_shared *s;
  struct katcp_arb *a;
  int fd, result, ran;
  

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  ran = 0;
  
  for(i = 0; i < s->s_total; i++){
    a = s->s_extras[i];

    switch(a->a_reap){
      case ARB_REAP_LIVE : 
        fd = a->a_fd;

        if(fd >= 0){

          mode = 0;

          if(FD_ISSET(fd, &(s->s_read))){
            mode |= KATCP_ARB_READ;
          }
          if(FD_ISSET(fd, &(s->s_write))){
            mode |= KATCP_ARB_WRITE;
          }

          if(a->a_mode & mode){
            ran++;
            result = (*(a->a_run))(d, a, mode);
            if(result != 0){
              if(a->a_mode & KATCP_ARB_STOP){
                a->a_reap = ARB_REAP_FADE;
              } else {
                a->a_reap = ARB_REAP_GONE;
              }
            }
          }
        }
        break;

      case ARB_REAP_FADE :
        if(a->a_mode & KATCP_ARB_STOP){
          (*(a->a_run))(d, a, KATCP_ARB_STOP);
        }
        a->a_reap = ARB_REAP_GONE;
        break;

      case ARB_REAP_GONE :
        break;

      default :
#ifdef KATCP_CONSISTENCY_CHECKS
        fprintf(stderr, "logic problem: bad arb state %d for %p while running\n", a->a_reap, a);
        abort();
#endif

        break;
    }

  }

  return ran;
}

int arb_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *cmd;
  struct katcp_arb *a;
  struct katcp_shared *s;
  unsigned int i;

  s = d->d_shared;
  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  cmd = arg_string_katcp(d, 1);
  if(cmd == NULL){
    return KATCP_RESULT_FAIL;
  } else {
    if(!strcmp(cmd, "list")){
      for(i = 0; i < s->s_total; i++){
        a = s->s_extras[i];
        if(a){
          log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s callback on fd %d of type %d in state %d which %s %s and %s", a->a_name ? a->a_name : "<anonymous>", a->a_fd, a->a_type, a->a_reap, (a->a_mode & KATCP_ARB_READ) ? "wants read" : "ignores read", (a->a_mode & KATCP_ARB_WRITE) ? "wants writes" : "ignores writes", (a->a_mode & KATCP_ARB_STOP) ? "handles stop" : "ignores stop" );
        }
      }
      return KATCP_RESULT_OK;
    } else {
      return KATCP_RESULT_FAIL;
    }
  }

}

