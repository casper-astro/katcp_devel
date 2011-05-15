#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "katcp.h"
#include "katpriv.h"
#include "avltree.h"

void destroy_type_katcp(struct katcp_type *t)
{
  if (t != NULL){
#ifdef DEBUG
    fprintf(stderr, "katcp_type: destroy type <%s>\n", t->t_name);
#endif
    if (t->t_name != NULL) { free(t->t_name); t->t_name = NULL; }
    /*TODO: pass delete function to avltree*/
    if (t->t_tree != NULL) { 
      destroy_avltree(t->t_tree, t->t_free); 
      t->t_tree = NULL; 
    }
    t->t_print = NULL;
    t->t_free = NULL;
    free(t);
  }
}

struct katcp_type *create_type_katcp()
{
  struct katcp_type *t;
  
  t = malloc(sizeof(struct katcp_type));
  if (t == NULL)
    return NULL;

  t->t_name   = NULL;
  t->t_tree   = NULL;
  t->t_print  = NULL;
  t->t_free   = NULL;

  return t;
}

int binary_search_type_list_katcp(struct katcp_type **ts, int t_size, char *str)
{
  int low, high, mid;
  int cmp;
  struct katcp_type *t;

  if (ts == NULL || t_size == 0 || str == NULL){
#ifdef DEBUG
    fprintf(stderr, "katcp_type: null ts or zero size rtn -1\n");
#endif
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "katcp_type: bsearch for <%s>\n", str);
#endif
  
  low = 0;
  high = t_size - 1;
  
  while (low <= high){
    mid = low + ((high-low) / 2);
#if 0
    fprintf(stderr, "katcp_type: set mid %d\n", mid);
#endif

    t = ts[mid];
  
    cmp = strcmp(str, t->t_name);
    if (cmp == 0){
#ifdef DEBUG
      fprintf(stderr, "katcp_type: found <%s> @ %d\n", str, mid);
#endif
      return mid;
    } else if (cmp < 0){
      high = mid - 1;
#if 0
      fprintf(stderr, "katcp_type: set high to %d low is %d\n", high, low);
#endif
    } else if (cmp > 0){ 
      low = mid + 1;
#if 0
      fprintf(stderr, "katcp_type: set low to %d high is %d\n", low, high);
#endif
    }
  }

#ifdef DEBUG
  fprintf(stderr, "katcp_type: bsearch return:%d\n", (-1)*(low+1));
#endif

  return (-1) * (low+1) ;
}

int register_at_id_type_katcp(struct katcp_dispatch *d, int tid, char *tname, int (*fn_print)(struct katcp_dispatch *, void *), void (*fn_free)(void *))
{
  struct katcp_shared *s;
  struct katcp_type **ts;
  struct katcp_type *t;
  int size, i;

  sane_shared_katcp(d);

    s = d->d_shared;
  if (s == NULL)
    return -1;
  
  ts = s->s_type;
  size = s->s_type_count;

  ts = realloc(ts, sizeof(struct katcp_type *) * (size + 1));
  if (ts == NULL)
    return -1;
  
  ts[size] = NULL;

  t = create_type_katcp();
  if (t == NULL)
    return -1;

  t->t_name = strdup(tname);
  if(t->t_name == NULL){
    destroy_type_katcp(t); 
    return -1;
  }

  t->t_tree = create_avltree();
  if (t->t_tree == NULL){
    destroy_type_katcp(t);
    return -1;
  }
  
  t->t_print = fn_print;
  t->t_free = fn_free;

  i = size;
  for (; i > tid; i--){
    ts[i] = ts[i-1];
  }

  ts[i] = t;

#ifdef DEBUG
  fprintf(stderr, "katcp_type: registerd type <%s> into (%p) at %d\n", t->t_name, ts, i);
#endif
  
  s->s_type = ts;
  s->s_type_count++;

  return i; 
}

int register_name_type_katcp(struct katcp_dispatch *d, char *name, int (*fn_print)(struct katcp_dispatch *, void *), void (*fn_free)(void *))
{
  struct katcp_shared *s;
  struct katcp_type **ts;
  int size, pos;

  sane_shared_katcp(d);

  s = d->d_shared;
  if (s == NULL)
    return -1;
  
  ts = s->s_type;
  size = s->s_type_count;

  pos = binary_search_type_list_katcp(ts, size, name);
  if (pos >= 0){
#ifdef DEBUG
    fprintf(stderr, "katcp_type: binary search of list returns a positive match\n");
    fprintf(stderr, "katcp_type: could not create type re-register named type <%s>\n", name);
#endif
    return -1;
  }

  pos = (pos+1)* (-1);

  return register_at_id_type_katcp(d, pos, name, fn_print, fn_free);
}

int deregister_type_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_shared *s;
  struct katcp_type **ts;
  int size, pos;

  sane_shared_katcp(d);

  s = d->d_shared;
  if (s == NULL)
    return -1;
  
  ts = s->s_type;
  size = s->s_type_count;

  pos = binary_search_type_list_katcp(ts, size, name);
  if (pos < 0){
#ifdef DEBUG
    fprintf(stderr, "katcp_type: could not find type <%s>\n", name);
#endif
    return -1;
  }
  
  if (ts[pos] != NULL){
    destroy_type_katcp(ts[pos]);
    ts[pos] = NULL;
  }
  
  for (; pos < (size-1); pos++){
    ts[pos] = ts[pos+1];
  }
  
  ts = realloc(ts, sizeof(struct katcp_type *) * (size - 1));
  if (ts == NULL)
    return -1;
  
  s->s_type = ts;
  s->s_type_count--;

  return 0;
}

int store_data_type_katcp(struct katcp_dispatch *d, char *t_name, char *d_name, void *d_data, int (*fn_print)(struct katcp_dispatch *, void *), void (*fn_free)(void *))
{
  struct katcp_shared *s;
  
  struct katcp_type **ts;
  struct katcp_type *t;
  
  struct avl_tree *at;
  struct avl_node *an;

  int size, pos;

  sane_shared_katcp(d);

  s = d->d_shared;
  if (s == NULL)
    return -1;
  
  ts = s->s_type;
  size = s->s_type_count;

  pos = binary_search_type_list_katcp(ts, size, t_name);
  
  if (pos < 0){
#ifdef DEBUG
    fprintf(stderr, "katcp_type: need to register new type for <%s> at %d which maps to %d\n", t_name, pos, (pos+1)*(-1)); 
#endif
    /*pos returned from bsearch is pos to insert new type of searched name 
      but it needs to be decremented and flipped positive*/
    pos = register_at_id_type_katcp(d, (pos+1)*(-1), t_name, fn_print, fn_free);
    if (pos < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not create new type %s\n", t_name);
#ifdef DEBUG
      fprintf(stderr, "katcp_type: could not create new type: %s\n", t_name); 
#endif
      return -1;
    }
  }
  /*now pos is the position of the type in the list*/
  ts = s->s_type;
  size = s->s_type_count;
  
  t = ts[pos];

  if (t == NULL)
    return -1;
  
  if (t->t_print != fn_print || t->t_free != fn_free){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "print and free callbacks for data with key <%s> dont match type %s\n", d_name, t_name);
#ifdef DEBUG
    fprintf(stderr, "katcp_type: print and free callbacks for data with key <%s> dont match type %s\n", d_name, t_name); 
#endif
    return -1;
  }

  if (t->t_tree == NULL){
    t->t_tree = create_avltree();
#ifdef DEBUG
    fprintf(stderr, "katcp_type: create avltree for type: <%s>\n", t_name);
#endif
  }

  at = t->t_tree;
  if (at == NULL)
    return -1;

  an = create_node_avltree(d_name, d_data);

  if (an == NULL)
    return -1;

  if (add_node_avltree(at, an) < 0){
    free_node_avltree(an, fn_free);
    an = NULL;
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "katcp_type: inserted {%s} for type tree: <%s>\n", d_name, t_name);
#endif

  return 0;
}


struct katcp_type *find_name_type_katcp(struct katcp_dispatch *d, char *str)
{
  struct katcp_shared *s;
  struct katcp_type **ts;
  int pos, size;

  sane_shared_katcp(d);

  s = d->d_shared;
  if (s == NULL)
    return NULL;
  
  ts = s->s_type;
  size = s->s_type_count;

  pos = binary_search_type_list_katcp(ts, size, str);
  if (pos < 0){
#ifdef DEBUG
    fprintf(stderr, "katcp_type: could not find type <%s>\n", str);
#endif
    return NULL;
  }

  return ts[pos];
}

struct avl_tree *get_tree_type_katcp(struct katcp_type *t)
{
  if (t == NULL)
    return NULL;
  
  return t->t_tree;
}

void *get_key_data_type_katcp(struct katcp_dispatch *d, char *type, char *key)
{
  struct katcp_type *t;
  struct avl_node *n;

  if (type == NULL || key == NULL)
    return NULL;

  t = find_name_type_katcp(d, type);
  if (t == NULL)
    return NULL;

  n = find_name_node_avltree(t->t_tree, key);
  if (n == NULL)
    return NULL;
  
  return get_node_data_avltree(n);
}

void print_types_katcp(struct katcp_dispatch *d)
{
#if 1
  struct katcp_shared *s;
  struct katcp_type **ts, *t;
  int size, i;

  sane_shared_katcp(d);

  s = d->d_shared;
  if (s == NULL)
    return;

  ts = s->s_type;
  size = s->s_type_count;
  
  if (ts == NULL)
    return;

  for (i=0; i<size; i++){
    t = ts[i];
    if (t != NULL){
      fprintf(stderr, "katcp_type: [%d] type <%s> (%p) with tree (%p)\n", i, t->t_name, t, t->t_tree);
      if (t->t_tree != NULL);
        print_avltree(t->t_tree->t_root, 0);
    }
  }
#endif
}

void destroy_type_list_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_type **ts;
  int size, i;

  sane_shared_katcp(d);

  s = d->d_shared;
  if (s == NULL)
    return;

  ts = s->s_type;
  size = s->s_type_count;
  
  if (ts == NULL)
    return;

  for (i=0; i<size; i++){
    destroy_type_katcp(ts[i]);
  }
  
  free(ts);
  ts = NULL;

  s->s_type = ts;
  s->s_type_count = 0;
}

#ifdef UNIT_TEST_KTYPE 
int main(int argc, char *argv[])
{
  struct katcp_dispatch *d;
  int rtn;

  d = startup_katcp();
  if (d == NULL){
    fprintf(stderr, "unable to create dispatch\n");
    return 1;
  }
  
  rtn = 0;

  rtn += register_name_type_katcp(d, "test", NULL, NULL);
  rtn += register_name_type_katcp(d, "test", NULL, NULL);
  
  rtn += store_data_type_katcp(d, "names", "john", NULL, NULL, NULL);
  
  rtn += store_data_type_katcp(d, "string", "test1", NULL, NULL, NULL);
  rtn += store_data_type_katcp(d, "string", "test2", NULL, NULL, NULL);

  rtn += store_data_type_katcp(d, "names", "adam", NULL, NULL, NULL);
  rtn += store_data_type_katcp(d, "names", "perry", NULL, NULL, NULL);
 
  rtn += store_data_type_katcp(d, "string", "thisisalongstring", NULL, NULL, NULL);
  
  fprintf(stderr, "katcp_type: cumulative rtn in main: %d\n", rtn);
  
  fprintf(stderr,"\n");
  print_types_katcp(d);
  fprintf(stderr,"\n");
  
  destroy_type_list_katcp(d);

  shutdown_katcp(d);
  return 0;
} 
#endif
