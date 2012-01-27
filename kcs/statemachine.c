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

/*Statemachine API*********************************************************************************************/
int statemachine_init_kcs(struct katcp_dispatch *d)
{ 
  int rtn;
  
  rtn  = init_statemachine_base_kcs(d);
  rtn += init_actor_tag_katcp(d);
  
  return rtn;
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
void print_sm_state_kcs(struct katcp_dispatch *d, char *key, void *data)
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
  struct kcs_sm_op *o;
  //struct katcp_tobject *to;
  
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

/*
  to = create_named_tobject_katcp(d, e, KATCP_TYPE_EDGE, 0);
  if (to == NULL){
    destroy_sm_edge_kcs(e);
    return -1;
  }
*/
 // o = create_sm_op_kcs(&follow_edge_process_kcs, to);
  o = create_sm_op_kcs(&trigger_edge_process_kcs, NULL);
  if (o == NULL){
    destroy_sm_edge_kcs(e);
    //destroy_tobject_katcp(to);
    return -1;
  }

  s_current->s_edge_list = realloc(s_current->s_edge_list, sizeof(struct kcs_sm_edge *) * (s_current->s_edge_list_count + 1));
  if (s_current->s_edge_list == NULL){
    destroy_sm_edge_kcs(e);
    destroy_sm_op_kcs(o);
    //destroy_tobject_katcp(to);
    return -1;
  }

  s_current->s_edge_list[s_current->s_edge_list_count] = e;
  s_current->s_edge_list_count++;

  s_current->s_op_list = realloc(s_current->s_op_list, sizeof(struct kcs_sm_op *) * (s_current->s_op_list_count + 1));
  if (s_current->s_op_list == NULL){
    s_current->s_edge_list = realloc(s_current->s_edge_list, sizeof(struct kcs_sm_edge *) * (s_current->s_edge_list_count - 1));
    s_current->s_edge_list_count--;
    destroy_sm_op_kcs(o);
    destroy_sm_edge_kcs(e);
    return -1;
  }

  s_current->s_op_list[s_current->s_op_list_count] = o;
  s_current->s_op_list_count++;

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


/*Modules*****************************************************************************************************/
void print_sm_mod_kcs(struct katcp_dispatch *d, char *key, void *data)
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

int statemachine_loadkcl_kcs(struct katcp_dispatch *d)
{
  char *file;

  file = arg_string_katcp(d, 2);
  if (file == NULL)
    return KATCP_RESULT_FAIL;
  
  if (load_from_file_katcp(d, file) < 0)
    return KATCP_RESULT_FAIL;
  
  return KATCP_RESULT_OK;
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

/*Task Scheduler**********************************************************************************************/
struct kcs_sched_task *create_sched_task_kcs(struct kcs_sm_state *s, struct katcp_tobject *to, int flags)
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
  t->t_op_i    = 0;
  t->t_flags   = flags;
  
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

#ifdef DEBUG
  fprintf(stderr, "statemachine: set pc: <%s>\n", s->s_name);
#endif

  return 0;
}

void print_task_stack_kcs(struct katcp_dispatch *d, struct kcs_sched_task *t)
{
  struct katcp_stack *stack;

  stack = get_task_stack_kcs(t);

  print_stack_katcp(d, stack);
}

int statemachine_run_ops_kcs(struct katcp_dispatch *d, struct katcp_notice *n, struct kcs_sched_task *t)
{
  struct kcs_sm_state *s;
  struct katcp_stack *stack;
  struct kcs_sm_op *op;
  int rtn;
  
  if (t == NULL)
    return TASK_STATE_CLEAN_UP;

  s = get_task_pc_kcs(t);
  stack = get_task_stack_kcs(t);
  
  if (s == NULL || stack == NULL)
    return TASK_STATE_CLEAN_UP;

#if 0 
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "running ops in state %s", s->s_name);
#endif
#if 0
  for (i=0; i<s->s_op_list_count; i++){
#endif
  for (; t->t_op_i < s->s_op_list_count; t->t_op_i++){
    op = s->s_op_list[t->t_op_i];
    if (op != NULL){

      if (op->o_call == &trigger_edge_process_kcs){
#ifdef DEBUG
        fprintf(stderr, "statemachine: hit trigger edge op @ [%d]\n", t->t_op_i);
#endif
        t->t_op_i++;
        break; 
      }

      rtn = (*(op->o_call))(d, stack, op->o_tobject);
#ifdef DEBUG
      fprintf(stderr,"statemachine RUN: op %p call returned %d\n", op, rtn);
#endif  
      if (rtn < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "statemachine op [%d] error rtn: %d", t->t_op_i, rtn);
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
  
  if (t == NULL)
    return TASK_STATE_CLEAN_UP;

  s = get_task_pc_kcs(t);
  stack = get_task_stack_kcs(t);

  if (s == NULL || stack == NULL)
    return TASK_STATE_CLEAN_UP;

#ifdef DEBUG
  fprintf(stderr, "statemachine: follow edges [%d]\n",t->t_edge_i);
#endif

  if (s->s_edge_list_count <= 0){
#ifdef DEBUG
    fprintf(stderr, "statemachine: no edges ending\n");
#endif
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
    } else {
      /*this is for edges with no callback (default edge) always follow*/
      rtn = 0;
      wake_notice_katcp(d, n, NULL);
    }

    switch (rtn){
      case EDGE_OKAY:
        set_task_pc_kcs(t, e->e_next);
        t->t_edge_i = 0;      
        t->t_op_i   = 0;
#ifdef DEBUG
        fprintf(stderr, "statemachine: follow edges EDGE RETURNS SUCCESS\n");
#endif
        return TASK_STATE_RUN_OPS;
      
      case EDGE_WAIT:
#ifdef DEBUG
        fprintf(stderr, "statemachine: follow edges WAITING\n");
#endif
        return TASK_STATE_EDGE_WAIT;

      case EDGE_FAIL:
      default:
        break;
    }

  }

  if ((t->t_edge_i+1) < s->s_edge_list_count){
    t->t_edge_i++;
#ifdef DEBUG
    fprintf(stderr, "statemachine: follow edges STILL TRYING\n");
#endif

    wake_notice_katcp(d, n, NULL);

    //return TASK_STATE_FOLLOW_EDGES;
    return TASK_STATE_RUN_OPS;
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
  struct katcl_parse *p;
  int rtn;
  char *ptr, *name;
  
  rtn = 0;
  t = data;
  if (t == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot run sched with null task");
    return 0;
  }
#if 0
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "running sched notice with task state: %d", t->t_state);
#endif 

  name = (n->n_name == NULL) ? "<anonymous>" : n->n_name;

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

    case TASK_STATE_EDGE_WAIT:
      
      p = get_parse_notice_katcp(d, n);
      if (p == NULL){
        rtn = -1;
      } else {
        ptr = get_string_parse_katcl(p, 1);
        if (ptr == NULL || strcmp(ptr, "ok") != 0){
#ifdef DEBUG
          fprintf(stderr, "statemachine: process edge wait FAIL try next OP\n");
#endif
#if 0
          if ((t->t_edge_i+1) < (t->t_pc->s_edge_list_count)){
            t->t_edge_i++;
#ifdef DEBUG
            fprintf(stderr, "statemachine: follow edges STILL TRYING\n");
#endif
            rtn = statemachine_follow_edges_kcs(d, n, t);
          } else {
#ifdef DEBUG
            fprintf(stderr, "statemachine: no more edges! FAIL\n");
#endif
            rtn = -1;
          }
#endif
          if ((t->t_edge_i+1) < (t->t_pc->s_edge_list_count)){
            t->t_edge_i++;
          }
          rtn = TASK_STATE_RUN_OPS;
          wake_notice_katcp(d, n, NULL);
        } else {
#ifdef DEBUG
          fprintf(stderr, "statemachine: process edge wait SUCCESS follow edge\n");
#endif
          /*TODO: err checking*/
          set_task_pc_kcs(t, t->t_pc->s_edge_list[t->t_edge_i]->e_next);
          t->t_edge_i = 0;      
          t->t_op_i   = 0;      
          rtn = TASK_STATE_RUN_OPS;
          wake_notice_katcp(d, n, NULL);
        }
      }
      break;

    case TASK_STATE_CLEAN_UP:
#ifdef DEBUG
      fprintf(stderr, "statemachine: process about to cleanup %s\n", name);
#endif
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "printing task stack and cleaning up %s", name);
      print_task_stack_kcs(d, t);
     
      if (t->t_flags & PROCESS_MASTER){
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "statemachine master process ending %s", name);
        prepend_reply_katcp(d);
        if (t->t_rtn < 0){
          append_string_katcp(d, KATCP_FLAG_STRING, "fail");  
          append_string_katcp(d, KATCP_FLAG_STRING, name);
          append_signed_long_katcp(d, KATCP_FLAG_SLONG | KATCP_FLAG_LAST, t->t_rtn);
        } else {
          append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "ok");  
        }
        resume_katcp(d);
      } else if (t->t_flags & PROCESS_SLAVE){
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "statemachine slave process ending %s", name);
      }

      destroy_sched_task_kcs(t);

#ifdef DEBUG
      fprintf(stderr, "**********[end statemachine (%s) run]**********\n", name);
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

/*
TODO: think about running each task as a subprocess
this will achive task / process ||ism
*/
int start_process_kcs(struct katcp_dispatch *d, char *startnode, struct katcp_tobject *to, int flags)
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
  
  t = create_sched_task_kcs(s, to, flags);
  if (t == NULL)
    return -1;
 
  name = gen_id_avltree("sm");

  n = register_notice_katcp(d, name, 0, &statemachine_process_kcs, t);

  if (name != NULL) 
    free(name);

  if (n == NULL){
    destroy_sched_task_kcs(t);
    return -1;
  }
  
  wake_notice_katcp(d, n, NULL);

  return 0;
}

int trigger_edge_process_kcs(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *to)
{
  return 0;
}

/*KATCP Dispatch API*******************************************************************************************/
int statemachine_run_kcs(struct katcp_dispatch *d)
{
  char *startnode;

  startnode = arg_string_katcp(d, 2);

  if (startnode == NULL)
    return KATCP_RESULT_FAIL;

  if (start_process_kcs(d, startnode, NULL, PROCESS_MASTER) < 0)
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

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "stopall found: %d machines", n_count);
  
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

int statemachine_tagsets_kcs(struct katcp_dispatch *d)
{
  if (dump_tagsets_katcp(d) < 0)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_OK;
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

int statemachine_flush_kcs(struct katcp_dispatch *d)
{
  
  struct katcp_type *t;

  t = find_name_type_katcp(d, KATCP_TYPE_STATEMACHINE_STATE);

  if (t == NULL)
    return KATCP_RESULT_FAIL;

  flush_type_katcp(t);
  
#if 0
  if (dump_tagsets_katcp(d) < 0)
    return KATCP_RESULT_FAIL;
#endif
  
  return KATCP_RESULT_OK;
}

int statemachine_print_ds_kcs(struct katcp_dispatch *d)
{
  print_types_katcp(d);
  return KATCP_RESULT_OK;
}

int statemachine_dump_type_kcs(struct katcp_dispatch *d)
{
  struct katcp_type *t;
  char *type;

  type = arg_string_katcp(d, 2);
  if (type == NULL)
    return KATCP_RESULT_FAIL;

  t = find_name_type_katcp(d, type);
  if (t == NULL)
    return KATCP_RESULT_FAIL;

  print_type_katcp(d, t, 0);
  
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

int statemachine_print_edgelist_kcs(struct katcp_dispatch *d)
{
  struct katcp_type *t;

  t = find_name_type_katcp(d, KATCP_TYPE_EDGE);
  if (t == NULL)
    return KATCP_RESULT_FAIL;

  print_type_katcp(d, t, 1); 

  return KATCP_RESULT_OK;
}

int statemachine_greeting_kcs(struct katcp_dispatch *d)
{
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "loadkcl [script file]");  
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
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "oplist (print op list)");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "edgelist (print edge list)");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "stopall");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "flush (remove defined statemachines)");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "tagsets");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "dt [type] dump items in type tree");
  return KATCP_RESULT_OK;
}

int statemachine_cmd(struct katcp_dispatch *d, int argc)
{
  switch (argc){
    case 1:
      return statemachine_greeting_kcs(d);
      
      break;
    case 2:
      if (strcmp(arg_string_katcp(d, 1), "flush") == 0)
        return statemachine_flush_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "ds") == 0)
        return statemachine_print_ds_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "oplist") == 0)
        return statemachine_print_oplist_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "edgelist") == 0)
        return statemachine_print_edgelist_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "stopall") == 0)
        return statemachine_stopall_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "tagsets") == 0)
        return statemachine_tagsets_kcs(d);
      
      break;
    case 3:
      if (strcmp(arg_string_katcp(d, 1), "loadkcl") == 0)
        return statemachine_loadkcl_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "loadmod") == 0)
        return statemachine_loadmod_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "node") == 0)
        return statemachine_node_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "run") == 0)
        return statemachine_run_kcs(d);
      if (strcmp(arg_string_katcp(d, 1), "dt") == 0)
        return statemachine_dump_type_kcs(d);
      
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

