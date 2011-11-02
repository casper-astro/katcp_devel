/*
  A Stack implementation
  push, pop, peek
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "katpriv.h"
#include "katcp.h"

struct katcp_stack *create_stack_katcp()
{
  struct katcp_stack *s;

  s = malloc(sizeof(struct katcp_stack));
  if (s == NULL)
    return NULL;

  s->s_objs     = NULL;
  s->s_count = 0;

  return s;
} 

struct katcp_tobject *create_tobject_katcp(void *data, struct katcp_type *type, int flagman)
{
  struct katcp_tobject *o;

#if 0
  if (data == NULL && type == NULL)
#endif
  if (data == NULL)
    return NULL;
  
  o =  malloc(sizeof(struct katcp_tobject));
  if (o == NULL)
    return NULL;

  o->o_data = data;
  o->o_type = type;
  o->o_man  = flagman;

  return o;
}

struct katcp_tobject *create_named_tobject_katcp(struct katcp_dispatch *d, void *data, char *type, int flagman)
{
  struct katcp_type *t;
  
  if (type == NULL || data == NULL)
    return NULL;

  t = find_name_type_katcp(d, type);
  if (t == NULL)
    return NULL;

  return create_tobject_katcp(data, t, flagman);
}
#if 1
struct katcp_tobject *copy_tobject_katcp(struct katcp_tobject *o)
{
  if (o == NULL)
    return NULL;
  return create_tobject_katcp(o->o_data, o->o_type, 0);
}
#endif

int compare_tobject_katcp(const void *m1, const void *m2)
{
  const struct katcp_tobject *a, *b;
  a = m1;
  b = m2;

  if (a == NULL || b == NULL)
    return 2;

  if (a->o_data > b->o_data)
    return 1;
  else if (a->o_data < b->o_data)
    return -1;

  return 0;
}


#if 0
void inc_ref_tobject_katcp(struct katcp_tobject *o)
{
  if (o == NULL)
    return;

  o->o_ref++;
}
#endif
int sizeof_stack_katcp(struct katcp_stack *s)
{
  return (s != NULL) ? s->s_count : 0;
}

int is_empty_stack_katcp(struct katcp_stack *s)
{
  if (s == NULL)
    return 1;
  return (s->s_count == 0) ? 1 : 0;
}

int empty_stack_katcp(struct katcp_stack *s)
{
  if (s == NULL)
    return -1;

  while(!is_empty_stack_katcp(s)){
    pop_data_stack_katcp(s);
  }
  
  return 0;
}

void destroy_tobject_katcp(void *data)
{
  struct katcp_tobject *o;
  struct katcp_type *t;
  o = data;
  if (o != NULL){

    if (o->o_man){
      t = o->o_type;
      if ((t != NULL) && (t->t_free != NULL)){
#ifdef DEBUG
        fprintf(stderr, "stack obj managed flag set del: %s %p %p\n",t->t_name, t, t->t_free);
#endif
        (*t->t_free)(o->o_data);
      }
    }

    o->o_data = NULL;
    o->o_type = NULL;
    free(o);

#if 0 
    def DEBUG
    fprintf(stderr, "stack destroy obj: refs: %d\n", o->o_ref);
    if (o->o_ref < 1){
      t = o->o_type;

#if 1 
      if ((t != NULL) && (t->t_free != NULL)){
#ifdef DEBUG
        fprintf(stderr, "stack obj del: %s %p %p\n",t->t_name, t, t->t_free);
#endif
        (*t->t_free)(o->o_data);
      }
#endif
      o->o_data = NULL;
      o->o_type = NULL;
      free(o);
    } else {
      o->o_ref--;
#ifdef DEBUG
      fprintf(stderr, "stack obj --refs %d\n", o->o_ref);
#endif
    }
#endif
  }
}

void destroy_stack_katcp(struct katcp_stack *s)
{
  int i;

  if (s != NULL){
    if (s->s_objs != NULL){
      for (i=0; i<s->s_count; i++)
        destroy_tobject_katcp(s->s_objs[i]);
      free(s->s_objs);
    }
    free(s);
  }
}

int push_tobject_katcp(struct katcp_stack *s, struct katcp_tobject *o)
{
  if (s == NULL || o == NULL)
    return -1;
  
  s->s_objs = realloc(s->s_objs, sizeof(struct katcp_tobject *) * (s->s_count + 1));
  if (s->s_objs == NULL){
    destroy_tobject_katcp(o);
    return -1;
  }
  
  s->s_objs[s->s_count] = o;
  s->s_count++;
 
  //inc_ref_tobject_katcp(o);

  return 0;
}
#if 0
int push_stack_ref_obj_katcp(struct katcp_stack *s, struct katcp_tobject *o)
{
  inc_ref_tobject_katcp(o);
  return push_tobject_katcp(s, o);
}
#endif

int push_stack_katcp(struct katcp_stack *s, void *data, struct katcp_type *type)
{
  struct katcp_tobject *o;

  if (s == NULL)
    return -1;

  o = create_tobject_katcp(data, type, 0);
  if (o == NULL)
    return -1;
  
  return push_tobject_katcp(s, o);
}

int push_named_stack_katcp(struct katcp_dispatch *d, struct katcp_stack *s, void *data, char *type)
{
  struct katcp_tobject *o;

  if (s == NULL || type == NULL)
    return -1;

  o = create_named_tobject_katcp(d, data, type, 0);
  if (o == NULL)
    return -1;
#if 0 
  return (refd > 0) ? push_stack_ref_obj_katcp(s, o) : push_tobject_katcp(s, o);
#endif
  return push_tobject_katcp(s, o);
}

struct katcp_tobject *pop_stack_katcp(struct katcp_stack *s)
{
  struct katcp_tobject *o;
  
  if (s == NULL)
    return NULL;

  if (s->s_count == 0)
    return NULL;
  
  o = s->s_objs[s->s_count - 1];
  
  s->s_objs = realloc(s->s_objs, sizeof(struct katcp_tobject *) * (s->s_count - 1));
  s->s_count--;

#if 0
  if (o != NULL)
    o->o_ref--;
#endif

  return o;  
}

void *pop_data_stack_katcp(struct katcp_stack *s)
{
  struct katcp_tobject *o;
  void *data;

  o = pop_stack_katcp(s);
  if (o == NULL)
    return NULL;
    
  data = o->o_data;

  destroy_tobject_katcp(o);

  return data;
}

void *pop_data_type_stack_katcp(struct katcp_stack *s, struct katcp_type *t)
{
  struct katcp_tobject *o;

  o = peek_stack_katcp(s);
  if (o == NULL)
    return NULL;

  if (o->o_type == t)
    return pop_data_stack_katcp(s);

  return NULL;
}

void *pop_data_expecting_stack_katcp(struct katcp_dispatch *d, struct katcp_stack *s, char *type)
{
  struct katcp_type *t;
  
  t = find_name_type_katcp(d, type);
  if (t == NULL)
    return NULL;
  
  return pop_data_type_stack_katcp(s, t);
}

struct katcp_tobject *peek_stack_katcp(struct katcp_stack *s)
{
  if (s == NULL || is_empty_stack_katcp(s))
    return NULL;
  return s->s_objs[s->s_count - 1];
}

struct katcp_tobject *index_stack_katcp(struct katcp_stack *s, int indx)
{
  if (s == NULL)
    return NULL;

  if ((s->s_count-1) < indx)
    return NULL;
  
  return s->s_objs[indx];
}

void *index_data_stack_katcp(struct katcp_stack *s, int indx)
{
  struct katcp_tobject *o;

  o = index_stack_katcp(s, indx);
  if (o == NULL)
    return NULL;

  return o->o_data;
}

void print_tobject_katcp(struct katcp_dispatch *d, struct katcp_tobject *o)
{
  struct katcp_type *t;
  if (o == NULL){
#ifdef DEBUG
    fprintf(stderr,"stack: null stack obj encountered ending\n");
#endif
    return;
  }
  t = o->o_type;
#if DEBUG >1
  fprintf(stderr, "stack: type: %p data (%p) managed flag: %d\n", o->o_type, o->o_data, o->o_man);
  //fprintf(stderr, "stack obj: %s %p %p\n",t->t_name, t, t->t_print);
#endif
  if ((t != NULL) && (t->t_print != NULL))
    (*t->t_print)(d, "unnamed_tobject", o->o_data);
}

void print_stack_katcp(struct katcp_dispatch *d, struct katcp_stack *s)
{
  struct katcp_tobject *o;
  int i;

  if (s == NULL)
    return;
  
  for (i=s->s_count-1; i>=0; i--){
    o = s->s_objs[i];
#if DEBUG >1
    fprintf(stderr,"stack: [%2d] ",i);
#endif
    print_tobject_katcp(d, o);
  }
}


#ifdef STANDALONE
int main(int argc, char *argv[])
{
  struct katcp_stack *s;
  struct katcp_tobject *o;

  s = create_stack_katcp();

  if (s == NULL)
    return 2;

  if (push_stack_katcp(s,"hello", NULL)<0){
#ifdef DEBUG
    fprintf(stderr, "stack: push error\n");
#endif
  }
  if (push_stack_katcp(s,"test", NULL)<0){
#ifdef DEBUG
    fprintf(stderr, "stack: push error\n");
#endif
  }
  if (push_stack_katcp(s,"echo", NULL)<0){
#ifdef DEBUG
    fprintf(stderr, "stack: push error\n");
#endif
  }
  if (push_stack_katcp(s,"dam", NULL)<0){
#ifdef DEBUG
    fprintf(stderr, "stack: push error\n");
#endif
  }
  
  o = peek_stack_katcp(s);
#ifdef DEBUG
  fprintf(stderr,"stack peek: %s type:%p\n",(char *)o->o_data, o->o_type);
#endif

  while ((o = pop_stack_katcp(s)) != NULL){
    #ifdef DEBUG
    fprintf(stderr,"stack [%d] pop: %s type:%p\n", s->s_count, (char *)o->o_data, o->o_type);
#endif
    destroy_tobject_katcp(o);
  }
  
  destroy_stack_katcp(s);

  return 0;
}
#endif
