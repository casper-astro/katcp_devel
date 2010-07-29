
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "katcp.h"
#include "katpriv.h"

void unlink_notices_katcp(struct katcp_dispatch *d)
{
  int i, j;
  struct katcp_notice *n;

  for(i = 0; i < d->d_count; i++){
    n = d->d_notices[i];
    j = 0;
    while(j < n->n_count){
      if(n->n_vector[j].v_client == d){
        n->n_count--;
        if(j < n->n_count){
          n->n_vector[j].v_client = n->n_vector[n->n_count].v_client;
          n->n_vector[j].v_call   = n->n_vector[n->n_count].v_call;
        }
      } else {
        j++;
      }
    }
    if(n->n_count == 0){
      if(n->n_vector != NULL){
        free(n->n_vector);
        n->n_vector = NULL;
      }
    }
  }

  if(d->d_notices){
    free(d->d_notices);
    d->d_notices = NULL;
  }
}

static void deallocate_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  if(n){

    if(n->n_count){
      /* TODO: could kill all dispatch entries, instead of leaving them hung */

      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "destroying notice with active users\n");

      if(n->n_vector){
        free(n->n_vector);
        n->n_vector = NULL;
      }

      n->n_count = 0;
    }

    if(n->n_name){
      free(n->n_name);
      n->n_name = NULL;
    }

    free(n);
  }
}

static void destroy_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  int i, found;
  struct katcp_shared *s;

  s = d->d_shared;
  i = 0;
  found = 0;

  while(i < s->s_pending){
    if(s->s_notices[i] == n){

      if(found == 0){
        deallocate_notice_katcp(d, n);
      }

      s->s_pending--;
      if(i < s->s_pending){
        s->s_notices[i] = s->s_notices[s->s_pending];
      }

      found++;
      
    } else {
      i++;
    }
  }

  if(s->s_pending <= 0){
    if(s->s_notices){
      free(s->s_notices);
      s->s_notices = NULL;
    }
  }

  switch(found){
    case 0 :
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "notice %p not found\n", n, found);
      break;
    case 1 :
      break;
    default : 
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "major corruption as notice %p encountered %d times\n", n, found);
      break;
  }
}

void destroy_notices_katcp(struct katcp_dispatch *d)
{
  int i;
  struct katcp_shared *s;

  s = d->d_shared;

  for(i = 0; i < s->s_pending; i++){
    deallocate_notice_katcp(d, s->s_notices[i]);
  }
  s->s_pending = 0;

  if(s->s_notices){
    free(s->s_notices);
    s->s_notices = NULL;
  }
}

/*******************************************************************************/

struct katcp_notice *create_notice_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_notice *n;
  struct katcp_notice **t;
  struct katcp_shared *s;

  s = d->d_shared;

  n = malloc(sizeof(struct katcp_notice));
  if(n == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes for notice\n", sizeof(struct katcp_notice));
    return NULL;
  }

  n->n_vector = NULL;
  n->n_count = 0;

  n->n_trigger = 0;

  if(name){
    n->n_name = strdup(name);
    if(n->n_name == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to duplicate name %s", name);
      free(n);
      return NULL;
    }
  } else {
    n->n_name = NULL;
  }


  t = realloc(s->s_notices, sizeof(struct katcp_notice *) * (s->s_pending + 1));
  if(t == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to insert notice into list");
    if(n->n_name){
      free(n->n_name);
      n->n_name = NULL;
    }
    free(n);
    return NULL;
  }

  s->s_notices = t;

  s->s_notices[s->s_pending] = n;
  s->s_pending++;

  return n;
}

int add_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n))
{
  struct katcp_invoke *v;

  v = realloc(n->n_vector, sizeof(struct katcp_invoke) * (n->n_count + 1));
  if(v == NULL){
    return -1;
  }

  n->n_vector = v;

  v = &(n->n_vector[n->n_count]);
  v->v_client = d;
  v->v_call = call;

  n->n_count++;

  return 0;
}

struct katcp_notice *register_notice_katcp(struct katcp_dispatch *d, char *name, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n))
{
  struct katcp_notice *n;

  n = create_notice_katcp(d, name);
  if(n == NULL){
    return NULL;
  }

  if(add_notice_katcp(d, n, call) < 0){
    destroy_notice_katcp(d, n);
    return NULL;
  }

  return n;
}

/*******************************************************************************/

struct katcp_notice *find_notice_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_notice *n;
  struct katcp_shared *s;
  int i;

  if(name == NULL){
    return NULL;
  }

  s = d->d_shared;

  for(i = 0; i < s->s_pending; i++){
    n = s->s_notices[i];
    if(n->n_name && (!strcmp(name, n->n_name))){
      return n;
    }
  }

  return NULL;
}

/*******************************************************************************/

void wake_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  n->n_trigger = 1;
}

int wake_name_notice_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_notice *n;

  n = find_notice_katcp(d, name);
  if(n == NULL){
    return -1;
  }

  wake_notice_katcp(d, n);

  return 0;
}

/*******************************************************************************/

int run_notices_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_notice *n;
  struct katcp_invoke *v;
  int i, j;

  s = d->d_shared;
  i = 0;

  while(i < s->s_pending){
    n = s->s_notices[i];
    if(n->n_trigger){
      for(j = 0; j < n->n_count; j++){
        v = &(n->n_vector[j]);
        (*(v->v_call))(v->v_client, n);
      }
      n->n_trigger = 0;

      deallocate_notice_katcp(d, n);

      s->s_pending--;
      if(i < s->s_pending){
        s->s_notices[i] = s->s_notices[s->s_pending];
      }

    } else {
      i++;
    }
  }

  return 0;
}

/*******************************************************************************/

int resume_notice(struct katcp_dispatch *d, struct katcp_notice *n)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "resuming after waiting for notice");

  resume_katcp(d);

  return 0;
}

int notice_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_notice *n;
  char *name, *value;
  int i;

  s = d->d_shared;

  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return KATCP_RESULT_FAIL;
  } else {
    if(!strcmp(name, "list")){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d notices", s->s_pending);

      for(i = 0; i < s->s_pending; i++){
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s notice at %p with %d subscribers", s->s_notices[i]->n_name ? s->s_notices[i]->n_name : "anonymous", s->s_notices[i], s->s_notices[i]->n_count);
      }
      return KATCP_RESULT_OK;
    } else if(!strcmp(name, "create")){
      value = arg_string_katcp(d, 2);
      if(value == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "require a name to create notice");
        return KATCP_RESULT_FAIL;
      }
      
      if(create_notice_katcp(d, value) == NULL){
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_OK;

    } else if(!strcmp(name, "watch")){

      value = arg_string_katcp(d, 2);
      if(value == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "require a notice to watch");
        return KATCP_RESULT_FAIL;
      }

      n = find_notice_katcp(d, value);
      if(n == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "notice %s not found", value);
        return KATCP_RESULT_FAIL;
      }

      if(add_notice_katcp(d, n, &resume_notice)){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to watch notice %s", value);
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_PAUSE;

    } else if(!strcmp(name, "wake")){

      value = arg_string_katcp(d, 2);
      if(value == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "require a notice to watch");
        return KATCP_RESULT_FAIL;
      }

      if(wake_name_notice_katcp(d, value) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "notice %s not found", value);
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_OK;

    } else {
      return KATCP_RESULT_FAIL;
    }
  }
}
