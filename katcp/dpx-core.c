#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sysexits.h>

#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <netc.h>
#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

#if 0
#define MAP_UNSET               (-1)
#define MAP_INNER                 0
#define MAP_REMOTE                1
#endif

#if 0
#define KATCP_MAP_FLAG_REQUEST  0x10
#define KATCP_MAP_FLAG_INFORM   0x20
#define KATCP_MAP_FLAG_REPLY    0x40  /* should not be used */
#endif

#define FLAT_MAGIC 0x49021faf

#define FLAT_STATE_GONE          0
#define FLAT_STATE_CONNECTING    1
#define FLAT_STATE_UP            2
#define FLAT_STATE_FINISHING     3
#define FLAT_STATE_ENDING        4
#define FLAT_STATE_CRASHING      5
#define FLAT_STATE_DEAD          6

#if 0
#define FLAT_STOP_IO             0
#define FLAT_STOP_PROPER         1
#define FLAT_STOP_RUSH           2
#endif


/*******************************************************************************/

/********************************************************************/

int setup_default_group(struct katcp_dispatch *d, char *name);

void destroy_cmd_map(struct katcp_cmd_map *m);

static void clear_current_flat(struct katcp_dispatch *d);
static void set_current_flat(struct katcp_dispatch *d, struct katcp_flat *fx);

#if 0
static int actually_set_output_flat_katcp(struct katcp_flat *fx, unsigned int destination, unsigned int persistence);
#endif

static void deallocate_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *f);

/********************************************************************/

/* groups of things *************************************************/

void destroy_group_katcp(struct katcp_dispatch *d, struct katcp_group *g)
{
  struct katcp_shared *s;
  unsigned int i;

  s = d->d_shared;

  if(g == NULL){
    return;
  }

  if(g->g_use > 0){
    g->g_use--;
    return;
  }

  if(g->g_count > 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "group destruction: group still in use (%u elements)\n", g->g_count);
    abort();
#endif
    return; 
  }

  if(g->g_name){
    free(g->g_name);
    g->g_name = NULL;
  }

  for(i = 0; i < KATCP_SIZE_MAP; i++){
    destroy_cmd_map(g->g_maps[i]);
    g->g_maps[i] = NULL;
  }

  g->g_log_level = (-1);
  g->g_scope = KATCP_SCOPE_INVALID;

  if(g->g_flats){
    free(g->g_flats);
    g->g_flats = NULL;
  }

  for(i = 0; (i < s->s_members) && (g != s->s_groups[i]); i++);

  s->s_members--;
  if(i < s->s_members){
    s->s_groups[i] = s->s_groups[s->s_members];
  } else {
    if(i > s->s_members){
      s->s_members++;
    }
  }

  free(g);
}

void destroy_groups_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  s = d->d_shared;

  if(s->s_fallback){
    destroy_group_katcp(d, s->s_fallback);
    s->s_fallback = NULL;
  }

  while(s->s_members){
    destroy_group_katcp(d, s->s_groups[0]);
  }

  if(s->s_groups){
    free(s->s_groups);
    s->s_groups = NULL;
  }
}

struct katcp_group *create_group_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_group *g, **tmp;
  struct katcp_shared *s;
  unsigned int i;

  s = d->d_shared;

  g = malloc(sizeof(struct katcp_group));
  if(g == NULL){
    return NULL;
  }

  g->g_name = NULL;

  for(i = 0; i < KATCP_SIZE_MAP; i++){
    g->g_maps[i] = NULL;
  }

  g->g_flats = NULL;
  g->g_count = 0;

  g->g_log_level = s->s_default;
  g->g_scope = KATCP_SCOPE_GROUP;

  g->g_use = 0;

  tmp = realloc(s->s_groups, sizeof(struct katcp_group *) * (s->s_members + 1));
  if(tmp == NULL){
    destroy_group_katcp(d, g);
    return NULL;
  }

  s->s_groups = tmp;
  s->s_groups[s->s_members] = g;
  s->s_members++;

  /* s_fallback not set here, set up in default_ */

  if(name){
    g->g_name = strdup(name);
    if(g->g_name == NULL){
      destroy_group_katcp(d, g);
      return NULL;
    }
  }

  return g;
}

int hold_group_katcp(struct katcp_group *g)
{
  if(g == NULL){
    return -1;
  }

  return g->g_use++;
}

struct katcp_group *this_group_katcp(struct katcp_dispatch *d)
{
  /* WARNING: there may be cases when we are not in the context of a duplex/flat, but still within a group - this doesn't cater for such a case */
  struct katcp_shared *s;
  struct katcp_flat *f;

  s = d->d_shared;
  if(s == NULL){
    return NULL;
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "duplex: stack is %u", s->s_level);

  if(s->s_level < 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "flat: level is %d, negative\n", s->s_level);
#endif
    return NULL;
  }

  f = s->s_this[s->s_level];
#ifdef KATCP_CONSISTENCY_CHECKS
  if(f == NULL){
    fprintf(stderr, "flat: logic problem - should not encounter a null entry\n");
    abort();
    return NULL;
  }
#endif

  return f->f_group;
}

struct katcp_group *find_group_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_shared *s;
  struct katcp_group *gx;
  unsigned int i;

  s = d->d_shared;
  if(s == NULL){
    return NULL;
  }

  if(name == NULL){
    /* WARNING: this could also be an error - helps detect usage problems */
    return s->s_fallback;
  }

  for(i = 0; i < s->s_members; i++){
    gx = s->s_groups[i];
    if(gx && gx->g_name){
      if(!strcmp(gx->g_name, name)){
        return gx;
      }
    }
  }

  return NULL;
}

/* cmd handling *****************************************************/

void destroy_cmd_item(struct katcp_cmd_item *i)
{
  if(i == NULL){
    return;
  }

  if(i->i_name){
    free(i->i_name);
    i->i_name = NULL;
  }

  if(i->i_help){
    free(i->i_help);
    i->i_help = NULL;
  }

  i->i_call = NULL;
  i->i_flags = 0;

  if(i->i_clear){
    (*(i->i_clear))(i->i_data);
    i->i_clear = NULL;
  }

  i->i_data = NULL;

  free(i);
}

void destroy_cmd_item_void(void *v)
{
  struct katcp_cmd_item *i;

  i = v;

  destroy_cmd_item(i);
}

struct katcp_cmd_item *create_cmd_item(char *name, char *help, unsigned int flags, int (*call)(struct katcp_dispatch *d, int argc), void *data, void (*clear)(void *data)){
  struct katcp_cmd_item *i;

  i = malloc(sizeof(struct katcp_cmd_item));
  if(i == NULL){
    return NULL;
  }

  i->i_name = NULL;
  i->i_help = NULL;

  i->i_flags = 0;

  i->i_call = NULL;
  i->i_data = NULL;
  i->i_clear = NULL;

  if(name){
    i->i_name = strdup(name);
    if(i->i_name == NULL){
      destroy_cmd_item(i);
      return NULL;
    }
  }

  if(help){
    i->i_help = strdup(help);
    if(i->i_help == NULL){
      destroy_cmd_item(i);
      return NULL;
    }
  }

  i->i_flags = flags;

  i->i_call = call;
  i->i_data = data;
  i->i_clear = clear;

  return i;
}

void destroy_cmd_map(struct katcp_cmd_map *m)
{
  if(m == NULL){
    return;
  }

  if(m->m_refs > 0){
    m->m_refs--;
#ifdef DEBUG
    fprintf(stderr, "map %p now decremented to %d\n", m, m->m_refs);
#endif
  }

  if(m->m_refs > 0){
    return;
  }

  if(m->m_tree){
    destroy_avltree(m->m_tree, &destroy_cmd_item_void);
    m->m_tree = NULL;
  }

  if(m->m_fallback){
    destroy_cmd_item(m->m_fallback);
    m->m_fallback = NULL;
  }

  if(m->m_name){
    free(m->m_name);
    m->m_name = NULL;
  }

  free(m);
}

void hold_cmd_map(struct katcp_cmd_map *m)
{
  m->m_refs++;
#ifdef DEBUG
  fprintf(stderr, "map %p now incremented to %d\n", m, m->m_refs);
#endif
}

struct katcp_cmd_map *create_cmd_map(char *name)
{
  struct katcp_cmd_map *m;

  m = malloc(sizeof(struct katcp_cmd_map));
  if(m == NULL){
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "created map %p\n", m);
#endif

  m->m_name = NULL;
  m->m_refs = 0;
  m->m_tree = NULL;
  m->m_fallback = NULL;

  m->m_tree = create_avltree();
  if(m->m_tree == NULL){
    destroy_cmd_map(m);
    return NULL;
  }

  if(name){
    m->m_name = strdup(name);
    if(m->m_name == NULL){
      destroy_cmd_map(m);
      return NULL;
    }
  }

  return m;
}

int add_full_cmd_map_katcp(struct katcp_cmd_map *m, char *name, char *help, unsigned int flags, int (*call)(struct katcp_dispatch *d, int argc), void *data, void (*clear)(void *data))
{
  struct katcp_cmd_item *i;
  struct avl_node *n;
  char *ptr;

  if(name == NULL){
    return -1;
  }

  switch(name[0]){
    case KATCP_REQUEST : 
    case KATCP_REPLY :
    case KATCP_INFORM : 
      ptr = name + 1;
      break;
    default: 
      ptr = name;
      break;
  }

  if(ptr[0] == '\0'){
    return -1;
  }

  i = create_cmd_item(ptr, help, flags, call, data, clear);
  if(i == NULL){
    return -1;
  }

  n = create_node_avltree(ptr, i);
  if(n == NULL){
    destroy_cmd_item(i);
    return -1;
  }
  
  if(add_node_avltree(m->m_tree, n) < 0){
    /* WARNING: the convention requires a caller to clean up if something fails, hence can't invoke the clear function yet */
    i->i_clear = NULL;
    free_node_avltree(n, &destroy_cmd_item_void);
    return -1;
  }

  return 0;
}

int add_cmd_map_katcp(struct katcp_cmd_map *m, char *name, char *help, int (*call)(struct katcp_dispatch *d, int argc))
{
  return add_full_cmd_map_katcp(m, name, help, 0, call, NULL, NULL);
}

#if 0
struct katcp_cmd_item *locate_cmd_item(struct katcp_flat *f, struct katcl_parse *p)
{
  struct katcp_cmd_item *i;
  struct katcp_cmd_map *m;
  char *str;

  str = get_string_parse_katcl(p, 0);
  if(str == NULL){
    return NULL;
  }

  if(str[0] == '\0'){
    return NULL;
  }

#if 1
  /* map not initialised */
  return NULL;
#endif

  i = find_data_avltree(m->m_tree, str + 1);
  if(i){
    return i;
  }

  return m->m_fallback;
}
#endif

/* duplex setup *****************************************************/

#ifdef KATCP_CONSISTENCY_CHECKS
static void sane_flat_katcp(struct katcp_flat *f)
{
  if(f == NULL){
    fprintf(stderr, "flat: received null pointer, expecting a flat structure\n");
    abort();
  }
  if(f->f_magic != FLAT_MAGIC){
    fprintf(stderr, "flat[%p]: bad magic 0x%x, expected 0x%x\n", f, f->f_magic, FLAT_MAGIC);
    abort();
  }
}
#else 
#define sane_flat_katcp(f);
#endif

int init_flats_katcp(struct katcp_dispatch *d, unsigned int stories)
{
  struct katcp_shared *s;
  
  if(d == NULL){
    return -1;
  }

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(s->s_this){
    fprintf(stderr, "flat: initialising stack which already has a value\n");
    abort();
  }
#endif

  s->s_stories = 0;
  s->s_level = (-1);

  s->s_this = malloc(sizeof(struct katcp_flat *) * stories);
  if(s->s_this == NULL){
    return -1;
  }

  s->s_stories = stories;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(s->s_fallback){
    fprintf(stderr, "flat: fallback group already initialised\n");
    abort();
  }

#endif

  if(setup_default_group(d, "default") < 0){
    /* TODO: should actually delete s_this, need a proper shutdown function */
    return -1;
  }

  return 0;
}

void destroy_flats_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  unsigned int i, j;
  struct katcp_flat *fx;
  struct katcp_group *gx;

  if(d == NULL){
    return;
  }

  s = d->d_shared;
  if(s == NULL){
    return;
  }

  for(j = 0; j < s->s_members; j++){
    gx = s->s_groups[j];
    i = 0;
    for(i = 0; i < gx->g_count; i++){
      fx = gx->g_flats[i];
      gx->g_flats[i] = NULL;
      deallocate_flat_katcp(d, fx);
    }
    gx->g_count = 0;
  }

  if(s->s_this){
    free(s->s_this);
    s->s_this = NULL;
  }

  s->s_level = (-1);
  s->s_stories = 0;
}

/********************************************************************/

static void deallocate_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *f)
{
  unsigned int i;
  struct katcp_response_handler *rh;

  sane_flat_katcp(f);

  /* TODO: make destruction an event ? */


#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: deallocating with endpoints: peer=%p, remote=%p\n", f, f->f_peer, f->f_remote);
#endif

  if(f->f_peer){
    /* WARNING: make sure we don't recurse on cleanup, invoking release callback */
    release_endpoint_katcp(d, f->f_peer);
    f->f_peer = NULL;
  }

  if(f->f_remote){
    /* WARNING: make sure we don't recurse on cleanup, invoking release callback */
    release_endpoint_katcp(d, f->f_remote);
    f->f_remote = NULL;
  }

  if(f->f_name){
    free(f->f_name);
    f->f_name = NULL;
  }

  if(f->f_line){
    destroy_katcl(f->f_line, 1);
    f->f_line = NULL;
  }

  /* TODO: will probably have to decouple ourselves here */
  f->f_shared = NULL;

#if 0
  if(f->f_backlog){
    destroy_queue_katcl(f->f_backlog);
    f->f_backlog = NULL;
  }
#endif

  if(f->f_orx){
#if 0
    /* destroy currently not needed, operate on assumption that it is transient ? */
    destroy_parse_katcl(f->f_orx);
#endif
    f->f_orx = NULL;
  }

  if(f->f_rx){
#if 0
    /* destroy currently not needed, operate on assumption that it is transient ? */
    destroy_parse_katcl(f->f_rx);
#endif
    f->f_rx = NULL;
  }

  if(f->f_tx){
    destroy_parse_katcl(f->f_tx);
    f->f_tx = NULL;
  }

  f->f_cmd = NULL;

  for(i = 0; i < KATCP_SIZE_REPLY; i++){
    rh = &(f->f_replies[i]);

    if(rh->r_message){
      free(rh->r_message);
      rh->r_message = NULL;
    }
    rh->r_flags = 0;
    rh->r_reply = NULL;

    if(rh->r_issuer){
      forget_endpoint_katcp(d, rh->r_issuer);
      rh->r_issuer = NULL;
    }

    if(rh->r_recipient){
      forget_endpoint_katcp(d, rh->r_recipient);
      rh->r_recipient = NULL;
    }

    if(rh->r_initial){
      destroy_parse_katcl(rh->r_initial);
      rh->r_initial = NULL;
    }
  }

  f->f_current_map = KATCP_MAP_UNSET;
  f->f_current_endpoint = NULL;

  for(i = 0; i < KATCP_SIZE_MAP; i++){
    destroy_cmd_map(f->f_maps[i]);
    f->f_maps[i] = NULL;
  }

  f->f_group = NULL;

  f->f_log_level = (-1);
  f->f_scope = KATCP_SCOPE_INVALID;

  f->f_magic = 0;


  free(f);
}

static void destroy_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *f)
{
  struct katcp_group *gx;
  unsigned int i;

  sane_flat_katcp(f);

  /* TODO: make destruction an event ? */

  gx = f->f_group;

  if((gx == NULL) || (gx->g_count == 0)){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "dpx: major logic problem: malformed or empty group at %p\n", gx);
    abort();
#endif
  } else {

    for(i = 0; (i < gx->g_count) && (gx->g_flats[i] != f); i++);

    gx->g_count--;

    if(i < gx->g_count){
      gx->g_flats[i] = gx->g_flats[gx->g_count];
    } else {
      if(i > gx->g_count){
        /* undo, we are in not found case */
        gx->g_count++;
      }
    }
  }

  deallocate_flat_katcp(d, f);
}

static void cancel_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx)
{
  unsigned int i;
  struct katcp_response_handler *rh;

  /* WARNING: unclear if we need trigger the r_reply. Might be needed to clean up
   * stuff in flight. Thusfar we only dereference the issuser 
   */


  for(i = 0; i < KATCP_SIZE_REPLY; i++){
    rh = &(fx->f_replies[i]);

    if(rh->r_message){
      free(rh->r_message);
      rh->r_message = NULL;
    }

    rh->r_flags = 0;
    rh->r_reply = NULL;

    if(rh->r_issuer){
      forget_endpoint_katcp(d, rh->r_issuer);
      rh->r_issuer = NULL;
    }

    if(rh->r_recipient){
      forget_endpoint_katcp(d, rh->r_recipient);
      rh->r_recipient = NULL;
    }

    if(rh->r_initial){
      destroy_parse_katcl(rh->r_initial);
      rh->r_initial = NULL;
    }

  }

}

/* callbacks ********************************************************/

int process_outstanding_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, int argc, struct katcp_response_handler *rh, unsigned int type)
{
  struct katcl_parse *px;
  int status, result;

  /* TODO: propagate KATCP_RESULT_* codes properly */

#ifdef KATCP_CONSISTENCY_CHECKS
  if(rh == NULL){
    fprintf(stderr, "dpx: handler invoked with no handle\n");
    abort();
  }

  if(type == KATCP_REQUEST){
    fprintf(stderr, "dpx: response handler invoked on a request\n");
    abort();
  }
#endif

  result = (*(rh->r_reply))(d, argc);
  status = 0;

#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: <response handler %p> returns %d\n", fx, rh->r_reply, result);
#endif

  switch(result){
    case KATCP_RESULT_FAIL :
    case KATCP_RESULT_INVALID :
    case KATCP_RESULT_OK :

      if(type == KATCP_REPLY){
        if(rh->r_initial){
          px = turnaround_extra_parse_katcl(rh->r_initial, result, NULL);
          rh->r_initial = NULL;
          if(px == NULL){
            return -1;
          }

          status = send_message_endpoint_katcp(d, fx->f_peer, rh->r_issuer, px, 0);
          destroy_parse_katcl(px);
          px = NULL;
        }
      }
      /* WARNING: fall */

    case KATCP_RESULT_OWN :

      if(type == KATCP_REPLY){
        if(rh->r_issuer){
          forget_endpoint_katcp(d, rh->r_issuer);
          rh->r_issuer = NULL;
        }

        if(rh->r_recipient){
          forget_endpoint_katcp(d, rh->r_recipient);
          rh->r_recipient = NULL;
        }

        if(rh->r_initial){
          destroy_parse_katcl(rh->r_initial);
          rh->r_initial = NULL;
        }

        rh->r_reply = NULL;
      }
      return status;

    case KATCP_RESULT_PAUSE :
    case KATCP_RESULT_YIELD :
      /* WARNING: breaks illusion of being able to "resume" things */
      /* could be fixed by passing back return codes ... */
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "pause or yield for %s not supported while responding", fx->f_name);
      return -1;

    default : 
      return -1;
  }

  return 0;
}

int process_map_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, int argc, char *str, int prior)
{
  struct katcp_cmd_map *mx;
  struct katcp_cmd_item *ix;

  int result, type;

  type = str[0];

  mx = map_of_flat_katcp(fx);
  if(mx == NULL){
    if(type == KATCP_REQUEST){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "task %s not configured to process requests from %s stream", fx->f_name, (fx->f_current_endpoint == fx->f_remote) ? "remote" : "internal");
      return KATCP_RESULT_FAIL;
    } else {
      return KATCP_RESULT_OWN;
    }
  }

#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: attempting to match %s to %s tree\n", fx, str + 1, (fx->f_current_endpoint == fx->f_remote) ? "remote" : "internal");
#endif

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "attempting to match %s to %s tree", str + 1, (fx->f_current_endpoint == fx->f_remote) ? "remote" : "internal");

  ix = find_data_avltree(mx->m_tree, str + 1);
  if((ix == NULL) || (ix->i_call == NULL)){
    if(type == KATCP_REQUEST){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to handle request %s", str + 1);
      return KATCP_RESULT_FAIL;
    } else {
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "not registered for inform %s", str + 1);
      return KATCP_RESULT_OWN;
    }
  }

  if(prior && ((ix->i_flags & KATCP_MAP_FLAG_GREEDY) == 0x0)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "skipping handler for message %s - already processed using reply", str);
    return KATCP_RESULT_OWN;
  }

#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: about to invoke <request handler %p> (matching %s)\n", fx, ix->i_call, str + 1);
#endif

  fx->f_cmd = ix;

  result = (*(ix->i_call))(d, argc);

  fx->f_cmd = NULL;

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "%s callback invocation returns %d", (fx->f_current_endpoint == fx->f_remote) ? "remote" : "internal", result);

  return result;
}

#if 0
int process_parse_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, struct katcp_response_handler *rh)
{
  /* most of the "environment" for callback already set up */

  int result, type, overridden, argc;
  struct katcp_cmd_map *mx;
  struct katcp_cmd_item *ix;
  char *str;

  sane_flat_katcp(fx);

  argc = get_count_parse_katcl(fx->f_rx);

  str = get_string_parse_katcl(fx->f_rx, 0);
  if(str == NULL){
    return KATCP_RESULT_FAIL;
  }

  type = str[0];

  if(type == KATCP_REQUEST){
    result = KATCP_RESULT_FAIL; /* assume the worst */
  } else {
    result = KATCP_RESULT_OWN;  /* informs and replies don't generate replies of their own */
  }

  overridden = 0;

  if((type == KATCP_REPLY) || (type == KATCP_INFORM)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "got %s reply or inform (%s), checking how to process it (rh=%p)", (fx->f_current_endpoint == fx->f_remote) ? "remote" : "internal", str, rh);
    if(rh){
      if(strcmp(rh->r_message, str + 1)){
        if(type == KATCP_REPLY){
          log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "received unexpected response %s, was expecting %s", str + 1, rh->r_message);
        } 
      } else {
        result = (*(rh->r_reply))(d, argc);
        /* with a bit of care this switch could go ... */
        switch(result){
          case KATCP_RESULT_FAIL :
          case KATCP_RESULT_INVALID :
          case KATCP_RESULT_OK :
            result = KATCP_RESULT_OWN;
            break;
          case KATCP_RESULT_PAUSE :
            /* WARNING: still unclear what a pause does to the message queue and endpoint, noting that it normally gets forgotten */
          case KATCP_RESULT_YIELD :
          case KATCP_RESULT_OWN :
            break;
        }
        /* WARNING: unclear if the return code makes sense here */
        overridden = 1;
        if(type == KATCP_REPLY){
          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "response handler got desired reply %s, now clearing it", str);
          /* forget reply handler once we see a response */
          if(rh->r_issuer){
            forget_endpoint_katcp(d, rh->r_issuer);
            rh->r_issuer = NULL;
          }
          if(rh->r_recipient){
            forget_endpoint_katcp(d, rh->r_recipient);
            rh->r_recipient = NULL;
          }
          if(rh->r_initial){
            destroy_parse_katcl(rh->r_initial);
            rh->r_initial = NULL;
          }
          rh->r_reply = NULL;
        }
      }
    } else {
      if(type == KATCP_REPLY){
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "no callback registered by %s to handle %s reply %s", fx->f_name, (fx->f_current_endpoint != fx->f_remote) ? "internal" : "remote", str);
      }
    }
  }

  if((type == KATCP_REQUEST) || (type == KATCP_INFORM)){
    mx = map_of_flat_katcp(fx);
    if(mx){

      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "attempting to match %s to %s tree", str + 1, (fx->f_current_endpoint == fx->f_remote) ? "remote" : "internal");

      ix = find_data_avltree(mx->m_tree, str + 1);
      if(ix && ix->i_call){
        if((overridden == 0) || (ix->i_flags & KATCP_MAP_FLAG_GREEDY)){

          result = (*(ix->i_call))(d, argc);

          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "%s callback invocation returns %d", (fx->f_current_endpoint == fx->f_remote) ? "remote" : "internal", result);

#if 0
          switch(result){
            case KATCP_RESULT_FAIL :
            case KATCP_RESULT_INVALID :
            case KATCP_RESULT_OK :
              break;
            case KATCP_RESULT_PAUSE :
              wantsreply = 0;
              /* TODO: update state */
              break;
            case KATCP_RESULT_YIELD :
              wantsreply = 0;
              break;
            case KATCP_RESULT_OWN :
              wantsreply = 0;
              break;
          }
#endif

        } else {
          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "skipping handler for message %s - already processed using reply", str);
        }

      } else {
         log_message_katcp(d, (type == KATCP_REQUEST) ? KATCP_LEVEL_INFO : KATCP_LEVEL_DEBUG, NULL, "no match for %s found", str + 1);
      }
    } else {
      if(type == KATCP_REQUEST){
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "task %s not configured to process requests from %s stream", fx->f_name, (fx->f_current_endpoint == fx->f_remote) ? "remote" : "internal");
      }
    }

  }

#if 0
  if(wantsreply){
    extra_response_katcl(fx->f_line, result, NULL);
    /* WARNING: we handle replies here, tell endpoint callback not to duplicate work */
    result = KATCP_RESULT_OWN;
  }
#endif

  return result;
}
#endif

static struct katcp_response_handler *find_handler_peer_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, struct katcp_endpoint *from, char *string)
{
  unsigned int i;
  struct katcp_response_handler *rh;

  for(i = 0; i < KATCP_SIZE_REPLY; i++){
    rh = &(fx->f_replies[i]);
    if(rh->r_reply && rh->r_message && (rh->r_recipient == from) && (strcmp(rh->r_message, string + 1) == 0)){
#ifdef DEBUG
      fprintf(stderr, "dpx[%p]: found candidate callback[%u]=%p: match for %s\n", fx, i, rh->r_reply, rh->r_message);
#endif
      return rh;
    } 
  }

  return NULL;
}

int wake_endpoint_peer_flat_katcp(struct katcp_dispatch *d, struct katcp_endpoint *ep, struct katcp_message *msg, void *data)
{
  /* might end up setting different wake functions - some flats might not field requests */
  struct katcp_flat *fx;
  struct katcp_endpoint *source;
  struct katcp_response_handler *rh;
  int result, type, argc;
  char *str;

  fx = data;
  sane_flat_katcp(fx);

#ifdef KATCP_CONSISTENCY_CHECKS
  if(fx->f_rx){
    fprintf(stderr, "logic problem: encountered set receive parse while processing endpoint\n");
    abort();
  }
  if(fx->f_current_endpoint){
    fprintf(stderr, "logic problem: encountered set current endpoint\n");
    abort();
  }
#endif

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "task %p (%s) received message", fx, fx->f_name);
#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: received message %p\n", fx, msg);
#endif

  switch(fx->f_state){
    case FLAT_STATE_UP : 
    case FLAT_STATE_FINISHING : 
    case FLAT_STATE_ENDING : 
      /* all ok, go on */
      break;
    case FLAT_STATE_CONNECTING :
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "duplex: warning: message arrived while still connecting\n");
#endif

      /* TODO: a KATCP_RESULT_PAUSE could be appropriate,
       * noting that a pause doesn't stall things completely
       * and we would have to restart the endpoint once the connection
       * comes up. For the time being fail messages arriving too soon
       */

      return KATCP_RESULT_FAIL;
    default :
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "duplex: possible problem: peer endpoint run while in state %u, not an operational one\n", fx->f_state);
      sleep(1);
#endif
      return KATCP_RESULT_FAIL;
  }

  fx->f_rx = parse_of_endpoint_katcp(d, msg);
  if(fx->f_rx == NULL){
    /* refuse to do anything with a message which doesn't have a payload */
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "task %p received an empty message", fx);
    return KATCP_RESULT_FAIL;
  }

  argc = get_count_parse_katcl(fx->f_rx);

  str = get_string_parse_katcl(fx->f_rx, 0);
  if(str == NULL){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "unable to acquire message name sent to %s", fx->f_name);
    /* how bad an error is this ? */
    fx->f_rx = NULL;
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "received a message %s ... ", str);

  type = str[0];
  source = source_endpoint_katcp(d, msg);
  rh = NULL;

  switch(type){
    case KATCP_REQUEST :
      /* we respond to requests */
      fx->f_current_endpoint = source;
      fx->f_current_map = (source == fx->f_remote) ? KATCP_MAP_REMOTE_REQUEST : KATCP_MAP_INNER_REQUEST;
      break;

    case KATCP_INFORM  :
      fx->f_current_map = (source == fx->f_remote) ? KATCP_MAP_REMOTE_INFORM : KATCP_MAP_INNER_INFORM;
      /* WARNING: fall */
    case KATCP_REPLY   :
      rh = find_handler_peer_flat_katcp(d, fx, source, str);
      if(rh){
        /* WARNING: adjusts default output */
#ifdef DEBUG
       fprintf(stderr, "dpx[%p]: setting output to %p\n", fx, rh->r_issuer);
#endif
        fx->f_current_endpoint = rh->r_issuer;
      }
      break;

    default : 
#ifdef KATCP_CONSISTENCY_CHECKS
      if(source != fx->f_remote){
        fprintf(stderr, "major usage problem: received an invalid internal message <%s>\n", str);
        abort();
      }
#endif
      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "ignoring malformed request %s", str);
      fx->f_rx = NULL;
      return KATCP_RESULT_FAIL;
  }

  set_current_flat(d, fx);

  /* before calling this we set up: 
   * - current lookup map,
   * - current endpoint (can be nothing) 
   * - message issuer callback (can be null, not only for requests) 
   * - the current flat (for this_flat)
   * - the message received 
   */

  /* TODO: use return code of process_outstanding_flat_katcp */
  result = KATCP_RESULT_OWN;

  if((type == KATCP_REPLY) || (type == KATCP_INFORM)){
    if(rh){
#ifdef DEBUG
      fprintf(stderr, "dpx[%p]: processing candidate callback (%p) match for %s\n", fx, rh, str);
#endif
      /* replies may have a different initiating message */
      fx->f_orx = rh->r_initial;
      process_outstanding_flat_katcp(d, fx, argc, rh, type);
#ifdef DEBUG
    } else {
      fprintf(stderr, "dpx[%p]: no callback registered for message %s\n", fx, str);
#endif
    }
  }

  if((type == KATCP_REQUEST) || (type == KATCP_INFORM)){
    /* for plain requests, origin is same as received */
    fx->f_orx = fx->f_rx;
    result = process_map_flat_katcp(d, fx, argc, str, rh ? 1 : 0);
  }

#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: result of processing %s is %d\n", fx, str, result);
#endif

  clear_current_flat(d);

  fx->f_current_endpoint = NULL;
  fx->f_current_map = KATCP_MAP_UNSET;

  fx->f_rx = NULL;
  fx->f_orx = NULL;

  return result;
}

int wake_endpoint_remote_flat_katcp(struct katcp_dispatch *d, struct katcp_endpoint *ep, struct katcp_message *msg, void *data)
{
  /* this is the endpoint for the outgoing queue of messages */
  /* TODO: If we see an outgoing request we should defer further sends */
  /* TODO: Set timeout when generating an outgoing request */

  struct katcp_flat *fx;
  struct katcl_parse *px;
  int result, request;

  fx = data;
  sane_flat_katcp(fx);

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "task %p (%s) got io", fx, fx->f_name);

  switch(fx->f_state){
    case FLAT_STATE_CONNECTING :
#ifdef DEBUG
      fprintf(stderr, "dpx[%p]: message arrived early, we are still connecting\n", fx);
#endif
      break;
    case FLAT_STATE_UP :
    case FLAT_STATE_FINISHING :
    case FLAT_STATE_ENDING :
      break;
    default :
#ifdef DEBUG
      fprintf(stderr, "dpx[%p]: message to remote arrived in unusual state %u, maybe shutting down\n", fx, fx->f_state);
#endif
      return KATCP_RESULT_OWN;
  }

  px = parse_of_endpoint_katcp(d, msg);
  if(px == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "task %p (%s) received emtpy message for network", fx, fx->f_name);
    return KATCP_RESULT_OWN;
  }

  request = is_request_parse_katcl(px);

  result = append_parse_katcl(fx->f_line, px);
  /* WARNING: do something with the return code */
  
  if(request){

    /* TODO: set timeout here ... */

#if 0
    return KATCP_RESULT_PAUSE;
#endif
  } 

  /* WARNING: unclear what the return code should be, we wouldn't want to generate acknowledgements to replies and informs */
#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: io result is %d\n", fx, KATCP_RESULT_OWN);
#endif

  return KATCP_RESULT_OWN; 
}

void release_endpoint_peer_flat_katcp(struct katcp_dispatch *d, void *data)
{
  struct katcp_flat *f;

  f = data;

  sane_flat_katcp(f);

  f->f_peer = NULL;
}

void release_endpoint_remote_flat_katcp(struct katcp_dispatch *d, void *data)
{
  struct katcp_flat *f;

  f = data;

  sane_flat_katcp(f);

  f->f_remote = NULL;
}

/********************************************************************/

struct katcp_flat *create_exec_flat_katcp(struct katcp_dispatch *d, char *name, struct katcp_group *gx, char **vector)
{
  struct katcp_flat *fx;
  struct katcl_line *xl;
  int fds[2], copies;
#if 0
  int efd;
#endif
  char *fallback[2];
  pid_t pid;

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create socketpair: %s", strerror(errno));
    return NULL;
  }

  pid = fork();
  if(pid < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to start process: %s", strerror(errno));
    close(fds[0]);
    close(fds[1]);
    return NULL;
  }

  if(pid > 0){
    close(fds[0]);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    fx = create_flat_katcp(d, fds[1], 1, name, gx);
    if(fx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate duplex state, terminating new child");
      kill(pid, SIGTERM);
      close(fds[1]);
      return NULL;
    }

    /* TODO: what about exit events ? */

    return fx;

  }

  /* WARNING: now in child - never return from here onwards */

  setenv("KATCP_CLIENT", name, 1);

  xl = create_katcl(fds[0]);

  close(fds[1]);

  copies = 0;
  if(fds[0] != STDOUT_FILENO){
    if(dup2(fds[0], STDOUT_FILENO) != STDOUT_FILENO){
      sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to set up standard output for child task <%s> (%s)", name, strerror(errno)); 
      exit(EX_OSERR);
    }
    copies++;
  }
  if(fds[0] != STDIN_FILENO){
    if(dup2(fds[0], STDIN_FILENO) != STDIN_FILENO){
      sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to set up standard input for child task <%s> (%s)", name, strerror(errno)); 
      exit(EX_OSERR);
    }
    copies++;
  }
  if(copies >= 2){
    close(fds[0]);
  }

#if 0 
  /* would be ifndef KATCP_STDERR_ERRORS, but may not be needed if all CLO_EXECs are done */
  efd = open("/dev/null", O_WRONLY);
  if(efd >= 0){
    if(efd != STDERR_FILENO){
      dup2(efd, STDERR_FILENO);
    }
  }
#endif

  if(vector == NULL){
    fallback[0] = name;
    fallback[1] = NULL;
    execvp(name, fallback);
  } else {
    execvp(vector[0], vector);
  }

  sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to launch command <%s>", vector ? vector[0] : name, strerror(errno)); 

  destroy_katcl(xl, 0);

  exit(EX_OSERR);

  /* pacify compiler */
  return NULL;
}

struct katcp_flat *create_flat_katcp(struct katcp_dispatch *d, int fd, int up, char *name, struct katcp_group *g)
{
  /* TODO: what about cloning an existing one to preserve misc settings, including log level, etc */
  struct katcp_flat *f, **tmp;
  struct katcp_shared *s;
  struct katcp_group *gx;
  unsigned int i;

  s = d->d_shared;

  if((s->s_members == 0) || (s->s_groups == NULL)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group to associate new connection with");
    return NULL;
  }

  gx = (g == NULL) ? s->s_fallback : g;

  tmp = realloc(gx->g_flats, sizeof(struct katcp_flat *) * (gx->g_count + 1));
  if(tmp == NULL){
    return NULL;
  }

  gx->g_flats = tmp;

  f = malloc(sizeof(struct katcp_flat));
  if(f == NULL){
    return NULL;
  }

  f->f_magic = FLAT_MAGIC;
  f->f_name = NULL;

  f->f_flags = 0;

  /* for cases where connect() still has to succeed */
  f->f_state = up ? FLAT_STATE_UP : FLAT_STATE_CONNECTING;

  f->f_exit_code = 0; /* WARNING: should technically be a fail, to catch cases where it isn't set at exit time */

  f->f_log_level = gx->g_log_level;
  f->f_scope = gx->g_scope;

  f->f_peer = NULL;
  f->f_remote = NULL;

  f->f_line = NULL;
  f->f_shared = NULL;

#if 0
  f->f_backlog = NULL;
#endif
  f->f_rx = NULL;

  f->f_tx = NULL;

  f->f_cmd = NULL;

  for(i = 0; i < KATCP_SIZE_REPLY; i++){
    f->f_replies[i].r_flags = 0;
    f->f_replies[i].r_message = NULL;
    f->f_replies[i].r_reply = NULL;
    f->f_replies[i].r_issuer = NULL;
    f->f_replies[i].r_recipient = NULL;
    f->f_replies[i].r_initial = NULL;
  }

  for(i = 0; i < KATCP_SIZE_MAP; i++){
    f->f_maps[i] = NULL;
  }

  f->f_current_map = KATCP_MAP_UNSET;
  f->f_current_endpoint = NULL;

  f->f_group = NULL;

  if(name){
    f->f_name = strdup(name);
    if(f->f_name == NULL){
      destroy_flat_katcp(d, f);
      return NULL;
    }
  }

  f->f_peer = create_endpoint_katcp(d, &wake_endpoint_peer_flat_katcp, &release_endpoint_peer_flat_katcp, f);
  if(f->f_peer == NULL){
    /* WARNING - unclear how bad a failure this is */
    destroy_flat_katcp(d, f);
    return NULL;
  }

  f->f_remote = create_endpoint_katcp(d, &wake_endpoint_remote_flat_katcp, &release_endpoint_remote_flat_katcp, f);
  if(f->f_remote == NULL){
    destroy_flat_katcp(d, f);
    return NULL;
  }

  f->f_line = create_katcl(fd);
  if(f->f_line == NULL){
    destroy_flat_katcp(d, f);
    return NULL;
  }

#if 0
  f->f_backlog = create_queue_katcl();
  if(f->f_backlog == NULL){
    destroy_flat_katcp(d, f);
    return NULL;
  }
#endif

  f->f_shared = s;

  gx->g_flats[gx->g_count] = f;
  gx->g_count++;

  f->f_group = gx;

  hold_group_katcp(gx); 

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "created instance for %s", name ? name : "<anonymous");

  return f;
}

/* auxillary calls **************************************************/

struct katcp_flat *find_name_flat_katcp(struct katcp_dispatch *d, char *group, char *name)
{
  struct katcp_group *gx;
  struct katcp_flat *fx;
  struct katcp_shared *s;
  unsigned int i, j;

  s = d->d_shared;

  if(name == NULL){
    return NULL;
  }

  for(j = 0; j < s->s_members; j++){
    gx = s->s_groups[j];
    if((group == NULL) || (gx->g_name && (strcmp(group, gx->g_name) == 0))){
      for(i = 0; i < gx->g_count; i++){
        fx = gx->g_flats[i];
        if(fx->f_name && (strcmp(name, fx->f_name) == 0)){
          return fx;
        }
      }
    }
  }

  return NULL;
}

int rename_flat_katcp(struct katcp_dispatch *d, char *group, char *was, char *should)
{
  struct katcp_flat *fx;
  char *ptr;
  int len;

#ifdef KATCP_CONSISTENCY_CHECKS
  if((was == NULL) || (should == NULL)){
    fprintf(stderr, "dpx: usage problem - rename needs valid parameters\n");
    abort();
  }
#endif

  len = strlen(should) + 1;

  fx = find_name_flat_katcp(d, group, was);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "no entry %s found", was);
    return -1;
  }

  ptr = realloc(fx->f_name, len);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "allocation failure");
    return -1;
  }

  fx->f_name = ptr;
  memcpy(fx->f_name, should, len);

  return 0;
}

int terminate_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx)
{
  if(fx == NULL){
    return -1;
  }

  sane_flat_katcp(fx);

  switch(fx->f_state){
    case FLAT_STATE_CONNECTING : 
      /* WARNING: at the moment we refuse to queue messages in this state. If this changes, then a termination has to happen more carefully here */
      fx->f_state = FLAT_STATE_CRASHING;
      return 0;
    case FLAT_STATE_UP : 
      fx->f_state = FLAT_STATE_FINISHING;
      return 0;
    default :
      /* already terminating */
      return -1;
  }
}

struct katcp_endpoint *peer_of_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx)
{
  if(fx == NULL){
    return NULL;
  }

  return fx->f_peer;
}

struct katcp_endpoint *remote_of_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx)
{
  if(fx == NULL){
    return NULL;
  }

  return fx->f_remote;
}

struct katcp_cmd_map *map_of_flat_katcp(struct katcp_flat *fx)
{
  struct katcp_group *gx;
  struct katcp_cmd_map *mx;

  if(fx == NULL){
    return NULL;
  }

  if(fx->f_current_map < 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "logic failure: searching for command without having a mode set\n");
    abort();
#endif
  }
  if(fx->f_current_map >= KATCP_SIZE_MAP){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "major logic failure: current map index %d is corrupt\n", fx->f_current_map);
    abort();
#endif
  }

  mx = fx->f_maps[fx->f_current_map];

  if(mx){
    return mx;
  }

  gx = fx->f_group;
  if(gx == NULL){
    return NULL;
  }

  mx = gx->g_maps[fx->f_current_map];

  return mx;
}

struct katcp_flat *require_flat_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_flat *f;

  s = d->d_shared;

  if(s == NULL){
    return NULL;
  }

  if(s->s_level < 0){
    /* require() aborts if not called inside a duplex context, this() does not */
#ifdef KATCP_CONSISTENCY_CHECKS
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "duplex logic: level variable set to %d", s->s_level);
    abort();
#endif
    return NULL;
  }

  f = s->s_this[s->s_level];
#ifdef KATCP_CONSISTENCY_CHECKS
  if(f == NULL){
    fprintf(stderr, "flat: logic problem - should not encounter a null entry\n");
    abort();
    return NULL;
  }
#endif

  return f;
}

struct katcp_cmd_item *this_cmd_katcp(struct katcp_dispatch *d)
{
  struct katcp_flat *fx;

  fx = this_flat_katcp(d);
  if(fx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "flat: logic error: no command item data structure as we are not in duplex context\n");
    abort();
#endif
    return NULL;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(fx->f_cmd == NULL){
    fprintf(stderr, "flat: logic error: no command item data structure as we are not in command handler\n");
    abort();
  }
#endif

  return fx->f_cmd;
}

struct katcp_flat *this_flat_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_flat *f;

  s = d->d_shared;

  if(s == NULL){
    return NULL;
  }

  if(s->s_level < 0){
    return NULL;
  }

  f = s->s_this[s->s_level];
#ifdef KATCP_CONSISTENCY_CHECKS
  if(f == NULL){
    fprintf(stderr, "flat: logic problem - should not encounter a null entry\n");
    abort();
    return NULL;
  }
#endif

  return f;
}

static void set_current_flat(struct katcp_dispatch *d, struct katcp_flat *fx)
{
  struct katcp_shared *s;

  s = d->d_shared;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(s->s_level >= 0){
    fprintf(stderr, "logic problem: setting a current duplex [%d]=%p while one is %p\n", s->s_level, fx, s->s_this[s->s_level]);
    abort();
  }
#endif

  s->s_this[0] = fx;
  s->s_level = 0;
}

static void clear_current_flat(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  s = d->d_shared;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(s->s_level < 0){
    fprintf(stderr, "logic problem: duplex field already cleared\n");
    abort();
  }
#endif

  s->s_this[0] = NULL;
  s->s_level = (-1);
}

#if 0
static int actually_set_output_flat_katcp(struct katcp_flat *fx, unsigned int destination, unsigned int persistence)
{
  sane_flat_katcp(fx);

  if(persistence & KATCP_DPX_SEND_RESET){
    fx->f_send = (fx->f_send & KATCP_DPX_SEND_DESTINATION) | KATCP_DPX_SEND_CURRENTLY;
  }

  if((fx->f_send & KATCP_DPX_SEND_PERSISTENCE) <= (persistence & KATCP_DPX_SEND_PERSISTENCE)){
    fx->f_send = (destination & KATCP_DPX_SEND_DESTINATION) | (persistence & KATCP_DPX_SEND_PERSISTENCE);

#ifdef DEBUG
    fprintf(stderr, "dpx: new send=0x%x, from dest=0x%x and persistence=0x%x\n", fx->f_send, destination, persistence);
#endif

    return 0;
  }

#ifdef DEBUG
  fprintf(stderr, "not updating destination to 0x%x, as set mask is 0x%x > 0x%x\n", destination, fx->f_send & KATCP_DPX_SEND_PERSISTENCE, persistence & KATCP_DPX_SEND_PERSISTENCE);
#endif

  return -1;
}
#endif

#if 0
int set_output_flat_katcp(struct katcp_dispatch *d, unsigned int destination, unsigned int persistence)
{
  struct katcp_flat *fx;

  if(d == NULL){
    return -1;
  }

  fx = require_flat_katcp(d);
  if(fx == NULL){
#ifdef DEBUG
    fprintf(stderr, "output can not be set, we are not in duplex handing code\n");
#endif
    return -1;
  }

  return actually_set_output_flat_katcp(fx, destination, persistence);
}
#endif

/**************************************************************************/

#if 0
static int set_generic_flat_katcp(struct katcp_dispatch *d, int direction, int (*call)(struct katcp_dispatch *d, int argc), char *string, unsigned int flags)
{
  struct katcp_flat *fx; 
  char *tmp, *ptr;
  unsigned int actual, len;
  struct katcp_response_handler *rh;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(string == NULL){
    fprintf(stderr, "major logic problem: need a valid string for callback");
    abort();
  }
#endif

  switch(direction){
    case KATCP_DIRECTION_INNER  : 
    case KATCP_DIRECTION_REMOTE :
      break;
    default :
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "major logic problem: invoked with incorrect direction %d\n", direction);
      abort();
#endif
      return -1;
  }

  fx = require_flat_katcp(d);

  if(fx == NULL){
    return -1;
  }

  /* WARNING: loads of helpful magic here ... */
  switch(string[0]){
    case KATCP_REQUEST   :
      ptr = string + 1;
      actual = flags ? flags : (KATCP_REPLY_HANDLE_REPLIES | KATCP_REPLY_HANDLE_INFORMS);
      break;
    case KATCP_REPLY :
      ptr = string + 1;
      actual = flags ? flags : KATCP_REPLY_HANDLE_REPLIES;
      break;
    case KATCP_INFORM  :
      ptr = string + 1;
      actual = flags ? flags : KATCP_REPLY_HANDLE_INFORMS;
      break;
    default :
      actual = flags;
      ptr = string;
      break;
  }


  rh = &(fx->f_replies[direction]);

  len = strlen(ptr);

  tmp = realloc(rh->r_message, len + 1);
  if(tmp == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "allocation failure, unable to duplicate %s", ptr);
    return -1;
  }
  rh->r_message = tmp;
  
  memcpy(rh->r_message, ptr, len + 1);

  rh->r_flags   = actual;
  rh->r_reply   = call;

  return 0;
}
#endif

#if 0
int set_inner_flat_katcp(struct katcp_dispatch *d, int (*call)(struct katcp_dispatch *d, int argc), char *string, unsigned int flags)
{
  return set_generic_flat_katcp(d, KATCP_DIRECTION_INNER, call, string, flags);
}

int set_remote_flat_katcp(struct katcp_dispatch *d, int (*call)(struct katcp_dispatch *d, int argc), char *string, unsigned int flags)
{
  return set_generic_flat_katcp(d, KATCP_DIRECTION_REMOTE, call, string, flags);
}
#endif

int callback_flat_katcp(struct katcp_dispatch *d, struct katcp_endpoint *issuer, struct katcl_parse *px, struct katcp_endpoint *recipient, int (*call)(struct katcp_dispatch *d, int argc), char *string, unsigned int flags)
{
  struct katcp_flat *fx; 
  char *tmp, *ptr;
  unsigned int actual, len, i, slot;
  struct katcp_response_handler *rh;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(string == NULL){
    fprintf(stderr, "major logic problem: need a valid string for callback");
    abort();
  }
#endif

  fx = require_flat_katcp(d);

  if(fx == NULL){
    return -1;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if((issuer != NULL) && (fx->f_peer != issuer) && (fx->f_remote != issuer) && (fx->f_current_endpoint != issuer)){
    fprintf(stderr, "possible usage problem: registering callback for issuer which isn't current task. Not yet supported\n");
    abort();
  }
#endif

  slot = KATCP_SIZE_REPLY;

  for(i = 0; i < KATCP_SIZE_REPLY; i++){
    rh = &(fx->f_replies[i]);
    if(rh->r_reply == NULL){
      slot = i;
#ifdef KATCP_CONSISTENCY_CHECKS
    } else {
      /* is somebody is sending something to this party ? */
      if(rh->r_recipient && (rh->r_recipient == recipient)){
        /* is it the same sender ? */
        if(rh->r_issuer == issuer){
          fprintf(stderr, "logic problem: issuer %p has already a callback in flight to %p, should not pipeline another one\n", issuer, recipient);
          abort();
        }
      }
#else 
      i = KATCP_SIZE_REPLY;
#endif
    }
  }

  if(slot >= KATCP_SIZE_REPLY){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "resource problem: no free reply handler slots (%u in use)", slot);
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "problem: no free callback slots\n");
#endif
  }

  rh = &(fx->f_replies[slot]);

#ifdef KATCP_CONSISTENCY_CHECKS
  if(rh->r_initial || rh->r_issuer || rh->r_recipient){
    fprintf(stderr, "logic problem: empty callback slot [%u]=%p not really empty\n", slot, rh);
    abort();
  }
#endif

  if(px){
    rh->r_initial = copy_parse_katcl(px);
    if(px == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "allocation error, unable to duplicate message");
      return -1;
    }
  }

  /* WARNING: loads of helpful magic here ... */
  switch(string[0]){
    case KATCP_REQUEST   :
      ptr = string + 1;
      actual = flags ? flags : (KATCP_REPLY_HANDLE_REPLIES | KATCP_REPLY_HANDLE_INFORMS);
      break;
    case KATCP_REPLY :
      ptr = string + 1;
      actual = flags ? flags : KATCP_REPLY_HANDLE_REPLIES;
      break;
    case KATCP_INFORM  :
      ptr = string + 1;
      actual = flags ? flags : KATCP_REPLY_HANDLE_INFORMS;
      break;
    default :
      actual = flags;
      ptr = string;
      break;
  }

  if(actual == 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "usage error, need to specify handler flags");
    return -1;
  }

  len = strlen(ptr);

  tmp = realloc(rh->r_message, len + 1);
  if(tmp == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "allocation failure, unable to duplicate %s", ptr);
    return -1;
  }
  rh->r_message = tmp;
  
  memcpy(rh->r_message, ptr, len + 1);

  rh->r_flags     = actual;
  rh->r_reply     = call;

  if(issuer){
    rh->r_issuer    = issuer;
    reference_endpoint_katcp(d, issuer);
  }
  
  if(recipient){
    rh->r_recipient = recipient;
    reference_endpoint_katcp(d, recipient);
  }

#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: registered callback[%u]=%p matching %p (for issuer %p)\n", fx, slot, call, rh->r_message, rh->r_issuer);
#endif

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "registered callback in flat %p (%s) at slot %u for %s", fx, fx->f_name, slot, rh->r_message);

  return 0;
}

/***************************************************************************/

int is_inner_flat_katcp(struct katcp_dispatch *d)
{
  struct katcp_flat *fx; 

  fx = require_flat_katcp(d);

  if(fx == NULL){
    return -1;
  }

  if(fx->f_current_endpoint == NULL){
    return -1;
  }

  if(fx->f_current_endpoint == fx->f_remote){
    return 0;
  }

  return 1;
}

int is_remote_flat_katcp(struct katcp_dispatch *d)
{
  struct katcp_flat *fx; 

  fx = require_flat_katcp(d);

  if(fx == NULL){
    return -1;
  }

  if(fx->f_current_endpoint == NULL){
    return -1;
  }

  if(fx->f_current_endpoint == fx->f_remote){
    return 1;
  }

  return 0;
}

/* output routines ********************************************************/

static struct katcl_parse *prepare_append_flat_katcp(struct katcp_flat *fx, int flags)
{
  sane_flat_katcp(fx);

  if(fx->f_tx == NULL){
    if(flags & KATCP_FLAG_FIRST){
      fx->f_tx = create_referenced_parse_katcl();
    } else {
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "logic or allocation problem: attempting to initialise message without it being marked as first word\n");
      abort();
#endif
    }
#ifdef KATCP_CONSISTENCY_CHECKS
  } else {
    if(flags & KATCP_FLAG_FIRST){
      fprintf(stderr, "logic problem: attempting to initialise parse structure while old one still exists\n");
      abort();
    }
#endif
  }

  return fx->f_tx;
}

static int finish_append_flat_katcp(struct katcp_dispatch *d, int flags, int result)
{
  struct katcp_flat *fx;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  sane_flat_katcp(fx);

  if(result < 0){
    if(fx->f_tx){
      destroy_parse_katcl(fx->f_tx);
      fx->f_tx = NULL;
    }
  }

  if(!(flags & KATCP_FLAG_LAST)){
    return result;
  }

  if(fx->f_tx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "duplex: not sending a null message\n");
#endif
    return -1;
  }

  if(fx->f_current_endpoint){
    result = send_message_endpoint_katcp(d, fx->f_peer, fx->f_current_endpoint, fx->f_tx, (is_request_parse_katcl(fx->f_tx) > 0) ? 1 : 0);

  } else {
    /* no output set, discarding output */
#ifdef DEBUG
    fprintf(stderr, "dpx[%p]: discarding message - no output set\n", fx);
#endif
    result = 0;
  }

  destroy_parse_katcl(fx->f_tx);

  fx->f_tx = NULL;
  return result;

#if 0
  switch(fx->f_send & KATCP_DPX_SEND_DESTINATION){
    case KATCP_DPX_SEND_NULL :
      destroy_parse_katcl(fx->f_tx);
      fx->f_tx = NULL;
      return 0;
    case KATCP_DPX_SEND_STREAM :
      result = append_parse_katcl(fx->f_line, fx->f_tx);
#if 0
      if(is_reply_parse_katcl(fx->f_tx)){
        fx->f_blocked = 0;
      }
#endif
      destroy_parse_katcl(fx->f_tx);
      fx->f_tx = NULL;
      return result;
    case KATCP_DPX_SEND_PEER :
      result = answer_endpoint_katcp(d, fx->f_peer, fx->f_tx);
      destroy_parse_katcl(fx->f_tx);
      fx->f_tx = NULL;
      return result; /* WARNING: result isn't amount of bytes written ... */
    default :
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "duplex: data corruption: send destination invalid: 0x%x\n", fx->f_send);
      abort();
#endif
      return -1 ;
  }
#endif

}

int prepend_generic_flat_katcp(struct katcp_dispatch *d, int reply)
{
  struct katcp_flat *fx;
  char *message;
  int result;
#ifdef KATCP_ENABLE_TAGS
  int tag;
#endif

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  if(fx->f_orx == NULL){
#ifdef DEBUG
    fprintf(stderr, "prepend: no origin message available\n");
#endif
    return -1;
  }

  message = copy_string_parse_katcl(fx->f_orx, 0);
  if(message == NULL){
#ifdef DEBUG
    fprintf(stderr, "prepend: unable to duplicate message\n");
#endif
    return -1;
  }

  message[0] = reply ? KATCP_REPLY : KATCP_INFORM;

#ifdef KATCP_ENABLE_TAGS
  tag = get_tag_parse_katcl(fx->f_orx);
  if(tag >= 0){
    result = append_args_flat_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "%s[%d]", message, tag);
  } else {
#endif
    result = append_string_flat_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, message);
#ifdef KATCP_ENABLE_TAGS
  }
#endif

  free(message);

  return result;
}

int append_string_flat_katcp(struct katcp_dispatch *d, int flags, char *buffer)
{
  struct katcl_parse *px;
  struct katcp_flat *fx;
  int result;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = prepare_append_flat_katcp(fx, flags);
  if(px == NULL){
    return -1;
  }

  result = add_string_parse_katcl(px, flags, buffer);

  return finish_append_flat_katcp(d, flags, result);
}

int append_unsigned_long_flat_katcp(struct katcp_dispatch *d, int flags, unsigned long v)
{
  struct katcl_parse *px;
  struct katcp_flat *fx;
  int result;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = prepare_append_flat_katcp(fx, flags);
  if(px == NULL){
    return -1;
  }

  result = add_unsigned_long_parse_katcl(px, flags, v);

  return finish_append_flat_katcp(d, flags, result);
}

int append_signed_long_flat_katcp(struct katcp_dispatch *d, int flags, unsigned long v)
{
  struct katcl_parse *px;
  struct katcp_flat *fx;
  int result;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = prepare_append_flat_katcp(fx, flags);
  if(px == NULL){
    return -1;
  }

  result = add_signed_long_parse_katcl(px, flags, v);

  return finish_append_flat_katcp(d, flags, result);
}

int append_hex_long_flat_katcp(struct katcp_dispatch *d, int flags, unsigned long v)
{
  struct katcl_parse *px;
  struct katcp_flat *fx;
  int result;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = prepare_append_flat_katcp(fx, flags);
  if(px == NULL){
    return -1;
  }

  result = add_hex_long_parse_katcl(px, flags, v);

  return finish_append_flat_katcp(d, flags, result);
}

#ifdef KATCP_USE_FLOATS
int append_double_flat_katcp(struct katcp_dispatch *d, int flags, double v)
{
  struct katcl_parse *px;
  struct katcp_flat *fx;
  int result;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = prepare_append_flat_katcp(fx, flags);
  if(px == NULL){
    return -1;
  }

  result = add_double_parse_katcl(px, flags, v);

  return finish_append_flat_katcp(d, flags, result);
}
#endif

int append_buffer_flat_katcp(struct katcp_dispatch *d, int flags, void *buffer, int len)
{
  struct katcl_parse *px;
  struct katcp_flat *fx;
  int result;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = prepare_append_flat_katcp(fx, flags);
  if(px == NULL){
    return -1;
  }

  result = add_buffer_parse_katcl(px, flags, buffer, len);

  return finish_append_flat_katcp(d, flags, result);
}

int append_parameter_flat_katcp(struct katcp_dispatch *d, int flags, struct katcl_parse *p, unsigned int index)
{
  struct katcl_parse *px;
  struct katcp_flat *fx;
  int result;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = prepare_append_flat_katcp(fx, flags);
  if(px == NULL){
    return -1;
  }

  result = add_parameter_parse_katcl(px, flags, p, index);

  return finish_append_flat_katcp(d, flags, result);
}

int append_trailing_flat_katcp(struct katcp_dispatch *d, int flags, struct katcl_parse *p, unsigned int start)
{
  struct katcl_parse *px;
  struct katcp_flat *fx;
  int result;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = prepare_append_flat_katcp(fx, flags);
  if(px == NULL){
    return -1;
  }

  result = add_trailing_parse_katcl(px, flags, p, start);

  return finish_append_flat_katcp(d, flags, result);
}

int append_parse_flat_katcp(struct katcp_dispatch *d, struct katcl_parse *p)
{
  struct katcp_flat *fx;

  if(p == NULL){
    return -1;
  }

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  if(fx->f_tx){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "refusing to insert a new message into partially completed message");
    return -1;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(p->p_magic != KATCL_PARSE_MAGIC){
    fprintf(stderr, "logic problem: expected parse at %p, not magic 0x%x\n", p, p->p_magic);
    abort();
  }

  if(p->p_state != KATCL_PARSE_DONE){
    fprintf(stderr, "usage problem: outputting incomplete parse %p\n", p);
    abort();
  }
#endif

  fx->f_tx = copy_parse_katcl(p);
  if(fx->f_tx == NULL){
    return -1;
  }

  /* WARNING: finish append should probably be split into two, we are abusing flags and result codes here, to get past the first part */
  return finish_append_flat_katcp(d, KATCP_FLAG_LAST, 0);
}

int append_vargs_flat_katcp(struct katcp_dispatch *d, int flags, char *fmt, va_list args)
{
  struct katcl_parse *px;
  struct katcp_flat *fx;
  int result;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = prepare_append_flat_katcp(fx, flags);
  if(px == NULL){
    return -1;
  }

  result = add_vargs_parse_katcl(px, flags, fmt, args);

  return finish_append_flat_katcp(d, flags, result);
}

int append_args_flat_katcp(struct katcp_dispatch *d, int flags, char *fmt, ...)
{
  int result;
  va_list args;

  va_start(args, fmt);
  result = append_vargs_flat_katcp(d, flags, fmt, args);
  va_end(args);

  return result;
}

/**************************************************************************/
/* mainloop related logic *************************************************/

int load_flat_katcp(struct katcp_dispatch *d)
{
  struct katcp_flat *fx;
  struct katcp_shared *s;
  struct katcp_group *gx;
  unsigned int i, j, inc;
  int result, fd;

  s = d->d_shared;

  result = 0;

#if DEBUG 
  fprintf(stderr, "dpx[*]: loading %u groups\n", s->s_members);
#endif

  for(j = 0; j < s->s_members; j++){
    gx = s->s_groups[j];
    i = 0;
    while(i < gx->g_count){
      inc = 1;

      fx = gx->g_flats[i];
      fd = fileno_katcl(fx->f_line);

#if DEBUG
  fprintf(stderr, "dpx[%p]: loading: peer=%p, remote=%p, name=%s, fd=%d, state=%u, log=%d, scope=%d\n", fx, fx->f_peer, fx->f_remote, fx->f_name, fd, fx->f_state, fx->f_log_level, fx->f_scope);
#endif

#if 0
      /* disabled: this log triggers io, which triggers this loop */
      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "loading %p in state %d with fd %d", f, f->f_state, fd);
#endif

      switch(fx->f_state){

        case FLAT_STATE_CONNECTING : 
          FD_SET(fd, &(s->s_write));
          if(fd > s->s_max){
            s->s_max = fd;
          }
          break;

        case FLAT_STATE_UP : 
          FD_SET(fd, &(s->s_read));
          if(flushing_katcl(fx->f_line)){
            FD_SET(fd, &(s->s_write));
          } 
          if(fd > s->s_max){
            s->s_max = fd;
          }
          break;

        case FLAT_STATE_FINISHING : 

          if(fx->f_peer){
            /* accept no further messages on input queue */
            close_endpoint_katcp(d, fx->f_peer);
          }

          fx->f_state = FLAT_STATE_ENDING;
          /* WARNING: falls */

        case FLAT_STATE_ENDING : /* not to be set directly */

#ifdef KATCP_CONSISTENCY_CHECKS
          if((fx->f_peer == NULL) || (fx->f_remote == NULL)){
            fprintf(stderr, "duplex: peer or remote endpoint is gone in ending state for %p\n", fx);
            abort();
          }
#endif

          if(flushing_katcl(fx->f_line)){
            FD_SET(fd, &(s->s_write));
            if(fd > s->s_max){
              s->s_max = fd;
            }
            break;
          } 

          if(pending_endpoint_katcp(d, fx->f_peer) > 0){
            break;
          }

          if(pending_endpoint_katcp(d, fx->f_remote) > 0){
            break;
          }

          /* TODO: another check if we have a response handler registered */


          fx->f_state = FLAT_STATE_CRASHING;
          /* WARNING: falls */

        case FLAT_STATE_CRASHING :

          if(fx->f_remote){
            release_endpoint_katcp(d, fx->f_remote);
            fx->f_remote = NULL;
          }

          if(fx->f_line){
            destroy_katcl(fx->f_line, 1);
            fx->f_line = NULL;
          }

          cancel_flat_katcp(d, fx);

          if(fx->f_peer){
            release_endpoint_katcp(d, fx->f_peer);
            fx->f_peer = NULL;
          }

          fx->f_state = FLAT_STATE_DEAD;
          /* WARNING: falls */

        case FLAT_STATE_DEAD : /* not to be set directly */
          gx->g_count--;
          if(i < gx->g_count){
            gx->g_flats[i] = gx->g_flats[gx->g_count];
          }
          deallocate_flat_katcp(d, fx);

          inc = 0;
          break;

        case FLAT_STATE_GONE :
#ifdef KATCP_CONSISTENCY_CHECKS
          fprintf(stderr, "flat: problem: state %u should have been removed already\n", fx->f_state);
          abort();
#endif
          break;

        default :
#ifdef KATCP_CONSISTENCY_CHECKS
          fprintf(stderr, "flat: problem: unknown state %u for %p\n", fx->f_state, fx);
          abort();
#endif
          break;

          /* WARNING: fall, but only in drain state and if no more output pending */



      }

      i += inc;
    }


    /* TODO: maybe do something if group is empty */
  }

  return result;
}

int run_flat_katcp(struct katcp_dispatch *d)
{
  struct katcp_flat *fx;
  struct katcp_shared *s;
  struct katcl_parse *px;
  struct katcp_group *gx;
  unsigned int i, j, len;
  int fd, result, code, acknowledge;

  s = d->d_shared;

#if DEBUG > 2
  fprintf(stderr, "dpx[*]: running %u groups\n", s->s_members);
#endif

  for(j = 0; j < s->s_members; j++){
    gx = s->s_groups[j];
    for(i = 0; i < gx->g_count; i++){
      fx = gx->g_flats[i];


      if(fx->f_line){

        fd = fileno_katcl(fx->f_line);

        if(FD_ISSET(fd, &(s->s_write))){
          /* resume connect */
          if(fx->f_state == FLAT_STATE_CONNECTING){
            len = sizeof(int);
            result = getsockopt(fd, SOL_SOCKET, SO_ERROR, &code, &len);
            if(result == 0){
              switch(code){
                case 0 :
                  fx->f_state = FLAT_STATE_UP;
                  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "async connect succeeded");
                  break;
                case EINPROGRESS :
                  log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "saw an in progress despite write set being ready");
                  break;
                default :
                  log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to connect: %s", strerror(code));
                  fx->f_state = FLAT_STATE_CRASHING;
                  break;
              }
            } else {
              fx->f_state = FLAT_STATE_CRASHING;
            }
          } else {
            /* flush messages */
            if(write_katcl(fx->f_line) < 0){
              fx->f_state = FLAT_STATE_CRASHING;
            }
          }
        }

        if(FD_ISSET(fd, &(s->s_read))){
          /* acquire data */
          if(read_katcl(fx->f_line) != 0){
            fx->f_state = FLAT_STATE_CRASHING;
          }
          /* load all data into request queue, all processing happens in the endpoint */
          while((result = parse_katcl(fx->f_line)) > 0){
            px = ready_katcl(fx->f_line);
#ifdef KATCP_CONSISTENCY_CHECKS
            if(px == NULL){
              fprintf(stderr, "flat: logic problem - parse claims data, but ready returns none\n");
              abort();
            }
#endif
            if(is_request_parse_katcl(px)){
              acknowledge = 1;
            } else {
              acknowledge = 0;
            }

            log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "sending network message to endpoint %p", fx->f_peer);

            if(send_message_endpoint_katcp(d, fx->f_remote, fx->f_peer, px, acknowledge) < 0){
              log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to enqueue remote message");

              fx->f_state = FLAT_STATE_CRASHING;

              /* WARNING: drops out of parsing loop */
              break;
            }

            show_endpoint_katcp(d, "peer", KATCP_LEVEL_TRACE, fx->f_peer);

            /* TODO: check the size of the queue, give up if it has grown to unreasonable */

            clear_katcl(fx->f_line);
          }
        }

      }
    }
  }

  return s->s_members;
}

/* new connection handling routines *****************************/

int accept_flat_katcp(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode)
{
#define LABEL_BUFFER 32
  int fd, nfd;
  unsigned int len;
  struct sockaddr_in sa;
  struct katcp_flat *f;
  struct katcp_group *gx;
  char label[LABEL_BUFFER];
  long opts;

  fd = fileno_arb_katcp(d, a);

  if(!(mode & KATCP_ARB_READ)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "ay caramba, accept handler expected to be called with the read flag");
    return 0;
  }

  len = sizeof(struct sockaddr_in);
  nfd = accept(fd, (struct sockaddr *) &sa, &len);

  if(nfd < 0){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "accept on %s failed: %s", name_arb_katcp(d, a), strerror(errno));
    return 0;
  }

  gx = data_arb_katcp(d, a);
  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "group needed for accept");
    close(fd);
    return 0;
  }

  opts = fcntl(nfd, F_GETFL, NULL);
  if(opts >= 0){
    opts = fcntl(nfd, F_SETFL, opts | O_NONBLOCK);
  }

  snprintf(label, LABEL_BUFFER, "%s:%d", inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
  label[LABEL_BUFFER - 1] = '\0';

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "accepted new connection from %s via %s", label, name_arb_katcp(d, a));

  f = create_flat_katcp(d, nfd, 1, label, gx);
  if(f == NULL){
    close(nfd);
  }
  
  return 0;
#undef LABEL_BUFFER
}

int destroy_listen_flat_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_arb *a;

  a = find_type_arb_katcp(d, name, KATCP_ARB_TYPE_LISTENER);
  if(a == NULL){
    return -1;
  }

  return unlink_arb_katcp(d, a);
}

struct katcp_arb *create_listen_flat_katcp(struct katcp_dispatch *d, char *name, struct katcp_group *g)
{
  int fd;
  struct katcp_arb *a;
  struct katcp_group *gx;
  long opts;
  struct katcp_shared *s;

  s = d->d_shared;

  gx = (g != NULL) ? g : s->s_fallback;

  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group given, no duplex instances can be created");
    return NULL;
  }

  fd = net_listen(name, 0, 0);
  if(fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to listen on %s: %s", name, strerror(errno));
    return NULL;
  }

  opts = fcntl(fd, F_GETFL, NULL);
  if(opts >= 0){
    opts = fcntl(fd, F_SETFL, opts | O_NONBLOCK);
  }

  fcntl(fd, F_SETFD, FD_CLOEXEC);

  a = create_type_arb_katcp(d, name, KATCP_ARB_TYPE_LISTENER, fd, KATCP_ARB_READ, &accept_flat_katcp, gx);
  if(a == NULL){
    close(fd);
    return NULL;
  }

  hold_group_katcp(gx);

  return a;
}

/* commands, both old and new ***************************************/


/* connection management commands ***********************************/

int list_duplex_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  unsigned int i, j, k;
  struct katcp_shared *s;
  struct katcp_group *gx;
  struct katcp_response_handler *rh;
  struct katcp_flat *fx;

  s = d->d_shared;

  for(j = 0; j < s->s_members; j++){
    gx = s->s_groups[j];
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "group[%d]=%p is %s", j, gx, gx->g_name ? gx->g_name : "<anonymous>");
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "group[%d]=%p has log level %u", j, gx, gx->g_log_level);
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "group[%d]=%p has %d references and %u members", j, gx, gx->g_use, gx->g_count);
    for(i = 0; i < gx->g_count; i++){
      fx = gx->g_flats[i];
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s at %p in state %u", fx->f_name, fx, fx->f_state);

      for(k = 0; k < KATCP_SIZE_REPLY; k++){
        rh = &fx->f_replies[k];
        if(rh->r_reply){
          log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s has callback handler for %s at %p (issuer=%p, recipient=%p)", fx->f_name, rh->r_message, rh->r_reply, rh->r_issuer, rh->r_recipient);
        }
      }
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s has log level %d", fx->f_name, fx->f_log_level);
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s is part of group %p", fx->f_name, fx->f_group);
    }
  }

  return KATCP_RESULT_OK;
}

#if 0
int complete_relay_watchdog_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  char *code;

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "triggering watchdog complete logic");

  fx = require_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not retrive current session detail");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "cmd not run within a session");
  }

  code = arg_string_katcp(d, 1);
  if(code == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  if(strcmp(code, KATCP_OK)){
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int relay_watchdog_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  char *name, *group;
  struct katcp_flat *fx;
  struct katcl_parse *px;
  struct katcp_endpoint *source, *target;

  s = d->d_shared;

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  if(argc > 2){
    group = arg_string_katcp(d, 2);
  } else {
    group = NULL;
  }

  fx = require_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not retrive current session detail");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "cmd not run within a session");
  }
  source = peer_of_flat_katcp(d, fx);

  fx = find_name_flat_katcp(d, group, name);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not look up name %s", name);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "resolver");
  }
  target = peer_of_flat_katcp(d, fx);

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "sending ping from endpoint %p to endpoint %p", source, target);

  px = create_parse_katcl();
  if(px == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "allocation");
  }

  add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, "?watchdog");

  if(send_message_endpoint_katcp(d, source, target, px, 1) < 0){
    return KATCP_RESULT_FAIL;
  }

  if(callback_flat_katcp(d, fx->f_current_endpoint, target, &complete_relay_watchdog_group_cmd_katcp, "?watchdog", 0)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "unable to register watchdog callback");
    return KATCP_RESULT_FAIL;
  }

  /* WARNING: broken - will currently not resume, despite returning an ok ... */

#if 1
  return KATCP_RESULT_PAUSE;
#else
  return KATCP_RESULT_OK;
#endif
}
#endif

/*********************************************************************************/

/* uses previously defined commands *********************************/

int setup_default_group(struct katcp_dispatch *d, char *name)
{
  struct katcp_group *gx;
  struct katcp_shared *s;
  struct katcp_cmd_map *m;

  s = d->d_shared;

  gx = create_group_katcp(d, name);
  if(gx == NULL){
    return -1;
  }

  if(gx->g_maps[KATCP_MAP_REMOTE_REQUEST] == NULL){
    m = create_cmd_map(name);
    if(m == NULL){
      destroy_group_katcp(d, gx);
      return -1;
    }

    gx->g_maps[KATCP_MAP_REMOTE_REQUEST] = m;
    hold_cmd_map(m);

    add_full_cmd_map_katcp(m, "help", "display help messages (?help [command])", 0, &help_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "watchdog", "pings the system (?watchdog)", 0, &watchdog_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "list-duplex", "display active connection detail (?list-duplex)", 0, &list_duplex_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "log-level", "retrieve or adjust the log level (?log-level [level])", 0, &log_level_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "log-local", "adjust the log level of the current connection (?log-default [level])", 0, &log_local_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "log-default", "retrieve or adjust the log level of subsequent connections (?log-default [level])", 0, &log_default_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "log-override", "retrieve or adjust the log level in various permutations (?log-override [level [client|group|default [name]]])", 0, &log_override_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "?version-list", "list versions (?version-list)", 0, &version_list_cmd_katcp, NULL, NULL);

#if 0
    add_full_cmd_map(m, "relay-watchdog", "ping a peer within the same process (?relay-watchdog peer)", 0, &relay_watchdog_group_cmd_katcp, NULL, NULL);
#endif
    add_full_cmd_map_katcp(m, "relay", "issue a request to a peer within the same process (?relay peer cmd)", 0, &relay_generic_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "?forward-symbolic", "create a command which generates a request against another party (?forward-symbolic)", 0, &forward_symbolic_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "client-list", "display currently connected clients (?client-list)", 0, &client_list_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "?client-rename", "rename a client (?client-rename new-name [old-name [group]])", 0, &client_rename_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "?client-halt", "stop a client (?client-halt [name [group]])", 0, &client_halt_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "?client-connect", "create a client to a remote host (?client-connect host:port [group])", 0, &client_connect_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "?client-exec", "create a client to a local process (?client-exec label [group [binary [args]*])", 0, &client_exec_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "?group-list", "list groups (?group-list)", 0, &group_list_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "?listener-create", "create a listener (?listener-create port [group]", 0, &listener_create_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "?listener-halt", "create a listener (?listener-halt port", 0, &listener_halt_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "?listener-list", "list listeners (?listener-list [port]", 0, &listener_list_group_cmd_katcp, NULL, NULL);




  } else {
    m = gx->g_maps[KATCP_MAP_REMOTE_REQUEST];
  }

  /* WARNING: duplicates map to field internal requests too */

  if(gx->g_maps[KATCP_MAP_INNER_REQUEST] == NULL){
    gx->g_maps[KATCP_MAP_INNER_REQUEST] = m;
    hold_cmd_map(m);
  }

  if(s->s_fallback == NULL){
    hold_group_katcp(gx);
    s->s_fallback = gx;
  }

  return 0;
}

#endif
