#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <katcp.h>
#include <katpriv.h>
#include <avltree.h>

#include "ktype.h"

void destroy_type_katcp(struct katcp_type *t)
{
  if (t != NULL){
    if (t->t_name != NULL) { free(t->t_name); t->t_name = NULL; }
    /*TODO: pass delete function to avltree*/
    if (t->t_tree != NULL) { destroy_avltree(t->t_tree); t->t_tree = NULL; }
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
  unsigned int low, high, mid;
  int cmp;
  struct katcp_type *t;

  if (ts == NULL || t_size == 0){
#ifdef DEBUG
    fprintf(stderr, "katcp_type: null ts or zero size");
#endif
    return 0;
  }

  low = 0;
  high = t_size - 1;
  
  while (low <= high){
    mid = (low + high) >> 1;

    t = ts[mid];
  
    cmp = strcmp(str, t->t_name);
    if (cmp == 0){
#ifdef DEBUG
      fprintf(stderr, "katcp_type: found <%s> @ %d\n", str, mid);
#endif
      return mid;
    } else if (cmp > 0){
      high = mid - 1;
#ifdef DEBUG
      fprintf(stderr, "katcp_type: set high to %d\n", high);
#endif
    } else if (cmp < 0){ 
      low = mid - 1;
#ifdef DEBUG
      fprintf(stderr, "katcp_type: set low to %d\n", low);
#endif
    }
  }
  return (-1) * (low + 1);
}

int register_type_katcp(struct katcp_dispatch *d, char *name, int (*fn_print)(struct katcp_dispatch *, void *), void (*fn_free)(void *))
{
  struct katcp_shared *s;
  struct katcp_type **ts;
  struct katcp_type *t;
  int size, pos, i;

  sane_dispatch_katcp(d);

  s = d->d_shared;
  if (s == NULL)
    return KATCP_RESULT_FAIL;
  
  ts = s->s_type;
  size = s->s_type_count;

  pos = binary_search_type_list_katcp(ts, size, name);
  if (pos > 0){
#ifdef DEBUG
    fprintf(stderr, "katcp_type: binary search of list returns a positive match\n");
    fprintf(stderr, "katcp_type: could not create type <%s>\n", name);
#endif
    return KATCP_RESULT_FAIL;
  }
  pos *= (-1);

  ts = realloc(ts, sizeof(struct katcp_type *) * (size + 1));
  if (ts == NULL)
    return KATCP_RESULT_FAIL;
  
  ts[size] = NULL;

  t = create_type_katcp();
  if (t == NULL)
    return KATCP_RESULT_FAIL;

  t->t_name = strdup(name);
  if(t->t_name == NULL){
    destroy_type_katcp(t); 
    return KATCP_RESULT_FAIL;
  }

  t->t_tree = create_avltree();
  if (t->t_tree == NULL){
    destroy_type_katcp(t);
    return KATCP_RESULT_FAIL;
  }
  
  t->t_print = fn_print;
  t->t_free = fn_free;

  i = size;
  for (; i > pos; i--){
    ts[i] = ts[i-1];
  }

  ts[i] = t;

#ifdef DEBUG
  fprintf(stderr, "katcp_type: inserted <%s> into (%p) at %d\n", t->t_name, ts, i);
#endif
  
  s->s_type = ts;
  s->s_type_count++;

  return KATCP_RESULT_OK;
}

int deregister_type_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_shared *s;
  struct katcp_type **ts;
  int size, pos;

  sane_dispatch_katcp(d);

  s = d->d_shared;
  if (s == NULL)
    return KATCP_RESULT_FAIL;
  
  ts = s->s_type;
  size = s->s_type_count;

  pos = binary_search_type_list_katcp(ts, size, name);
  if (pos < 0){
#ifdef DEBUG
    fprintf(stderr, "katcp_type: could not find type <%s>\n", name);
#endif
    return KATCP_RESULT_FAIL;
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
    return KATCP_RESULT_FAIL;
  
  s->s_type = ts;
  s->s_type_count--;

  return KATCP_RESULT_OK;
}

void print_types_katcp(struct katcp_dispatch *d)
{
#ifdef DEBUG
  struct katcp_shared *s;
  struct katcp_type **ts, *t;
  int size, i;

  sane_dispatch_katcp(d);

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
    }
  }
#endif
}

void destroy_type_list_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;
  struct katcp_type **ts;
  int size, i;

  sane_dispatch_katcp(d);

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

#ifdef STANDALONE


int main(int argc, char *argv[])
{
  struct katcp_dispatch *d;
  
  d = startup_katcp();
  if (d == NULL){
    fprintf(stderr, "unable to create dispatch\n");
    return 1;
  }
  
  if (register_type_katcp(d, "node_store", NULL, NULL) == KATCP_RESULT_FAIL){
    destroy_type_list_katcp(d);
    return 1;
  }
  
  print_types_katcp(d);
  
  destroy_type_list_katcp(d);
  return 0;
} 
#endif
