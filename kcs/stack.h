#ifndef _STACK_H_
#define _STACK_H_

struct stack_obj {
  void *o_data;
  int o_type;
};

struct stack {
  struct stack_obj **s_objs;
  int s_count;
};

struct stack *create_stack();
struct stack_obj *create_obj_stack(void *data, int type);
int push_stack(struct stack *s, void *data, int type);
struct stack_obj *pop_stack(struct stack *s);
struct stack_obj *peek_stack(struct stack *s);
void destroy_stack(struct stack *s);
void destroy_obj_stack(struct stack_obj *o);

#endif
