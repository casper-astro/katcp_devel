#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sysexits.h>

#include <sys/stat.h>

#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>
#include <avltree.h>

#define WIT_MAGIC 0x120077e1

/* sorry, sensor was taken, now move onto wit ... */

#ifdef KATCP_CONSISTENCY_CHECKS
static void sane_wit(struct katcp_wit *w)
{
  if(w == NULL){
    fprintf(stderr, "sensor: null wit handle\n");
    abort();
  }

  if(w->w_magic != WIT_MAGIC){
    fprintf(stderr, "sensor: bad magic 0x%x in wit handle %p\n", w->w_magic, w);
    abort();
  }
}
#else
#define sane_wit(w)
#endif

/*************************************************************************/

int wake_endpoint_wit(struct katcp_dispatch *d, struct katcp_endpoint *ep, struct katcp_message *msg, void *data)
{
  struct katcp_wit *w;

  return -1;
}
  
void release_endpoint_wit(struct katcp_dispatch *d, void *data)
{
}

/*************************************************************************/

static void destroy_wit_katcp(struct katcp_dispatch *d, struct katcp_wit *w)
{
  struct katcp_subscribe *sub;
  unsigned int i;

  sane_wit(w);

  for(i = 0; i < w->w_size; i++){
    sub = w->w_vector[i];

    /* WARNING: TODO - send out device changed messages ? */
  }

  if(w->w_vector){
    free(w->w_vector);
  }
  w->w_size = 0;

  if(w->w_endpoint){
    release_endpoint_katcp(d, w->w_endpoint);
    w->w_endpoint = NULL;
  }

  w->w_magic = 0;

  free(w);
}

static struct katcp_wit *create_wit_katcp(struct katcp_dispatch *d)
{
  struct katcp_wit *w;

  w = malloc(sizeof(struct katcp_wit));
  if(w == NULL){
    return NULL;
  }

  w->w_magic = WIT_MAGIC;
  w->w_endpoint = NULL;
  w->w_vector = NULL;
  w->w_size = 0;

  w->w_endpoint = create_endpoint_katcp(d, &wake_endpoint_wit, &release_endpoint_wit, w);
  if(w->w_endpoint == NULL){
    destroy_wit_katcp(d, w);
    return NULL;
  }

  return w;
}

/*************************************************************************/

struct katcp_subscribe *create_subscribe_katcp(struct katcp_dispatch *d, struct katcp_wit *w, struct katcp_flat *fx)
{
  struct katcp_subscribe *sub, **re;
  struct katcp_flat *tx;

  sane_wit(w);

  if(fx == NULL){
    tx = this_flat_katcp(d);
    if(tx == NULL){
      return NULL;
    }
  } else {
    tx = fx;
  }

  re = realloc(w->w_vector, sizeof(struct katcp_subscribe *) * (w->w_size + 1));
  if(re == NULL){
    return NULL;
  }

  w->w_vector = re;

  sub = malloc(sizeof(struct katcp_subscribe));
  if(sub == NULL){
    return NULL;
  }

  sub->s_variable = NULL;
  sub->s_endpoint = NULL;

  sub->s_strategy = KATCP_STRATEGY_OFF;

  /*********************/

  reference_endpoint_katcp(d, tx->f_peer);
  sub->s_endpoint = tx->f_peer;

  w->w_vector[w->w_size] = sub;
  w->w_size++;

  return sub;
}

int find_subscribe_katcp(struct katcp_dispatch *d, struct katcp_wit *w, struct katcp_flat *fx)
{
  int i;
  struct katcp_subscribe *sub;

  sane_wit(w);

  for(i = 0; i < w->w_size; i++){
    sub = w->w_vector[i];
    if(sub->s_endpoint == fx->f_peer){
      return i;
    }
  }

  return -1;
}

int delete_subscribe_katcp(struct katcp_dispatch *d, struct katcp_wit *w, unsigned int index)
{
  struct katcp_subscribe *sub;

  sane_wit(w);

  if(index >= w->w_size){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "delete subscribe: major logic problem, removed subscribe out of range\n");
    abort();
#endif
    return -1;
  }

  sub = w->w_vector[index];

  w->w_size--;

  if(index < w->w_size){
    w->w_vector[index] = w->w_vector[w->w_size];
  }

  if(sub->s_endpoint){
    forget_endpoint_katcp(d, sub->s_endpoint);
    sub->s_endpoint = NULL;
  }

  if(sub->s_variable){
#if 1
    /* WARNING */
    fprintf(stderr, "incomplete code: variable shadow needs to be deallocated\n");
    abort();
#endif
  }

  sub->s_strategy = KATCP_STRATEGY_OFF;

  free(sub);

  return 0;
}

/*************************************************************************/

struct katcp_subscribe *locate_subscribe_katcp(struct katcp_dispatch *d, struct katcp_vrbl *vx, struct katcp_flat *fx)
{
  int index;
  struct katcp_wit *w;

  if(vx->v_extra == NULL){
    return NULL;
  }
 
  w = vx->v_extra;

  sane_wit(w);

  index = find_subscribe_katcp(d, w, fx);
  if(index < 0){
    return NULL;
  }

  return w->w_vector[index];
}

/*************************************************************************/

int broadcast_subscribe_katcp(struct katcp_dispatch *d, struct katcp_wit *w, struct katcl_parse *px)
{
  unsigned int i;
  struct katcp_subscribe *sub;

  sane_wit(w);

  for(i = 0; i < w->w_size; i++){
    sub = w->w_vector[i];

#ifdef KATCP_CONSISTENCY_CHECKS
    if(sub){
      fprintf(stderr, "major logic problem: null entry at %u in vector of subscribers\n", i);
      abort();
    }
#endif

    if(sub->s_strategy == KATCP_STRATEGY_EVENT){
      if(send_message_endpoint_katcp(d, w->w_endpoint, sub->s_endpoint, px, 0) < 0){
        /* other end could have gone away, notice it ... */
        delete_subscribe_katcp(d, w, i);
      }
#ifdef KATCP_CONSISTENCY_CHECKS
    } else {
      fprintf(stderr, "major logic problem: unimplemented sensor strategy %u\n", sub->s_strategy);
      abort();
#endif
    }

  }

  return w->w_size;
}

/*************************************************************************/

int change_sensor_katcp(struct katcp_dispatch *d, void *state, char *name, struct katcp_vrbl *vx)
{
  struct katcp_wit *w;
  struct katcl_parse *px;

  if(state == NULL){
    return -1;
  }

  w = state;

  sane_wit(w);

  px = create_referenced_parse_katcl();
  if(px == NULL){
    return -1;
  }

  add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_SENSOR_STATUS_INFORM);
#if KATCP_PROTOCOL_MAJOR_VERSION >= 5   
  add_timestamp_parse_katcl(px, 0, NULL);
#endif
  add_string_parse_katcl(px, KATCP_FLAG_STRING, "1");

  /* TODO: get name */
  add_string_parse_katcl(px, KATCP_FLAG_STRING, "unknown");




}

void release_sensor_katcp(struct katcp_dispatch *d, void *state, char *name, struct katcp_vrbl *vx)
{
  struct katcp_wit *w;

  if(state == NULL){
    return;
  }

  w = state;

  sane_wit(w);

  destroy_wit_katcp(d, w);
}


struct katcp_subscribe *attach_variable_katcp(struct katcp_dispatch *d, struct katcp_vrbl *vx, struct katcp_flat *fx)
{
  struct katcp_wit *w;
  struct katcp_subscribe *sub;

  if((vx->v_flags & KATCP_VRF_SEN) == 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "variable not declared as sensor, unwilling to monitor it");
    return NULL;
  }

  /* TODO - could be more eleborate function pointer checking */
  if(vx->v_extra == NULL){
    w = create_wit_katcp(d);
    if(w == NULL){
      return NULL;
    }

    if(configure_vrbl_katcp(d, vx, vx->v_flags, w, NULL, &change_sensor_katcp, &release_sensor_katcp) < 0){
      destroy_wit_katcp(d, w);
      return NULL;
    }
  }

  sub = create_subscribe_katcp(d, w, fx);
  if(sub == NULL){
    return NULL;
  }

  return sub;
}

int monitor_event_variable_katcp(struct katcp_dispatch *d, struct katcp_vrbl *vx, struct katcp_flat *fx)
{
  struct katcp_subscribe *sub;

  sub = locate_subscribe_katcp(d, vx, fx);
  if(sub == NULL){
    sub = attach_variable_katcp(d, vx, fx);
    if(sub == NULL){
      return -1;
    }
  }

  sub->s_strategy = KATCP_STRATEGY_EVENT;

  return 0;
}

#endif
