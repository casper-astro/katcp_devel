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
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "integer type: %d", *o);
}
void destroy_integer_type_kcs(void *data)
{
  int *o;
  o = data;
  if (o != NULL){
    free(o);
  }
}
int compare_integer_type_kcs(void *a, void *b)
{
  int *x, *y;
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
void *parse_integer_type_kcs(char **str)
{
  int *o;
  o = malloc(sizeof(int));
  if (o == NULL)
    return NULL;
  *o = atoi(str[0]);
  return o;
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
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "string type: %s", o);
}
void destroy_string_type_kcs(void *data)
{
  char *o;
  o = data;
  if (o != NULL){
    free(o);
  }
}
void *parse_string_type_kcs(char **str)
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

struct kcs_sm_op *create_sm_op_kcs(int (*call)(struct katcp_dispatch *, struct kcs_sm_state *, struct katcp_stack_obj *), struct katcp_stack_obj *o)
{
  struct kcs_sm_op *op;
  
  if (call == NULL)
    return NULL;

  op = malloc(sizeof(struct kcs_sm_op));
  if (op == NULL)
    return NULL;

  op->o_call = call;
  op->o_stack_obj = o;

  return op;
}
void destroy_sm_op_kcs(struct kcs_sm_op *op)
{
  if (op != NULL){
    op->o_call = NULL;
    destroy_obj_stack_katcp(op->o_stack_obj);
    free(op);
  }
} 
void print_sm_op_kcs(struct katcp_dispatch *d, struct kcs_sm_op *op)
{
  if (op != NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "node operation: call:(%p) stackable data:(%p)", op->o_call, op->o_stack_obj);
#ifdef DEBUG
    fprintf(stderr,"statemachine: print_sm_op call: (%p)\n", op->o_call);
#endif
    print_stack_obj_katcp(d, op->o_stack_obj);
  } 
}

int pushstack_statemachine_kcs(struct katcp_dispatch *d, struct kcs_sm_state *s, struct katcp_stack_obj *o)
{
  struct kcs_sm *m;
  struct katcp_stack *stack;

  if (s == NULL || o == NULL)
    return -1;

  m = s->s_sm;
  if (m == NULL)
    return -1;

  stack = m->m_stack;
  if (stack == NULL)
    return -1;

#ifdef DEBUG
  fprintf(stderr, "statemachine: pushstack call in sm [%s] node <%s> with o:(%p)\n",m->m_name, s->s_name, o);
#endif

  return push_stack_obj_katcp(stack, copy_obj_stack_katcp(o));
}

struct kcs_sm_op *pushstack_setup_statemachine_kcs(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
#define ARG_BASE 5
  struct katcp_type *t;
  struct katcp_stack_obj *o;
  struct kcs_sm_op *op;
  char **data, *type;
  void *temp;
  int num, i;

  type = arg_string_katcp(d, 4);
  //data = arg_string_katcp(d, 5);

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
    fprintf(stderr, "statemachine: pushsetup data[%d of %d]: %s\n", i, num, data[i]);
#endif
  }
  data[num] = NULL; 
  
#ifdef DEBUG
  fprintf(stderr, "statemachine: call type parse function\n");
#endif
  temp = (*t->t_parse)(data);
  
  if (temp != NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: free temp parse data\n");
#endif
    free(data);
  }

  o = create_obj_stack_katcp(temp, t);
  if (o == NULL){
    (*t->t_free)(temp);
#ifdef DEBUG
    fprintf(stderr, "statemachine: pushsetup could not create stack obj\n");
#endif
    return NULL;
  }
  
  op = create_sm_op_kcs(&pushstack_statemachine_kcs, o);
  if (op == NULL){
    (*t->t_free)(temp);
    destroy_obj_stack_katcp(o);
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "statemachine: pushsetup created op (%p)\n", op);
#endif

  return op;
#undef ARG_BASE
}


int statemachine_init_kcs(struct katcp_dispatch *d)
{ /*statemachine initializations go here*/
  int rtn;
  
  /*register basic types*/
  rtn  = register_name_type_katcp(d, KATCP_TYPE_INTEGER, &print_integer_type_kcs, &destroy_integer_type_kcs, NULL, &compare_integer_type_kcs, &parse_integer_type_kcs);
  rtn += register_name_type_katcp(d, KATCP_TYPE_STRING, &print_string_type_kcs, &destroy_string_type_kcs, NULL, NULL, &parse_string_type_kcs);
  rtn += register_name_type_katcp(d, KATCP_TYPE_FLOAT, NULL, NULL, NULL, NULL, NULL);
  rtn += register_name_type_katcp(d, KATCP_TYPE_DOUBLE, NULL, NULL, NULL, NULL, NULL);
  rtn += register_name_type_katcp(d, KATCP_TYPE_CHAR, NULL, NULL, NULL, NULL, NULL);

  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_PUSH, &pushstack_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL);
  
 /* 
  if (store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_PUSH, &pushstack_setup_statemachine_kcs, NULL, NULL, NULL, NULL, NULL) < 0){
#ifdef DEBUG
    fprintf(stderr, "statemachine: init could not store %s %s\n", KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_PUSH);
#endif
    return -1;
  }

  if (store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_POP, &pop_stack_katcp, NULL, NULL) < 0){
#ifdef DEBUG
    fprintf(stderr, "statemachine: init could not store %s %s\n", KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_POP);
#endif
    return -1;
  }
  
  if (store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_PEEK, &peek_stack_katcp, NULL, NULL) < 0){
#ifdef DEBUG
    fprintf(stderr, "statemachine: init could not store %s %s\n", KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_PEEK);
#endif
    return -1;
  }
  
  if (store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_INDEX, &index_stack_katcp, NULL, NULL) < 0){
#ifdef DEBUG
    fprintf(stderr, "statemachine: init could not store %s %s\n", KATCP_TYPE_OPERATION, KATCP_OPERATION_STACK_PEEK);
#endif
    return -1;
  }
  
  */
  return rtn;
}


struct kcs_sm *create_sm_kcs(char *name)
{
  struct kcs_sm *m;

  if (name == NULL)
    return NULL;

  m = malloc(sizeof(struct kcs_sm));

  if (m == NULL)
    return NULL;

  m->m_pc    = NULL;
  m->m_name  = strdup(name);

  if (m->m_name == NULL){
    free(m);
    return NULL;
  }
  
  m->m_stack = create_stack_katcp();

  if (m->m_stack == NULL){
    free(m->m_name);
    m->m_name = NULL;
    free(m);
    return NULL;
  }

  return m;
}
void destroy_sm_kcs(void *data)
{
  struct kcs_sm *m;
  m = data;
  if (m != NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: destroy_sm_kcs %s\n", m->m_name);
#endif
    if (m->m_name != NULL){ free(m->m_name); m->m_name = NULL; } 
    if (m->m_stack != NULL) { destroy_stack_katcp(m->m_stack); m->m_stack = NULL; }
    if (m->m_pc != NULL){ m->m_pc = NULL; }
    free(m);
  }
}
void print_sm_kcs(struct katcp_dispatch *d, void *data)
{
  struct kcs_sm *m;
  m = data;
  if (m == NULL)
    return;
#ifdef DEBUG
  fprintf(stderr, "statemachine: print_sm_kcs %s (%p) with pc (%p) and stack follows\n", m->m_name, m, m->m_pc);
  print_stack_katcp(d, m->m_stack);
#endif
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "statemachine: %s", m->m_name);
}

struct kcs_sm_edge *create_sm_edge_kcs(struct kcs_sm_state *s_next, int (*call)(struct katcp_dispatch *, struct katcp_notice *, void *))
{
  struct kcs_sm_edge *e;
#if 0
  if (s_next == NULL || call == NULL)
#endif
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

  
struct kcs_sm_state *create_sm_state_kcs(struct kcs_sm *m, char *name)
{
  struct kcs_sm_state *s;

  if (name == NULL || m == NULL)
    return NULL;

  s = malloc(sizeof(struct kcs_sm_state));
  if (s == NULL)
    return NULL;

  s->s_sm              = m;
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
    if (s->s_sm)        { s->s_sm = NULL; }
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
#ifdef DEBUG
  fprintf(stderr, "statemachine: print_sm_state_kcs %s (%p) with sm (%p)\n", s->s_name, s, s->s_sm);
#endif

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "state node: %s", s->s_name);

  for (i=0; i<s->s_edge_list_count; i++){
    if (s->s_edge_list[i]->e_next != NULL)
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "node edge [%d]: to %s", i, s->s_edge_list[i]->e_next->s_name);
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


int statemachine_declare_kcs(struct katcp_dispatch *d)
{
  struct kcs_sm *m;
  char *name;

  name = arg_string_katcp(d, 2);

  if (name == NULL)
    return KATCP_RESULT_FAIL;

  m = create_sm_kcs(name);
  if (m == NULL)
    return KATCP_RESULT_FAIL;
 
  if (store_data_type_katcp(d, KATCP_TYPE_STATEMACHINE, name, m, &print_sm_kcs, &destroy_sm_kcs, NULL, NULL, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not store datatype %s %s", KATCP_TYPE_STATEMACHINE, name);
#ifdef DEBUG
    fprintf(stderr, "statemachine: could not store datatype %s %s\n", KATCP_TYPE_STATEMACHINE, name);
#endif
    destroy_sm_kcs(m);
    return KATCP_RESULT_FAIL;
  }

#ifdef DEBUG
  fprintf(stderr, "statemachine: created statemachine %s (%p)\n", name, m);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "created statemachine %s (%p)", name, m);
#endif

  return KATCP_RESULT_OK;
}

int statemachine_node_kcs(struct katcp_dispatch *d)
{
  struct kcs_sm *m;
  struct kcs_sm_state *s;
  char *m_name, *s_name;

  m_name = arg_string_katcp(d, 1);
  s_name = arg_string_katcp(d, 3);

  if (m_name == NULL || s_name == NULL)
    return KATCP_RESULT_FAIL;
  
  m = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE, m_name);
  if (m == NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: could not find statemachine <%s>\n", m_name);
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not find statemachine <%s>", m_name);
    return KATCP_RESULT_FAIL;
  }

  s = create_sm_state_kcs(m, s_name);
  if (s == NULL)
    return KATCP_RESULT_FAIL;
   
  if (store_data_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE, s_name, s, &print_sm_state_kcs, &destroy_sm_state_kcs, NULL, NULL, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not store datatype %s %s", KATCP_TYPE_STATEMACHINE_STATE, s_name);
#ifdef DEBUG
    fprintf(stderr, "statemachine: could not store datatype %s %s\n", KATCP_TYPE_STATEMACHINE_STATE, s_name);
#endif
    destroy_sm_state_kcs(s);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int statemachine_edge_kcs(struct katcp_dispatch *d)
{
  struct kcs_sm_state *s_current, *s_next;
  struct kcs_sm_edge *e;
  struct kcs_sm_edge *(*e_call)(struct katcp_dispatch *, struct kcs_sm_state *);
  char *n_next, *n_current, *edge;

  n_current = arg_string_katcp(d, 1);
  n_next    = arg_string_katcp(d, 3);
  edge      = arg_string_katcp(d, 4);
  
  if (n_current == NULL || n_next == NULL)
    return KATCP_RESULT_FAIL;
  
  s_current = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE, n_current);
  if (s_current == NULL)
    return KATCP_RESULT_FAIL;

  s_next = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE, n_next);
  if (s_next == NULL)
    return KATCP_RESULT_FAIL;
  
  e_call = get_key_data_type_katcp(d, KATCP_TYPE_EDGE, edge);
  if (e_call == NULL)
    return KATCP_RESULT_FAIL;

  e = (*e_call)(d, s_next);
  if (e == NULL)
    return KATCP_RESULT_FAIL;

  s_current->s_edge_list = realloc(s_current->s_edge_list, sizeof(struct kcs_sm_edge *) * (s_current->s_edge_list_count + 1));
  if (s_current->s_edge_list == NULL){
    destroy_sm_edge_kcs(e);
    return KATCP_RESULT_FAIL;
  }

  s_current->s_edge_list[s_current->s_edge_list_count] = e;
  s_current->s_edge_list_count++;
  
  return KATCP_RESULT_OK;
}

int statemachine_op_kcs(struct katcp_dispatch *d)
{
  struct kcs_sm_state *s;
  struct kcs_sm_op *o;
  struct kcs_sm_op *(*o_setup)(struct katcp_dispatch *, struct kcs_sm_state *);
  char *state, *op;

  state = arg_string_katcp(d, 1);
  op    = arg_string_katcp(d, 3);

  if (state == NULL || op == NULL)
    return KATCP_RESULT_FAIL;

  s = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE, state);
  if (s == NULL)
    return KATCP_RESULT_FAIL;

  o_setup = get_key_data_type_katcp(d, KATCP_TYPE_OPERATION, op);
  if (o_setup == NULL)
    return KATCP_RESULT_FAIL;

  o = (*o_setup)(d, s);
  if (o == NULL){
#ifdef DEBUG
    fprintf(stderr, "statemachine: op call failed\n");
#endif
    return KATCP_RESULT_FAIL;
  }

  s->s_op_list = realloc(s->s_op_list, sizeof(struct kcs_sm_op *) * (s->s_op_list_count + 1));
  if (s->s_op_list == NULL){
    destroy_sm_op_kcs(o);
    return KATCP_RESULT_FAIL;
  }

  s->s_op_list[s->s_op_list_count] = o;
  s->s_op_list_count++;

  return KATCP_RESULT_OK;
}

int statemachine_print_ds_kcs(struct katcp_dispatch *d)
{
  print_types_katcp(d);
  return KATCP_RESULT_OK;
}

int statemachine_print_stack_kcs(struct katcp_dispatch *d)
{
  struct kcs_sm *m;
  char *m_name;

  m_name = arg_string_katcp(d, 2);

  if (m_name == NULL)
    return KATCP_RESULT_FAIL;

  m = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE, m_name);
  if (m == NULL)
    return KATCP_RESULT_FAIL;
  
  print_stack_katcp(d, m->m_stack);

  return KATCP_RESULT_OK;
}

int statemachine_print_oplist_kcs(struct katcp_dispatch *d)
{
  struct katcp_type *t;

  t = find_name_type_katcp(d, KATCP_TYPE_OPERATION);
  if (t == NULL)
    return KATCP_RESULT_FAIL;

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "listing avaliable statemachine operations");
  print_type_katcp(d, t); 

  return KATCP_RESULT_OK;
}

void print_sm_mod_kcs(struct katcp_dispatch *d, void *data)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "module: handle (%p)", data);
}
void destroy_sm_mod_kcs(void *data)
{
  if (data != NULL){
    dlclose(data);
  }
}

int statemachine_loadmod_kcs(struct katcp_dispatch *d)
{
  char *mod, *err;
  void *handle;
  int (*call)(struct katcp_dispatch *);

  mod = arg_string_katcp(d, 2);

  if (mod == NULL)
    return KATCP_RESULT_FAIL;
#if 0
  handle = dlopen(mod, RTLD_NOW | RTLD_NODELETE);
#endif
  handle = dlopen(mod, RTLD_NOW);

  if (handle == NULL){
    err = dlerror();
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s\n", err);
#ifdef DEBUG
    fprintf(stderr, "statemachine: %s\n", err);
#endif
    return KATCP_RESULT_FAIL;
  }
  
  call = dlsym(handle, "init_mod");
  if ((err = dlerror()) != NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "error btw symlist and syms%s", err);
#ifdef DEBUG
    fprintf(stderr, "statemachine: error btw symlist and syms %s\n", err);
#endif
    return KATCP_RESULT_FAIL;
  } 
   
  if ((*call)(d) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "error running init_mod");
#ifdef DEBUG
    fprintf(stderr, "statemachine: error running init_mod\n");
#endif
    dlclose(handle);
    return KATCP_RESULT_FAIL;
  }
#if 0
  dlclose(handle);
#endif
  
  if (store_data_type_katcp(d, KATCP_TYPE_MODULES, mod, handle, &print_sm_mod_kcs, &destroy_sm_mod_kcs, NULL, NULL, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not store datatype %s %s", KATCP_TYPE_MODULES, mod);
#ifdef DEBUG
    fprintf(stderr, "statemachine: could not store datatype %s %s\n", KATCP_TYPE_MODULES, mod);
#endif
    destroy_sm_mod_kcs(handle);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int statemachine_run_kcs(struct katcp_dispatch *d)
{
  struct kcs_sm *m;
  struct kcs_sm_state *s;
  struct kcs_sm_edge *e;
  struct kcs_sm_op *op;

  char *machine, *startnode;
  int run, i, rtn;

  machine = arg_string_katcp(d, 1);
  startnode = arg_string_katcp(d, 3);

  if (machine == NULL || startnode == NULL)
    return KATCP_RESULT_FAIL;

  m = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE, machine);
  if (m == NULL)
    return KATCP_RESULT_FAIL;

  s = get_key_data_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE, startnode);
  if (s == NULL)
    return KATCP_RESULT_FAIL;

  m->m_pc = s;
  run = 1;

#ifdef DEBUG
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "about to run statemachine %s with startnode %s", m->m_name, s->s_name);
#endif

  do {
    s = m->m_pc;

    for (i=0; i<s->s_op_list_count; i++){
      op = s->s_op_list[i];
      if (op != NULL){
        rtn = (*(op->o_call))(d, s, op->o_stack_obj);
#ifdef DEBUG
        fprintf(stderr,"statemachine RUN: op %p call returned %d\n", op, rtn);
#endif  
        if (rtn < 0){
          run = 0;
          return KATCP_RESULT_FAIL;
        } 
      }
    }
    
    if (s->s_edge_list_count == 0)
      run = 0;

    for (i=0; i<s->s_edge_list_count; i++){
      e = s->s_edge_list[i];
      if (e != NULL){
        rtn = (*(e->e_call))(d, NULL, s);
        if (rtn == 0){
          m->m_pc = e->e_next;
          break;
        }
      }
    }
    if (i+1 == s->s_edge_list_count)
      run = 0;

  } while (run);
  
  return KATCP_RESULT_OK;
}

int statemachine_greeting_kcs(struct katcp_dispatch *d)
{
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"loadmod [so name]");  
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"declare [sm name]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"[sm name] node [state name]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"[state name] op [operation] ([type] [value])");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"[state name] edge [state name 1] [state name 2] ([condition])");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"[sm name] run [start state]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"ds (print the entire datastore)");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"ps [sm name] (print statemachine stack)");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"oplist (print op list)");
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
      
      break;
    case 3:
      if (strcmp(arg_string_katcp(d, 1), "declare") == 0)
        return statemachine_declare_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "ps") == 0)
        return statemachine_print_stack_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "loadmod") == 0)
        return statemachine_loadmod_kcs(d);

      break;
    case 4:
      if (strcmp(arg_string_katcp(d, 2), "node") == 0)
        return statemachine_node_kcs(d);
      if (strcmp(arg_string_katcp(d, 2), "run") == 0)
        return statemachine_run_kcs(d);
      
      break;
    case 6:
      
      break;
  }
  if (argc > 3){
    if (strcmp(arg_string_katcp(d, 2), "op") == 0)
      return statemachine_op_kcs(d);
  }
  if (argc > 4){
    if (strcmp(arg_string_katcp(d, 2), "edge") == 0)
      return statemachine_edge_kcs(d);
  }
  return KATCP_RESULT_FAIL;
}


#if 0
void destroy_statemachine_kcs(struct kcs_sm *m)
{
  struct kcs_sm_state *s;
  struct kcs_sm_edge *e;

  if (m == NULL)
    return;

#ifdef DEBUG
  fprintf(stderr, "statemachine: destroy_statemachine_kcs %s\n", m->m_name);
#endif
  
  if (m->m_name){
    free(m->m_name);
    m->m_name = NULL;
  }

  if (m->m_start){
    s = m->m_start;
    
    for (;;){
      if (s){
        e = s->s_edge;
        destroy_sm_state_kcs(s);
        s = NULL;
      } else
        break;

      if (e){
        s = e->e_next;
        destroy_sm_edge_kcs(e);
        e = NULL;
      } else
        break;
    }
  }

  free(m);
}

void destroy_statemachine_data_kcs(struct katcp_dispatch *d)
{
  struct kcs_basic *kb;
  struct kcs_sm_list *l;
  struct avl_tree *t;
  struct avl_node *n;
  struct kcs_mod_store *ms;
  struct avl_node_list *al;
  int i;
  void *handle;

  kb = get_mode_katcp(d, KCS_MODE_BASIC);
  if (kb == NULL)
    return;

  t = kb->b_ds;

  if (t == NULL)
    return;

  n = find_name_node_avltree(t, STATEMACHINE_LIST);

  if (n != NULL) {
    l = get_node_data_avltree(n);
    if (l != NULL){
#ifdef DEBUG
      fprintf(stderr, "statemachine: destroy_statemachine_data_kcs kcs_sm_list (%p)\n", l);
#endif

      for (i=0; i<l->l_count; i++){
        destroy_statemachine_kcs(l->l_sm[i]);
      }
      if (l->l_sm) { free(l->l_sm); l->l_sm = NULL; }
      free(l);
      l = NULL;
    }
  }

  n = find_name_node_avltree(t, MOD_STORE);

  if (n != NULL){
    ms = get_node_data_avltree(n);
    if (ms != NULL){
#ifdef DEBUG
      fprintf(stderr, "statemachine: destroy_statemachine_data_kcs avl_node_list (%p)\n", ms);
#endif
      al = ms->m_sl;
      if (al != NULL){
        if (al->l_n != NULL)
          free(al->l_n);
        al->l_count =0;
        free(al);
      }

      al = ms->m_hl;
      if (al != NULL){
        if (al->l_n != NULL){
          for (i=0; i<al->l_count; i++){
            handle = get_node_data_avltree(al->l_n[i]);
#ifdef DEBUG
            fprintf(stderr, "statemachine: close %s handle %p\n", get_node_name_avltree(al->l_n[i]), handle);
#endif
            dlclose(handle);
          }
          free(al->l_n);
        }
        al->l_count = 0;
        free(al);
      }
      free(ms);
    }
  }
}
#endif

#if 0

int is_active_sm_kcs(struct kcs_roach *kr){
  if (kr->ksm == NULL)
    return 0;
  return 1;
}

/*******************************************************************************************************/
/*Statemachine lookup function usually called from the statemachine notice*/
/*******************************************************************************************************/
int run_statemachine(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_statemachine *ksm;
  int rtn;

  ko = data;
  if (!ko)
    return 0;
  
  kr = ko->payload;
  if (!kr)
    return 0;
  
  ksm = kr->ksm[kr->ksmactive];
  if (!ksm)
    return 0;

#ifdef DEBUG
  fprintf(stderr,"SM: run_statemachine (%p) fn %s current state:%d\n",ksm,(!n)?ko->name:n->n_name,ksm->state);
#endif

  if (ksm->sm[ksm->state]){
#ifdef DEBUG
    fprintf(stderr,"SM: running state: (%p)\n",ksm->sm[ksm->state]);
#endif
    rtn = (*ksm->sm[ksm->state])(d,n,ko);
  }
  else {  
    wake_single_name_notice_katcp(d,KCS_SCHEDULER_NOTICE,NULL,ko);
#ifdef DEBUG
    fprintf(stderr,"SM: cleaning up: (%p)\n",ksm);
#endif
    log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Destroying kcs_statemachine %p",ksm);
    /*free(ksm);
    kr->ksm = NULL;*/
    //destroy_roach_ksm_kcs(kr);
    return 0;
  }
  
  ksm->state = rtn;
  
  return rtn;
}

/*******************************************************************************************************/
/*PING*/
/*******************************************************************************************************/
#if 0
int kcs_sm_ping_s1(struct katcp_dispatch *d,struct katcp_notice *n, void *data){
  struct katcp_job *j;
  struct katcl_parse *p;
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_statemachine *ksm;
  char * p_kurl;
  ko = data;
  kr = ko->payload;
  ksm = kr->ksm[kr->ksmactive];
#ifdef DEBUG
  fprintf(stderr,"SM: kcs_sm_ping_s1 %s\n",(!n)?ko->name:n->n_name);
#endif
  j = find_job_katcp(d,ko->name);
  if (j == NULL){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Couldn't find job labeled %s",ko->name);
    return KCS_SM_PING_STOP;
  }
  p = create_parse_katcl();
  if (p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create message");
    return KCS_SM_PING_STOP;
  }
  if (add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, "?watchdog") < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    return KCS_SM_PING_STOP;
  }
  if (!n){
    if (!(p_kurl = copy_kurl_string_katcp(kr->kurl,"?ping")))
      p_kurl = add_kurl_path_copy_string_katcp(kr->kurl,"?ping");
    n = create_parse_notice_katcp(d, p_kurl, 0, p);
    if (!n){
      log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create notice %s",p_kurl);
      free(p_kurl);
      return KCS_SM_PING_STOP;
    }
    ksm->n = n;
    if (p_kurl) { free(p_kurl); p_kurl = NULL; }
    if (add_notice_katcp(d, n, &run_statemachine, ko) != 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to add to notice %s",n->n_name);
      return KCS_SM_PING_STOP;
    }
  }
  else { 
    /*notice already exists so update it with new parse but dont wake it*/
    set_parse_notice_katcp(d, n, p);
  } 
  gettimeofday(&kr->lastnow, NULL);
  if (notice_to_job_katcp(d, j, n) != 0){
    /*send the notice to the job this adds it to the bottom of the list of thinngs the job must do*/
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to notice_to_job %s",n->n_name);
    return KCS_SM_PING_STOP;
  }
  return KCS_SM_PING_S2;
}

int time_ping_wrapper_call(struct katcp_dispatch *d, void *data){
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_statemachine *ksm; 
  struct katcp_notice *n;
  ko = data;
  kr = ko->payload;
  if (!is_active_sm_kcs(kr))
    return KCS_SM_PING_STOP;
  ksm = kr->ksm[kr->ksmactive];
  n = ksm->n;
#ifdef DEBUG
  fprintf(stderr, "SM: running from timer, waking notice %p\n", n);
#endif
  wake_notice_katcp(d,n,NULL);
  return KCS_SM_PING_S2;
}

int kcs_sm_ping_s2(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct katcl_parse *p;
  char *ptr;
  struct timeval when, now, delta;
#ifdef DEBUG
  fprintf(stderr,"SM: kcs_sm_ping_s2 %s\n",n->n_name);
#endif
  delta.tv_sec  = 1;
  delta.tv_usec = 0;
  gettimeofday(&now,NULL);
  add_time_katcp(&when,&now,&delta);
  p = get_parse_notice_katcp(d,n);
  if (p){
    ptr = get_string_parse_katcl(p,1);
    sub_time_katcp(&delta,&now,&((struct kcs_roach *)((struct kcs_obj *)data)->payload)->lastnow);
    log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"%s reply in %4.3fms returns: %s",n->n_name,(float)(delta.tv_sec*1000)+((float)delta.tv_usec/1000),ptr);
    if (strcmp(ptr,"fail") == 0)
      return KCS_SM_PING_STOP;
  }
  if (register_at_tv_katcp(d, &when, &time_ping_wrapper_call, data) < 0)
    return KCS_SM_PING_STOP;
  return KCS_SM_PING_S1;
}
#endif
/*******************************************************************************************************/
/*CONNECT*/
/*******************************************************************************************************/
int connect_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct kcs_basic *kb;
  struct katcp_job *j;
  struct katcl_parse *p;

  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_obj *root;

  struct kcs_statemachine *ksm;
  
  char *dc_kurl, *newpool;
  int fd;

  kb = get_mode_katcp(d,KCS_MODE_BASIC);
  if (!kb)
    return KCS_SM_CONNECT_STOP;
  
  root = kb->b_pool_head;
  if (!root)
    return KCS_SM_CONNECT_STOP;

  ko = data;
  kr = ko->payload;
  
  ksm = kr->ksm[kr->ksmactive];
  if (ksm == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "ksm is null for roach cannot continue");
    return KCS_SM_CONNECT_STOP;
  }

  newpool = ksm->data;

  if (n != NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "expected null notice but got (%p)", n);
    destroy_last_roach_ksm_kcs(kr);
    return KCS_SM_CONNECT_STOP;
  }

  if (!(dc_kurl = copy_kurl_string_katcp(kr->kurl,"?disconnect")))
    dc_kurl = add_kurl_path_copy_string_katcp(kr->kurl,"?disconnect");
  
  n = find_notice_katcp(d, dc_kurl);
  if (n != NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "find notice katcp returns (%p) %s is already connected", n, ko->name);
    free(dc_kurl);
    destroy_last_roach_ksm_kcs(kr);
    return KCS_SM_CONNECT_STOP;
  }
  
  fd = net_connect(kr->kurl->u_host, kr->kurl->u_port, NETC_ASYNC | NETC_TCP_KEEP_ALIVE);
  if (fd < 0) {
    log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to connect to %s",kr->kurl->u_str);
    free(dc_kurl);
    destroy_last_roach_ksm_kcs(kr);
    return KCS_SM_CONNECT_STOP;     
  } 
  
  n = create_notice_katcp(d, dc_kurl, 0);
  if (n == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create halt notice %s",dc_kurl);
    free(dc_kurl);
    destroy_last_roach_ksm_kcs(kr);
    return KCS_SM_CONNECT_STOP;
  } 

  free(dc_kurl);
  
  ksm->n = n;

  j = create_job_katcp(d, kr->kurl, 0, fd, 1, n); /*dispatch, katcp_url, pid, fd, async, notice*/
  if (j == NULL){
    log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to create job for %s",kr->kurl->u_str);
    destroy_last_roach_ksm_kcs(kr);
    return KCS_SM_CONNECT_STOP;
  } 
  
  if (add_notice_katcp(d, n, ksm->sm[KCS_SM_CONNECT_CONNECTED], ko)) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Unable to add the halt function to notice");
    zap_job_katcp(d, j);
    destroy_last_roach_ksm_kcs(kr);
    return KCS_SM_CONNECT_STOP;
  }

  if (mod_roach_to_new_pool(root, newpool, ko->name) == KCS_FAIL){
    log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Could not move roach %s to pool %s\n", kr->kurl->u_str, newpool);
    zap_job_katcp(d, j);
    destroy_last_roach_ksm_kcs(kr);
    return KCS_SM_CONNECT_STOP;
  } 
  
  log_message_katcp(d,KATCP_LEVEL_INFO, NULL, "Success: roach %s moved to pool %s", kr->kurl->u_str, newpool);

  if (add_sensor_to_roach_kcs(d, ko) < 0){
       
  }

  update_sensor_for_roach_kcs(d, ko, 1);
  
  p = create_parse_katcl();
  if (p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create message");
    destroy_last_roach_ksm_kcs(kr);
    return KCS_SM_CONNECT_STOP;
  }
  if (add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!sm") < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    destroy_last_roach_ksm_kcs(kr);
    return KCS_SM_CONNECT_STOP;
  }
  if (add_string_parse_katcl(p, KATCP_FLAG_STRING, "ok") < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    destroy_last_roach_ksm_kcs(kr);
    return KCS_SM_CONNECT_STOP;
  }
  if (add_string_parse_katcl(p, KATCP_FLAG_LAST | KATCP_FLAG_STRING, "connect") < 0){
    destroy_last_roach_ksm_kcs(kr);
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    return KCS_SM_CONNECT_STOP;
  }

  ksm->data = NULL;
  wake_single_name_notice_katcp(d, KCS_SCHEDULER_NOTICE, p, ko);
  
  return KCS_SM_CONNECT_CONNECTED;
}

int disconnect_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct kcs_basic *kb;
  
  struct kcs_roach *kr;
  struct kcs_obj *ko;
  struct kcs_statemachine *ksm;

  char *newpool;

  kb = get_mode_katcp(d, KCS_MODE_BASIC);
  if (kb == NULL){
    return KCS_SM_CONNECT_STOP;
  }

  ko = data;
  kr = ko->payload;
  ksm = kr->ksm[0]; /*state machine at zero will be the connect one if it is valid*/

  newpool = ksm ? ( ksm->data ? ksm->data : "disconnected") : "disconnected";
  
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Halt notice (%p) %s", n, n->n_name);
  if (mod_roach_to_new_pool(kb->b_pool_head, newpool, ko->name) == KCS_FAIL){
    log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Could not move roach %s to pool %s\n", kr->kurl->u_str, newpool);
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "scheduler ksmactive:%d",kr->ksmactive);
  } 
  else { 
    log_message_katcp(d,KATCP_LEVEL_INFO, NULL, "Success: roach %s moved to pool %s", kr->kurl->u_str, newpool);
  }

  update_sensor_for_roach_kcs(d, ko, 0);

  /*if (kr->io_ksm){
    destroy_ksm_kcs(kr->io_ksm);
    kr->io_ksm = NULL;
  }*/
  //kr->data = NULL; 
  return KCS_SM_CONNECT_STOP;
}

/*******************************************************************************************************/
/*PROGDEV*/
/*******************************************************************************************************/
#if 1 
int try_progdev_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_job *j;
  struct katcl_parse *p;
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_statemachine *ksm;
  char * p_kurl;
  struct p_value *conf_bs;
  
  ko = data;
  kr = ko->payload;
  ksm = kr->ksm[kr->ksmactive];
  conf_bs = ksm->data;
  
  j = find_job_katcp(d,ko->name);
  if (j == NULL){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Couldn't find job labeled %s",ko->name);
    return KCS_SM_PROGDEV_STOP;
  }
  
  p = create_parse_katcl();
  if (p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create message");
    return KCS_SM_PROGDEV_STOP;
  }
  if (add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?progdev") < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    return KCS_SM_PROGDEV_STOP;
  }
  if (add_string_parse_katcl(p,KATCP_FLAG_LAST | KATCP_FLAG_STRING,conf_bs->str) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    return KCS_SM_PROGDEV_STOP;
  }
  
  ksm->data = NULL;
  if (!n){
    if (!(p_kurl = copy_kurl_string_katcp(kr->kurl,"?progdev")))
      p_kurl = add_kurl_path_copy_string_katcp(kr->kurl,"?progdev");
    
    n = create_parse_notice_katcp(d, p_kurl, 0, p);
    if (!n){
      log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create notice %s",p_kurl);
      free(p_kurl);
      return KCS_SM_PROGDEV_STOP;
    }
    
    ksm->n = n;
    if (p_kurl) { free(p_kurl); p_kurl = NULL; }
    
    if (add_notice_katcp(d, n, &run_statemachine, ko) != 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to add to notice %s",n->n_name);
      return KCS_SM_PROGDEV_STOP;
    }
  }
  else { 
    /*notice already exists so update it with new parse but dont wake it*/
#if 0
    update_notice_katcp(d, n, p, KATCP_NOTICE_TRIGGER_OFF, 0, NULL);
#endif
    set_parse_notice_katcp(d, n, p);
  } 
  if (notice_to_job_katcp(d, j, n) != 0){
    /*send the notice to the job this adds it to the bottom of the list of thinngs the job must do*/
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to notice_to_job %s",n->n_name);
    return KCS_SM_PROGDEV_STOP;
  }
  /*okay*/
  //log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "done try progdev %s",n->n_name);
  return KCS_SM_PROGDEV_OKAY;
}

int okay_progdev_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcl_parse *p;
  char *ptr;

  p = get_parse_notice_katcp(d,n);
  
  if (p){
    ptr = get_string_parse_katcl(p,1);
    log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"%s replies: %s",n->n_name,ptr);
    if (strcmp(ptr,"fail") == 0)
      return KCS_SM_PROGDEV_STOP;
  }
#ifdef DEBUG
  fprintf(stderr,"SM: about to run wake_notice_katcp\n");
#endif
  
  //wake_notice_katcp(d,n,p);
  wake_notice_katcp(d,n,NULL);
  //wake_single_name_notice_katcp(d, KCS_SCHEDULER_NOTICE, p, ko);
  //log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "done okay progdev %s",n->n_name);
  return KCS_SM_PROGDEV_STOP;
}
#endif
/*******************************************************************************************************/
/*StateMachine Setups*/
/*******************************************************************************************************/
#if 0
struct kcs_statemachine *get_sm_ping_kcs(void *data){
  struct kcs_statemachine *ksm;
  
  ksm = malloc(sizeof(struct kcs_statemachine));
  if (ksm == NULL)
    return NULL;

  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  if (ksm->sm == NULL) {
    free(ksm);
    return NULL;
  }

  ksm->state = KCS_SM_PING_S1;
  ksm->n = NULL;
  ksm->sm[KCS_SM_PING_STOP] = NULL;
  ksm->sm[KCS_SM_PING_S1]   = &kcs_sm_ping_s1;
  ksm->sm[KCS_SM_PING_S2]   = &kcs_sm_ping_s2;
  ksm->data = data;

#ifdef DEBUG
  fprintf(stderr,"SM: pointer to ping sm: %p\n",ksm);
#endif

  return ksm;
}
#endif

struct kcs_statemachine *get_sm_connect_kcs(void *data){
  struct kcs_statemachine *ksm;
  
  ksm = malloc(sizeof(struct kcs_statemachine));
  if (ksm == NULL)
    return NULL;

  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  if (ksm->sm == NULL) {
    free(ksm);
    return NULL;
  }

  ksm->state = KCS_SM_CONNECT_DISCONNECTED;
  ksm->n = NULL;
  ksm->sm[KCS_SM_CONNECT_STOP]         = NULL;
  ksm->sm[KCS_SM_CONNECT_DISCONNECTED] = &connect_sm_kcs;
  ksm->sm[KCS_SM_CONNECT_CONNECTED]    = &disconnect_sm_kcs;
  ksm->data = data;

#ifdef DEBUG
  fprintf(stderr,"SM: pointer to connect sm: %p\n",ksm);
#endif

  return ksm;
}
#if 1
struct kcs_statemachine *get_sm_progdev_kcs(void *data){
  struct kcs_statemachine *ksm;
  
  ksm = malloc(sizeof(struct kcs_statemachine));
  if (ksm == NULL)
    return NULL;

  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  if (ksm->sm == NULL) {
    free(ksm);
    return NULL;
  }

  ksm->state = KCS_SM_PROGDEV_TRY;
  ksm->n = NULL;
  ksm->sm[KCS_SM_PROGDEV_TRY]   = &try_progdev_sm_kcs;
  ksm->sm[KCS_SM_PROGDEV_OKAY]  = &okay_progdev_sm_kcs;
  ksm->sm[KCS_SM_PROGDEV_STOP]  = NULL;
  ksm->data = data;

#ifdef DEBUG
  fprintf(stderr,"SM: pointer to progdev sm: %p\n",ksm);
#endif
  
  return ksm;
}
#endif


/*******************************************************************************************************/
/*KCS Scheduler Stuff*/
/* ******************************************************************************************************/

int tick_scheduler_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct katcl_parse *p;

  int i;
  char *ptr, *sm_name;

  ko = data;
  kr = ko->payload;

#ifdef DEBUG
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "scheduler tick function r: %s ksmcount: %d ksmactive: %d", ko->name, kr->ksmcount, kr->ksmactive);
  for (i=0;i<kr->ksmcount;i++){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "scheduler ksm[%d]: %p",i,kr->ksm[i]);
  }
#endif
 
  p = get_parse_notice_katcp(d,n);
  if (p){
  
    i = get_count_parse_katcl(p);
    if (i == 3){
  
      ptr = get_string_parse_katcl(p, 1);  
      sm_name = get_string_parse_katcl(p, 2);

      if (strcmp(ptr,"ok") == 0){
#ifdef DEBUG
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "statemachine scheduler: current ksm [%d] %s returns %s", kr->ksmactive, sm_name, ptr);
#endif
        kr->ksmactive++;
      } 
      else {   
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "sm: %s returns %s, about to stop scheduler", sm_name, ptr);
        return 0;
      }
    }

  }

#ifdef DEBUG
  fprintf(stderr,"SM: scheduler tick with data: %p\n",data);
#endif
  
  return KCS_SCHEDULER_TICK;
}

struct katcp_notice *add_scheduler_notice_kcs(struct katcp_dispatch *d, void *data)
{
  struct katcp_notice *n;

  n = find_notice_katcp(d,KCS_SCHEDULER_NOTICE);

#ifdef DEBUG
  fprintf(stderr,"SM: scheduler: add_scheduler_notice to: %p\n",n);
#endif

  if(n == NULL){
#ifdef DEBUG
    log_message_katcp(d,KATCP_LEVEL_INFO, NULL, "creating kcs scheduler and attaching tick callback");
#endif
    n = create_notice_katcp(d,KCS_SCHEDULER_NOTICE,0);
    
    if (n == NULL){
      log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "unable to create kcs scheduler notice");
      return NULL;
    }

#ifdef DEBUG
    fprintf(stderr,"SM: scheduler: needed to create notice %s: %p\n",KCS_SCHEDULER_NOTICE,n);
#endif
  }

  if (add_notice_katcp(d, n, &tick_scheduler_kcs, data)<0){
    log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "unable to add function to notice");
    return NULL; 
  }
#ifdef DEBUG
  fprintf(stderr,"SM: scheduler added tick callback with data: %p\n",data);
#endif

  return n;  
}


/*******************************************************************************************************/
/*API Functions*/
/*******************************************************************************************************/

int api_prototype_sm_kcs(struct katcp_dispatch *d, struct kcs_obj *ko, struct kcs_statemachine *(*call)(void *),void *data)
{
  struct kcs_roach *kr;
  struct kcs_node *kn;
  struct katcp_notice *n;
  int i, oldcount;
  
  n = NULL;
  i=0;

  switch (ko->tid){
    case KCS_ID_ROACH:
      kr = (struct kcs_roach *) ko->payload;
      
      if (add_scheduler_notice_kcs(d,ko) == NULL)
        return KATCP_RESULT_FAIL;
      
      kr->ksm = realloc(kr->ksm,sizeof(struct kcs_statemachine *)*++kr->ksmcount);
      if (kr->ksm == NULL)
        return KATCP_RESULT_FAIL;
      
      kr->ksm[kr->ksmcount-1] = (*call)(data);
      //kr->ksm[kr->ksmcount-1]->data = data;
      
      run_statemachine(d, n, ko);
      break;
    
    case KCS_ID_NODE:
      kn = (struct kcs_node *) ko->payload;
      while (i < kn->childcount){  
        kr = kn->children[i]->payload;
        
        if (add_scheduler_notice_kcs(d,kn->children[i]) == NULL)
          return KATCP_RESULT_FAIL;
        
        kr->ksm = realloc(kr->ksm,sizeof(struct kcs_statemachine *)*++kr->ksmcount);
        if (kr->ksm == NULL)
          return KATCP_RESULT_FAIL;
        
        kr->ksm[kr->ksmcount-1] = (*call)(data);
        //kr->data = data;
        
        oldcount = kn->childcount;
        run_statemachine(d, n, kn->children[i]);
        if (kn->childcount == oldcount)
          i++; 
      }
      break;
  }
  return KATCP_RESULT_OK;
}

int statemachine_stop(struct katcp_dispatch *d)
{
  /*struct kcs_obj *ko;
  struct kcs_node *kn;
  struct kcs_roach *kr;
  int state, i;
  ko = roachpool_get_obj_by_name_kcs(d,arg_string_katcp(d,2));
  if (!ko)
    return KATCP_RESULT_FAIL;
  switch (ko->tid){
    case KCS_ID_ROACH:
      kr = (struct kcs_roach*) ko->payload;
      if (is_active_sm_kcs(kr)){
        for (state=0; kr->ksm->sm[state]; state++);
        log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"%s is in state: %d going to stop state: %d",kr->ksm->n->n_name,kr->ksm->state,state);
        kr->ksm->state = state;
        rename_notice_katcp(d,kr->ksm->n,"<zombie>");
        wake_notice_katcp(d,kr->ksm->n,NULL);
      }
      else
        return KATCP_RESULT_FAIL;
      break;
    case KCS_ID_NODE:
      kn = (struct kcs_node *) ko->payload;
      for (i=0; i<kn->childcount; i++){
        kr = kn->children[i]->payload;
        if (is_active_sm_kcs(kr)){
          for (state=0; kr->ksm->sm[state]; state++);
          log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"%s is in state: %d going to stop state: %d",kr->ksm->n->n_name,kr->ksm->state,state);
          kr->ksm->state = state;
          rename_notice_katcp(d,kr->ksm->n,"<zombie>");
          wake_notice_katcp(d,kr->ksm->n,NULL);
        }
        else
          return KATCP_RESULT_FAIL;
      }
      break;
  }
  */
  return KATCP_RESULT_OK;
}

int statemachine_ping(struct katcp_dispatch *d)
{
  struct kcs_obj *ko;
  char *obj;

  obj = arg_string_katcp(d,2);

  ko = roachpool_get_obj_by_name_kcs(d,obj);
  if (!ko)
    return KATCP_RESULT_FAIL;
#if 0
  return api_prototype_sm_kcs(d,ko,&get_sm_ping_kcs,NULL);
#endif
  return KATCP_RESULT_FAIL;
}

int statemachine_connect(struct katcp_dispatch *d)
{
  struct kcs_obj *ko;
  char *obj, *pool;

  obj = arg_string_katcp(d,2);
  pool = arg_string_katcp(d,3);

  ko = roachpool_get_obj_by_name_kcs(d,obj);
  if (!ko)
    return KATCP_RESULT_FAIL;

  return api_prototype_sm_kcs(d,ko,&get_sm_connect_kcs,pool);
}

int statemachine_disconnect(struct katcp_dispatch *d)
{
  struct kcs_obj *ko;
  struct kcs_node *kn;
  struct kcs_roach *kr;
  struct katcp_job *j;
  int i;
  char *obj, *newpool;

  obj = arg_string_katcp(d,2);
  newpool = arg_string_katcp(d,3);

  ko = roachpool_get_obj_by_name_kcs(d,obj);
  if (!ko)
    return KATCP_RESULT_FAIL;
  
  switch (ko->tid) {
    case KCS_ID_ROACH:
      j = find_job_katcp(d,ko->name);
      if (!j){
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to find job %s",ko->name);
        return KATCP_RESULT_FAIL;
      }
      
      kr = ko->payload;
      kr->ksm[0]->data = newpool;
      zap_job_katcp(d,j);
      break;
    
    case KCS_ID_NODE:
      kn = ko->payload;
      
      for (i=0;i<kn->childcount;i++){
        j = find_job_katcp(d,kn->children[i]->name);
        if (!j){
          log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to find job %s",kn->children[i]->name);
          return KATCP_RESULT_FAIL;
        }
        
        kr = kn->children[i]->payload;
        kr->ksm[0]->data = newpool;
        zap_job_katcp(d,j);
      }
      break;
  }
  return KATCP_RESULT_OK;
}

int statemachine_progdev(struct katcp_dispatch *d)
{
  struct kcs_obj *ko;
  struct p_value *conf_bitstream;
  char *label, *setting, *value;

  label   = arg_string_katcp(d,2);
  setting = arg_string_katcp(d,3);
  value   = arg_string_katcp(d,4);

  conf_bitstream = NULL;
  
  ko = roachpool_get_obj_by_name_kcs(d,arg_string_katcp(d,5));
  if (!ko)
    return KATCP_RESULT_FAIL;
  
  conf_bitstream = parser_get(d, label, setting, atoi(value));
  if (!conf_bitstream)
    return KATCP_RESULT_FAIL;

  return api_prototype_sm_kcs(d,ko,&get_sm_progdev_kcs,conf_bitstream);
#if 0  
  return KATCP_RESULT_FAIL;
#endif
}

int statemachine_poweron(struct katcp_dispatch *d)
{
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_node *kn;
  struct katcp_job *j;
  char *obj;
  int i;

  obj = arg_string_katcp(d,2);

  ko = roachpool_get_obj_by_name_kcs(d,obj);
  if (!ko)
    return KATCP_RESULT_FAIL;

  switch (ko->tid) {
    case KCS_ID_ROACH:
      kr = ko->payload;
      if (kr == NULL)
        return KATCP_RESULT_FAIL;
      if (strcasecmp(kr->kurl->u_scheme,"xport") != 0) {
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "need a xport:// scheme in url");
        return KATCP_RESULT_FAIL;
      }
      j = run_child_process_kcs(d, kr->kurl, &xport_sync_connect_and_start_subprocess_kcs, kr->kurl, NULL);
      if (j == NULL){
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "run child process kcs returned null job");
        return KATCP_RESULT_FAIL;
      }
      break;
    
    case KCS_ID_NODE:
      kn = ko->payload;
      
      for (i=0;i<kn->childcount;i++){
        kr = kn->children[i]->payload;
        if (kr == NULL)
          return KATCP_RESULT_FAIL;
        if (strcasecmp(kr->kurl->u_scheme,"xport") != 0) {
          log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "need a xport:// scheme in url");
          return KATCP_RESULT_FAIL;
        }
        j = run_child_process_kcs(d, kr->kurl, &xport_sync_connect_and_start_subprocess_kcs, kr->kurl, NULL);
        if (j == NULL){
          log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "run child process kcs returned null job");
          return KATCP_RESULT_FAIL;
        }
      }
      break;
  }
  return KATCP_RESULT_OK;
}
int statemachine_poweroff(struct katcp_dispatch *d)
{
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_node *kn;
  struct katcp_job *j;
  char *obj;
  int i;

  obj = arg_string_katcp(d,2);

  ko = roachpool_get_obj_by_name_kcs(d,obj);
  if (!ko)
    return KATCP_RESULT_FAIL;

  switch (ko->tid) {
    case KCS_ID_ROACH:
      kr = ko->payload;
      if (kr == NULL)
        return KATCP_RESULT_FAIL;
      if (strcasecmp(kr->kurl->u_scheme,"xport") != 0) {
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "need a xport:// scheme in url");
        return KATCP_RESULT_FAIL;
      }
      j = run_child_process_kcs(d, kr->kurl, &xport_sync_connect_and_stop_subprocess_kcs, kr->kurl, NULL);
      if (j == NULL){
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "run child process kcs returned null job");
        return KATCP_RESULT_FAIL;
      }
      break;
    
    case KCS_ID_NODE:
      kn = ko->payload;
      
      for (i=0;i<kn->childcount;i++){
        kr = kn->children[i]->payload;
        if (kr == NULL)
          return KATCP_RESULT_FAIL;
        if (strcasecmp(kr->kurl->u_scheme,"xport") != 0) {
          log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "need a xport:// scheme in url");
          return KATCP_RESULT_FAIL;
        }
        j = run_child_process_kcs(d, kr->kurl, &xport_sync_connect_and_stop_subprocess_kcs, kr->kurl, NULL);
        if (j == NULL){
          log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "run child process kcs returned null job");
          return KATCP_RESULT_FAIL;
        }
      }
      break;
  }
  return KATCP_RESULT_OK;
}
int statemachine_powersoft(struct katcp_dispatch *d)
{
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_node *kn;
  struct katcp_job *j;
  char *obj;
  int i;

  obj = arg_string_katcp(d,2);

  ko = roachpool_get_obj_by_name_kcs(d,obj);
  if (!ko)
    return KATCP_RESULT_FAIL;

  switch (ko->tid) {
    case KCS_ID_ROACH:
      kr = ko->payload;
      if (kr == NULL)
        return KATCP_RESULT_FAIL;
      if (strcasecmp(kr->kurl->u_scheme,"xport") != 0) {
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "need a xport:// scheme in url");
        return KATCP_RESULT_FAIL;
      }
      j = run_child_process_kcs(d, kr->kurl, &xport_sync_connect_and_soft_restart_subprocess_kcs, kr->kurl, NULL);
      if (j == NULL){
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "run child process kcs returned null job");
        return KATCP_RESULT_FAIL;
      }
      break;
    
    case KCS_ID_NODE:
      kn = ko->payload;
      
      for (i=0;i<kn->childcount;i++){
        kr = kn->children[i]->payload;
        if (kr == NULL)
          return KATCP_RESULT_FAIL;
        if (strcasecmp(kr->kurl->u_scheme,"xport") != 0) {
          log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "need a xport:// scheme in url");
          return KATCP_RESULT_FAIL;
        }
        j = run_child_process_kcs(d, kr->kurl, &xport_sync_connect_and_soft_restart_subprocess_kcs, kr->kurl, NULL);
        if (j == NULL){
          log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "run child process kcs returned null job");
          return KATCP_RESULT_FAIL;
        }
      }
      break;
  }
  return KATCP_RESULT_OK;
}

void destroy_ksm_kcs(struct kcs_statemachine *ksm){
  if (ksm){
    if (ksm->n != NULL) { ksm->n = NULL; }
    if (ksm->sm != NULL) { free(ksm->sm); ksm->sm = NULL; }
    if (ksm->data != NULL) { ksm->data = NULL; }
#ifdef DEBUG
    fprintf(stderr,"sm: about to free ksm (%p)\n",ksm);
#endif
    free(ksm); 
  }
}
void destroy_last_roach_ksm_kcs(struct kcs_roach *kr)
{
  if (kr){
    destroy_ksm_kcs(kr->ksm[kr->ksmcount-1]);
    kr->ksm[kr->ksmcount-1] = NULL;
    kr->ksmcount--;
  }
}
/*
void destroy_roach_ksm_kcs(struct kcs_roach *kr){
  if (kr->ksm){
#ifdef DEBUG
    fprintf(stderr,"SM: about to free (%p)\n",kr->ksm);
#endif
    destroy_ksm_kcs(kr->ksm);
    kr->ksm = NULL; 
  }
}
*/

int statemachine_greeting(struct katcp_dispatch *d){
  prepend_inform_katcp(d);
 // append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"katcp://roach:port/?ping | katcp://*roachpool/?ping");
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"ping [roachpool|roachurl]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"connect [roachpool|roachurl] [newpool]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"disconnect [roachpool|roachurl] [newpool]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"progdev [conf-label] [conf-setting] [conf-value] [roachpool|roach]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"poweron [xports]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"poweroff [xports]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"powersoft [xports]");
  return KATCP_RESULT_OK;
}

int statemachine_cmd(struct katcp_dispatch *d, int argc){
  switch (argc){
    case 1:
      return statemachine_greeting(d);
      
      break;
    case 2:
      
      break;
    case 3:
      if (strcmp(arg_string_katcp(d,1),"stop") == 0)
        return statemachine_stop(d);

      if (strcmp(arg_string_katcp(d,1),"ping") == 0)
        return statemachine_ping(d);
      
      if (strcmp(arg_string_katcp(d,1),"poweron") == 0)
        return statemachine_poweron(d);
      
      if (strcmp(arg_string_katcp(d,1),"poweroff") == 0)
        return statemachine_poweroff(d);
      
      if (strcmp(arg_string_katcp(d,1),"powersoft") == 0)
        return statemachine_powersoft(d);
      
      break;
    case 4:
      if (strcmp(arg_string_katcp(d,1),"connect") == 0)
        return statemachine_connect(d);
      
      if (strcmp(arg_string_katcp(d,1),"disconnect") == 0)
        return statemachine_disconnect(d);
      
      break;
    case 6:
      if (strcmp(arg_string_katcp(d,1),"progdev") == 0)
        return statemachine_progdev(d);
      
      break;
  }
  return KATCP_RESULT_FAIL;
}



#ifdef STANDALONE
int main(int argc, char **argv){
  int i;
  int cf;
  int (*statemachine[])(int) = {
    &func1,
    &func2,
    &func3,
    &func1,
    NULL
  };
  for (i=0; statemachine[i]; i++)
  {
    cf = (*statemachine[i])(i);
#ifdef DEBUG
      fprintf(stderr,"function returned: %d\n",cf);
#endif
  }
  return EX_OK;
}
#endif

#endif
