#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "katpriv.h"
#include "katcp.h"

static void destroy_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a);

struct katcp_arb *create_arb_katcp(struct katcp_dispatch *d, char *name, int fd, unsigned int mode, int (*run)(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode), void *data)
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

  a->a_mode = mode & (KATCP_ARB_READ | KATCP_ARB_WRITE);
  a->a_run = run;
  a->a_data = data;

  s->s_extras[s->s_total] = a;
  s->s_total++;

  return a;
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

    /* TODO: might want to run callback once to inform of shutdown */
    destroy_arb_katcp(d, a);

    s->s_extras[i] = NULL;
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
  if(a->a_name){
    free(a->a_name);
    a->a_name = NULL;
  }

  if(a->a_fd >= 0){
    close(a->a_fd);
    a->a_fd = (-1);
  }

  free(a);
}

int unlink_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a)
{
  unsigned int i;
  struct katcp_shared *s;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }
  
  for(i = 0; i < s->s_total; i++){
    if(s->s_extras[i] == a){
      break;
    }
  }

  if(i >= s->s_total){
    return -1;
  }

  s->s_total--;

  if(i < s->s_total){
    s->s_extras[i] = s->s_extras[s->s_total];
  }

  destroy_arb_katcp(d, a);

  return 0;
}

void mode_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode)
{
  if(a == NULL){
    return;
  }

  a->a_mode = mode & (KATCP_ARB_READ | KATCP_ARB_WRITE);
}

char *name_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a)
{
  if(a == NULL){
    return NULL;
  }

  return a->a_name;
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

struct katcp_arb *find_arb_katcp(struct katcp_dispatch *d, char *name)
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

    if(a->a_name && (strcmp(a->a_name, name) == 0)){
      return a;
    }
  }

  return NULL;
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
  
  for(i = 0; i < s->s_total; i++){
    a = s->s_extras[i];

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
  
  i = 0;
  while(i < s->s_total){
    a = s->s_extras[i];

    fd = a->a_fd;

    if(fd >= 0){

      mode = 0;

      if(FD_ISSET(fd, &(s->s_read))){
        mode = KATCP_ARB_READ;
      }
      if(FD_ISSET(fd, &(s->s_write))){
        mode = KATCP_ARB_WRITE;
      }

      if(mode & (KATCP_ARB_READ | KATCP_ARB_WRITE)){
        ran++;
        result = (*(a->a_run))(d, a, mode);
        if(result != 0){
           destroy_arb_katcp(d, a);
           s->s_total--;
           if(i < s->s_total){ /* WARNING: this messes with the order of execution - might be problematic later */
             s->s_extras[i] = s->s_extras[s->s_total];
           }
           continue; /* WARNING */
        }
      }

      i++;
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
          log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s callback on fd %d which %s %s", a->a_name ? a->a_name : "<anonymous>", a->a_fd, (a->a_mode & KATCP_ARB_READ) ? "wants read" : "ignores read", (a->a_mode & KATCP_ARB_WRITE) ? "wants writes" : "ignores writes");
        }
      }
      return KATCP_RESULT_OK;
    } else {
      return KATCP_RESULT_FAIL;
    }
  }

}

