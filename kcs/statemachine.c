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

#define STATEMACHINE_LIST       "statemachine_list"
#define MOD_STORE               "mod_store"
#define MOD_STORE_TYPE_SYMBOL   0  
#define MOD_STORE_TYPE_HANDLE   1

struct avl_node *create_mod_store_kcs()
{
  struct avl_node *n;
  struct kcs_mod_store *ms;

  ms = malloc(sizeof(struct kcs_mod_store));

  if (ms == NULL)
    return NULL;

  ms->m_sl = NULL;
  ms->m_hl = NULL;

  n = create_node_avltree(MOD_STORE, ms);
  if (n == NULL){
    free(ms);
    return NULL;
  }

  return n;
}

int add_node_mod_store_kcs(struct kcs_mod_store *ms, struct avl_node *nn, int type)
{
  struct avl_node_list *al;

  if (ms == NULL || nn == NULL)
    return -1;

  switch (type){
    case MOD_STORE_TYPE_SYMBOL:
      al = ms->m_sl;
      break;
    case MOD_STORE_TYPE_HANDLE:
      al = ms->m_hl;
      break;
    default:
      return -1;
  }

  if (al == NULL){
    al = malloc(sizeof(struct avl_node_list));
    if (al == NULL)
      return -1;
    
    al->l_n     = NULL;
    al->l_count = 0;

    switch (type){
      case MOD_STORE_TYPE_SYMBOL:
        ms->m_sl = al;
        break;
      case MOD_STORE_TYPE_HANDLE:
        ms->m_hl = al;
        break;
    }
  }

  al->l_n = realloc(al->l_n, sizeof(struct avl_node *) * (al->l_count + 1));

  if (al->l_n == NULL){
    free(al);
    return -1;  
  }

  al->l_n[al->l_count] = nn;
  al->l_count++;
  
  return 0;
}

struct avl_node *create_statemachine_list_kcs()
{
  struct avl_node *n;
  struct kcs_sm_list *l;
  
  l = malloc(sizeof(struct kcs_sm_list));

  if (l == NULL)
    return NULL;

  l->l_sm = NULL;
  l->l_count = 0;

  n = create_node_avltree(STATEMACHINE_LIST, l);

  if (n == NULL){
    free(l);
    return NULL;
  }
  
  return n;
}

int add_to_statemachine_list_kcs(struct avl_tree *t, struct kcs_sm *m)
{
  struct kcs_sm_list *l;
  struct avl_node *n;
  
  if (t == NULL || m == NULL)
    return -1;

  n = find_name_node_avltree(t, STATEMACHINE_LIST);
  if (n == NULL)
    return -1;

  l = get_node_data_avltree(n);
  if (l == NULL)
    return -1;

  l->l_sm = realloc(l->l_sm, sizeof(struct kcs_sm *) * (l->l_count+1));

  if (l->l_sm == NULL)
    return -1;
  
  l->l_sm[l->l_count] = m;
  l->l_count++;

  return 0;
}

struct avl_tree *get_datastore_tree_kcs(struct katcp_dispatch *d)
{
  struct kcs_basic *kb;
  struct avl_tree *t;
  struct avl_node *n;
  
  kb = get_mode_katcp(d, KCS_MODE_BASIC);
  if (kb == NULL)
    return NULL;

  t = kb->b_ds;
  if (t == NULL){
    if((t = create_avltree()) == NULL){
#ifdef DEBUG
      fprintf(stderr, "statemachine: cannot create datastore\n");
#endif
      return NULL;
    }

#ifdef DEBUG
    fprintf(stderr, "statemachine: create tree (%p)\n", t);
#endif
    
    n = create_statemachine_list_kcs();
    
    if (n == NULL){
      destroy_avltree(t);
      return NULL;
    }

    if (add_node_avltree(t, n) < 0){
      free_node_avltree(n);
      destroy_avltree(t);
      return NULL;
    }

#ifdef DEBUG
    fprintf(stderr, "statemachine: create %s node (%p)\n", STATEMACHINE_LIST, n);
#endif

    n = create_mod_store_kcs();

    if (n == NULL){
      destroy_avltree(t);
    }

    if (add_node_avltree(t, n) < 0){
      free_node_avltree(n);
      destroy_avltree(t);
      return NULL;
    }
#ifdef DEBUG
    fprintf(stderr, "statemachine: create %s node (%p)\n", MOD_STORE, n);
#endif

    kb->b_ds = t;
  }
  
  return t;
}

struct kcs_sm *create_sm_kcs(char *name)
{
  struct kcs_sm *m;

  if (name == NULL)
    return NULL;

  m = malloc(sizeof(struct kcs_sm));

  if (m == NULL)
    return NULL;

  m->m_name  = strdup(name);
  m->m_start = NULL;

  if (m->m_name == NULL){
    free(m);
    return NULL;
  }

  return m;
}

void destroy_sm_state_kcs(struct kcs_sm_state *s)
{
#ifdef DEBUG
  fprintf(stderr, "statemachine: destroy_sm_state_kcs %s\n", s->s_name);
#endif
  if (s->s_name) { free(s->s_name); s->s_name = NULL; }
  if (s->s_edge) { s->s_edge = NULL; }
  if (s->s_data) { s->s_data = NULL; }
}
void destroy_sm_edge_kcs(struct kcs_sm_edge *e)
{
#ifdef DEBUG
  fprintf(stderr, "statemachine: destroy_sm_edge_kcs\n");
#endif
  if (e->e_next) { e->e_next = NULL; }
  if (e->e_call) { e->e_call = NULL; }
}
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

int statemachine_declare_kcs(struct katcp_dispatch *d)
{
  struct kcs_sm *m;
  struct avl_tree *t;
  struct avl_node *n;
  char *name;

  name = arg_string_katcp(d, 2);

  if (name == NULL)
    return KATCP_RESULT_FAIL;

  t = get_datastore_tree_kcs(d);
  
  if (t == NULL){
    return KATCP_RESULT_FAIL;
  }

  m = create_sm_kcs(name);
  if (m == NULL)
    return KATCP_RESULT_FAIL;
  
  n = create_node_avltree(name, m);
  if (n == NULL)
    return KATCP_RESULT_FAIL;

  if (add_node_avltree(t, n) < 0){
#ifdef DEBUG
    fprintf(stderr, "statemachine: could not add node <%s> to tree\n",get_node_name_avltree(n));
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not add %s to datastore", get_node_name_avltree(n));
    free_node_avltree(n);
    return KATCP_RESULT_FAIL;
  }
  
  if (add_to_statemachine_list_kcs(t, m) < 0){
    destroy_statemachine_kcs(m);
    return KATCP_RESULT_FAIL;
  }

#ifdef DEBUG
  fprintf(stderr, "created statemachine %s (%p) stored it in datastore (%p) and statemachine list\n", get_node_name_avltree(n), n, t);
#endif
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "created statemachine %s (%p) and stored it in datastore (%p) and statemachine list", get_node_name_avltree(n), n, t);
  
  return KATCP_RESULT_OK;
}

int statemachine_print_ds_kcs(struct katcp_dispatch *d)
{
  struct avl_tree *t;

  t = get_datastore_tree_kcs(d);
  if (t == NULL)
    return KATCP_RESULT_FAIL;

  print_avltree(t->t_root, 0);
  
  return KATCP_RESULT_OK;
}

int statemachine_print_ls_kcs(struct katcp_dispatch *d)
{
  struct kcs_sm_list *l;
  struct avl_tree *t;
  struct avl_node *n;
  int i;

  t = get_datastore_tree_kcs(d);
  if (t == NULL)
    return KATCP_RESULT_FAIL;

  n = find_name_node_avltree(t, STATEMACHINE_LIST);

  if (n == NULL)
    return KATCP_RESULT_FAIL;

  l = get_node_data_avltree(n);

  if (l == NULL)
    return KATCP_RESULT_FAIL;

  if (l->l_count == 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no statemachines have been declared yet");
    return KATCP_RESULT_FAIL;
  }

  for (i=0; i<l->l_count; i++){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s (%p)", l->l_sm[i]->m_name, l->l_sm[i]);  
  }

  return KATCP_RESULT_OK;
}

int statemachine_print_ms_kcs(struct katcp_dispatch *d)
{
  struct kcs_mod_store *ms;
  struct avl_tree *t;
  struct avl_node *n;
  struct avl_node_list *l;
  int i;

  t = get_datastore_tree_kcs(d);
  if (t == NULL)
    return KATCP_RESULT_FAIL;

  n = find_name_node_avltree(t, MOD_STORE);

  if (n == NULL)
    return KATCP_RESULT_FAIL;

  ms = get_node_data_avltree(n);

  if (ms == NULL)
    return KATCP_RESULT_FAIL;

  l = ms->m_hl;
  
  if (l == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no modules have been loaded yet try ?sm loadmod [filename]");
    return KATCP_RESULT_FAIL;
  }

  for (i=0; i<l->l_count; i++){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "module: %s handle: (%p)", get_node_name_avltree(l->l_n[i]), get_node_data_avltree(l->l_n[i]));  
  }

  l = ms->m_sl;
  for (i=0; i<l->l_count; i++){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "symbol: %s call: (%p)", get_node_name_avltree(l->l_n[i]), get_node_data_avltree(l->l_n[i]));  
  }
  
  return KATCP_RESULT_OK;
}

int statemachine_loadmod_kcs(struct katcp_dispatch *d)
{
  struct avl_tree *t;
  struct avl_node *n;
  struct kcs_mod_store *ms;
  char *mod;
  char **sl;
  void *handle, *call;

  mod = arg_string_katcp(d, 2);
  
  t = get_datastore_tree_kcs(d);
  if (t == NULL)
    return KATCP_RESULT_FAIL;

  n = find_name_node_avltree(t, MOD_STORE);
  if (n == NULL)
    return KATCP_RESULT_FAIL;
  
  ms = get_node_data_avltree(n);
  if (ms == NULL)
    return KATCP_RESULT_FAIL;
  
  handle = dlopen(mod, RTLD_LAZY);

  if (handle == NULL){
    mod = dlerror();
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s\n", mod);
#ifdef DEBUG
    fprintf(stderr, "statemachine: %s\n", mod);
#endif
    return KATCP_RESULT_FAIL;
  }

  n = create_node_avltree(mod, handle);
  if(n == NULL){
    dlclose(handle);
    return KATCP_RESULT_FAIL;
  }
  if (add_node_avltree(t, n) < 0){
    dlclose(handle);
    free_node_avltree(n);
    return KATCP_RESULT_FAIL;
  }
  
  if (add_node_mod_store_kcs(ms, n, MOD_STORE_TYPE_HANDLE) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not add: %s to handle list", mod);
#ifdef DEBUG
    fprintf(stderr, "statemachine: could not add: %s to handle list\n", mod);
#endif
    dlclose(handle);
    return KATCP_RESULT_FAIL;
  }

  dlerror();
  call = dlsym(handle, "sym_list_mod");
  if ((mod = dlerror()) != NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "error btw symlist and syms%s", mod);
#ifdef DEBUG
    fprintf(stderr, "statemachine: error btw symlist and syms %s\n", mod);
#endif
    dlclose(handle);
    return KATCP_RESULT_FAIL;
  } 

  sl = call;
  do {
#ifdef DEBUG
    fprintf(stderr, "statemachine: sym_list_mod: %s\n", *sl);
#endif
    dlerror();
    
    call = dlsym(handle, *sl);
    if ((mod = dlerror()) != NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "error btw symlist and syms%s", mod);
#ifdef DEBUG
      fprintf(stderr, "statemachine: error btw symlist and syms %s\n", mod);
#endif
      return KATCP_RESULT_FAIL;
    } 
    
    n = create_node_avltree(*sl, call);

    if (add_node_avltree(t, n) < 0){
#ifdef DEBUG
      fprintf(stderr, "statemachine: could not add: %s to tree\n", get_node_name_avltree(n));
#endif
      free_node_avltree(n);
      return KATCP_RESULT_FAIL;
    }
    
    if (add_node_mod_store_kcs(ms, n, MOD_STORE_TYPE_SYMBOL) < 0){
#ifdef DEBUG
      fprintf(stderr, "statemachine: could not add: %s to mod store symbol list\n", get_node_name_avltree(n));
#endif
      return KATCP_RESULT_FAIL;
    }
     
    sl++;
  } while (*sl != NULL);

#ifdef DEBUG
  fprintf(stderr, "statemachine: loaded all symbols into tree and mod store\n");
#endif

  return KATCP_RESULT_OK;
}

int statemachine_exec_kcs(struct katcp_dispatch *d)
{
  struct avl_tree *t;
  struct avl_node *n;
  char *mod;
  int (*call)(int);
  int rtn;

  mod = arg_string_katcp(d, 2);
  
  t = get_datastore_tree_kcs(d);
  if (t == NULL)
    return KATCP_RESULT_FAIL;

  n = find_name_node_avltree(t, mod);
  if (n == NULL)
    return KATCP_RESULT_FAIL;

  call = get_node_data_avltree(n);
  
  if (call == NULL)
    return KATCP_RESULT_FAIL;

  rtn = (*call)(555);
#ifdef DEBUG
  fprintf(stderr, "statemachine: call returned %d\n", rtn);
#endif

  return KATCP_RESULT_OK;
}

int statemachine_greeting_kcs(struct katcp_dispatch *d)
{
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"loadmod [so name]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"declare [sm name]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"state [sm name] [state name]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"edge [sm name] [state name 1] [state name 2] [condition state | ok(generic)]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"run [sm name]");
  return KATCP_RESULT_OK;
}

int statemachine_cmd(struct katcp_dispatch *d, int argc)
{
  switch (argc){
    case 1:
      return statemachine_greeting_kcs(d);
      
      break;
    case 2:
      if (strcmp(arg_string_katcp(d,1), "ds") == 0)
        return statemachine_print_ds_kcs(d);
      if (strcmp(arg_string_katcp(d,1), "ls") == 0)
        return statemachine_print_ls_kcs(d);
      if (strcmp(arg_string_katcp(d,1), "ms") == 0)
        return statemachine_print_ms_kcs(d);
      
      break;
    case 3:
      if (strcmp(arg_string_katcp(d,1), "declare") == 0)
        return statemachine_declare_kcs(d);
      if (strcmp(arg_string_katcp(d,1), "loadmod") == 0)
        return statemachine_loadmod_kcs(d);

      if (strcmp(arg_string_katcp(d,1), "exec") == 0)
        return statemachine_exec_kcs(d);
       
      break;
    case 4:
      
      break;
    case 6:
      
      break;
  }
  return KATCP_RESULT_FAIL;
}




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
