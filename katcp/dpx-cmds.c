
#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

/* item management **************************************************/

void destroy_cmd_item_katcp(struct katcp_cmd_item *i)
{
  if(i == NULL){
    return;
  }

  if(i->i_refs > 1){
    /* not yet time */
    i->i_refs--;
    return;
  }

  i->i_refs = 0;

  if(i->i_name){
    free(i->i_name);
    i->i_name = NULL;
  }

  if(i->i_help){
    free(i->i_help);
    i->i_help = NULL;
  }

  i->i_call = NULL;
  i->i_flags = 0;

  if(i->i_clear){
    (*(i->i_clear))(i->i_data);
    i->i_clear = NULL;
  }

  i->i_data = NULL;

  free(i);
}

void void_destroy_cmd_item_katcp(void *v)
{
  struct katcp_cmd_item *i;

  i = v;

  destroy_cmd_item_katcp(i);
}

struct katcp_cmd_item *create_cmd_item_katcp(char *name, char *help, unsigned int flags, int (*call)(struct katcp_dispatch *d, int argc), void *data, void (*clear)(void *data)){
  struct katcp_cmd_item *i;

  i = malloc(sizeof(struct katcp_cmd_item));
  if(i == NULL){
    return NULL;
  }

  i->i_name = NULL;
  i->i_help = NULL;

  i->i_flags = 0;

  i->i_call = NULL;
  i->i_data = NULL;
  i->i_clear = NULL;

  if(name){
    i->i_name = strdup(name);
    if(i->i_name == NULL){
      destroy_cmd_item_katcp(i);
      return NULL;
    }
  }

  if(help){
    i->i_help = strdup(help);
    if(i->i_help == NULL){
      destroy_cmd_item_katcp(i);
      return NULL;
    }
  }

  i->i_flags = flags;

  i->i_call = call;
  i->i_data = data;
  i->i_clear = clear;

  i->i_refs = 0;

  return i;
}

void set_flag_cmd_item_katcp(struct katcp_cmd_item *ix, unsigned int flags)
{
  if(ix == NULL){
    return;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if((flags | KATCP_MAP_FLAG_HIDDEN) != KATCP_MAP_FLAG_HIDDEN){
    /* so GREEDY could also be set, but unclear if this should be visible to runtime katcp control channel, as there might be a crash/inconsistency risk */
    fprintf(stderr, "logic problem: set flag should only be used to control HIDDEN flag\n");
    abort();
  }
#endif

  ix->i_flags = flags;
}

int set_help_cmd_item_katcp(struct katcp_cmd_item *ix, char *help)
{
  char *ptr;

  if(ix == NULL){
    return -1;
  }

  if(help){
    ptr = strdup(help);
    if(ptr == NULL){
      return -1;
    }
  } else {
    ptr = NULL;
  }

  if(ix->i_help){
    free(ix->i_help);
    ix->i_help = NULL;
  }

  ix->i_help = ptr;

  return 0;
}

/* full map management **********************************************/

int remove_cmd_map_katcp(struct katcp_cmd_map *m, char *name)
{
  if((m == NULL) || (name == NULL)){
    return -1;
  }

  return del_name_node_avltree(m->m_tree, name, &void_destroy_cmd_item_katcp);
}

struct katcp_cmd_item *find_cmd_map_katcp(struct katcp_cmd_map *m, char *name)
{
  char *ptr;

  if(name == NULL){
    /* WARNING - could return default value */
    return NULL;
  }

  switch(name[0]){
    case KATCP_REQUEST :
    case KATCP_INFORM :
    case KATCP_REPLY :
      ptr = name + 1;
      break;
    default :
      ptr = name;
      break;
  }

  return find_data_avltree(m->m_tree, ptr);
}

void destroy_cmd_map_katcp(struct katcp_cmd_map *m)
{
  if(m == NULL){
    return;
  }

  if(m->m_refs > 0){
    m->m_refs--;
#ifdef DEBUG
    fprintf(stderr, "map %p now decremented to %d\n", m, m->m_refs);
#endif
  }

  if(m->m_refs > 0){
    return;
  }

  if(m->m_tree){
    destroy_avltree(m->m_tree, &void_destroy_cmd_item_katcp);
    m->m_tree = NULL;
  }

  if(m->m_fallback){
    destroy_cmd_item_katcp(m->m_fallback);
    m->m_fallback = NULL;
  }

  if(m->m_name){
    free(m->m_name);
    m->m_name = NULL;
  }

  free(m);
}

void hold_cmd_map_katcp(struct katcp_cmd_map *m)
{
  m->m_refs++;
#ifdef DEBUG
  fprintf(stderr, "map %p now incremented to %d\n", m, m->m_refs);
#endif
}

struct katcp_cmd_map *create_cmd_map_katcp(char *name)
{
  struct katcp_cmd_map *m;

  m = malloc(sizeof(struct katcp_cmd_map));
  if(m == NULL){
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "created map %p\n", m);
#endif

  m->m_name = NULL;
  m->m_refs = 0;
  m->m_tree = NULL;
  m->m_fallback = NULL;

  m->m_tree = create_avltree();
  if(m->m_tree == NULL){
    destroy_cmd_map_katcp(m);
    return NULL;
  }

  if(name){
    m->m_name = strdup(name);
    if(m->m_name == NULL){
      destroy_cmd_map_katcp(m);
      return NULL;
    }
  }

  return m;
}

/* WARNING: allows us to abuse print_inorder_avltree to duplicate tree. This line here makes the code thread unsafe */
static struct katcp_cmd_map *duplicate_cmd_map_state = NULL;

void duplicate_item_for_cmd_map_katcp(struct katcp_dispatch *d, char *key, void *v)
{
  struct katcp_cmd_item *i;
  struct avl_node *n;

  i = v;

  if(duplicate_cmd_map_state == NULL){
    return;
  }

  n = create_node_avltree(key, i);
  if(n == NULL){
    duplicate_cmd_map_state = NULL;
    return;
  }
  
  if(add_node_avltree(duplicate_cmd_map_state->m_tree, n) < 0){
    free_node_avltree(n, NULL);
    duplicate_cmd_map_state = NULL;
    return;
  }

  i->i_refs++;

#ifdef DEBUG
  fprintf(stderr, "map: after duplication references to handler %s now %d\n", i->i_name, i->i_refs);
#endif
}

struct katcp_cmd_map *duplicate_cmd_map_katcp(struct katcp_cmd_map *mo, char *name)
{
  struct katcp_cmd_map *mx;

  /* threaded stuff might want to lock this, or even better just implement a clone_avltree function */
  if(duplicate_cmd_map_state != NULL){
    return NULL;
  }

  mx = create_cmd_map_katcp(name);
  if(mx == NULL){
    return NULL;
  }

  duplicate_cmd_map_state = mx;

  /* WARNING: taking severe liberties with the API - assumes d unused if flags == 0 */
  print_inorder_avltree(NULL, mo->m_tree->t_root, &duplicate_item_for_cmd_map_katcp, 0);

  if(duplicate_cmd_map_state == NULL){
    destroy_cmd_map_katcp(mx);
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "map: duplicating map %p as %p\n", mo, mx);
#endif

  duplicate_cmd_map_state = NULL;

  return mx;
}

int add_full_cmd_map_katcp(struct katcp_cmd_map *m, char *name, char *help, unsigned int flags, int (*call)(struct katcp_dispatch *d, int argc), void *data, void (*clear)(void *data))
{
  struct katcp_cmd_item *i;
  struct avl_node *n;
  char *ptr;

  if(name == NULL){
    return -1;
  }

  switch(name[0]){
    case KATCP_REQUEST : 
    case KATCP_REPLY :
    case KATCP_INFORM : 
      ptr = name + 1;
      break;
    default: 
      ptr = name;
      break;
  }

  if(ptr[0] == '\0'){
    return -1;
  }

  i = create_cmd_item_katcp(ptr, help, flags, call, data, clear);
  if(i == NULL){
    return -1;
  }

  n = create_node_avltree(ptr, i);
  if(n == NULL){
    destroy_cmd_item_katcp(i);
    return -1;
  }
  
  if(add_node_avltree(m->m_tree, n) < 0){
    /* WARNING: the convention requires a caller to clean up if something fails, hence can't invoke the clear function yet */
    i->i_clear = NULL;
    free_node_avltree(n, &void_destroy_cmd_item_katcp);
    return -1;
  }

  i->i_refs = 1;

  return 0;
}

int add_cmd_map_katcp(struct katcp_cmd_map *m, char *name, char *help, int (*call)(struct katcp_dispatch *d, int argc))
{
  return add_full_cmd_map_katcp(m, name, help, 0, call, NULL, NULL);
}


#endif /* KATCP_EXPERIMENTAL */
