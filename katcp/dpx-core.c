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
#define DEBUG 1
#endif

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

static int pop_flat(struct katcp_dispatch *d, struct katcp_flat *fx);
static int push_flat(struct katcp_dispatch *d, struct katcp_flat *fx, unsigned int set);

#if 0
static void clear_current_flat(struct katcp_dispatch *d);
static void set_current_flat(struct katcp_dispatch *d, struct katcp_flat *fx);
#endif

#if 0
static int actually_set_output_flat_katcp(struct katcp_flat *fx, unsigned int destination, unsigned int persistence);
#endif

static void deallocate_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *f);

/********************************************************************/

/* groups of things *************************************************/

static int deallocate_group_katcp(struct katcp_dispatch *d, struct katcp_group *g)
{
  unsigned int i;

  if(g == NULL){
    return -1;
  }

  if(g->g_use > 0){
    g->g_use--;
    return 1;
  }

  if(g->g_count > 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "group destruction: group still in use (%u elements)\n", g->g_count);
    abort();
#endif
    return -1; 
  }

  if(g->g_region){
    destroy_region_katcp(d, g->g_region);
    g->g_region = NULL;
  }

  if(g->g_name){
    free(g->g_name);
    g->g_name = NULL;
  }

  g->g_flags = 0;

  for(i = 0; i < KATCP_SIZE_MAP; i++){
    destroy_cmd_map_katcp(g->g_maps[i]);
    g->g_maps[i] = NULL;
  }

  g->g_log_level = (-1);
  g->g_scope = KATCP_SCOPE_INVALID;

  g->g_autoremove = 0;

  g->g_flushdefer = 0;

  if(g->g_flats){
    free(g->g_flats);
    g->g_flats = NULL;
  }

  free(g);

  return 0;
}

void destroy_group_katcp(struct katcp_dispatch *d, struct katcp_group *g)
{
  /* WARNING: this function unlinks group from shared ... */
  struct katcp_shared *s;
  unsigned int i;

  s = d->d_shared;

#ifdef DEBUG
  fprintf(stderr, "group[%p]: destroying %s\n", g, g->g_name);
#endif

  if(deallocate_group_katcp(d, g)){
    return;
  }

  for(i = 0; (i < s->s_members) && (g != s->s_groups[i]); i++);

  s->s_members--;
  if(i < s->s_members){
    s->s_groups[i] = s->s_groups[s->s_members];
  } else {
    if(i > s->s_members){
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "logic problem: attempting to remove group %p not in list of groups\n", g);
      abort();
#endif
      s->s_members++;
    }
  }
}

void destroy_groups_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_group *gx;

  s = d->d_shared;

  if(s->s_fallback){
    destroy_group_katcp(d, s->s_fallback);
    s->s_fallback = NULL;
  }

  while(s->s_members > 0){
    gx = s->s_groups[s->s_members - 1];
    deallocate_group_katcp(d, gx);
    s->s_members--;
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
  g->g_flags = 0;

  for(i = 0; i < KATCP_SIZE_MAP; i++){
    g->g_maps[i] = NULL;
  }

  g->g_flats = NULL;
  g->g_count = 0;

  g->g_log_level = s->s_default;
  g->g_scope = KATCP_SCOPE_GROUP;

  g->g_use = 0;
  g->g_autoremove = 0;
  g->g_flushdefer = KATCP_FLUSH_DEFER;

  g->g_region = NULL;

  tmp = realloc(s->s_groups, sizeof(struct katcp_group *) * (s->s_members + 1));
  if(tmp == NULL){
    /* WARNING: destroy not yet appropriate, not yet in shared structure */
    deallocate_group_katcp(d, g);
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

  g->g_region = create_region_katcp(d);
  if(g->g_region == NULL){
    destroy_group_katcp(d, g);
    return NULL;
  }

  return g;
}

struct katcp_group *duplicate_group_katcp(struct katcp_dispatch *d, struct katcp_group *go, char *name, int depth)
{
  struct katcp_group *gx;
  int i;

  if(go == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "usage problem: given a null group to clone\n");
    abort();
#endif
    return NULL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "performing %s copy of %s %s", (depth > 0) ? "deep" : "linked", go->g_name ? "group" : "unnamed", go->g_name ? go->g_name : "group");

  gx = create_group_katcp(d, name);
  if(gx == NULL){
    return NULL;
  }

  gx->g_flags = go->g_flags;
  gx->g_log_level = go->g_log_level;
  gx->g_scope = go->g_scope;
  gx->g_flushdefer = go->g_flushdefer;

  for(i = 0; i < KATCP_SIZE_MAP; i++){
    if(go->g_maps[i]){
      if(depth > 0){
        /* unclear what the name of the copied map should be ... */
        gx->g_maps[i] = duplicate_cmd_map_katcp(go->g_maps[i], name);
        if(gx->g_maps[i] == NULL){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to copy command map during deep copy of group");
          destroy_group_katcp(d, gx);
          return NULL;
        }
      } else {
        gx->g_maps[i] = go->g_maps[i];
      }
      hold_cmd_map_katcp(gx->g_maps[i]);
    }
  }

  return gx;
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
  /* WARNING: maybe there are use-cases when we are not in the context of a duplex/flat, but still within a group - this doesn't cater for such a case */
  struct katcp_shared *s;
  struct katcp_flat *f;

  s = d->d_shared;
  if(s == NULL){
    return NULL;
  }

#if 0
  /* used inside log, can't call again */
  /* TODO: should have some protection logic */
  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "duplex: stack is %u", s->s_level);
#endif

  if(s->s_level < 0){
#if 0  /* used to be ifdef KATCP_CONSISTENCY_CHECKS, but verbose cause of group stuff */
    fprintf(stderr, "flat: level is %d, negative\n", s->s_level);
#endif

    /* now fall back on default group - that means everything happens within the context of a group ... even "system" level activity */
    return s->s_fallback;
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

#if 0

/* NOPE: not quite as simple as it seems, run_flat traverses group array, changing it is likely to result in unhappiness */

int change_group_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, struct katcp_group *gx)
{
  if((gx == NULL) || (fx == NULL)){
    return -1;
  }

  fx->f_group = gx;

  return -1;
}
#endif

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

int switch_group_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, struct katcp_group *gx)
{
  /* a somewhat high risk operation - the new group might be terminating, in which case we delay its shutdown */

  struct katcp_shared *s;
  struct katcp_group *gt;
  struct katcp_flat **tmp;
  unsigned int i;

  if((gx == NULL) || (fx == NULL)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "null arguments in change group operation");
    return -1;
  }

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  if(s->s_lock){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "refusing to change group as currently traversing group structure");
    return -1;
  }

  if(fx->f_group == gx){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "group membership unchanged");
    return -1;
  }

  gt = fx->f_group;
  if(gt == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "duplex: major logic problem: duplex %p has no group\n", fx);
    abort();
#endif
    return -1;
  }

  for(i = 0; (i < gt->g_count) && (gt->g_flats[i] != fx); i++);

  if(i >= gt->g_count){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "duplex: major logic problem: duplex %p not to be found it its putative group %p of %u elements\n", fx, gt, gt->g_count);
    abort();
#endif
    return -1;
  }

  tmp = realloc(gx->g_flats, sizeof(struct katcp_flat *) * (gx->g_count + 1));
  if(tmp == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable to increase size of target group containing %u elements", gx->g_count);
    return -1;
  }

  gx->g_flats = tmp;

  /* add to new group */
  gx->g_flats[gx->g_count] = fx;
  gx->g_count++;

  /* remove from old */
  gt->g_count--;
  if(i < gt->g_count){
    gt->g_flats[i] = gt->g_flats[gt->g_count];
  }

  /* update ownership */
  fx->f_group = gx;

#if 0
  /* maybe ? */
  reconfigure_flat_katcp(d, f, flags);
#endif

  return 0;
}

static int stop_listener_from_group_katcp(struct katcp_dispatch *d, struct katcp_arb *a, void *data)
{
  struct katcp_listener *kl;
  struct katcp_group *gx;

  gx = data;
  kl = data_arb_katcp(d, a);

  /* WARNING: close coupling, peers into listener internals from group logic, needs to be kept in sync with destroy_listen_flat_katcp */

  if(kl->l_group == gx){
    unlink_arb_katcp(d, a);
  }

  return 0;
}

int terminate_group_katcp(struct katcp_dispatch *d, struct katcp_group *gx, int hard)
{
  unsigned int i;
  struct katcp_flat *fx;
  int result;

  if(gx == NULL){
    return -1;
  }

  result = 0;

  for(i = 0; i < gx->g_count; i++){
    fx = gx->g_flats[i];
    if(terminate_flat_katcp(d, fx) < 0){
      result = (-1);
    }
  }

  if(hard){
    foreach_arb_katcp(d, KATCP_ARB_TYPE_LISTENER, &stop_listener_from_group_katcp, gx);
    gx->g_autoremove = 1;
  }

  return result;
}

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

void shutdown_duplex_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  if(d == NULL){
    return;
  }
  
  s = d->d_shared;
  if(s == NULL){
    return;
  }

  destroy_flats_katcp(d);
  destroy_groups_katcp(d);
  release_endpoints_katcp(d);

  if(s->s_region){
    destroy_region_katcp(d, s->s_region);
    s->s_region = NULL;
  }
}

int startup_duplex_katcp(struct katcp_dispatch *d, unsigned int stories)
{
#define BUFFER 128
  struct katcp_shared *s;
  char buffer[BUFFER];
  
  if(d == NULL){
    return -1;
  }

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(s->s_groups || s->s_fallback || s->s_this || s->s_endpoints || s->s_region){
    fprintf(stderr, "duplex: major logic problem: duplex data structures not empty at initialisation\n");
    abort();
  }
#endif

  s->s_stories = 0;
  s->s_level = (-1);

  s->s_this = malloc(sizeof(struct katcp_flat *) * stories);
  if(s->s_this == NULL){
    shutdown_duplex_katcp(d);
    return -1;
  }

  s->s_stories = stories;

  if(setup_default_group(d, "default") < 0){
    shutdown_duplex_katcp(d);
    return -1;
  }

  s->s_region = create_region_katcp(d);
  if(s->s_region == NULL){
    shutdown_duplex_katcp(d);
    return -1;
  }

  snprintf(buffer, BUFFER - 1, "%d.%d-%c", KATCP_PROTOCOL_MAJOR_VERSION, KATCP_PROTOCOL_MINOR_VERSION, 'M');
  buffer[BUFFER - 1] = '\0';
  make_string_vrbl_katcp(d, NULL, KATCP_PROTOCOL_LABEL, KATCP_VRF_VER, buffer);

#ifdef VERSION
  snprintf(buffer, BUFFER - 1, "%s-%s", KATCP_CODEBASE_NAME, VERSION);
  buffer[BUFFER - 1] = '\0';
  make_string_vrbl_katcp(d, NULL, KATCP_LIBRARY_LABEL, KATCP_VRF_VER, buffer);
#endif

  return 0;
#undef BUFFER  
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

  if(f->f_name){
    free(f->f_name);
    f->f_name = NULL;
  }

  f->f_max_defer = 0;
  f->f_deferring = (KATCP_DEFER_OUTSIDE_REQUEST | KATCP_DEFER_OWN_REQUEST);
  if(f->f_defer){
    destroy_gueue_katcl(f->f_defer);
    f->f_defer = NULL;
  }

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
    destroy_cmd_map_katcp(f->f_maps[i]);
    f->f_maps[i] = NULL;
  }

  f->f_group = NULL;

  /* WARNING: might be awkward if there a callbacks which assume a valid flat ... maybe move this code higher up ... */

  if(f->f_region){
    destroy_region_katcp(d, f->f_region);
    f->f_region = NULL;
  }

  f->f_log_level = (-1);
  f->f_scope = KATCP_SCOPE_INVALID;
  f->f_stale = 0;

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
#if 0
      /* have a destroy_group_call here if we decide to increment hold count for group on flat creation */
#endif
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

  type = str[0];
  source = source_endpoint_katcp(d, msg);
  rh = NULL;

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "received a message %s ... (message source endpoint %p, remote %p)", str, source, fx->f_remote);

  switch(type){
    case KATCP_REQUEST :
      /* we respond to requests */
      fx->f_current_endpoint = source;
      fx->f_current_map = (source == fx->f_remote) ? KATCP_MAP_REMOTE_REQUEST : KATCP_MAP_INNER_REQUEST;
      break;

    case KATCP_INFORM  :
      fx->f_current_endpoint = source;
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

  push_flat(d, fx, 1);
#if 0
  set_current_flat(d, fx);
#endif

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

  pop_flat(d, fx);
#if 0
  clear_current_flat(d);
#endif

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
  struct katcl_parse *px, *pt, *pq;
  int result, request, reply;
  unsigned int size, limit;

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

  /* API ugly, could do with improvements */
  request = is_request_parse_katcl(px);
  reply   = is_reply_parse_katcl(px);

  result = append_parse_katcl(fx->f_line, px);
  /* WARNING: do something with the return code */
  
  if(request > 0){

    /* TODO: set timeout here ... */

    if(fx->f_deferring & KATCP_DEFER_OWN_REQUEST){
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "behaving antisocially and piplelining a request to %s", fx->f_name);
    }
    fx->f_deferring |= KATCP_DEFER_OWN_REQUEST;
  }

  if(reply > 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    if((fx->f_deferring & KATCP_DEFER_OUTSIDE_REQUEST) == 0) {
      fprintf(stderr, "dpx[%p]: major logic problem - send a reply to %s where no request outstanding (defer flags 0x%x)\n", fx, fx->f_name, fx->f_deferring);
      abort();
    }
#endif

    size = size_gueue_katcl(fx->f_defer);

    if(size > 0){

      limit = fx->f_group ? fx->f_group->g_flushdefer : KATCP_FLUSH_DEFER;

      if(size > limit){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cancelling %u outstanding requests as pipeline limit is %u", size, limit);
        while((pt = remove_head_gueue_katcl(fx->f_defer))){
          pq = turnaround_extra_parse_katcl(pt, KATCP_RESULT_FAIL, "cancelled");
          if(pq){
            /* TODO - failure to append is also a problem */
            append_parse_katcl(fx->f_line, pq);
            destroy_parse_katcl(pq);
          } else {
            fx->f_state = FLAT_STATE_CRASHING;
          }
#if 0
          /* WARNING: no destroy needed, turnaround invalidates its arg, as it may be reused in pq */
          destroy_parse_katcl(pt);
#endif
        }
      } else {
        pt = remove_head_gueue_katcl(fx->f_defer);
        if(pt){
          if(send_message_endpoint_katcp(d, fx->f_remote, fx->f_peer, pt, 1) < 0){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to enqueue remote message");

            fx->f_state = FLAT_STATE_CRASHING;
          }
          destroy_parse_katcl(pt);
        } else {
#ifdef KATCP_CONSISTENCY_CHECKS
          fprintf(stderr, "dpx[%p]: major logic problem - deferred queue of %s contained a NULL element\n", fx, fx->f_name);
          abort();
#endif
        }
      }
    } else {
#ifdef DEBUG
      fprintf(stderr, "dpx[%p]: clearing request flag from 0x%x\n", fx, fx->f_deferring);
#endif
      fx->f_deferring &= (~KATCP_DEFER_OUTSIDE_REQUEST);
    }
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

struct katcp_flat *create_exec_flat_katcp(struct katcp_dispatch *d, unsigned int flags, char *name, struct katcp_group *gx, char **vector)
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

    fx = create_flat_katcp(d, fds[1], flags, name, gx);
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

int reconfigure_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, unsigned int flags)
{
  struct katcp_group *gx;

  /* update setttings, either at creation or while running */

#ifdef DEBUG
  fprintf(stderr, "dpx: reconfiguring %p with flags 0x%x\n", fx, flags);
#endif

  gx = fx->f_group;
  if(gx == NULL){
    return -1;
  }

  if(flags & KATCP_FLAT_TOCLIENT){
    if(fx->f_log_level == KATCP_LEVEL_OFF){
      fx->f_log_level = gx->g_log_level;
    }
  } else {
    /* WARNING: we assume servers aren't interested in our log messages */
    fx->f_log_level = KATCP_LEVEL_OFF;
  }

  if((flags & KATCP_FLAT_TOCLIENT) && ((fx->f_flags & KATCP_FLAT_TOCLIENT) == 0)){
    trigger_connect_flat(d, fx);
  }

  fx->f_flags = flags & (KATCP_FLAT_TOSERVER | KATCP_FLAT_TOCLIENT | KATCP_FLAT_HIDDEN | KATCP_FLAT_PREFIXED);

  return 0;
}

struct katcp_flat *create_flat_katcp(struct katcp_dispatch *d, int fd, unsigned int flags, char *name, struct katcp_group *g)
{
  /* TODO: what about cloning an existing one to preserve misc settings, including log level, etc */
  struct katcp_flat *f, **tmp;
  struct katcp_shared *s;
  struct katcp_group *gx;
  unsigned int i, mask, set;

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
  f->f_state = (flags & KATCP_FLAT_CONNECTING) ? FLAT_STATE_CONNECTING : FLAT_STATE_UP;

  f->f_exit_code = 0; /* WARNING: should technically be a fail, to catch cases where it isn't set at exit time */

  f->f_log_level = gx->g_log_level;

  f->f_scope = gx->g_scope;
  f->f_stale = KATCP_STALE_SENSOR_NAIVE;

  f->f_max_defer = 0;
  f->f_deferring = 0;
  f->f_defer = NULL;

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

  f->f_region = NULL;

  if(name){
    f->f_name = strdup(name);
    if(f->f_name == NULL){
      destroy_flat_katcp(d, f);
      return NULL;
    }
  }

  f->f_defer = create_parse_gueue_katcl();
  if(f->f_defer == NULL){
    destroy_flat_katcp(d, f);
    return NULL;
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

  f->f_region = create_region_katcp(d);
  if(f->f_region == NULL){
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

#if 0
  /* WARNING: use count is actually g_use+g_count */
  hold_group_katcp(gx); 
#endif

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "created instance for %s", name ? name : "<anonymous");

  set  = 0;
  mask = 0;

  if(gx->g_flags & KATCP_GROUP_OVERRIDE_SENSOR){
    if(gx->g_flags & KATCP_FLAT_PREFIXED){
      set = KATCP_FLAT_PREFIXED;
    } else {
      mask = KATCP_FLAT_PREFIXED;
    }
  }

  reconfigure_flat_katcp(d, f, (flags & (~mask)) | set);

  return f;
}

/* auxillary calls **************************************************/

static int broadcast_group_katcp(struct katcp_dispatch *d, struct katcp_group *gx, struct katcl_parse *px, int (*check)(struct katcp_dispatch *d, struct katcp_flat *fx, void *data), void *data)
{
  int i, sum;
  struct katcp_flat *fx;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(gx == NULL){
    fprintf(stderr, "major logic problem: broadcast to a group requires a group\n");
    abort();
  }
  if(px == NULL){
    fprintf(stderr, "major logic problem: broadcast requires a message to broadcast\n");
    abort();
  }
#endif

  sum = 0;

  for(i = 0; i < gx->g_count; i++){
    fx = gx->g_flats[i];
    if((check == NULL) || ((*(check))(d, fx, data) == 0)){
      /* ... eh, dis be horrible - reaching directly into fx - shouldn't we use the endpoints to queue messages, ... or is that just needlessly inefficient ? log_message uses a the same approach as here ... */
      if(append_parse_katcl(fx->f_line, px) < 0){
        sum = (-1);
      } else {
        if(sum >= 0){
          sum++;
        }
      }
    }
  }

  return sum;
}

int broadcast_flat_katcp(struct katcp_dispatch *d, struct katcp_group *gx, struct katcl_parse *px, int (*check)(struct katcp_dispatch *d, struct katcp_flat *fx, void *data), void *data)
{
  int i, sum, result;
  struct katcp_shared *s;

  s = d->d_shared;

  if(gx){
    return broadcast_group_katcp(d, gx, px, check, data);
  }

  sum = 0; /* assume things went ok */

  for(i = 0; i < s->s_members; i++){
    result = broadcast_group_katcp(d, s->s_groups[i], px, check, data);
    if(result >= 0){
      if(sum >= 0){
        sum += result;
      }
    } else {
      sum = (-1);
    }
  }

  return sum;
}

static struct katcp_flat *search_name_flat_katcp(struct katcp_dispatch *d, char *name, struct katcp_group *gx, int limit)
{
  struct katcp_flat *fx;
  struct katcp_group *gt, *gr;
  struct katcp_shared *s;
  unsigned int i, j;

  s = d->d_shared;

  if(name == NULL){
    return NULL;
  }

  if(gx == NULL){
    gr = this_group_katcp(d);
  } else {
    gr = gx;
  }

  if(gr){
    for(i = 0; i < gr->g_count; i++){
      fx = gr->g_flats[i];
      if(fx->f_name && (strcmp(name, fx->f_name) == 0)){
        return fx;
      }
    }
  }

  if(limit && (gr != NULL)){
    /* WARNING: risky - if user can persuade us not to find gr, then can search global scope */
    return NULL;
  }

  for(j = 0; j < s->s_members; j++){
    gt = s->s_groups[j];
    if(gt == gr){
      /* already examined ... */
      continue;
    }
    for(i = 0; i < gt->g_count; i++){
      fx = gt->g_flats[i];
      if(fx->f_name && (strcmp(name, fx->f_name) == 0)){
        return fx;
      }
    }
  }

  return NULL;
}

struct katcp_flat *find_name_flat_katcp(struct katcp_dispatch *d, char *group, char *name, int limit)
{
  struct katcp_group *gx;
#if 0
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
#endif

  if(group){
    gx = find_group_katcp(d, group);
    if(gx == NULL){
      if(limit){
        return NULL;
      }
    }
  } else {
    gx = NULL;
  }

  return search_name_flat_katcp(d, name, gx, limit);
}

struct katcp_flat *scope_name_full_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, char *group, char *name)
{
  struct katcp_flat *fy;
  struct katcp_group *gy;
  int limit;

  if(name == NULL){
    return NULL;
  }

  if(fx == NULL){
    fy = this_flat_katcp(d);
    if(fy == NULL){
      return NULL;
    }
  } else {
    fy = fx;
  }

  switch(fy->f_scope){
    case KATCP_SCOPE_SINGLE :
      if(fy->f_name == NULL){
        return NULL;
      }
      if(strcmp(name, fy->f_name)){
        return NULL;
      }
      return fy;
    case KATCP_SCOPE_GROUP :
      gy = fy->f_group;
#ifdef KATCP_CONSISTENCY_CHECKS
      if(gy == NULL){
        fprintf(stderr, "logic problem: flat %p does not have a group\n", fy);
        abort();
      }
#endif
      if(group){
        if(gy->g_name == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
          fprintf(stderr, "possible logic problem: group %p does not have a name\n", gy);
#endif
          return NULL;
        }
        if(strcmp(group, gy->g_name)){
          return NULL;
        }
      }
      return search_name_flat_katcp(d, name, gy, 1);
    case KATCP_SCOPE_GLOBAL :
      gy = NULL;
      if((group == NULL) || (strcmp(group, "*"))){ /* WARNING: '*' now means any group ... */
        gy = fy->f_group;
        limit = 0;
      } else {
        gy = find_group_katcp(d, group);
        if(gy == NULL){
          return NULL;
        }
        limit = 1;
      }
      return search_name_flat_katcp(d, name, gy, limit);
    default :
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "logic problem: flat %p has scope value %d which is invalid\n", fy, fy->f_scope);
      abort();
#endif
      return NULL;
  }
}

#if 0
/* superceeded by scope_name_full ... */
struct katcp_flat *scope_name_flat_katcp(struct katcp_dispatch *d, char *name, struct katcp_flat *fx)
{
  if((fx == NULL) || (name == NULL)){
    return NULL;
  }

  switch(fx->f_scope){
    case KATCP_SCOPE_SINGLE :
      if(fx->f_name == NULL){
        return NULL;
      }
      if(strcmp(name, fx->f_name)){
        return NULL;
      }
      return fx;
    case KATCP_SCOPE_GROUP :
#ifdef KATCP_CONSISTENCY_CHECKS
      if(fx->f_group == NULL){
        fprintf(stderr, "logic problem: flat %p does not have a group\n", fx);
        abort();
      }
#endif
      return search_name_flat_katcp(d, name, fx->f_group, 1);
    case KATCP_SCOPE_GLOBAL :
      return search_name_flat_katcp(d, name, fx->f_group, 0);
    default :
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "logic problem: flat %p has scope value %d which is invalid\n", fx, fx->f_scope);
      abort();
#endif
      return NULL;
  }
}
#endif

struct katcp_group *scope_name_group_katcp(struct katcp_dispatch *d, char *name, struct katcp_flat *fx)
{
  struct katcp_group *gx;

  if((fx == NULL) || (name == NULL)){
    return NULL;
  }

  gx = fx->f_group;
  if(gx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "logic problem: flat %p has no group\n", fx);
    abort();
#endif
    return NULL;
  }

  switch(fx->f_scope){
    case KATCP_SCOPE_GROUP :
    case KATCP_SCOPE_SINGLE :
      if(fx->f_name == NULL){
        return NULL;
      }
      if(gx->g_name){
        return NULL;
      }
      if(strcmp(name, gx->g_name)){
        return NULL;
      }
      return gx;
    case KATCP_SCOPE_GLOBAL :
      return find_group_katcp(d, name);
    default :
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "logic problem: flat %p has scope value %d which is invalid\n", fx, fx->f_scope);
      abort();
#endif
      return NULL;
  }
}

/*********************/


int version_generic_callback_katcp(struct katcp_dispatch *d, void *state, char *key, struct katcp_vrbl *vx)
{
  /* WARNING: does cleverness, where state is NULL assumes to be called from connect trigger, else assumes list command */
  struct katcp_vrbl_payload *pversion, *pbuild;
  unsigned int *cp;
  int type;

  cp = state;

#ifdef DEBUG
  fprintf(stderr, "version: about to consider variable %p\n", vx);
#endif

  if(vx == NULL){
    return -1;
  }

  if((vx->v_flags & KATCP_VRF_VER) == 0){
#ifdef DEBUG
    fprintf(stderr, "version: %p not a version variable\n", vx);
#endif
    return 0;
  }

  if(vx->v_flags & KATCP_VRF_HID){
#ifdef DEBUG
    fprintf(stderr, "version: version variable %p currently hidden\n", vx);
#endif
    return 0;
  }

  type = find_type_vrbl_katcp(d, vx, NULL);
  switch(type){
    case KATCP_VRT_TREE :

      pversion = find_payload_katcp(d, vx, KATCP_VRC_VERSION_VERSION);
      pbuild = find_payload_katcp(d, vx, KATCP_VRC_VERSION_BUILD);

      if(pversion){
        if(type_payload_vrbl_katcp(d, vx, pversion) != KATCP_VRT_STRING){
          pversion = NULL;
        }
      }
      if(pbuild){
        if(type_payload_vrbl_katcp(d, vx, pbuild) != KATCP_VRT_STRING){
          pbuild = NULL;
        }
      }

      if(pversion || pbuild){

        if(state){ /* WARNING: abuse of this variable */
          prepend_inform_katcp(d);
        } else {
          append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_VERSION_CONNECT_INFORM);
        }

        append_string_katcp(d, KATCP_FLAG_STRING, key);
        if(pbuild){
          if(pversion){
            append_payload_vrbl_katcp(d, 0, vx, pversion);
          } else {
            append_string_katcp(d, KATCP_FLAG_STRING, "unknown");
          }
          append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, pbuild);
        } else {
          append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, pversion);
        }
        if(state){
          *cp = (*cp) + 1;
        }
      }

      return 0;

    case KATCP_VRT_ARRAY : 
      /* dumps the entire array - we assume there are only two elements in there */

    case KATCP_VRT_STRING :
      if(state){ /* WARNING: abuse of this variable */
        prepend_inform_katcp(d);
      } else {
        append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_VERSION_CONNECT_INFORM);
      }
      append_string_katcp(d, KATCP_FLAG_STRING, key);
      append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, NULL);
      if(state){
        *cp = (*cp) + 1;
      }
      return 0;

    default :
      return 0;
  }

  return 0;
}

#if 0
int version_connect_callback_katcp(struct katcp_dispatch *d, void *state, char *key, struct katcp_vrbl *vx)
{
  struct katcp_payload *pversion, *pbuild:
  int type;

#ifdef DEBUG
  fprintf(stderr, "version: about to consider variable %p\n", vx);
#endif

  if(vx == NULL){
    return -1;
  }

  if((vx->v_flags & KATCP_VRF_VER) == 0){
#ifdef DEBUG
    fprintf(stderr, "version: %p not a version variable\n", vx);
#endif
    return 0;
  }

  append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_VERSION_CONNECT_INFORM);
  append_string_katcp(d, KATCP_FLAG_STRING, key);
  append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, NULL);

  /* WARNING: this duplicates too much from version_list, but that uses prepend_inform */

  type = find_type_vrbl_katcp(d, vx, NULL);
  switch(type){
    case KATCP_VRT_STRING :
      append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_VERSION_CONNECT_INFORM);
      append_string_katcp(d, KATCP_FLAG_STRING, key);
      append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, NULL);
      *cp = (*cp) + 1;
      return 0;
    case KATCP_VRT_TREE :

      pversion = find_payload_katcp(d, vx, KATCP_VRC_VERSION_VERSION);
      pbuild = find_payload_katcp(d, vx, KATCP_VRC_VERSION_BUILD);

      if(pversion){
        if(type_payload_vrbl_katcp(d, vx, pversion) != KATCP_VRT_STRING){
          pversion = NULL;
        }
      }
      if(pbuild){
        if(type_payload_vrbl_katcp(d, vx, pbuild) != KATCP_VRT_STRING){
          pbuild = NULL;
        }
      }

      if(pversion || pbuild){
        prepend_inform_katcp(d);
        append_string_katcp(d, KATCP_FLAG_STRING, key);
        if(pbuild){
          if(pversion){
            append_payload_vrbl_katcp(d, 0, vx, pversion);
          } else {
            append_string_katcp(d, KATCP_FLAG_STRING, "unknown");
          }
          append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, pbuild);
        } else {
          append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, pversion);
        }
        *cp = (*cp) + 1;
      }

      return 0;

    default :
      return 0;
  }

  return 0;
}
#endif

int version_connect_void_callback_katcp(struct katcp_dispatch *d, void *state, char *key, void *data)
{
  struct katcp_vrbl *vx;

  vx = data;

  return version_generic_callback_katcp(d, state, key, vx);
}

int trigger_connect_flat(struct katcp_dispatch *d, struct katcp_flat *fx)
{
#if 0
  struct katcp_group *gx;
#endif
  struct katcp_endpoint *save;
  
  if(fx == NULL){
    return -1;
  }

  if(push_flat(d, fx, 0)){
    return -1;
  }

  save = fx->f_current_endpoint;
  fx->f_current_endpoint = fx->f_remote;

#ifdef DEBUG
  fprintf(stderr, "about to run version callback\n");
#endif

  /* TODO: there could be some group specific logic */

  traverse_vrbl_katcp(d, NULL, &version_connect_void_callback_katcp);

  fx->f_current_endpoint = save;

  return pop_flat(d, fx);
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

  fx = find_name_flat_katcp(d, group, was, 0);
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

#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: terminating %s in state %d\n", fx, fx->f_name, fx->f_state);
#endif

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

struct katcp_endpoint *handler_of_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx)
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

struct katcp_endpoint *sender_to_flat_katcp(struct katcp_dispatch *d, struct katcp_flat *fx)
{
  if(fx == NULL){
    return NULL;
  }

  return fx->f_current_endpoint;
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
    return NULL;
  }
  if(fx->f_current_map >= KATCP_SIZE_MAP){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "major logic failure: current map index %d is corrupt\n", fx->f_current_map);
    abort();
#endif
    return NULL;
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

static int push_flat(struct katcp_dispatch *d, struct katcp_flat *fx, unsigned int set)
{
  struct katcp_shared *s;

  s = d->d_shared;
#ifdef KATCP_CONSISTENCY_CHECKS
  if((s == NULL) || (s->s_this == NULL)){
    fprintf(stderr, "major logic problem: no duplex stack initialised\n");
    abort();
  }

  if(set){
    if(s->s_level >= 0){
      fprintf(stderr, "logic problem: setting a current duplex [%d]=%p while one is %p\n", s->s_level, fx, s->s_this[s->s_level]);
      abort();
    }
  }
#endif

  if(s->s_level >= s->s_stories){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "size problem: level=%d, stories=%u\n", s->s_level, s->s_stories);
    abort();
#endif
    return -1;
  }

  s->s_level++;
#ifdef DEBUG
  fprintf(stderr, "push: [%d] <- %p\n", s->s_level, fx);
#endif
  s->s_this[s->s_level] = fx;

  return 0;
}

static int pop_flat(struct katcp_dispatch *d, struct katcp_flat *fx)
{
  struct katcp_shared *s;

  s = d->d_shared;
  if(s->s_level < 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "logic problem: attempting to pop from an empty stack\n");
    abort();
#endif
    if(fx){
      return -1;
    } else {
      return 0;
    }
  }

  if(fx){
    if(s->s_this[s->s_level] != fx){
      return -1;
    }
  } 

  s->s_this[s->s_level] = NULL;
  s->s_level--;

  return 0;
}

#if 0
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
#endif

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
#ifndef KATCP_CONSISTENCY_CHECKS
      i = KATCP_SIZE_REPLY;
      /* break out of loop */
#else 
    } else {
      /* is somebody is sending something to this party ? */
      if(rh->r_recipient && (rh->r_recipient == recipient)){
        /* is it the same sender ? */
        if(rh->r_issuer == issuer){
          fprintf(stderr, "logic problem: issuer %p has already a callback in flight to %p, should not pipeline another one\n", issuer, recipient);
          abort();
        }
      }
#endif
    }
  }

  if(slot >= KATCP_SIZE_REPLY){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "resource problem: no free reply handler slots (%u in use) in client %s for callback %s", slot, fx->f_name, ptr);
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "problem: no free callback slots\n");
#endif
    return -1;
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

int append_payload_vrbl_flat_katcp(struct katcp_dispatch *d, int flags, struct katcp_vrbl *vx, struct katcp_vrbl_payload *py)
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

  result = add_payload_vrbl_katcp(d, px, flags, vx, py);

  return finish_append_flat_katcp(d, flags, result);
}

int append_timestamp_flat_katcp(struct katcp_dispatch *d, int flags, struct timeval *tv)
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

  result = add_timestamp_parse_katcl(px, flags, tv);

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

int append_end_flat_katcp(struct katcp_dispatch *d)
{
  struct katcl_parse *px;
  struct katcp_flat *fx;
  int result;

  fx = require_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = prepare_append_flat_katcp(fx, KATCP_FLAG_LAST);
  if(px == NULL){
    return -1;
  }

  result = add_end_parse_katcl(px);

  return finish_append_flat_katcp(d, KATCP_FLAG_LAST, result);
}

/**************************************************************************/
/* mainloop related logic *************************************************/

int load_flat_katcp(struct katcp_dispatch *d)
{
  struct katcp_flat *fx;
  struct katcp_shared *s;
  struct katcp_group *gx;
  unsigned int i, j, inc, jnc;
  int result, fd;

  s = d->d_shared;

  result = 0;

#ifdef DEBUG 
  fprintf(stderr, "dpx[*]: loading %u groups\n", s->s_members);
#endif

#ifdef KATCP_CONSISTENCY_CHECKS
  if(s->s_lock){
    fprintf(stderr, "dpx[*]: group structures should not be locked\n");
    abort();
  }
#endif
  s->s_lock = 1;

  j = 0;
  while(j < s->s_members){
    jnc = 1;
    gx = s->s_groups[j];

    i = 0;
    while(i < gx->g_count){
      inc = 1;

      fx = gx->g_flats[i];
      fd = fileno_katcl(fx->f_line);

#ifdef DEBUG
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
            close_receiving_endpoint_katcp(d, fx->f_peer);
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

    if((gx->g_autoremove > 0) && (gx->g_count == 0) && (gx->g_use == 0)){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "about to remove group %s", gx->g_name);
#ifdef DEBUG 
      fprintf(stderr, "group[%p]: %s about to be removed\n", gx, gx->g_name);
#endif

      deallocate_group_katcp(d, gx);
      jnc  = 0;

      s->s_members--;

#ifdef DEBUG 
      fprintf(stderr, "dpx[*]: reduced to %u groups\n", s->s_members);
#endif
    }

    j += jnc;
  }

  s->s_lock = 0;

  return result;
}

int run_flat_katcp(struct katcp_dispatch *d)
{
  struct katcp_flat *fx;
  struct katcp_shared *s;
  struct katcl_parse *px, *pt;
  struct katcp_group *gx;
  unsigned int i, j, len, size, limit;
  int fd, result, code, reply, request;
  char *name, *ptr;

  s = d->d_shared;

#if DEBUG > 2
  fprintf(stderr, "dpx[*]: running %u groups\n", s->s_members);
#endif

#ifdef KATCP_CONSISTENCY_CHECKS
  if(s->s_lock){
    fprintf(stderr, "dpx[*]: group structures should not be locked\n");
    abort();
  }
#endif
  s->s_lock = 1;

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

            request = is_request_parse_katcl(px);
            reply   = is_reply_parse_katcl(px);
            name    = get_string_parse_katcl(px, 0);
            if(name){
              ptr  = name + 1;
            } else {
              ptr = "<null>";
            }

            if(reply > 0){
#ifdef KATCP_CONSISTENCY_CHECKS
              if((fx->f_deferring & KATCP_DEFER_OWN_REQUEST) == 0){
                log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "saw a reply %s from %s where no request was outstanding", ptr, fx->f_name);
              }
#endif
              /* presume to have serviced out our own request - might not be the case if it is a nonmatching reply ... */
              fx->f_deferring &= (~KATCP_DEFER_OWN_REQUEST);
            }

            pt = NULL;
            if(request > 0){
              if(fx->f_deferring & KATCP_DEFER_OUTSIDE_REQUEST){
                pt = copy_parse_katcl(px);
                if(pt == NULL){
                  fx->f_state = FLAT_STATE_CRASHING;
                  pt = px; /* horrible abuse, there to suppress sending of message to peer queue */
                } else {
                  if(add_tail_gueue_katcl(fx->f_defer, pt) < 0){
                    destroy_parse_katcl(pt);
                    fx->f_state = FLAT_STATE_CRASHING;
                  } else {
                    size = size_gueue_katcl(fx->f_defer);
                    if(size > fx->f_max_defer){
                      limit = fx->f_group ? fx->f_group->g_flushdefer : KATCP_FLUSH_DEFER;
                      if(size > limit){
                        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "client %s has exceeded pipeline limit %u with a new record of %u requests without waiting for a reply while issuing %s", fx->f_name, limit, size, ptr);
                      } else {
                        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "iffy client behaviour from %s which is issuing %s request which is a new record of %u requests without waiting for a reply", fx->f_name, ptr, size);
                      }
                      fx->f_max_defer = size;
                    }
                  }
                }
              }
#ifdef DEBUG
              fprintf(stderr, "dpx[%p]: saw external request: setting 0x%x on 0x%x\n", fx, KATCP_DEFER_OUTSIDE_REQUEST, fx->f_deferring);
#endif

              fx->f_deferring |= KATCP_DEFER_OUTSIDE_REQUEST;
            }

            if(pt == NULL){
              log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "sending network message to endpoint %p", fx->f_peer);

              if(send_message_endpoint_katcp(d, fx->f_remote, fx->f_peer, px, (request > 0) ? 1 : 0) < 0){
                log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to enqueue remote message");

                fx->f_state = FLAT_STATE_CRASHING;

                /* WARNING: drops out of parsing loop */
                break;
              }
            }

            show_endpoint_katcp(d, "peer", KATCP_LEVEL_TRACE, fx->f_peer);

            clear_katcl(fx->f_line);
          }
        }

      }
    }
  }

  s->s_lock = 0;

  return s->s_members;
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
  source = handler_of_flat_katcp(d, fx);

  fx = find_name_flat_katcp(d, group, name, 0);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not look up name %s", name);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "resolver");
  }
  target = handler_of_flat_katcp(d, fx);

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
    m = create_cmd_map_katcp(name);
    if(m == NULL){
      destroy_group_katcp(d, gx);
      return -1;
    }

    gx->g_maps[KATCP_MAP_REMOTE_REQUEST] = m;
    hold_cmd_map_katcp(m);

    add_full_cmd_map_katcp(m, "help", "display help messages (?help [command])", 0, &help_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "watchdog", "pings the system (?watchdog)", 0, &watchdog_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "list-duplex", "display active connection detail (?list-duplex)", 0, &list_duplex_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "log-level", "retrieve or adjust the log level (?log-level [level])", 0, &log_level_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "log-local", "adjust the log level of the current connection (?log-local [level])", 0, &log_local_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "log-limit", "adjust the log level of the current connection (?log-limit [level])", 0, &log_local_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "log-default", "retrieve or adjust the log level of subsequent connections (?log-default [level])", 0, &log_default_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "log-override", "retrieve or adjust the log level in various permutations (?log-override [level [client|group|default [name]]])", 0, &log_override_group_cmd_katcp, NULL, NULL);

#if 0
    add_full_cmd_map_katcp(m, "?version-list", "list versions (?version-list)", 0, &version_list_cmd_katcp, NULL, NULL);

    add_full_cmd_map(m, "relay-watchdog", "ping a peer within the same process (?relay-watchdog peer)", 0, &relay_watchdog_group_cmd_katcp, NULL, NULL);
#endif

    add_full_cmd_map_katcp(m, "relay", "issue a request to a peer within the same process (?relay peer cmd)", 0, &relay_generic_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "forward-symbolic", "create a command which generates a request against another party (?forward-symbolic command remote [remote-command])", 0, &forward_symbolic_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "client-list", "display currently connected clients (?client-list)", 0, &client_list_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "client-rename", "rename a client (?client-rename new-name [old-name [group]])", 0, &client_rename_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "client-halt", "stop a client (?client-halt [name [group]])", 0, &client_halt_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "client-connect", "create a client to a remote host (?client-connect host:port [group])", 0, &client_connect_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "?client-exec", "create a client to a local process (?client-exec label [group [binary [args]*])", 0, &client_exec_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "client-config", "set a client option (?client-config option [client])", 0, &client_config_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "client-switch", "set a client option (?client-switch group [client])", 0, &client_switch_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "group-create", "create a new group (?group-create name [group])", 0, &group_create_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "group-list", "list groups (?group-list)", 0, &group_list_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "group-halt", "halt a group (?group-halt [group])", 0, &group_halt_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "group-config", "set a group option (?group-config option [group])", 0, &group_config_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "listener-create", "create a listener (?listener-create label [port [interface [group]]])", 0, &listener_create_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "listener-halt", "stop a listener (?listener-halt port)", 0, &listener_halt_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "listener-list", "list listeners (?listener-list [label])", 0, &listener_list_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "restart", "restart (?restart)", 0, &restart_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "halt", "halt (?halt)", 0, &halt_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "cmd-hide", "hide a command (?cmd-hide command)", 0, &hide_cmd_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "cmd-uncover", "reveal a hidden command (?cmd-uncover command)", 0, &uncover_cmd_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "cmd-delete", "remove a command (?cmd-delete command)", 0, &delete_cmd_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "cmd-help", "set the help message for a command (?cmd-help command message)", 0, &help_cmd_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "sensor-list", "lists available sensors (?sensor-list [sensor])", 0, &sensor_list_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "sensor-value", "query a sensor (?sensor-value sensor)", 0, &sensor_value_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "sensor-sampling", "configure a sensor (?sensor-sampling sensor [strategy [parameter]])", 0, &sensor_sampling_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "var-declare", "declare a variable (?var-declare name attribute[,attribute]* [path])", 0, &var_declare_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "var-list", "list variables (?var-list [variable])", 0, &var_list_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "var-set", "set a variable (?var-set variable value [type [path]])", 0, &var_set_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "var-delete", "remove a variable (?var-set variable)", 0, &var_delete_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "version-list", "list version information (?version-list)", 0, &version_list_group_cmd_katcp, NULL, NULL);

    add_full_cmd_map_katcp(m, "scope", "change scoping level (?scope scope [(group | client) name])", 0, &scope_group_cmd_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "broadcast", "send messages to everybody (?broadcast group inform [arguments])", 0, &broadcast_group_cmd_katcp, NULL, NULL);

  } else {
    m = gx->g_maps[KATCP_MAP_REMOTE_REQUEST];
  }

  /* WARNING: duplicates map to field internal requests too */

  if(gx->g_maps[KATCP_MAP_INNER_REQUEST] == NULL){
    gx->g_maps[KATCP_MAP_INNER_REQUEST] = duplicate_cmd_map_katcp(m, name);
    if(gx->g_maps[KATCP_MAP_INNER_REQUEST]){
      hold_cmd_map_katcp(gx->g_maps[KATCP_MAP_INNER_REQUEST]);
    }
  }

  if(gx->g_maps[KATCP_MAP_REMOTE_INFORM] == NULL){
    m = create_cmd_map_katcp(name);
    if(m == NULL){
      destroy_group_katcp(d, gx);
      return -1;
    }

    gx->g_maps[KATCP_MAP_REMOTE_INFORM] = m;
    hold_cmd_map_katcp(m);

    add_full_cmd_map_katcp(m, "log", "collect log messages (#log priority timestamp module text)", 0, &log_group_info_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "sensor-status", "handle sensor updates (#sensor-status timestamp 1 name status value)", 0, &sensor_status_group_info_katcp, NULL, NULL);
    add_full_cmd_map_katcp(m, "sensor-list", "handle sensor definitions (#sensor-list name description units type [range])", 0, &sensor_list_group_info_katcp, NULL, NULL);
  }

  if(gx->g_maps[KATCP_MAP_INNER_INFORM] == NULL){
    gx->g_maps[KATCP_MAP_INNER_INFORM] = duplicate_cmd_map_katcp(m, name);
    if(gx->g_maps[KATCP_MAP_INNER_INFORM]){
      hold_cmd_map_katcp(gx->g_maps[KATCP_MAP_INNER_INFORM]);
    }
  }

  if(s->s_fallback == NULL){
    hold_group_katcp(gx);
    s->s_fallback = gx;
  }

  return 0;
}

#endif
