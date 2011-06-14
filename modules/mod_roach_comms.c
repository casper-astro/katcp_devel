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
#include <katpriv.h>
#include <kcs.h>

#define KATCP_OPERATION_ROACH_CONNECT "roachconnect"
#define KATCP_TYPE_KAT_URL            "katurl"


void print_kat_url_type_mod(struct katcp_dispatch *d, void *data)
{
  struct katcp_url *ku;
  ku = data;
  if (ku == NULL)
    return;

#ifdef DEBUG
  fprintf(stderr, "mod_roach_comms: print_kat_url_type %s\n", ku->u_str);
#endif
  //log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "kat url: %s", ku->u_str);
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, "kat url:");
  append_string_katcp(d, KATCP_FLAG_STRING, ku->u_str);
}
void destroy_kat_url_type_mod(void *data)
{
  struct katcp_url *ku;
  ku = data;
  destroy_kurl_katcp(ku);
}
void *parse_kat_url_type_mod(char **str)
{ 
  return create_kurl_from_string_katcp(str[0]);
}

int roach_connect_mod(struct katcp_dispatch *d, struct kcs_sm_state *s, struct katcp_stack_obj *o)
{
  struct kcs_sm *m;
  struct katcp_stack *stack;
  struct katcp_stack_obj *a;
  
  if (o != NULL)
    return -1;
  
  m = s->s_sm;
  if (m == NULL)
    return -1;

  stack = m->m_stack;
  if (stack == NULL)
    return -1;
    
  a = pop_stack_katcp(stack);
  if (a == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "roach connect expects kurl on the stack");
    return -1;
  }
  
  /*TODO: do stuff*/
  
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
  
  rtn  = register_name_type_katcp(d, KATCP_TYPE_KAT_URL, &print_kat_url_type_mod, &destroy_kat_url_type_mod, NULL, NULL, &parse_kat_url_type_mod);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added type:");
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_TYPE_KAT_URL);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added operations:");
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_OPERATION_ROACH_CONNECT, &roach_connect_setup_mod, NULL, NULL, NULL, NULL, NULL);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_OPERATION_ROACH_CONNECT);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "to see the full operation list: ?sm oplist");

  return rtn;
}
