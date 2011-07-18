#include <stdio.h>
#include <stdlib.h>

#include <katcp.h>
#include <katpriv.h>
#include <kcs.h>

#define KATCP_OPERATION_ADD        "+"
#define KATCP_EDGE_COMPARE_EQUAL   "="

void *add_int_mod(void *a, void *b)
{
  int *x, *y, *ans;
  x = a;
  y = b;

  if (x == NULL || y == NULL)
    return NULL;
  
  ans = malloc(sizeof(int));
  if (ans == NULL)
    return NULL;

  *ans = *x + *y;

  return ans;
}
#if 0
void *add_float_mod(void *a, void *b)
{

}
#endif

int rpn_add_mod(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
#define TYPE_COUNT  1
#if 0
  struct kcs_sm *m;
  struct katcp_stack *stack;
  struct katcp_tobject *a, *b;
#endif
  int *a, *b;
  //int i;
  //void *temp;

  struct type_funcs {
    struct katcp_type * f_type;
    void *(*f_call)(void *, void *);
  };

  struct type_funcs t_set[TYPE_COUNT];  
#if 0
  struct type_funcs t_set[] = { 
    {  },
    {  }
  };
#endif

  t_set[0].f_type = find_name_type_katcp(d, KATCP_TYPE_INTEGER);
  t_set[0].f_call = &add_int_mod;
#if 0
  t_set[1].f_tid = find_name_id_type(d, KATCP_TYPE_FLOAT);
  t_set[1].f_call = &add_float_mod;
#endif
  
  a = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_INTEGER);
  b = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_INTEGER);

#if 0
  if (s == NULL || o == NULL)
    return -1;
  
  if (o->o_type == NULL)
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
    destroy_tobject_katcp(a);
    destroy_tobject_katcp(b);
    return -1;
  }
  
  if (!((o->o_type == a->o_type) && (o->o_type == b->o_type))){
#ifdef DEBUG
    fprintf(stderr, "rpn_add_mod: runtime error o: (%p) a: (%p) b: (%p)\n", o->o_type, a->o_type, b->o_type);
#endif
    destroy_tobject_katcp(a);
    destroy_tobject_katcp(b);
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "rpn_add runtime type mismatch");
    return -1;
  }
#endif

#if 0
  for (i=0;i<TYPE_COUNT;i++){
    if (o->o_type == t_set[i].f_type){
      temp = (*(t_set[i].f_call))(a->o_data, b->o_data);
      if (push_stack_katcp(stack, temp, o->o_type) < 0){
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "rpn_add runtime could not push answer");
        if (o->o_type->t_free != NULL){
          (*o->o_type->t_free)(temp);
        } else {
#ifdef DEBUG
          fprintf(stderr,"rpn_add_mod: possbile leaking memory since type free func is null\n");
#endif
          free(temp);
        }
        destroy_tobject_katcp(a);
        destroy_tobject_katcp(b);
        return -1;
      }
      //log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "rpn_add to print ans");
      if (o->o_type->t_print != NULL)
        (*o->o_type->t_print)(d, temp);
      break;
    } 
  }
  
  destroy_tobject_katcp(a);
  destroy_tobject_katcp(b);
#endif

  return 0;
#undef TYPE_COUNT
}

struct kcs_sm_op *rpn_add_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct katcp_type *t;
  struct katcp_tobject *o;
  struct kcs_sm_op *op;
  char *type;

  type = arg_string_katcp(d, 4);

  if (type == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "rpn_add requires a type");
    return NULL;
  }

  t = find_name_type_katcp(d, type);
  if (t == NULL)
    return NULL;

  o = create_tobject_katcp(NULL, t, 1);
  if (o == NULL)
    return NULL;

  op = create_sm_op_kcs(&rpn_add_mod, o);
  if (op == NULL){
    destroy_tobject_katcp(o);
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "rpn_add_mod: rpn_add_setup created op (%p)\n", op);
#endif
  
  return op;
}

int compare_generic_mod(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  int rtn;
  rtn = 0;
#if 0
  struct kcs_sm *m;
  struct kcs_sm_state *s;
  struct katcp_stack *stack;
  struct katcp_tobject *a, *b;

  s = data;
  if (s == NULL)
    return -2;

  m = s->s_sm;
  if (m == NULL)
    return -2;

  stack = m->m_stack;
  if (stack == NULL)
    return -2;

  a = pop_stack_katcp(stack);
  b = pop_stack_katcp(stack);

  if (a == NULL || b == NULL){
    destroy_tobject_katcp(a);
    destroy_tobject_katcp(b);
    return -2;
  }
  
  if (a->o_type == NULL){
    destroy_tobject_katcp(a);
    destroy_tobject_katcp(b);
    return -2;
  }
  
  if (a->o_type->t_compare == NULL){
    destroy_tobject_katcp(a);
    destroy_tobject_katcp(b);
    return -2;
  }
 
  rtn = (*(a->o_type->t_compare))(a->o_data, b->o_data);

#ifdef DEBUG
  fprintf(stderr, "compare_generic_mod: RUNNN with (%p) t_compare rtn: %d\n", s, rtn);
#endif
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "compare_generic t_compare rtn: %d", rtn);

  destroy_tobject_katcp(a);
  destroy_tobject_katcp(b);
#endif

  wake_notice_katcp(d, n, NULL);
  return rtn;
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

  rtn  = store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_ADD, &rpn_add_setup_mod, NULL, NULL, NULL, NULL, NULL, NULL);
  rtn  = store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_DEP_BASE, KATCP_EDGE_COMPARE_EQUAL, &compare_generic_setup_mod, NULL, NULL, NULL, NULL, NULL, NULL);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "successfully loaded mod_simple_ops");
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added operations:");
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_OPERATION_ADD);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added edge test:");
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_EDGE_COMPARE_EQUAL);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "to see the full operation list: ?sm oplist");
  
  return rtn;
}
