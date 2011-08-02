/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

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

void print_integer_type_kcs(struct katcp_dispatch *d, void *data)
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


void print_string_type_kcs(struct katcp_dispatch *d, void *data)
{
  char *o;
  o = data;
  if (o == NULL)
    return;
#ifdef DEBUG
  fprintf(stderr, "statemachine: print_string_type %s\n",o);
#endif
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "#string type:");
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, o);
}
void destroy_string_type_kcs(void *data)
{
  char *o;
  o = data;
  if (o != NULL){
    free(o);
  }
}
void *parse_string_type_kcs(struct katcp_dispatch *d, char **str)
{
  char *o;
  int i, len, start, size;

  len = 0;
  o   = NULL;

  for (i=0; str[i] != NULL; i++) {
    
    start = len;
    size  = strlen(str[i]); 
    len   += size;

#ifdef DEBUG
    fprintf(stderr,"statemachine: parse_string_type_kcs: start: %d size: %d len: %d str: %s\n", start, size, len, str[i]);
#endif

    o = realloc(o, sizeof(char *) * (len + 1));
    if (o == NULL)
      return NULL;
    
    strncpy(o + start, str[i], size); 
    o[len] = ' ';
    len++;
    
  }
  
  o[len-1] = '\0';

#ifdef DEBUG
  fprintf(stderr,"statemachine: parse_string_type_kcs: string: <%s> len:%d strlen:%d\n", o, len, (int)strlen(o));
#endif

  return o;
}

struct katcp_stack *get_task_stack_kcs(struct kcs_sched_task *t)
{
  return (t != NULL) ? t->t_stack : NULL;
}
struct kcs_sm_state *get_task_pc_kcs(struct kcs_sched_task *t)
{
  return (t != NULL) ? t->t_pc : NULL;
}
int set_task_pc_kcs(struct kcs_sched_task *t, struct kcs_sm_state *s)
{
  if (t == NULL || s == NULL)
    return -1;
  
  t->t_pc = s;

  return 0;
}
void print_task_stack_kcs(struct katcp_dispatch *d, struct kcs_sched_task *t)
{
  struct katcp_stack *stack;

  stack = get_task_stack_kcs(t);

  print_stack_katcp(d, stack);
}

struct kcs_sm_op *create_sm_op_kcs(int (*call)(struct katcp_dispatch *, struct katcp_stack *, struct katcp_tobject *), struct katcp_tobject *o)
{
  struct kcs_sm_op *op;
  
  if (call == NULL)
    return NULL;

  op = malloc(sizeof(struct kcs_sm_op));
  if (op == NULL)
    return NULL;

  op->o_call = call;
  op->o_tobject = o;

  return op;
}
void destroy_sm_op_kcs(struct kcs_sm_op *op)
{
  if (op != NULL){
    op->o_call = NULL;
    destroy_tobject_katcp(op->o_tobject);
    free(op);
  }
} 
void print_sm_op_kcs(struct katcp_dispatch *d, struct kcs_sm_op *op)
{
  if (op != NULL){
    append_args_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_LAST, "#node op: call:(%p) stackable data:(%p)", op->o_call, op->o_tobject);
#ifdef DEBUG
    fprintf(stderr,"statemachine: print_sm_op call: (%p)\n", op->o_call);
#endif
    print_tobject_katcp(d, op->o_tobject);
  } 
}

int pushstack_statemachine_kcs(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
  if (stack == NULL || o == NULL)
    return -1;
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

  type = arg_string_katcp(d, 4);

  if (type == NULL || s == NULL)
    return NULL;
  
  t = find_name_type_katcp(d, type);
  if (t == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "invalid type: %s\n", type); 
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
  
  if (stack == NULL || o == NULL)
    return -1;

  data = pop_stack_katcp(stack);
  startnode = o->o_data;
  
  return start_process_kcs(d, startnode, data);
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

  o = create_named_tobject_katcp(d, parse_string_type_kcs(d, str), KATCP_TYPE_STRING, 1);
  if (o == NULL)
    return NULL;
  
  return create_sm_op_kcs(&spawn_statemachine_kcs, o);
}

#if 0
int store_statemachine_kcs(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
  struct katcp_type *t;
  char *key;

  if (o == NULL)
    return -1;

  t = o->o_type;
  if (t == NULL)
    return -1;

  if (t->t_getkey == NULL)
    return -1;

  key = (*t->t_getkey)(o->o_data);
  
#ifdef DEBUG
  fprintf(stderr, "statemachine: about to store <%s>\n", key);
#endif

  return store_data_at_type_katcp(d, t, KATCP_DEP_BASE, key, o->o_data, t->t_print, t->t_free, t->t_copy, t->t_compare, t->t_parse, t->t_getkey); 
}

struct kcs_sm_op *store_setup_statemachine_kcs(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
#define ARG_BASE 5
  struct kcs_sm_op *op;

  struct katcp_type *t;
  struct katcp_tobject *o;
  char **data, *type;
  void *temp;
  int num, i;

  type = arg_string_katcp(d, 4);

  if (type == NULL || s == NULL)
    return NULL;
  
  t = find_name_type_katcp(d, type);
  if (t == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "invalid type: %s\n", type); 
    return NULL;
  }
  
  num = arg_count_katcp(d) - ARG_BASE;
  
  data = malloc(sizeof(char *) * (num + 1));
  if (data == NULL)
    return NULL;
  
  for (i=0; i<num; i++){
    data[i] = arg_string_katcp(d, ARG_BASE + i);
#ifdef DEBUG
    fprintf(stderr, "statemachine: storesetup data[%d of %d]: %s\n", i+1, num, data[i]);
#endif
  }
  data[num] = NULL; 
  
#ifdef DEBUG
  fprintf(stderr, "statemachine: call type parse function\n");
#endif
  temp = (*t->t_parse)(d, data);
  
  free(data);

  if (temp == NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: type parse fn failed\n");
#endif
    return NULL;  
  }

  o = create_tobject_katcp(temp, t, 0);
  if (o == NULL){
    (*t->t_free)(temp);
#ifdef DEBUG
    fprintf(stderr, "statemachine: storesetup could not create tobject\n");
#endif
    return NULL;
  }
  
  op = create_sm_op_kcs(&store_statemachine_kcs, o);
  if (op == NULL){
    (*t->t_free)(temp);
    destroy_tobject_katcp(o);
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "statemachine: storesetup created op (%p)\n", op);
#endif

  return op;
#undef ARG_BASE
}
#endif 

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

int statemachine_init_kcs(struct katcp_dispatch *d)
{ 
  int rtn;
  
  /*register basic types*/
  rtn  = register_name_type_katcp(d, KATCP_TYPE_INTEGER, KATCP_DEP_BASE, &print_integer_type_kcs, &destroy_integer_type_kcs, NULL, &compare_integer_type_kcs, &parse_integer_type_kcs, &getkey_integer_type_kcs);
  rtn += register_name_type_katcp(d, KATCP_TYPE_STRING, KATCP_DEP_BASE, &print_string_type_kcs, &destroy_string_type_kcs, NULL, NULL, &parse_string_type_kcs, NULL);

#if 0
  rtn += register_name_type_katcp(d, KATCP_TYPE_FLOAT, NULL, NULL, NULL, NULL, NULL);
  rtn += register_name_type_katcp(d, KATCP_TYPE_DOUBLE, NULL, NULL, NULL, NULL, NULL);
  rtn += register_name_type_katcp(d, KATCP_TYPE_CHAR, NULL, NULL, NULL, NULL, NULL);
#endif

  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_STACK_PUSH, &pushstack_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);
  
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_SPAWN, &spawn_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);

  rtn += store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_DEP_BASE, KATCP_EDGE_SLEEP, &msleep_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);
  
  rtn += store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_DEP_BASE, KATCP_EDGE_PEEK_STACK_TYPE, &peek_stack_type_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);
  
#if 0
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_STORE, &store_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL, NULL);
#endif

  rtn += init_actor_tag_katcp(d);
 
  return rtn;
}

struct kcs_sm_edge *create_sm_edge_kcs(struct kcs_sm_state *s_next, int (*call)(struct katcp_dispatch *, struct katcp_notice *, void *))
{
  struct kcs_sm_edge *e;
  
  if (s_next == NULL)
    return NULL;

  e = malloc(sizeof(struct kcs_sm_edge));
  if (e == NULL)
    return NULL;

  e->e_next = s_next;
  e->e_call = call;
 
  return e;
}
void destroy_sm_edge_kcs(void *data)
{
  struct kcs_sm_edge *e;
  e = data;
  if (e != NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: destroy_sm_edge_kcs\n");
#endif
    if (e->e_next) { e->e_next = NULL; }
    if (e->e_call) { e->e_call = NULL; }
    free(e);
  }
}

struct kcs_sm_state *create_sm_state_kcs(char *name)
{
  struct kcs_sm_state *s;

  if (name == NULL)
    return NULL;

  s = malloc(sizeof(struct kcs_sm_state));
  if (s == NULL)
    return NULL;
  s->s_name            = strdup(name);
  s->s_edge_list       = NULL;
  s->s_edge_list_count = 0;
  s->s_op_list         = NULL;
  s->s_op_list_count   = 0;
  
  if (s->s_name == NULL){
    free(s);
    return NULL;
  }

  return s;
}
void destroy_sm_state_kcs(void *data)
{
  struct kcs_sm_state *s;
  int i;

  s = data;
  if (s != NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: destroy_sm_state_kcs %s\n", s->s_name);
#endif
    if (s->s_name)      { free(s->s_name); s->s_name = NULL; }
    if (s->s_edge_list) { 
      for (i=0; i<s->s_edge_list_count; i++){
        destroy_sm_edge_kcs(s->s_edge_list[i]);
        s->s_edge_list[i] = NULL;
      }
      free(s->s_edge_list); 
      s->s_edge_list = NULL; 
    }
    if (s->s_op_list){
      for (i=0; i<s->s_op_list_count; i++){
        destroy_sm_op_kcs(s->s_op_list[i]);
        s->s_op_list[i] = NULL;
      }
      free(s->s_op_list); 
      s->s_op_list = NULL; 
    }
    free(s);
  }
}
void print_sm_state_kcs(struct katcp_dispatch *d, void *data)
{
  struct kcs_sm_state *s;
  int i;
  s = data;
  if (s == NULL)
    return;

  //log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "state node: %s", s->s_name);
  //prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "#state node:");
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, s->s_name);

  for (i=0; i<s->s_edge_list_count; i++){
    if (s->s_edge_list[i]->e_next != NULL){
      //log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "node edge [%d]: to %s", i, s->s_edge_list[i]->e_next->s_name);
      append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "#node edge:");
      append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, s->s_edge_list[i]->e_next->s_name);
    }
#ifdef DEBUG
    fprintf(stderr,"statemachine: edge [%d] %p\n", i, s->s_edge_list[i]);
#endif
  }

  for (i=0; i<s->s_op_list_count; i++){
#ifdef DEBUG
    fprintf(stderr,"statemachine: op [%d] %p\n", i, s->s_op_list[i]);
#endif
    print_sm_op_kcs(d, s->s_op_list[i]);
  }
}

char *getkey_sm_state_kcs(void *data)
{
  struct kcs_sm_state *s;
  s = data;
  if (s == NULL)
    return NULL;
  return s->s_name;
}

int create_named_node_kcs(struct katcp_dispatch *d, char *s_name)
{
  struct kcs_sm_state *s;

  if (s_name == NULL)
    return -1;

#ifdef DEBUG
  fprintf(stderr, "statemachine: about to create statemachine <%s>\n", s_name);
#endif

  s = create_sm_state_kcs(s_name);
  if (s == NULL)
    return -1;
   
  if (store_data_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE, KATCP_DEP_BASE, s_name, s, &print_sm_state_kcs, &destroy_sm_state_kcs, NULL, NULL, NULL, &getkey_sm_state_kcs) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not store datatype %s %s", KATCP_TYPE_STATEMACHINE_STATE, s_name);
#ifdef DEBUG
    fprintf(stderr, "statemachine: could not store datatype %s %s\n", KATCP_TYPE_STATEMACHINE_STATE, s_name);
#endif
    destroy_sm_state_kcs(s);
    return -1;
  }
  
  return 0;
}

int create_named_edge_kcs(struct katcp_dispatch *d, char *n_current, char *n_next, char *edge)
{
  struct kcs_sm_state *s_current, *s_next;
  struct kcs_sm_edge *e;
  struct kcs_sm_edge *(*e_call)(struct katcp_dispatch *, struct kcs_sm_state *);
  
  if (n_current == NULL || n_next == NULL)
    return -1;
  
  s_current = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE, n_current);
  if (s_current == NULL)
    return -1;

  s_next = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE, n_next);
  if (s_next == NULL)
    return -1;
  
  e_call = get_key_data_type_katcp(d, KATCP_TYPE_EDGE, edge);
  if (e_call != NULL){
    e = (*e_call)(d, s_next);
    if (e == NULL)
      return -1;
  } else {
    e = create_sm_edge_kcs(s_next, NULL);
  }

  s_current->s_edge_list = realloc(s_current->s_edge_list, sizeof(struct kcs_sm_edge *) * (s_current->s_edge_list_count + 1));
  if (s_current->s_edge_list == NULL){
    destroy_sm_edge_kcs(e);
    return -1;
  }

  s_current->s_edge_list[s_current->s_edge_list_count] = e;
  s_current->s_edge_list_count++;

  return 0;
}

int create_named_op_kcs(struct katcp_dispatch *d, char *state, char *op)
{
  struct kcs_sm_state *s;
  struct kcs_sm_op *o;
  struct kcs_sm_op *(*o_setup)(struct katcp_dispatch *, struct kcs_sm_state *);
  
  if (state == NULL || op == NULL)
    return -1;

  s = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE, state);
  if (s == NULL)
    return -1;

  o_setup = get_key_data_type_katcp(d, KATCP_TYPE_OPERATION, op);
  if (o_setup == NULL)
    return -1;

  o = (*o_setup)(d, s);
  if (o == NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: op call failed\n");
#endif
    return -1;
  }

  s->s_op_list = realloc(s->s_op_list, sizeof(struct kcs_sm_op *) * (s->s_op_list_count + 1));
  if (s->s_op_list == NULL){
    destroy_sm_op_kcs(o);
    return -1;
  }

  s->s_op_list[s->s_op_list_count] = o;
  s->s_op_list_count++;
  
  return 0;
}

int statemachine_node_kcs(struct katcp_dispatch *d)
{
  char *s_name;

  s_name = arg_string_katcp(d, 2);

  if (create_named_node_kcs(d, s_name) < 0)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_OK;
}

int statemachine_edge_kcs(struct katcp_dispatch *d)
{
  char *n_next, *n_current, *edge;

  n_current = arg_string_katcp(d, 1);
  n_next    = arg_string_katcp(d, 3);
  edge      = arg_string_katcp(d, 4);
   
  if (create_named_edge_kcs(d, n_current, n_next, edge) < 0)
    return KATCP_RESULT_FAIL;
  
  return KATCP_RESULT_OK;
}

int statemachine_op_kcs(struct katcp_dispatch *d)
{
  char *state, *op;
  //struct katcl_parse *p;
  //int max;
  
  state = arg_string_katcp(d, 1);
  op    = arg_string_katcp(d, 3);

#if 0
  p = ready_katcp(d);
  max = get_count_parse_katcl(p);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "parse count: %d", max);

#endif

  if (create_named_op_kcs(d, state, op) < 0)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_OK;
}

int statemachine_print_ds_kcs(struct katcp_dispatch *d)
{
  print_types_katcp(d);
  return KATCP_RESULT_OK;
}

int statemachine_print_stack_kcs(struct katcp_dispatch *d)
{
#if 0
  struct kcs_sm *m;
  char *m_name;

  m_name = arg_string_katcp(d, 1);

  if (m_name == NULL)
    return KATCP_RESULT_FAIL;

  m = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE, m_name);
  if (m == NULL)
    return KATCP_RESULT_FAIL;
  
  print_stack_katcp(d, m->m_stack);
#endif
  return KATCP_RESULT_OK;
}

int statemachine_print_pc_kcs(struct katcp_dispatch *d)
{
#if 0
  struct kcs_sm *m;
  struct kcs_sm_state *s;
  char *m_name;

  m_name = arg_string_katcp(d, 1);

  if (m_name == NULL)
    return KATCP_RESULT_FAIL;

  m = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE, m_name);
  if (m == NULL)
    return KATCP_RESULT_FAIL;
  
  s = m->m_pc;
  if (s == NULL)
    return KATCP_RESULT_FAIL;

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s program counter is %s", m_name, s->s_name);
#endif
  return KATCP_RESULT_OK;
}

int statemachine_print_oplist_kcs(struct katcp_dispatch *d)
{
  struct katcp_type *t;

  t = find_name_type_katcp(d, KATCP_TYPE_OPERATION);
  if (t == NULL)
    return KATCP_RESULT_FAIL;

  print_type_katcp(d, t, 1); 

  return KATCP_RESULT_OK;
}

void print_sm_mod_kcs(struct katcp_dispatch *d, void *data)
{
  struct katcp_module *m;
  m = data;
  if (m == NULL)
    return;
  
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "#module:");
  append_string_katcp(d, KATCP_FLAG_STRING, m->m_name);
  append_string_katcp(d, KATCP_FLAG_STRING, "handle:");
  append_args_katcp(d, KATCP_FLAG_LAST, "%p", m->m_handle);
}
void destroy_sm_mod_kcs(void *data)
{
  struct katcp_module *m;
  m = data;
  if (m != NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: destroy module %s\n", m->m_name); 
#endif
    if (m->m_name) free(m->m_name);
    if (m->m_handle) dlclose(m->m_handle);
    free(m);
  }
}

char *getkey_module_katcp(void *data)
{
  struct katcp_module *m;
  m = data;
  if (m == NULL)
    return NULL;
  return m->m_name;
}

struct katcp_module *create_module_katcp()
{
  struct katcp_module *m;

  m = malloc(sizeof(struct katcp_module));
  if (m == NULL)
    return NULL;

  m->m_name = NULL;
  m->m_handle = NULL;

  return m;
}

int statemachine_loadmod_kcs(struct katcp_dispatch *d)
{
  char *mod, *err;
  void *handle;
  int (*call)(struct katcp_dispatch *);
  struct katcp_module *m;
  
  m = create_module_katcp();
  if (m == NULL)
    return KATCP_RESULT_FAIL;

  mod = arg_copy_string_katcp(d, 2);

  if (mod == NULL)
    return KATCP_RESULT_FAIL;
  
  m->m_name = mod;

  handle = dlopen(mod, RTLD_NOW);

  if (handle == NULL){
    err = dlerror();
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s", err);
#ifdef DEBUG
    fprintf(stderr, "statemachine: %s\n", err);
#endif
    destroy_sm_mod_kcs(m);
    return KATCP_RESULT_FAIL;
  }
  
  m->m_handle = handle;
  
  call = dlsym(handle, "init_mod");
  if ((err = dlerror()) != NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "error btw symlist and syms%s", err);
#ifdef DEBUG
    fprintf(stderr, "statemachine: error btw symlist and syms %s\n", err);
#endif
    destroy_sm_mod_kcs(m);
    return KATCP_RESULT_FAIL;
  } 
   
  if ((*call)(d) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "error running init_mod");
#ifdef DEBUG
    fprintf(stderr, "statemachine: error running init_mod\n");
#endif
    destroy_sm_mod_kcs(m);
    return KATCP_RESULT_FAIL;
  } 
  
  if (store_data_type_katcp(d, KATCP_TYPE_MODULES, KATCP_DEP_ELAV,  mod, m, &print_sm_mod_kcs, &destroy_sm_mod_kcs, NULL, NULL, NULL, &getkey_module_katcp) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not store datatype %s %s", KATCP_TYPE_MODULES, mod);
#ifdef DEBUG
    fprintf(stderr, "statemachine: could not store datatype %s %s\n", KATCP_TYPE_MODULES, mod);
#endif
    destroy_sm_mod_kcs(m);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

struct kcs_sched_task *create_sched_task_kcs(struct kcs_sm_state *s, struct katcp_tobject *to)
{
  struct kcs_sched_task *t;

  if (s == NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: cannot schedule a null task\n");
#endif
    return NULL;
  }

  t = malloc(sizeof(struct kcs_sched_task));
  if (t == NULL)
    return NULL;
  
  t->t_rtn     = 0;
  t->t_state   = TASK_STATE_RUN_OPS; 
  t->t_edge_i  = 0;
  
  t->t_pc = s;
  
  t->t_stack = create_stack_katcp();
  if (t->t_stack == NULL){
    free(t);
    return NULL;
  }
  
  if (to != NULL){
    push_tobject_katcp(t->t_stack, to);
  }

  return t;
}

void destroy_sched_task_kcs(struct kcs_sched_task *t)
{
  if (t != NULL){
    destroy_stack_katcp(t->t_stack);
    free(t);
  }
}


int statemachine_run_ops_kcs(struct katcp_dispatch *d, struct katcp_notice *n, struct kcs_sched_task *t)
{
  struct kcs_sm_state *s;
  struct katcp_stack *stack;
  struct kcs_sm_op *op;
  int i, rtn;

  s = get_task_pc_kcs(t);
  stack = get_task_stack_kcs(t);

#if 1 
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "running ops in state %s", s->s_name);
#endif

  for (i=0; i<s->s_op_list_count; i++){
    op = s->s_op_list[i];
    if (op != NULL){
      rtn = (*(op->o_call))(d, stack, op->o_tobject);
#ifdef DEBUG
      fprintf(stderr,"statemachine RUN: op %p call returned %d\n", op, rtn);
#endif  
      if (rtn < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "statemachine op [%d] error rtn: %d", i, rtn);
        return rtn;
      }
    }
  }
  
  wake_notice_katcp(d, n, NULL);
  
  return TASK_STATE_FOLLOW_EDGES;
}

int statemachine_follow_edges_kcs(struct katcp_dispatch *d, struct katcp_notice *n, struct kcs_sched_task *t)
{
  struct kcs_sm_state *s;
  struct kcs_sm_edge *e;
  struct katcp_stack *stack;
  int rtn;

  s = get_task_pc_kcs(t);
  stack = get_task_stack_kcs(t);
  
#ifdef DEBUG
  fprintf(stderr, "statemachine: follow edges [%d]\n",t->t_edge_i);
#endif

  if (s->s_edge_list_count <= 0){
    wake_notice_katcp(d, n, NULL);
    return TASK_STATE_CLEAN_UP;
  }
#if 0  
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "testing edges in state %s", s->s_name);
#endif
  e = s->s_edge_list[t->t_edge_i];
  if (e != NULL){
    if (e->e_call != NULL){
      rtn = (*(e->e_call))(d, n, stack);
    }
    else {
      /*this is for edges with no callback (default edge) always follow*/
      rtn = 0;
      wake_notice_katcp(d, n, NULL);
    }
    if (rtn == 0){
      set_task_pc_kcs(t, e->e_next);
      t->t_edge_i = 0;      
#ifdef DEBUG
      fprintf(stderr, "statemachine: follow edges EDGE RETURNS SUCCESS\n");
#endif
      return TASK_STATE_RUN_OPS;
    }
  }

  if ((t->t_edge_i+1) < s->s_edge_list_count){
    t->t_edge_i++;
#ifdef DEBUG
    fprintf(stderr, "statemachine: follow edges STILL TRYING\n");
#endif

    wake_notice_katcp(d, n, NULL);

    return TASK_STATE_FOLLOW_EDGES;
  }

#ifdef DEBUG
  fprintf(stderr, "statemachine: follow edges FAIL cleanup state\n");
#endif

  wake_notice_katcp(d, n, NULL);

  return TASK_STATE_CLEAN_UP;
}

int statemachine_process_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct kcs_sched_task *t;
  int rtn;
  
  t = data;
  if (t == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot run sched with null task");
    return 0;
  }
#if 0
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "running sched notice with task state: %d", t->t_state);
#endif 
  switch (t->t_state){
    case TASK_STATE_RUN_OPS:
#ifdef DEBUG
      fprintf(stderr, "statemachine: process about to run ops\n");
#endif
      rtn = statemachine_run_ops_kcs(d, n, t);
      break;

    case TASK_STATE_FOLLOW_EDGES:
#ifdef DEBUG
      fprintf(stderr, "statemachine: process about to follow edges\n");
#endif
      rtn = statemachine_follow_edges_kcs(d, n, t);
      break;

    case TASK_STATE_CLEAN_UP:
#ifdef DEBUG
      fprintf(stderr, "statemachine: process about to cleanup\n");
#endif
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "printing task stack and cleaning up");
      print_task_stack_kcs(d, t);
     
      prepend_reply_katcp(d);
      if (t->t_rtn < 0){
        append_string_katcp(d, KATCP_FLAG_STRING, "fail");  
        append_signed_long_katcp(d, KATCP_FLAG_SLONG | KATCP_FLAG_LAST, t->t_rtn);  
      } else {
        append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "ok");  
      }
      
      destroy_sched_task_kcs(t);

      resume_katcp(d);
#ifdef DEBUG
      fprintf(stderr, "**********[end statemachine run]**********\n");
#endif
      return 0;
  }

  if (rtn < 0){
#ifdef DEBUG
    fprintf(stderr, "statemachine: process error setting task state to CLEAN UP\n");
#endif
#if 0
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "statemachine: going to cleanup state");
#endif
    t->t_rtn  = rtn;
    rtn = TASK_STATE_CLEAN_UP;
    wake_notice_katcp(d, n, NULL);
  }

  t->t_state = rtn;
  
  //log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "process abt to rtn %d", t->t_state);

  return t->t_state;
}

int start_process_kcs(struct katcp_dispatch *d, char *startnode, struct katcp_tobject *to)
{
  struct katcp_notice *n;
  struct kcs_sched_task *t;
  struct kcs_sm_state *s;
  char *name;
  
#ifdef DEBUG
  fprintf(stderr, "**********[start statemachine run]**********\n");
#endif

  s = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE, startnode);
  
  if (s == NULL)
    return -1;
  
  t = create_sched_task_kcs(s, to);
  if (t == NULL)
    return -1;
 
  name = gen_id_avltree("sm");

  n = register_notice_katcp(d, name, 0, &statemachine_process_kcs, t);

  free(name);

  if (n == NULL){
    destroy_sched_task_kcs(t);
    return -1;
  }
  
  wake_notice_katcp(d, n, NULL);

  return 0;
}

int statemachine_run_kcs(struct katcp_dispatch *d)
{
  char *startnode;

  startnode = arg_string_katcp(d, 2);

  if (startnode == NULL)
    return KATCP_RESULT_FAIL;

  if (start_process_kcs(d, startnode, NULL) < 0)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_PAUSE;
}

int statemachine_stopall_kcs(struct katcp_dispatch *d)
{
  struct katcp_notice **n_set, *n;
  int n_count, i;
  struct kcs_sched_task *t;
  void *data[1];

#ifdef DEBUG
  fprintf(stderr, "statemachine: STOPALL start\n");
#endif
  
  n_set = NULL;
  n_count = 0;

  n_count = find_prefix_notices_katcp(d, "sm", n_set, n_count);
  
  if (n_count <= 0)
    return KATCP_RESULT_FAIL;

  n_set = malloc(sizeof(struct katcp_notice *) * n_count);

  if (n_set == NULL)
    return KATCP_RESULT_FAIL;

  n_count = find_prefix_notices_katcp(d, "sm", n_set, n_count);
  
  for (i=0; i<n_count; i++){
    n = n_set[i];
    if (n != NULL && n->n_vector != NULL){
      
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "stopall notice: %s", n->n_name);
      
      if (fetch_data_notice_katcp(d, n, data, 1) == 1){
        t = data[0];
        if (t != NULL){
          if (remove_notice_katcp(d, n, &statemachine_process_kcs, t) < 0){
            log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "could not remove notice");
          }

          t->t_state = TASK_STATE_CLEAN_UP;
          statemachine_process_kcs(d, n, t);  
        }
      }
    }
  }
  
  free(n_set);

#ifdef DEBUG
  fprintf(stderr, "statemachine: STOPALL end\n");
#endif

  return KATCP_RESULT_OK;
}

int statemachine_greeting_kcs(struct katcp_dispatch *d)
{
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "loadmod [so name]");  
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "node [state name]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "[state name] op [operation] ([type] [value])");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "[from state name] edge [to state name] ([condition])");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "run [start state]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "ds (print the entire datastore)");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "[sm name] ps (print statemachine stack)");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "[sm name] pc (print statemachine program counter)");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "oplist (print op list)");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "stopall");
  return KATCP_RESULT_OK;
}

int statemachine_cmd(struct katcp_dispatch *d, int argc)
{
  switch (argc){
    case 1:
      return statemachine_greeting_kcs(d);
      
      break;
    case 2:
      if (strcmp(arg_string_katcp(d, 1), "ds") == 0)
        return statemachine_print_ds_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "oplist") == 0)
        return statemachine_print_oplist_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "stopall") == 0)
        return statemachine_stopall_kcs(d);
      
      break;
    case 3:
#if 0
      if (strcmp(arg_string_katcp(d, 1), "declare") == 0)
        return statemachine_declare_kcs(d);
#endif
      if (strcmp(arg_string_katcp(d, 2), "ps") == 0)
        return statemachine_print_stack_kcs(d);
      if (strcmp(arg_string_katcp(d, 2), "pc") == 0)
        return statemachine_print_pc_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "loadmod") == 0)
        return statemachine_loadmod_kcs(d);

#if 0
      break;
    case 3:
#endif
      if (strcmp(arg_string_katcp(d, 1), "node") == 0)
        return statemachine_node_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "run") == 0)
        return statemachine_run_kcs(d);
      
      break;
    case 6:
      
      break;
  }
  if (argc > 3){
    if (strcmp(arg_string_katcp(d, 2), "op") == 0)
      return statemachine_op_kcs(d);
    if (strcmp(arg_string_katcp(d, 2), "edge") == 0)
      return statemachine_edge_kcs(d);
  }
  return KATCP_RESULT_FAIL;
}

