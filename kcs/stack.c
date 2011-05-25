/*
  A Stack implementation
  push, pop, peek
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <katpriv.h>

#include "stack.h"


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

struct katcp_stack_obj *create_obj_stack_katcp(void *data, struct katcp_type *type)
{
  struct katcp_stack_obj *o;

  if (data == NULL)
    return NULL;
  
  o =  malloc(sizeof(struct katcp_stack_obj));
  if (o == NULL)
    return NULL;

  o->o_data = data;
  o->o_type = type;

  return o;
}

void destroy_obj_stack_katcp(struct katcp_stack_obj *o)
{
  struct katcp_type *t;
  if (o != NULL){
    t = o->o_type;
#ifdef DEBUG
    fprintf(stderr, "stack obj: %s %p %p\n",t->t_name, t, t->t_free);
#endif
    if ((t != NULL) && (t->t_free != NULL))
      (*t->t_free)(o->o_data);
    o->o_data = NULL;
    o->o_type = NULL;
    free(o);
  }
}

void destroy_stack_katcp(struct katcp_stack *s)
{
  int i;

  if (s != NULL){
    if (s->s_objs != NULL){
      for (i=0; i<s->s_count; i++)
        destroy_obj_stack_katcp(s->s_objs[i]);
      free(s->s_objs);
    }
    free(s);
  }
}

int push_stack_obj_katcp(struct katcp_stack *s, struct katcp_stack_obj *o)
{
  if (s == NULL || o == NULL)
    return -1;
  
  s->s_objs = realloc(s->s_objs, sizeof(struct katcp_stack_obj *) * (s->s_count + 1));
  if (s->s_objs == NULL){
    destroy_obj_stack_katcp(o);
    return -1;
  }
  
  s->s_objs[s->s_count] = o;
  s->s_count++;
  
  return 0;
}

int push_stack_katcp(struct katcp_stack *s, void *data, struct katcp_type *type)
{
  struct katcp_stack_obj *o;

  if (s == NULL)
    return -1;

  o = create_obj_stack_katcp(data, type);
  if (o == NULL)
    return -1;
  
  return push_stack_obj_katcp(s, o);
}

struct katcp_stack_obj *pop_stack_katcp(struct katcp_stack *s)
{
  struct katcp_stack_obj *o;
  
  if (s == NULL)
    return NULL;

  if (s->s_count == 0)
    return NULL;
  
  o = s->s_objs[s->s_count - 1];
  
  s->s_objs = realloc(s->s_objs, sizeof(struct katcp_stack_obj *) * (s->s_count - 1));
  s->s_count--;

  return o;  
}

struct katcp_stack_obj *peek_stack_katcp(struct katcp_stack *s)
{
  if (s == NULL)
    return NULL;
  return s->s_objs[s->s_count - 1];
}

struct katcp_stack_obj *index_stack_katcp(struct katcp_stack *s, int indx)
{
  if (s == NULL)
    return NULL;

  if ((s->s_count-1) < indx)
    return NULL;
  
  return s->s_objs[indx];
}

void print_stack_obj_katcp(struct katcp_dispatch *d, struct katcp_stack_obj *o)
{
  struct katcp_type *t;
  if (o == NULL){
#ifdef debug
    fprintf(stderr,"stack: null stack obj encountered ending\n");
#endif
    return;
  }
  t = o->o_type;
#ifdef DEBUG
  fprintf(stderr, "stack: type: %p data (%p)\n", o->o_type, o->o_data);
#ifdef DEBUG
  fprintf(stderr, "stack obj: %s %p %p\n",t->t_name, t, t->t_print);
#endif
  if ((t != NULL) && (t->t_print != NULL))
    (*t->t_print)(d, o->o_data);
#endif
}

void print_stack_katcp(struct katcp_dispatch *d, struct katcp_stack *s)
{
  struct katcp_stack_obj *o;
  int i;

  if (s == NULL)
    return;
  
  for (i=s->s_count-1; i>=0; i--){
    o = s->s_objs[i];
#ifdef DEBUG
    fprintf(stderr,"stack: [%2d] ",i);
#endif
    print_stack_obj_katcp(d, o);
  }
}


#ifdef STANDALONE
int main(int argc, char *argv[])
{
  struct katcp_stack *s;
  struct katcp_stack_obj *o;

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
    destroy_obj_stack_katcp(o);
  }
  
  destroy_stack_katcp(s);

  return 0;
}
#endif
