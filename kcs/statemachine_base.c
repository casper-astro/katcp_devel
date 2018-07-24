#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <katcp.h>
#include <katcl.h>
#include <katpriv.h>
#include <netc.h>

#include "kcs.h"

/*Statemachine Base********************************************************************************************/
void print_integer_type_kcs(struct katcp_dispatch *d, char *key, void *data)
{
  int *o;
  o = data;
  if (o == NULL)
    return;
#ifdef DEBUG
  fprintf(stderr, "statemachine: print_integer_type %d\n",*o);
#endif
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "#integer type:");
  append_signed_long_katcp(d, KATCP_FLAG_SLONG | KATCP_FLAG_LAST, *o);
}
void destroy_integer_type_kcs(void *data)
{
  int *o;
  o = data;
  if (o != NULL){
    free(o);
  }
}
int *create_integer_type_kcs(int val)
{
  int *i;
  i = malloc(sizeof(int));
  if (i==NULL)
    return NULL;
  *i = val;
  return i;
}
int compare_integer_type_kcs(const void *a, const void *b)
{
  const int *x, *y;
  x = a;
  y = b;

  if (x == NULL || y == NULL)
    return -2;

#ifdef DEBUG
  fprintf(stderr, "compare_integer_type: a: %d b: %d\n", *x, *y);
#endif
  
  if (*x == *y)
    return 0;
  else if (*x < *y)
    return -1;
  else if (*x > *y)
    return 1;
  
  return -2;
}
void *parse_integer_type_kcs(struct katcp_dispatch *d, char **str)
{
  int *o;
  o = malloc(sizeof(int));
  if (o == NULL)
    return NULL;
  *o = atoi(str[0]);
  return o;
}
char *getkey_integer_type_kcs(void *data)
{
  int *a, count;
  char *key;
  count = 0;
#define INTMAX 21
  a = data;
  if (a == NULL) 
    return NULL;
  key = malloc(sizeof(char)* INTMAX);
  if (key == NULL)
    return NULL;
  count = snprintf(key, INTMAX, "%d", *a);
#undef INTMAX
  return key;
}



int pushstack_statemachine_kcs(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
  if (stack == NULL || o == NULL)
    return -1;

#ifdef DEBUG
  fprintf(stderr, "statemachine: PUSH STACK\n");
#endif

#if 0
  return push_stack_ref_obj_katcp(stack, o);
#endif
#if 1
  return push_tobject_katcp(stack, copy_tobject_katcp(o));
#endif
}

struct kcs_sm_op *pushstack_setup_statemachine_kcs(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
#define ARG_BASE 5
  struct katcp_type *t;
  struct katcp_tobject *o;
  struct kcs_sm_op *op;
  char **data, *type;
  void *ptemp, *stemp;
  int num, i;

  stemp = NULL;

  type = arg_string_katcp(d, 4);

  if (type == NULL || s == NULL)
    return NULL;
  
  t = find_name_type_katcp(d, type);
  if (t == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "invalid type: %s", type); 
    return NULL;
  }
  
  num = arg_count_katcp(d) - ARG_BASE;
  
  data = malloc(sizeof(char *) * (num + 1));
  if (data == NULL)
    return NULL;
  
  for (i=0; i<num; i++){
    data[i] = arg_string_katcp(d, ARG_BASE + i);
#ifdef DEBUG
    fprintf(stderr, "statemachine: pushsetup data[%d of %d]: %s\n", i+1, num, data[i]);
#endif
  }
  data[num] = NULL; 
  
#ifdef DEBUG
  fprintf(stderr, "statemachine: call type parse function\n");
#endif
  ptemp = (*t->t_parse)(d, data);
  
  if (data[0] != NULL)
    stemp = search_type_katcp(d, t, data[0], ptemp);

  free(data);

  if (ptemp == NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: type parse fn failed\n");
#endif
    //return NULL;  
  }

  if (stemp == NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: type search fn failed\n");
#endif
    (*t->t_free)(ptemp);
    return NULL;
  }

  o = create_tobject_katcp(stemp, t, 0);
  if (o == NULL){
    (*t->t_free)(stemp);
#ifdef DEBUG
    fprintf(stderr, "statemachine: pushsetup could not create stack obj\n");
#endif
    return NULL;
  }
  
  op = create_sm_op_kcs(&pushstack_statemachine_kcs, o);
  if (op == NULL){
    (*t->t_free)(stemp);
    destroy_tobject_katcp(o);
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "statemachine: pushsetup created op (%p)\n", op);
#endif

  return op;
#undef ARG_BASE
}

int spawn_statemachine_kcs(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
  char *startnode;
  struct katcp_tobject  *data;
  struct katcp_dispatch *dl;

  dl = template_shared_katcp(d);
  
  if (stack == NULL || o == NULL)
    return -1;

  data = pop_stack_katcp(stack);
  startnode = o->o_data;

  if (data == NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: spawn popped a null\n");
#endif
    //return 0;
  }

#ifdef DEBUG
  fprintf(stderr, "statemachine: spawn new @ %s\n", startnode);
#endif
  
  return start_process_kcs(dl, startnode, data, PROCESS_SLAVE);
}

struct kcs_sm_op *spawn_setup_statemachine_kcs(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct katcp_tobject *o;
  char *node, *str[2];
  
  node = arg_string_katcp(d, 4);

  if (node == NULL)
    return NULL;

  str[0] = node;
  str[1] = NULL;

  o = create_named_tobject_katcp(d, parse_string_type_katcp(d, str), KATCP_TYPE_STRING, 1);
  if (o == NULL)
    return NULL;
  
  return create_sm_op_kcs(&spawn_statemachine_kcs, o);
}

int msleep_statemachine_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_stack *stack;
  int *time;
  struct timeval tv;

  stack = data;
  if (stack == NULL)
    return -1;

  time = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_INTEGER);
  if (time == NULL)
    return -1;
  
  component_time_katcp(&tv, (unsigned int) (*time));

  wake_notice_in_tv_katcp(d, n, &tv);

  return 0;
}

struct kcs_sm_edge *msleep_setup_statemachine_kcs(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  return create_sm_edge_kcs(s, &msleep_statemachine_kcs);
}

int peek_stack_type_statemachine_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_stack *stack;
  struct katcp_type *t;
  struct katcp_tobject *to;
  char *ctype;

  stack = data;
  if (stack == NULL)
    return -1;

  ctype = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_STRING);
  if (ctype == NULL)
    return -1;
  
  t = find_name_type_katcp(d, ctype);
  if (t == NULL)
    return -1;

  to = peek_stack_katcp(stack);
  if (to == NULL && to->o_type != t)
    return -1;

  wake_notice_katcp(d, n, NULL);
  
  return 0;
}

struct kcs_sm_edge *peek_stack_type_setup_statemachine_kcs(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  return create_sm_edge_kcs(s, &peek_stack_type_statemachine_kcs);
}

int print_stack_statemachine_kcs(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
#ifdef DEBUG
  fprintf(stderr, "statemachine: about to runtime print stack\n");
#endif
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "----STACK PRINT START----");
  print_stack_katcp(d, stack);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "-----STACK PRINT END-----");
  return 0;
}

struct kcs_sm_op *print_stack_setup_statemachine_kcs(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  return create_sm_op_kcs(&print_stack_statemachine_kcs, NULL);
}

int is_stack_empty_statemachine_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_stack *stack;

  stack = data;
  if (stack == NULL)
    return -1;

  if (is_empty_stack_katcp(stack)){
    wake_notice_katcp(d, n, NULL);
    return 0;
  }

  return -1;
}

struct kcs_sm_edge *is_stack_empty_setup_statemachine_kcs(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  return create_sm_edge_kcs(s, &is_stack_empty_statemachine_kcs);
}

int get_values_dbase_katcp(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
  struct katcp_dbase *db;
  struct katcp_tobject *temp;
  struct katcp_stack *values;
  int count, i;

  db = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_DBASE);
  if (db == NULL)
    return -1;
  
  count = get_value_count_dbase_katcp(db);
  values = get_value_stack_dbase_katcp(db);

  for (i=0; i<count; i++){
    temp = index_stack_katcp(values, i);
    push_tobject_katcp(stack, copy_tobject_katcp(temp));
  }
  
  return 0;
}

struct kcs_sm_op *get_values_setup_dbase_katcp(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  return create_sm_op_kcs(&get_values_dbase_katcp, NULL);
}

int init_statemachine_base_kcs(struct katcp_dispatch *d)
{
  int rtn;
  
  /*register basic types*/
  rtn  = register_name_type_katcp(d, KATCP_TYPE_INTEGER, KATCP_DEP_BASE, &print_integer_type_kcs, &destroy_integer_type_kcs, NULL, &compare_integer_type_kcs, &parse_integer_type_kcs, &getkey_integer_type_kcs);

#if 0
  rtn += register_name_type_katcp(d, KATCP_TYPE_FLOAT, NULL, NULL, NULL, NULL, NULL);
  rtn += register_name_type_katcp(d, KATCP_TYPE_DOUBLE, NULL, NULL, NULL, NULL, NULL);
  rtn += register_name_type_katcp(d, KATCP_TYPE_CHAR, NULL, NULL, NULL, NULL, NULL);
#endif

  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_STACK_PUSH, &pushstack_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);
  
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_SPAWN, &spawn_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);

  rtn += store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_DEP_BASE, KATCP_EDGE_SLEEP, &msleep_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);
  
  rtn += store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_DEP_BASE, KATCP_EDGE_PEEK_STACK_TYPE, &peek_stack_type_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);

  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_PRINT_STACK, &print_stack_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);
  
  rtn += store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_DEP_BASE, KATCP_EDGE_IS_STACK_EMPTY, &is_stack_empty_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);
  
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_GET_DBASE_VALUES, &get_values_setup_dbase_katcp, NULL, NULL, NULL, NULL, NULL, NULL);
#if 0
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_STORE, &store_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);
#endif
 
  return rtn;
}

