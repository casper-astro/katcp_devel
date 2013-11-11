/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"
#include "netc.h"

/**********************************************************************************/

#define NOTICE_CHANGE_ADD     0x01
#define NOTICE_CHANGE_CLEAR   0x02
#define NOTICE_CHANGE_REMOVE  0x04
#define NOTICE_CHANGE_GONE    0x80

/**********************************************************************************/


#ifdef KATCP_CONSISTENCY_CHECKS
static void sane_notice_katcp(struct katcp_notice *n)
{
  if(n == NULL){
    fprintf(stderr, "sane: notice is null\n");
    abort();
  }

  if(n->n_queue == NULL){
    fprintf(stderr, "sane: empty queue structure in notice\n");
    abort();
  }
}
#else
#define sane_notice_katcp(n)
#endif

/**********************************************************************************/

static void deallocate_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  if(n == NULL){
    return;
  }

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

  if(n->n_queue){
    destroy_queue_katcl(n->n_queue);
    n->n_queue = NULL;
  }

  if(n->n_parse){
    destroy_parse_katcl(n->n_parse);
    n->n_parse = NULL;
  }

#if 0
  n->n_position = (-1);
#endif

  n->n_changes = NOTICE_CHANGE_GONE;

  free(n);
}

static void reap_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  int i, k, check;
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

      for(k = i; k < s->s_pending; k++){
        s->s_notices[k] = s->s_notices[k + 1];
      }

#if 0
      if(i < s->s_pending){
        s->s_notices[i] = s->s_notices[s->s_pending];
      }
#endif
      
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

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "creating %s notice %s message", name ? name : "<anonymous>", p ? "with" : "without");

  s = d->d_shared;

  if(name){
    n = find_notice_katcp(d, name);
    if(n != NULL){
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "not creating notice %s as it already exists but returning a reference to it", n->n_name);

      if(p){
        if(add_tail_queue_katcl(n->n_queue, p) < 0){
          return NULL;
        }
        n->n_changes |= NOTICE_CHANGE_ADD;
      }

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

  n->n_name = NULL;
  n->n_trigger = KATCP_NOTICE_TRIGGER_OFF;

  n->n_tag = tag;
  n->n_use = 0;

  n->n_queue = NULL;
  n->n_parse = NULL;

#if 0
  n->n_position = (-1);
#endif

  n->n_changes = NOTICE_CHANGE_CLEAR;

#if 0
  n->n_msg = NULL;
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

  n->n_queue = create_queue_katcl();
  if(n->n_queue == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate queue for notice %s", name);
    deallocate_notice_katcp(d, n);
    return NULL;
  }

  t = realloc(s->s_notices, sizeof(struct katcp_notice *) * (s->s_pending + 1));
  if(t == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to insert notice into list");
    deallocate_notice_katcp(d, n);
    return NULL;
  }
  s->s_notices = t;

  /* has to be last - on failure, deallocate notice will destroy p */
  if(p){
    if(add_tail_queue_katcl(n->n_queue, p) < 0){
      deallocate_notice_katcp(d, n);
      return NULL;
    }
    n->n_changes |= NOTICE_CHANGE_ADD;
  }

  s->s_notices[s->s_pending] = n;
  s->s_pending++;

  return n;
}

struct katcp_notice *create_notice_katcp(struct katcp_dispatch *d, char *name, unsigned int tag)
{
  struct katcp_notice *n;

  /* WARNING: used to have a parse structure associated with it */

  n = create_parse_notice_katcp(d, name, tag, NULL);
  if(n == NULL){
    return NULL;
  }

  return n;
}

int has_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data)
{
  struct katcp_invoke *v;
  int i;

  for(i = 0; i < n->n_count; i++){
    v = &(n->n_vector[i]);

    if((v->v_call == call) && (v->v_data == data)){
      return 1;
    }
  }

  return 0;
}

unsigned int fetch_data_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void **vector, unsigned int size)
{
  struct katcp_invoke *v;
  int i;
  unsigned int min;

  min = (size < n->n_count) ? size : n->n_count;

  for(i = 0; i < min; i++){
    v = &(n->n_vector[i]);
    vector[i] = v->v_data;
  }

  return n->n_count;
}

int remove_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data)
{
  struct katcp_invoke *v;
  int i;

  for(i = 0; i < n->n_count; i++){
    v = &(n->n_vector[i]);

    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "comparing given data %p to entry[%d]=%p", data, i, v->v_data);

    if(v->v_data == data){
      if((call == NULL) || (v->v_call == call)){
        n->n_count--;

        dispatch_compact_notice_katcp(v->v_client, n);

        if(i < n->n_count){
          n->n_vector[i].v_client  = n->n_vector[n->n_count].v_client;
          n->n_vector[i].v_call    = n->n_vector[n->n_count].v_call;
          n->n_vector[i].v_data    = n->n_vector[n->n_count].v_data;
          n->n_vector[i].v_trigger = n->n_vector[n->n_count].v_trigger;
        }

        if(n->n_count == 0){
          free(n->n_vector);
          n->n_vector = NULL;
        }

        /* WARNING: require a return here, otherwise i will be increment while count decremented, skipping one invoke entry */

        return 0;
      } else {
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "found match for data %p but function call %p does not match", data, call);
      }
    }
  }

  log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "did not find match for data %p in vector of %u elements", data, n->n_count);

  return -1;
}

int add_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data)
{
  struct katcp_invoke *v;
  struct katcp_notice **tn;
  int i;

#ifdef DEBUG
  fprintf(stderr, "dispatch %p has %d notices, now adding notice %p having %d subscribers\n", d, d->d_count, n, n->n_count);
#endif

  for(i = 0; i < n->n_count; i++){
    v = &(n->n_vector[i]);

    if((v->v_call == call) && (v->v_data == data)){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "callback %p of notice %s is a duplicate", call, n->n_name ? n->n_name : "<anonymous>");
      return 0;
    }
  }

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
  v->v_trigger = 0;

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

struct katcp_notice *register_parse_notice_katcp(struct katcp_dispatch *d, char *name, struct katcl_parse *p, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data)
{
  struct katcp_notice *n;

  n = create_parse_notice_katcp(d, name, 0, p);
  if (n == NULL)
    return NULL;

  if (add_notice_katcp(d, n, call, data) < 0)
    return NULL;

  return n;
}
/*******************************************************************************/

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

int find_prefix_notices_katcp(struct katcp_dispatch *d, char *prefix, struct katcp_notice **n_set, int n_count)
{
  struct katcp_notice *n;
  struct katcp_shared *s;
  int len, i, found;

  if (prefix == NULL)
    return -1;

  s     = d->d_shared;
  len   = strlen(prefix);
  found = 0;

  for (i = 0; i < s->s_pending; i++){
    n = s->s_notices[i];
    if (n->n_name && (!strncmp(prefix, n->n_name, len))){
      if (found < n_count && n_set != NULL){
        n_set[found] = n;
      } 
      found++;    
    }
  }

  return found;
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

char *path_from_notice_katcp(struct katcp_notice *n, char *suffix, int flags)
{
  char *copy;
  int last, first, total;
  struct katcp_url *ku;

  if(suffix == NULL){
    return NULL;
  }

  last = strlen(suffix);

  /* absolute paths */
  if(suffix[0] == '.'){
    copy = malloc(last);
    if(copy == NULL){
      return NULL;
    }
    strcpy(copy, suffix + 1);
    return copy; 
  }

  /* anonymous notices */
  if(n->n_name == NULL){
    total = 9 + 18 + 1 + last + 1;

    copy = malloc(total);
    if(copy == NULL){
      return NULL;
    }

    snprintf(copy, total, "anonymous%p.%s", n, suffix);
    copy[total - 1] = '\0';

    return copy;
  }

  ku = create_kurl_from_string_katcp(n->n_name);
  /* notices which are not urls (should not happen, should be prohibited */
  if(ku == NULL){

    total = strlen(n->n_name) + 1 + last + 1;

    copy = malloc(total);
    if(copy == NULL){
      return NULL;
    }

    snprintf(copy, total, "%s.%s", n->n_name, suffix);
    copy[total - 1] = '\0';

    return NULL;
  }

  /* names from urls */
  if(ku->u_cmd){
    first = strlen(ku->u_cmd);
  } else {
    first = strlen(ku->u_host);
  }

  total = first + 7 + last + 1;

  copy = malloc(total);
  if(copy == NULL){
    destroy_kurl_katcp(ku);
    return NULL;
  }

  if(ku->u_cmd){
    snprintf(copy, total, "%s.%s", ku->u_cmd, suffix);
  } else {
    if(ku->u_port == NETC_DEFAULT_PORT){
      snprintf(copy, total, "%s.%s", ku->u_host, suffix);
    } else {
      snprintf(copy, total, "%s.%d.%s", ku->u_host, ku->u_port, suffix);
    }
  }

  copy[total - 1] = '\0';

#if 0
  ptr = strchr(copy, '#');
  if(ptr){
    ptr[0] = '\0';
  }
#endif

  destroy_kurl_katcp(ku);

  return copy;
}

/*******************************************************************************/

#if 0
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
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no back link from dispatch %p to notice %p", v->v_client, n);
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
#endif

/*******************************************************************************/

void hold_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  n->n_use++;
}

void release_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
 log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "releasing %p %s with use %d", n, n->n_name ? n->n_name : "<anonymous>", n->n_use);
  if(n->n_use > 0){
    n->n_use--;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "notice: releasing %p %s already at 0 refcount", n, n->n_name ? n->n_name : "<anonymous>");
  }
}

/******************************************************************************/

struct katcl_parse *get_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  sane_notice_katcp(n);

  if(n->n_parse){
    return n->n_parse;
  }

  return get_head_queue_katcl(n->n_queue);

#if 0
  if(n->n_position < 0){
#ifdef DEBUG
    fprintf(stderr, "warning: calling get_parse_notice outsite a notice callback. Results may be unpredictable\n");
#endif
    pos = 0;
  } else {
    pos = n->n_position;
  }

  return get_index_queue_katcl(n->n_queue, pos);
#endif
}

int set_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p)
{
  /* WARNING: does not deal with case where added item is the same as already there */
  int result;

  sane_notice_katcp(n);

  if(n->n_parse){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "notice: setting parse %p in notice callback for %p", p, n);
  } else {
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "notice: setting parse %p outside notice callback for %p", p, n);
  }

  clear_queue_katcl(n->n_queue);
  n->n_changes |= NOTICE_CHANGE_CLEAR;

  if(p == NULL){
    return 0;
  }

  result = add_tail_queue_katcl(n->n_queue, p);
  if(result == 0){
    n->n_changes |= NOTICE_CHANGE_ADD;
  }
  
  return result;
}

struct katcl_parse *remove_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  struct katcl_parse *p;

  sane_notice_katcp(n);

#if 1
  if(n->n_parse){
    n->n_changes |= NOTICE_CHANGE_REMOVE;
    p = n->n_parse;
    n->n_parse = NULL;
    return p;
  }
#endif

  p = remove_head_queue_katcl(n->n_queue);
  if(p){
    n->n_changes |= NOTICE_CHANGE_REMOVE;
  }

  return p;
}

int add_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p)
{
  int result;

  sane_notice_katcp(n);

  result = add_tail_queue_katcl(n->n_queue, p);

  if(result == 0){
    n->n_changes |= NOTICE_CHANGE_ADD;
  }

  return result;
}

/******************************************************************************/

static int configure_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, int trigger, void *data)
{
  int i;
#ifdef DEBUG
  int w;
#endif

#ifdef DEBUG
  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "notice: triggering %p %s", n, n->n_name ? n->n_name : "<anonymous>");
#endif

  switch(trigger){
    case KATCP_NOTICE_TRIGGER_SINGLE :
    case KATCP_NOTICE_TRIGGER_ALL :
      n->n_trigger = trigger;

      if(trigger == KATCP_NOTICE_TRIGGER_SINGLE){
#ifdef DEBUG
        w = 0;
#endif
        for(i = 0; i < n->n_count; i++){
          if(n->n_vector[i].v_data == data){
            n->n_vector[i].v_trigger = 1;
#ifdef DEBUG
            w++;
#endif
          }
        }
#ifdef DEBUG
        if(w){
          fprintf(stderr, "notice: attempted to wake single item (%p) which can not be found\n", data);
        }
#endif

        mark_busy_katcp(d);

      }

      return 0;
  }

  return -1;
}

int trigger_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n)
{
  return configure_notice_katcp(d, n, KATCP_NOTICE_TRIGGER_ALL, NULL);
}

int trigger_single_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  return configure_notice_katcp(d, n, KATCP_NOTICE_TRIGGER_SINGLE, data);
}

void wake_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p)
{
  set_parse_notice_katcp(d, n, p);
  configure_notice_katcp(d, n, KATCP_NOTICE_TRIGGER_ALL, NULL);
}

int wake_name_notice_katcp(struct katcp_dispatch *d, char *name, struct katcl_parse *p)
{
  struct katcp_notice *n;

  n = find_notice_katcp(d, name);
  if(n == NULL){
    return -1;
  }

  set_parse_notice_katcp(d, n, p);
  configure_notice_katcp(d, n, KATCP_NOTICE_TRIGGER_ALL, NULL);

  return 0;
}

void wake_single_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p, void *data)
{
  set_parse_notice_katcp(d, n, p);
  configure_notice_katcp(d, n, KATCP_NOTICE_TRIGGER_SINGLE, data);
}

int wake_single_name_notice_katcp(struct katcp_dispatch *d, char *name, struct katcl_parse *p, void *data)
{
  struct katcp_notice *n;

  n = find_notice_katcp(d, name);
  if(n == NULL){
    return -1;
  }

  set_parse_notice_katcp(d, n, p);
  configure_notice_katcp(d, n, KATCP_NOTICE_TRIGGER_SINGLE, data);

  return 0;
}

/******************************************************************************/

int rename_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, char *name)
{
  char *ptr;

  if(n == NULL){
    return -1;
  }

  if(name){
    ptr = strdup(name);
    if(ptr == NULL){
      return -1;
    }
  } else {
    ptr = NULL;
  }

  if(n->n_name){
    free(n->n_name);
  }

  n->n_name = ptr;

  return 0;
}

int change_name_notice_katcp(struct katcp_dispatch *d, char *name, char *newname)
{
  struct katcp_notice *n;

  n = find_notice_katcp(d,name);
  if(n == NULL){
    return -1;
  }

  if(rename_notice_katcp(d, n, newname) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not rename notice %p",n);
    return -1;
  }

  return 0;
}

/*******************************************************************************/

int run_notices_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_notice *n;
  struct katcp_invoke *v;
  int i, k, result, test, limit;

  s = d->d_shared;
  i = 0;

#ifdef DEBUG
  fprintf(stderr, "notice: running %d pending entries\n", s->s_pending);
#endif

  while(i < s->s_pending){
    n = s->s_notices[i];

    if(n->n_trigger != KATCP_NOTICE_TRIGGER_OFF){

      test = (n->n_trigger == KATCP_NOTICE_TRIGGER_ALL) ? 0 : 1;

#ifdef DEBUG
      fprintf(stderr, "notice: trigger[%d] (%s) with code %d\n", i, n->n_name ? n->n_name : "<anonymous>", test);
#endif

      n->n_trigger = KATCP_NOTICE_TRIGGER_OFF;

      if(n->n_changes == 0){
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "major logic problem: parse value left");
      }

      n->n_changes = 0;
      limit = size_queue_katcl(n->n_queue);

      if(n->n_parse){
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "major logic problem: parse value left");
        destroy_parse_katcl(n->n_parse);
        n->n_parse = NULL;
      }

#ifdef DEBUG
     fprintf(stderr, "notice: notice %p triggered with %d messages and %d subscribers\n", n, limit, n->n_count);
#endif

      /* WARNING: vague semantics ahead. What does it mean to only have woken a single call back which then clears the set of parse messages ?  */

      n->n_parse = remove_head_queue_katcl(n->n_queue);
      do{ /* WARNING: will run once only if triggered zero or more times but no parse has been set ...  */
        k = 0;
        while(k < n->n_count){
          v = &(n->n_vector[k]);
          if(v->v_trigger >= test){
            
            v->v_trigger = 0;
            result = (*(v->v_call))(v->v_client, n, v->v_data);

#ifdef DEBUG
            fprintf(stderr, "notice: notice %p callback[%d]=%p returns %d (parse=%p)\n", n, k, v->v_call, result, n->n_parse);
#endif

            if(result <= 0){
              dispatch_compact_notice_katcp(v->v_client, n);

              n->n_count--;
              if(k < n->n_count){
                n->n_vector[k].v_client  = n->n_vector[n->n_count].v_client;
                n->n_vector[k].v_call    = n->n_vector[n->n_count].v_call;
                n->n_vector[k].v_data    = n->n_vector[n->n_count].v_data;
                n->n_vector[k].v_trigger = n->n_vector[n->n_count].v_trigger;
              }
            } else {
              k++;
            }
          } else {
            k++;
          }
        }

        if(n->n_parse){
          destroy_parse_katcl(n->n_parse);
          n->n_parse = NULL;
        }

        /* make sure we let go of the cpu, even if callbacks schedule more stuff */
        limit--;
        if(limit > 0){
          n->n_parse = remove_head_queue_katcl(n->n_queue);
        } else {
          n->n_parse = NULL;
        }

      } while(n->n_parse != NULL);

    }

    /* move onto the next notice */

    if((n->n_count <= 0) && (n->n_use <= 0)){
      deallocate_notice_katcp(d, n);

      s->s_pending--;

      for(k = i; k < s->s_pending; k++){
        s->s_notices[k] = s->s_notices[k + 1];
      }
#if 0
      if(i < s->s_pending){
        s->s_notices[i] = s->s_notices[s->s_pending];
      }
#endif
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

int resume_notice(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  char *ptr;

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "resuming after waiting for notice %s", n->n_name ? n->n_name : "<anonymous>");

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
  int i, k, when;
  struct timeval tv;

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
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "notice %s at %p with %d subscribers and %d references", n->n_name ? n->n_name : "<anonymous>", n, n->n_count, n->n_use);
        for(k = 0; k < n->n_count; k++){
          log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "subscriber %d has callback %p with data %p and is %s", k, n->n_vector[k].v_call, n->n_vector[k].v_data, n->n_vector[k].v_trigger ? "triggered" : "waiting");
        }
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

        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "notice %s not found so creating it", value);

        n = create_notice_katcp(d, value, 0);
        if(n == NULL){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create notice %s", value);
          return KATCP_RESULT_FAIL;
        }
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

      if(argc > 3){
        when = arg_unsigned_long_katcp(d, 3);

        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "waking notice %s in %dms", value, when);

        tv.tv_sec = when / 1000;
        tv.tv_usec = (when % 1000) * 1000;
        wake_notice_in_tv_katcp(d, n, &tv);

      } else {
        wake_notice_katcp(d, n, NULL);
      }

      return KATCP_RESULT_OK;
    } else {
      return KATCP_RESULT_FAIL;
    }
  }
}
