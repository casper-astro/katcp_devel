
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "katcp.h"
#include "katpriv.h"

/**********************************************************************************/

static void deallocate_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  if(n){

    if(n->n_count){
      /* TODO: could kill all dispatch entries, instead of leaving them hung */
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "destroying notice with active users");
      n->n_count = 0;
    }

    if(n->n_vector){
      free(n->n_vector);
      n->n_vector = NULL;
    }

    if(n->n_release){
      (*(n->n_release))(d, n, n->n_payload);
      n->n_release = NULL;
    }

    if(n->n_name){
      free(n->n_name);
      n->n_name = NULL;
    }

    n->n_tag = (-1);
    n->n_payload = NULL;

    free(n);
  }
}

static void destroy_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  int i, check;
  struct katcp_shared *s;

  if(n == NULL){
    return;
  }

  s = d->d_shared;
  if(s == NULL){
    return;
  }

  i = 0;
  check = 0;

  while(i < s->s_pending){
    if(s->s_notices[i] == n){

      if(check == 0){
        deallocate_notice_katcp(d, n);
      }

      s->s_pending--;
      check++;

      if(i < s->s_pending){
        s->s_notices[i] = s->s_notices[s->s_pending];
      }
      
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

  if(check > 1){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "major corruption as notice %p encountered %d times", n, check);
  }

}

/**********************************************************************************/

static int notice_compact_notice_katcp(struct katcp_notice *n, struct katcp_dispatch *d)
{
  int i, check;

  check = 0;
  i = 0;

  while(i < n->n_count){
    if(n->n_vector[i].v_client == d){
      n->n_count--;
      check++;
      if(i < n->n_count){
        n->n_vector[i].v_client = n->n_vector[n->n_count].v_client;
        n->n_vector[i].v_call   = n->n_vector[n->n_count].v_call;
      }
    } else {
      i++;
    }
  }

  if(n->n_count == 0){
    destroy_notice_katcp(d, n);
  }

  if(check > 1){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "dispatch %p present multiple times in notice %p", d, n);
  }

  return check;
}

static int dispatch_compact_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  int i, check;

  check = 0;
  i = 0;

  while(i < d->d_count){
    if(d->d_notices[i] == n){
      d->d_count--;
      check++;
      if(i < d->d_count){
        d->d_notices[i] = d->d_notices[d->d_count];
      }
    } else {
      i++;
    }
  }

  if(d->d_count == 0){
    if(d->d_notices){
      free(d->d_notices);
      d->d_notices = NULL;
    }
  }

  if(check > 1){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "notice %p present multiple times in dispatch %p", n, d);
  }

  return check;
}

/**********************************************************************************/

void unlink_notices_katcp(struct katcp_dispatch *d)
{
  int i;
  struct katcp_notice *n;

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "unlinking %d pending notices for dispatch %p", d->d_count, d);


  for(i = (d->d_count - 1); i >= 0; i--){
    n = d->d_notices[i];
    if(notice_compact_notice_katcp(n, d) <= 0){
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to remove notice %p from dispatch %p", n, d);
    }
  }

  d->d_count = 0;

  if(d->d_notices){
    free(d->d_notices);
    d->d_notices = NULL;
  }
}

int unlink_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  if(dispatch_compact_notice_katcp(d, n) == 0){
    return 0;
  }

  if(notice_compact_notice_katcp(n, d) == 0){ /* after this point n may not be valid */
    return 0;
  }
  
  return 1;
}

/**********************************************************************************/

void destroy_notices_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  s = d->d_shared;

  while((s->s_pending > 0) && (s->s_notices != NULL)){
    destroy_notice_katcp(d, s->s_notices[0]);
  }

  if((s->s_pending != 0) || (s->s_notices != NULL)){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "cleared notice list left inconsistent");
  }
}

/**********************************************************************************/

struct katcp_notice *create_notice_katcp(struct katcp_dispatch *d, char *name, unsigned int tag)
{
  struct katcp_notice *n;
  struct katcp_notice **t;
  struct katcp_shared *s;

  s = d->d_shared;

  if(name){
    n = find_notice_katcp(d, name);
    if(n != NULL){
      /* TODO: maybe check tag */
      return n;
    }
  }

  n = malloc(sizeof(struct katcp_notice));
  if(n == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes for notice", sizeof(struct katcp_notice));
    return NULL;
  }

  n->n_vector = NULL;
  n->n_count = 0;

  n->n_trigger = 0;

  n->n_tag = tag;
  n->n_payload = NULL;
  n->n_release = NULL;

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
  struct katcp_notice **tn;

#ifdef DEBUG
  fprintf(stderr, "dispatch %p has %d notices, now adding notice %p having %d subscribers\n", d, d->d_count, n, n->n_count);
#endif

  v = realloc(n->n_vector, sizeof(struct katcp_invoke) * (n->n_count + 1));
  if(v == NULL){
    return -1;
  }
  n->n_vector = v;

  tn = realloc(d->d_notices, sizeof(struct katcp_notice *) * (d->d_count + 1));
  if(tn == NULL){
    return -1;
  }
  d->d_notices = tn;

  v = &(n->n_vector[n->n_count]);
  v->v_client = d;
  v->v_call = call;

  n->n_count++;

  d->d_notices[d->d_count] = n;
  d->d_count++;

  return 0;
}

struct katcp_notice *register_notice_katcp(struct katcp_dispatch *d, char *name, unsigned int tag, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n))
{
  struct katcp_notice *n;

  n = create_notice_katcp(d, name, tag);
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

int cancel_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  int i;
  struct katcp_invoke *v;

  for(i = 0; i < n->n_count; i++){
    v = &(n->n_vector[i]);

    if(dispatch_compact_notice_katcp(v->v_client, n) == 0){
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no back link from dispatch %p to notice %p\n", v->v_client, n);
    }
  }

  n->n_count = 0;
  if(n->n_vector){
    free(n->n_vector);
    n->n_vector = NULL;
  }
  
  destroy_notice_katcp(d, n);

  return 0;
}

int cancel_name_notice_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_notice *n;

  n = find_notice_katcp(d, name);
  if(n == NULL){
    return -1;
  }

  cancel_notice_katcp(d, n);

  return 0;
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
  int i, j, remove;

  s = d->d_shared;
  i = 0;
  remove = 0;

#ifdef DEBUG
  fprintf(stderr, "notice: running %d pending entries\n", s->s_pending);
#endif

  while(i < s->s_pending){
    n = s->s_notices[i];
    if(n->n_trigger){

      j = 0;
      while(j < n->n_count){
        v = &(n->n_vector[j]);
        if((*(v->v_call))(v->v_client, n) <= 0){

          dispatch_compact_notice_katcp(v->v_client, n);

          n->n_count--;
          if(j < n->n_count){
            n->n_vector[j].v_client = n->n_vector[n->n_count].v_client;
            n->n_vector[j].v_call   = n->n_vector[n->n_count].v_call;
          }
        } else {
          j++;
        }
      }

      n->n_trigger = 0;

      if(n->n_count <= 0){
        deallocate_notice_katcp(d, n);

        s->s_pending--;
        if(i < s->s_pending){
          s->s_notices[i] = s->s_notices[s->s_pending];
        }
      } else {
        i++;
      }

    } else {
      i++;
    }
  }

  return 0;
}

#if 0
/* optimise later */
int run_notices_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_notice *n;
  struct katcp_invoke *v;
  int i, j, remove;

  s = d->d_shared;
  i = 0;
  remove = 0;

  while(i < s->s_pending){
    n = s->s_notices[i];
    if(n->n_trigger){

      j = 0;
      while(j < n->n_count){
        v = &(n->n_vector[j]);
        if((*(v->v_call))(v->v_client, n) <= 0){
          n->n_count--;
          if(j < n->n_count){
            n->n_vector[j].v_client = n->n_vector[n->n_count].v_client;
            n->n_vector[j].v_call   = n->n_vector[n->n_count].v_call;
          }
        } else {
          j++;
        }
      }

      n->n_trigger = 0;

      if(n->n_count <= 0){
        deallocate_notice_katcp(d, n);

        s->s_pending--;
        if(i < s->s_pending){
          s->s_notices[i] = s->s_notices[s->s_pending];
        }
      } else {
        i++;
      }

    } else {
      i++;
    }
  }

  return 0;
}
#endif

/**********************************************************************************/

int resume_notice(struct katcp_dispatch *d, struct katcp_notice *n)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "resuming after waiting for notice");

#if 0
  append_string_katcp(d, KATCP_FLAG_FIRST, "!notice");
#endif

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_LAST, KATCP_OK);

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
      
      if(create_notice_katcp(d, value, 0) == NULL){
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
