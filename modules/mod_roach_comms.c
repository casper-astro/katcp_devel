#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <katcp.h>
#include <katcl.h>
#include <katpriv.h>
#include <kcs.h>

#define KATCP_OPERATION_ROACH_CONNECT         "roachconnect"
#define KATCP_OPERATION_ROACH_CONNECT_MULTI   "roachconnectmulti"
#define KATCP_OPERATION_URL_CONSTRUCT         "urlconstruct"
#define KATCP_OPERATION_URL_TO_ACTOR          "url2actor"
#define KATCP_EDGE_ROACH_PING                 "ping"
#define KATCP_TYPE_ROACH                      "roach"
#define KATCP_TYPE_URL                        "url"

void print_katcp_url_type_mod(struct katcp_dispatch *d, char *key, void *data)
{
  struct katcp_url *ku;
  ku = data;
  if (ku == NULL)
    return;

  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "#katcp url:");
  append_string_katcp(d, KATCP_FLAG_STRING, ku->u_str);
  append_string_katcp(d, KATCP_FLAG_STRING, "use count:");
  append_unsigned_long_katcp(d, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, ku->u_use);
}
void destroy_katcp_url_type_mod(void *data)
{
  struct katcp_url *ku;
  ku = data;
  destroy_kurl_katcp(ku);
}
void *parse_katcp_url_type_mod(struct katcp_dispatch *d, char **str)
{
  struct katcp_url *ku;
  ku = create_kurl_from_string_katcp(str[0]);
  if (ku == NULL)
    return NULL;
  ku->u_use++;
  return ku;
}
char *getkey_katcp_url_type_mod(void *data)
{
  struct katcp_url *u;
  u = data;
  if (u == NULL)
    return NULL;
  return u->u_str;
}

int url_construct_mod(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
  char *str;
  int port, i;//, *count;
  struct katcp_url *url;
  //struct katcp_actor *a;
  
  struct katcp_stack *tempstack;

  port = 0;

#if 0
  count = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_INTEGER);
  if (count == NULL)
    return -1;
#endif

  str = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_STRING);
  if (str == NULL)
    return -1;

  port = atoi(str);

  str = NULL;

#if 0
  count = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_INTEGER);
  if (count == NULL)
    return -1;
#endif

  tempstack = create_stack_katcp();

#if 0
  for (i=0; i<*count; i++){
#endif
  for (i=0; (str = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_STRING)) != NULL ; i++){
    #if 0
    if (str == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "url construct encounted a null param");
      destroy_stack_katcp(tempstack);
      return -1;
    }
    #endif

    url = create_kurl_katcp("katcp", str, port, NULL);
    if (url == NULL){
      destroy_stack_katcp(tempstack);
      return -1;
    }
    url->u_use++;
    url = search_named_type_katcp(d, KATCP_TYPE_URL, url->u_str, url);
    push_named_stack_katcp(d, tempstack, url, KATCP_TYPE_URL);  
#if 0
    a = create_actor_type_katcp(d, url->u_str, NULL, NULL, NULL, NULL);
    search_named_type_katcp(d, KATCP_TYPE_ACTOR, url->u_str, a);
    str = NULL;
#endif
  }
  
  while (!is_empty_stack_katcp(tempstack)){
    push_tobject_katcp(stack, pop_stack_katcp(tempstack));
  }

#if 0
  push_named_stack_katcp(d, stack, count, KATCP_TYPE_INTEGER);
#endif

  destroy_stack_katcp(tempstack);

  return 0;
}

struct kcs_sm_op *url_construct_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  return create_sm_op_kcs(&url_construct_mod, NULL);
}

int url_to_actor_mod(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *to)
{
  struct katcp_actor *a;
  struct katcp_url *u;

  u = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_URL);
  if (u == NULL)
    return -1;
  
  a = search_named_type_katcp(d, KATCP_TYPE_ACTOR, u->u_str, create_actor_type_katcp(d, u->u_str, NULL, NULL, NULL, NULL));
  if (a == NULL)
    return -1;

  push_named_stack_katcp(d, stack, a, KATCP_TYPE_ACTOR);

  return 0;
}

struct kcs_sm_op *url_to_actor_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  return create_sm_op_kcs(&url_to_actor_mod, NULL);
}

int roach_disconnect_mod(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_actor *a;

  a = data;
  if (a == NULL)
    return 0;

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "roach comms: notice disconnect %s", a->a_key);
  
  //del_data_type_katcp(d, KATCP_TYPE_ACTOR, a->a_key);
  
  return 0;
}

int roach_connect_mod(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
  struct katcp_url *u;
  struct katcp_notice *n;
  struct katcp_job *j;
  struct katcp_actor *a;

  u = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_URL);
  if (u == NULL){
#ifdef DEBUG
    fprintf(stderr, "mod_roach_comms: could not pop url\n");
#endif
    return -1;
  }
#if 0 
  n = register_notice_katcp(d, NULL, 0, &roach_disconnect_mod, NULL);
#endif
  n = create_notice_katcp(d, NULL, 0);
  if (n == NULL){
#ifdef DEBUG
    fprintf(stderr, "mod_roach_comms: cannot create notice\n");
#endif
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "mod_roach_comms: running roach connect to <%s>\n", u->u_str);
#endif
  
  j = network_connect_job_katcp(d, u, n);
  if (j == NULL){
    return -1;
  }
  
  a = create_actor_type_katcp(d, u->u_str, j, NULL, NULL, NULL);
  if (a == NULL){
    zap_job_katcp(d, j);
    return -1;
  }

  if (store_data_type_katcp(d, KATCP_TYPE_ACTOR, KATCP_DEP_BASE, u->u_str, a, &print_actor_type_katcp, &destroy_actor_type_katcp, &copy_actor_type_katcp, &compare_actor_type_katcp, &parse_actor_type_katcp, &getkey_actor_katcp) < 0){
    zap_job_katcp(d, j);
    destroy_actor_type_katcp(a);
    return -1;
  }
  
  if (add_notice_katcp(d, n, &roach_disconnect_mod, a) < 0){
    zap_job_katcp(d, j);
    destroy_actor_type_katcp(a);
    return -1;
  }
    
  push_named_stack_katcp(d, stack, a, KATCP_TYPE_ACTOR);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "roach connected %s with job %p", u->u_str, j);

  return 0;
}

struct kcs_sm_op *roach_connect_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct kcs_sm_op *op;

  op = create_sm_op_kcs(&roach_connect_mod, NULL);
  if (op == NULL)
    return NULL;

#ifdef DEBUG
  fprintf(stderr, "mod_roach_comms: created op %s (%p)\n", KATCP_OPERATION_ROACH_CONNECT, op);
#endif

  return op;
}

int roach_connect_multi_mod(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
  int i, rtn1, rtn2, *count;
  struct katcp_stack *tempstack;

  tempstack = create_stack_katcp();
  if (tempstack == NULL)
    return -1;

#ifdef DEBUG
  fprintf(stderr, "mod_roach_comms: running roach connect multi\n");
#endif

  count = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_INTEGER);
  if (count == NULL)
    return -1;
  
  rtn1 = 0;
  rtn2 = 0;

  for (i=0;i<*count; i++) {
    rtn1 = roach_connect_mod(d, stack, o);
    if (rtn1 == 0){
      push_tobject_katcp(tempstack, pop_stack_katcp(stack));
    }
    rtn2 += rtn1;
  }

  while (!is_empty_stack_katcp(tempstack)){
    push_tobject_katcp(stack, pop_stack_katcp(tempstack));
  }

#if 0
  push_named_stack_katcp(d, stack, count, KATCP_TYPE_INTEGER);
#endif

  destroy_stack_katcp(tempstack);
  
  return rtn2;
}

struct kcs_sm_op *roach_connect_multi_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct kcs_sm_op *op;

  op = create_sm_op_kcs(&roach_connect_multi_mod, NULL);
  if (op == NULL)
    return NULL;

#ifdef DEBUG
  fprintf(stderr, "mod_roach_comms: created op %s (%p)\n", KATCP_OPERATION_ROACH_CONNECT, op);
#endif

  return op;
}

int roach_ping_returns_mod(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_actor *a;
  struct katcl_parse *p;
  char *ptr;
  int max;

  a = data;
  if (a == NULL){
    return 0;
  }

  p = get_parse_notice_katcp(d, n);
  if (p){
    max = get_count_parse_katcl(p);
    if (max >= 2){
      ptr = get_string_parse_katcl(p, 1);
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s watchdog %s", a->a_key, ptr);
    }
#if 0
    for (i=0; i<max; i++){
      ptr = get_string_parse_katcl(p, i);
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", ptr);
    }
#endif
  }

  release_sm_notice_actor_katcp(d, a, get_parse_notice_katcp(d, n));
#if 0
  wake_notice_katcp(d, a->a_sm_notice, NULL);
  struct timeval now, delta;
 
  r = data;
  if (r == NULL)
    return 0;
 
  gettimeofday(&now, NULL);
    sub_time_katcp(&delta, &now, &r->r_seen);
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s reply in %4.3fms", r->r_url->u_str, (float)(delta.tv_sec*1000)+((float)delta.tv_usec/1000));

#endif
  return 0;
}

int roach_ping_mod(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_stack *stack;
  struct katcp_actor *a;
  struct katcp_notice *pn;
  struct katcl_parse *p;

  stack = data;

  a = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_ACTOR);
  if (a == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "got no actor from the stack");
    return -1;
  }

  if (hold_sm_notice_actor_katcp(a, n) < 0)
    return -1; 

  p = create_parse_katcl();
  if (p == NULL){
    return -1;
  }

  if (add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, "?watchdog") < 0){
    destroy_parse_katcl(p);
    return -1;
  }

  pn = register_parse_notice_katcp(d, NULL, p, &roach_ping_returns_mod, a);
  if (pn == NULL){
    destroy_parse_katcl(p);
    return -1;
  }
  
  if (notice_to_job_katcp(d, a->a_job, pn) < 0){
    destroy_parse_katcl(p);
    return -1;
  }
  
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "sent ping to %s", a->a_key);

#if 0
  gettimeofday(&r->r_seen, NULL);
#endif

  return 0;
}

struct kcs_sm_edge *roach_ping_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct kcs_sm_edge *e;

  e = create_sm_edge_kcs(s, &roach_ping_mod);
  if (e == NULL)
    return NULL;

  return e;
}

int init_mod(struct katcp_dispatch *d)
{
  int rtn;
  
  if (check_code_version_katcp(d) != 0){
#ifdef DEBUG
    fprintf(stderr, "mod: ERROR was build against an incompatible katcp lib\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot load module katcp version mismatch");
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "successfully loaded mod_config_parser");
 
#if 0
  rtn  = register_name_type_katcp(d, KATCP_TYPE_ROACH, KATCP_DEP_BASE, &print_roach_type_mod, &destroy_roach_type_mod, NULL, NULL, &parse_roach_type_mod);
#endif
  rtn  = register_name_type_katcp(d, KATCP_TYPE_URL, KATCP_DEP_BASE, &print_katcp_url_type_mod, &destroy_katcp_url_type_mod, NULL, NULL, &parse_katcp_url_type_mod, &getkey_katcp_url_type_mod);
  
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added type:");
#if 0
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_TYPE_ROACH);
#endif
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_TYPE_URL);

  
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added operations:");

  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_ROACH_CONNECT, &roach_connect_setup_mod, NULL, NULL, NULL, NULL, NULL, NULL);
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_ROACH_CONNECT_MULTI, &roach_connect_multi_setup_mod, NULL, NULL, NULL, NULL, NULL, NULL);
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_URL_CONSTRUCT, &url_construct_setup_mod, NULL, NULL, NULL, NULL, NULL, NULL);
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_URL_TO_ACTOR, &url_to_actor_setup_mod, NULL, NULL, NULL, NULL, NULL, NULL);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_OPERATION_ROACH_CONNECT);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_OPERATION_ROACH_CONNECT_MULTI);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_OPERATION_URL_CONSTRUCT);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_OPERATION_URL_TO_ACTOR);


  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added edges:");

  rtn += store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_DEP_BASE, KATCP_EDGE_ROACH_PING, &roach_ping_setup_mod, NULL, NULL, NULL, NULL, NULL, NULL);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_EDGE_ROACH_PING);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "to see the full operation list: ?sm oplist");

  return rtn;
}
