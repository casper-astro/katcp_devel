#include <stdio.h>
#include <stdlib.h>

#include <katcp.h>
#include <kcs.h>

#define KATCP_OPERATION_RPN_ADD        "rpn_add"
#define KATCP_EDGE_COMPARE             "compare"

int rpn_add_mod(struct katcp_dispatch *d, struct kcs_sm_state *s, struct katcp_stack_obj *o)
{
  struct kcs_sm *m;
  struct katcp_stack *stack;
  struct katcp_stack_obj *a, *b;

  if (s == NULL)
    return -1;

  m = s->s_sm;
  if (m == NULL)
    return -1;

  stack = m->m_stack;
  if (stack == NULL)
    return -1;

  a = pop_stack_katcp(stack);
  b = pop_stack_katcp(stack);

  if (a == NULL || b == NULL){
    destroy_obj_stack_katcp(a);
    destroy_obj_stack_katcp(b);
    return -1;
  }

  /*TODO:finish*/
  
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "rpn_add about to return");
  return 0;
}

struct kcs_sm_op *rpn_add_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct kcs_sm_op *op;

  op = create_sm_op_kcs(&rpn_add_mod, NULL);
  if (op == NULL)
    return NULL;

#ifdef DEBUG
  fprintf(stderr, "rpn_add_mod: rpn_add_setup created op (%p)\n",op);
#endif
  
  return op;
}

int compare_generic_mod(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct kcs_sm_state *s;

  s = data;
  if (s == NULL)
    return -2;

  /*TODO:finish*/

#ifdef DEBUG
  fprintf(stderr, "compare_generic_mod: RUNNN with (%p)\n", s);
#endif
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "compare_generic about to return");

  return 0;
}

struct kcs_sm_edge *compare_generic_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct kcs_sm_edge *e;
  
  e = create_sm_edge_kcs(s, &compare_generic_mod);
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

  rtn  = store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_OPERATION_RPN_ADD, &rpn_add_setup_mod, NULL, NULL, NULL, NULL, NULL);
  rtn  = store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_EDGE_COMPARE, &compare_generic_setup_mod, NULL, NULL, NULL, NULL, NULL);


  return rtn;
}
