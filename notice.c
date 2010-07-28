
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

struct katcp_notice *create_notice_katcp(struct katcp_dispatch *d)
{
  struct katcp_notice *n;
  struct katcp_notice **t;
  struct katcp_shared *s;

  s = d->d_shared;

  n = malloc(sizeof(struct katcp_notice));
  if(n == NULL){
    return NULL;
  }

  n->n_vector = NULL;
  n->n_count = 0;

  n->n_trigger = 0;

  t = realloc(s->s_notices, sizeof(struct katcp_notice *) * (s->s_pending + 1));
  if(t == NULL){
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

struct katcp_notice *register_notice_katcp(struct katcp_dispatch *d, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n))
{
  struct katcp_notice *n;

  n = create_notice_katcp(d);
  if(n == NULL){
    return NULL;
  }

  if(add_notice_katcp(d, n, call) < 0){
    destroy_notice_katcp(d, n);
    return NULL;
  }

  return n;
}

void trigger_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  n->n_trigger = 1;
}

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

void wake_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  n->n_trigger = 1;
}

int dump_notices_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  int i;

  s = d->d_shared;

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "%d notices", s->s_pending);

  for(i = 0; i < s->s_pending; i++){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "notice %p with %d subscribers", s->s_notices[i], s->s_notices[i]->n_count);
  }

  return 0;
}
