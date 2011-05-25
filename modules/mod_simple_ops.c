#include <stdio.h>
#include <stdlib.h>

#include <katcp.h>
#include <kcs.h>
#include <stack.h>

#define KATCP_OPERATION_RPN_ADD   "rpn_add"

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



  return 0;
  /*
  struct katcp_stack_obj * (*popstack)(struct katcp_stack *); 

  popstack = get_key_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_POP);

  if (popstack == NULL){
#ifdef DEBUG
    fprintf(stderr, "module: cannot get function %s, %s\n", KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_POP);
#endif
    return -1;
  }
  */
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

int init_mod(struct katcp_dispatch *d)
{
  int rtn;

  if (check_code_version_katcp(d) != 0){
#ifdef DEBUG
    fprintf(stderr, "mod: rpn_add was build against an incompatible katcp lib\n");
#endif
    return -1;
  }

  rtn  = store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_OPERATION_RPN_ADD, &rpn_add_setup_mod, NULL, NULL, NULL, NULL, NULL);


  return rtn;
}

