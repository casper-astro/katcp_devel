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

void destroy_subscribe_katcp(struct katcp_dispatch *d, struct katcp_subscribe *sub);

/*************************************************************************/

void release_endpoint_wit(struct katcp_dispatch *d, void *data)
{
  struct katcp_wit *w;

  w = data;

  sane_wit(w);

  w->w_endpoint = NULL;
}

/*************************************************************************/

static void destroy_wit_katcp(struct katcp_dispatch *d, struct katcp_wit *w)
{
  unsigned int i;

  sane_wit(w);

  /* WARNING: TODO - send out device changed messages ? */
  /* probably to everybody in group, not just subscribers ... */

  for(i = 0; i < w->w_size; i++){
    destroy_subscribe_katcp(d, w->w_vector[i]);

    w->w_vector[i] = NULL;
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

  w->w_endpoint = create_endpoint_katcp(d, NULL, &release_endpoint_wit, w);
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

void destroy_subscribe_katcp(struct katcp_dispatch *d, struct katcp_subscribe *sub)
{

  if(sub == NULL){
    return;
  }

  if(sub->s_endpoint){
#ifdef DEBUG
    fprintf(stderr, "subscribe: forgetting endpoint %p\n", sub->s_endpoint);
#endif
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

  destroy_subscribe_katcp(d, sub);

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

#ifdef DEBUG
  fprintf(stderr, "sensor: broadcasting sensor update to %u interested parties\n", w->w_size);
#endif

  for(i = 0; i < w->w_size; i++){
    sub = w->w_vector[i];

#ifdef KATCP_CONSISTENCY_CHECKS
    if(sub == NULL){
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

int is_vrbl_sensor_katcp(struct katcp_dispatch *d, struct katcp_vrbl *vx)
{
  struct katcp_vrbl_payload *py;

  if(vx == NULL){
    return -1;
  }

  if((vx->v_flags & KATCP_VRF_SEN) == 0){
    return 0;
  }

  py = find_payload_katcp(d, vx, ":value");
  if(py == NULL){
    return 0;
  }

  if(py->p_type >= KATCP_MAX_VRT){
    return -1;
  }

  return 1;
}

/*************************************************************************/

static char *sensor_strategy_table[KATCP_STRATEGIES_COUNT] = { "none", "period", "event", "differential", "forced" };

char *strategy_to_string_sensor_katcp(struct katcp_dispatch *d, unsigned int strategy)
{
  if(strategy >= KATCP_STRATEGIES_COUNT){
    return NULL;
  }

  return sensor_strategy_table[strategy];
}

int strategy_from_string_sensor_katcp(struct katcp_dispatch *d, char *name)
{
  int i;

  if(name == NULL){
    return -1;
  }

  for(i = 0; i < KATCP_STRATEGIES_COUNT; i++){
    if(!strcmp(name, sensor_strategy_table[i])){
      return i;
    }
  }

  if(!strcmp(name, "auto")){
    return KATCP_STRATEGY_EVENT;
  }

  return -1;
}

/*************************************************************************/

int add_partial_sensor_katcp(struct katcp_dispatch *d, struct katcl_parse *px, char *name, int flags, struct katcp_vrbl *vx)
{
  int results[5];
  struct katcp_vrbl_payload *py;
  unsigned int r;
  int i, sum;
  char *ptr;

  if((px == NULL) || (vx == NULL)){
    return -1;
  }

  if(name){
    ptr = name;
  } else if(vx->v_name){
    ptr = vx->v_name;
  } else {
    ptr = "unnamed";
  }

  r = 0;

#if KATCP_PROTOCOL_MAJOR_VERSION >= 5   
  results[r++] = add_timestamp_parse_katcl(px, flags & KATCP_FLAG_FIRST, NULL);
  results[r++] = add_string_parse_katcl(px, KATCP_FLAG_STRING, "1");
#else
  results[r++] = add_string_parse_katcl(px, (flags & KATCP_FLAG_FIRST) | KATCP_FLAG_STRING, "1");
#endif

  results[r++] = add_string_parse_katcl(px, KATCP_FLAG_STRING, ptr);

  py = find_payload_katcp(d, vx, ":status");
  if(py){
    results[r++] = add_payload_vrbl_katcp(d, px, 0, vx, py);
  } else {
    results[r++] = add_string_parse_katcl(px, KATCP_FLAG_STRING, "unknown");
  }

  py = find_payload_katcp(d, vx, ":value");
  if(py){
    results[r++] = add_payload_vrbl_katcp(d, px, flags & KATCP_FLAG_LAST, vx, py);
  } else {
    results[r++] = add_string_parse_katcl(px, KATCP_FLAG_STRING | (flags & KATCP_FLAG_LAST), "unset");
  }

  sum = 0;
  for(i = 0; i < r; i++){
    if(results[i] < 0){
      sum = (-1);
      i = r;
    } else {
      sum += results[i];
    }
  }

  if(sum <= 0){
    return (-1);
  }

  return sum;
}

struct katcl_parse *make_sensor_katcp(struct katcp_dispatch *d, char *name, struct katcp_vrbl *vx, char *prefix)
{
  struct katcl_parse *px;

#ifdef DEBUG
  fprintf(stderr, "sensor: triggering sensor change logic on sensor %s\n", name);
#endif

  px = create_referenced_parse_katcl();
  if(px == NULL){
    return NULL;
  }

  if(add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, prefix) < 0){
    destroy_parse_katcl(px);
    return NULL;
  }

  if(add_partial_sensor_katcp(d, px, name, KATCP_FLAG_LAST, vx) < 0){
    destroy_parse_katcl(px);
    return NULL;
  }

  return px;
}

int change_sensor_katcp(struct katcp_dispatch *d, void *state, char *name, struct katcp_vrbl *vx)
{
  struct katcp_wit *w;
  struct katcl_parse *px;

  w = state;
  sane_wit(w);

#ifdef DEBUG
  fprintf(stderr, "sensor: triggering sensor change logic on sensor %s\n", name);
#endif

  px = make_sensor_katcp(d, name, vx, KATCP_SENSOR_STATUS_INFORM);
  if(px == NULL){
    return -1;
  }

  broadcast_subscribe_katcp(d, w, px);

  destroy_parse_katcl(px);

  return 0;
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

  /* TODO - could be more eleborate function pointer checking, in particular does it have a :value field */

  if(vx->v_extra == NULL){
    w = create_wit_katcp(d);
    if(w == NULL){
      return NULL;
    }

    if(configure_vrbl_katcp(d, vx, vx->v_flags, w, NULL, &change_sensor_katcp, &release_sensor_katcp) < 0){
      destroy_wit_katcp(d, w);
      return NULL;
    }
  } else {
    w = vx->v_extra;
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
  struct katcl_parse *px;

  px = make_sensor_katcp(d, NULL, vx, KATCP_SENSOR_STATUS_INFORM);
  if(px == NULL){
    return -1;
  }

  sub = locate_subscribe_katcp(d, vx, fx);
  if(sub == NULL){
    sub = attach_variable_katcp(d, vx, fx);
    if(sub == NULL){
      destroy_parse_katcl(px);
      return -1;
    }
  }

  sub->s_strategy = KATCP_STRATEGY_EVENT;

  append_parse_katcp(d, px);
  destroy_parse_katcl(px);

  return 0;
}

#endif
