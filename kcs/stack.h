#ifndef _STACK_H_
#define _STACK_H_

#include <katpriv.h>

struct katcp_stack_obj {
  void *o_data;
  struct katcp_type *o_type;
};

struct katcp_stack {
  struct katcp_stack_obj **s_objs;
  int s_count;
};

struct katcp_stack *create_stack_katcp();
struct katcp_stack_obj *create_obj_stack_katcp(void *data, struct katcp_type *type);
int push_stack_katcp(struct katcp_stack *s, void *data, struct katcp_type *type);
int push_stack_obj_katcp(struct katcp_stack *s, struct katcp_stack_obj *o);
struct katcp_stack_obj *pop_stack_katcp(struct katcp_stack *s);
struct katcp_stack_obj *peek_stack_katcp(struct katcp_stack *s);
struct katcp_stack_obj *index_stack_katcp(struct katcp_stack *s, int indx);
void print_stack_obj_katcp(struct katcp_dispatch *d, struct katcp_stack_obj *o);
void print_stack_katcp(struct katcp_dispatch *d, struct katcp_stack *s);
void destroy_stack_katcp(struct katcp_stack *s);
void destroy_obj_stack_katcp(struct katcp_stack_obj *o);

#endif
