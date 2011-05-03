/*
  A Stack implementation
  push, pop, peek
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "stack.h"


struct stack *create_stack()
{
  struct stack *s;

  s = malloc(sizeof(struct stack));
  if (s == NULL)
    return NULL;

  s->s_objs     = NULL;
  s->s_count = 0;

  return s;
} 

struct stack_obj *create_obj_stack(void *data, int type)
{
  struct stack_obj *o;

  if (data == NULL)
    return NULL;
  
  o =  malloc(sizeof(struct stack_obj));
  if (o == NULL)
    return NULL;

  o->o_data = data;
  o->o_type = type;

  return o;
}

void destroy_obj_stack(struct stack_obj *o)
{
  if (o != NULL){
    o->o_data = NULL;
    o->o_type = 0;
    free(o);
  }
}

void destroy_stack(struct stack *s)
{
  int i;

  if (s != NULL){
    if (s->s_objs != NULL){
      for (i=0; i<s->s_count; i++)
        destroy_obj_stack(s->s_objs[i]);
      free(s->s_objs);
    }
    free(s);
  }
}

int push_stack(struct stack *s, void *data, int type)
{
  struct stack_obj *o;

  if (s == NULL)
    return -1;

  o = create_obj_stack(data, type);
  if (o == NULL)
    return -1;
  
  s->s_objs = realloc(s->s_objs, sizeof(struct stack_objs *) * (s->s_count + 1));
  if (s->s_objs == NULL){
    destroy_obj_stack(o);
    return -1;
  }
  
  s->s_objs[s->s_count] = o;
  s->s_count++;
  
  return 0;
}

struct stack_obj *pop_stack(struct stack *s)
{
  struct stack_obj *o;
  
  if (s == NULL)
    return NULL;

  if (s->s_count == 0)
    return NULL;
  
  o = s->s_objs[s->s_count - 1];
  
  s->s_objs = realloc(s->s_objs, sizeof(struct stack_objs *) * (s->s_count - 1));
  s->s_count--;

  return o;  
}

struct stack_obj *peek_stack(struct stack *s)
{
  if (s == NULL)
    return NULL;
  return s->s_objs[s->s_count - 1];
}


#ifdef STANDALONE
int main(int argc, char *argv[])
{
  struct stack *s;
  struct stack_obj *o;

  s = create_stack();

  if (s == NULL)
    return 2;

  if (push_stack(s,"hello",0)<0){
#ifdef DEBUG
    fprintf(stderr, "stack: push error\n");
#endif
  }
  if (push_stack(s,"test",1)<0){
#ifdef DEBUG
    fprintf(stderr, "stack: push error\n");
#endif
  }
  if (push_stack(s,"echo",3)<0){
#ifdef DEBUG
    fprintf(stderr, "stack: push error\n");
#endif
  }
  if (push_stack(s,"dam",2)<0){
#ifdef DEBUG
    fprintf(stderr, "stack: push error\n");
#endif
  }
  
  o = peek_stack(s);
#ifdef DEBUG
  fprintf(stderr,"stack peek: %s %d\n",(char *)o->o_data, o->o_type);
#endif

  while ((o = pop_stack(s)) != NULL){
    #ifdef DEBUG
    fprintf(stderr,"stack [%d] pop: %s %d\n", s->s_count, (char *)o->o_data, o->o_type);
#endif
    destroy_obj_stack(o);
  }
  
  destroy_stack(s);

  return 0;
}
#endif
