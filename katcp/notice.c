
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

/**********************************************************************************/

static void deallocate_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  if(n){

    /* a notice should only disappear if all things pointing to it are gone */
    if(n->n_use > 0){
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "major logic problem: destroying notice  %p (%s) which is used", n, n->n_name ? n->n_name : "<anonymous>");
      n->n_use = 0;
    }

    if(n->n_vector){
      free(n->n_vector);
      n->n_vector = NULL;
    }

    if(n->n_count){
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "major logic problem: destroying notice with active users");
      n->n_count = 0;
    }

    if(n->n_name){
      free(n->n_name);
      n->n_name = NULL;
    }

    n->n_tag = (-1);

    if(n->n_parse){
      destroy_parse_katcl(n->n_parse);
      n->n_parse = NULL;
    }

    free(n);
  }
}

static void reap_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  int i, check;
  struct katcp_shared *s;

  if(n == NULL){
    return;
  }

  if((n->n_use > 0) || (n->n_count > 0)){
#ifdef DEBUG
    fprintf(stderr, "notice: still in use\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "major corruption: refusing to destroy notice which is in use");
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
        n->n_vector[i].v_data   = n->n_vector[n->n_count].v_data;
      }
    } else {
      i++;
    }
  }

  if(n->n_count == 0){
    if(n->n_vector){
      free(n->n_vector);
      n->n_vector = NULL;
    }
  }

#if 0
  if((n->n_count == 0) && (n->n_use == 0)){
    reap_notice_katcp(d, n);
  }
#endif

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

void disown_notices_katcp(struct katcp_dispatch *d)
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

#if 0
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
#endif

/**********************************************************************************/

void destroy_notices_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  s = d->d_shared;

#ifdef DEBUG
  fprintf(stderr, "notice: final cleanup: pending=%d, notices=%p\n", s->s_pending, s->s_notices);
#endif

  while(s->s_pending > 0){
    reap_notice_katcp(d, s->s_notices[0]);
  }

  if(s->s_notices){
    free(s->s_notices);
    s->s_notices = NULL;
  }
}

/**********************************************************************************/

struct katcp_notice *create_parse_notice_katcp(struct katcp_dispatch *d, char *name, unsigned int tag, struct katcl_parse *p)
{
  struct katcp_notice *n;
  struct katcp_notice **t;
  struct katcp_shared *s;

#ifdef DEBUG
  fprintf(stderr, "notice: creating notice (%s) with parse %p\n", name, p);
#endif

  if(name){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "creating %s notice with tag %d", name, tag);
  } else {
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "creating anonymous notice with tag %d", tag);
  }

  if(p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "creating a notice without a message is a usage problem");
    return NULL;
  }

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
  n->n_name = NULL;

  n->n_tag = tag;
  n->n_use = 0;

#if 0
  n->n_msg = NULL;
#endif

  n->n_parse = NULL;

#if 0
  n->n_target = NULL;
  n->n_release = NULL;
#endif

  if(name){
    n->n_name = strdup(name);
    if(n->n_name == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to duplicate name %s", name);
      deallocate_notice_katcp(d, n);
      return NULL;
    }
  }

  t = realloc(s->s_notices, sizeof(struct katcp_notice *) * (s->s_pending + 1));
  if(t == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to insert notice into list");
    deallocate_notice_katcp(d, n);
    return NULL;
  }
  s->s_notices = t;

  n->n_parse = p;

  s->s_notices[s->s_pending] = n;
  s->s_pending++;

  return n;
}

struct katcp_notice *create_notice_katcp(struct katcp_dispatch *d, char *name, unsigned int tag)
{
  struct katcl_parse *p;
  struct katcp_notice *n;

  p = create_parse_katcl(NULL);
  if(p == NULL){
    return NULL;
  }

  n = create_parse_notice_katcp(d, name, tag, p);
  if(n == NULL){
    destroy_parse_katcl(p);
    return NULL;
  }

  return n;
}

int add_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data)
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
  v->v_data = data;

  n->n_count++;

  d->d_notices[d->d_count] = n;
  d->d_count++;

  return 0;
}

struct katcp_notice *register_notice_katcp(struct katcp_dispatch *d, char *name, unsigned int tag, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data)
{
  struct katcp_notice *n;

  n = create_notice_katcp(d, name, tag);
  if(n == NULL){
    return NULL;
  }

  if(add_notice_katcp(d, n, call, data) < 0){
#if 0
    destroy_notice_katcp(d, n); /* not needed: an empty notice will be collected */
#endif
    return NULL;
  }

  return n;
}

/*******************************************************************************/

#if 0
struct katcl_msg *message_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  return n->n_msg;
}
#endif

struct katcl_parse *get_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  return n->n_parse;
}

#if 0
void forget_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  n->n_parse = NULL;
}
#endif

#if 0
int code_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
#ifdef DEBUG
  fprintf(stderr, "notice %p: my code is %d\n", n, n->n_code);
#endif
  return n->n_code;
}

char *code_name_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  return code_to_name_katcm(n->n_code);
}
#endif

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

struct katcp_notice *find_used_notice_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_notice *n;

  n = find_notice_katcp(d, name);
  if(n == NULL){
    return NULL;
  }

  if(n->n_use <= 0){
    return NULL;
  }

  return n;
}

/*******************************************************************************/

int cancel_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  /* WARNING: not currently used. Consider implications of cancelling a notice still wakeable */
  int i;
  struct katcp_invoke *v;

  if(n->n_use > 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "cancelling notice which could still be woken");
  }

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
  
#if 0
  destroy_notice_katcp(d, n);
#endif

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

void wake_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p)
{
  struct katcl_parse *tmp;

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "waking notice %s@%p (source=%d, client=%d) with parse %p (%s ...)", n->n_name, n, n->n_use, n->n_count, p, p ? get_string_parse_katcl(p, 0) : "<null>");

  n->n_trigger = 1;

  /* WARNING: deals with case where tmp == p */
  tmp = n->n_parse;

  if(p){
    n->n_parse = copy_parse_katcl(p);
#ifdef DEBUG
    if(n->n_parse == NULL){
      fprintf(stderr, "wake notice: copy parse failed, which should not happen");
      abort();
    }
#endif
  } else {
    n->n_parse = NULL;
  }

  /* destruction posthoc, in order to not trigger GC of p, in case p == n->n_parse */
  if(tmp){
    destroy_parse_katcl(tmp);
  }
}

int wake_name_notice_katcp(struct katcp_dispatch *d, char *name, struct katcl_parse *p)
{
  struct katcp_notice *n;

  n = find_notice_katcp(d, name);
  if(n == NULL){
    return -1;
  }

  wake_notice_katcp(d, n, p);

  return 0;
}

/*******************************************************************************/

int run_notices_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_notice *n;
  struct katcp_invoke *v;
  int i, j, remove, result;

  s = d->d_shared;
  i = 0;
  remove = 0;
  result = 0;

#ifdef DEBUG
  fprintf(stderr, "notice: running %d pending entries\n", s->s_pending);
#endif

  while(i < s->s_pending){
    n = s->s_notices[i];

#ifdef DEBUG
    fprintf(stderr, "notice: running notice %p, trigger=%d (with parse %p)\n", n, n->n_trigger, n->n_parse);
#endif

    if(n->n_trigger){

      j = 0;
      while(j < n->n_count){
        v = &(n->n_vector[j]);
        if((*(v->v_call))(v->v_client, n, v->v_data) <= 0){

          dispatch_compact_notice_katcp(v->v_client, n);

          n->n_count--;
          if(j < n->n_count){
            n->n_vector[j].v_client = n->n_vector[n->n_count].v_client;
            n->n_vector[j].v_call   = n->n_vector[n->n_count].v_call;
            n->n_vector[j].v_data   = n->n_vector[n->n_count].v_data;
          }
        } else {
          j++;
        }
      }

      n->n_trigger = 0;

      if((n->n_count <= 0) && (n->n_use <= 0)){
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

  return result;
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

int resume_notice(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  char *ptr;

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "resuming after waiting for notice");

#if 0
  append_string_katcp(d, KATCP_FLAG_FIRST, "!notice");
#endif

  p = get_parse_notice_katcp(d, n);
  if(p){
    ptr = get_string_parse_katcl(p, 1);
  } else {
    ptr = NULL;
  }

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_LAST, ptr ? ptr : KATCP_FAIL);

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
      for(i = 0; i < s->s_pending; i++){
        n = s->s_notices[i];
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s notice at %p with %d subscribers and %d references", n->n_name ? n->n_name : "<anonymous>", n, n->n_count, n->n_use);
      }
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d notices", s->s_pending);
      return KATCP_RESULT_OK;

#if 0 /* thesedays an empty notice will be collected immediately */
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
#endif

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

      if(add_notice_katcp(d, n, &resume_notice, NULL)){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to watch notice %s", value);
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_PAUSE;

    } else if(!strcmp(name, "wake")){

      value = arg_string_katcp(d, 2);
      if(value == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "require a notice to wake");
        return KATCP_RESULT_FAIL;
      }

      n = find_notice_katcp(d, value);
      if(n == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "notice %s not found", value);
        return KATCP_RESULT_FAIL;
      }

      wake_notice_katcp(d, n, NULL);

      return KATCP_RESULT_OK;
    } else {
      return KATCP_RESULT_FAIL;
    }
  }
}
