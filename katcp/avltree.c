/***
  An AVLTree Implementation
  with parent pointers 
***/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "katcp.h"
#include "avltree.h"

struct avl_tree *create_avltree()
{
  struct avl_tree *t;

  t = malloc(sizeof(struct avl_tree));
  if (t == NULL)
    return NULL;

  t->t_root = NULL;

  return t;
}

struct avl_node *create_node_avltree(char *key, void *data)
{
  struct avl_node *n;

  if (key == NULL)
    return NULL;

  n = malloc(sizeof(struct avl_node));
  
  if (n == NULL)
    return NULL;

  n->n_key = strdup(key);
  if (n->n_key == NULL) {
    free(n);
    return NULL;
  }
  
  n->n_parent  = NULL;
  n->n_data    = data;
  n->n_left    = NULL;
  n->n_right   = NULL;
  n->n_balance = 0;

  return n;
}

char *get_node_name_avltree(struct avl_node *n)
{
  return (n != NULL) ? n->n_key : NULL;
}

void *get_node_data_avltree(struct avl_node *n)
{
  return (n != NULL) ? n->n_data : NULL;
}
  
int update_node_data_avltree(struct avl_node *n, void *data)
{
  if (n == NULL)
    return -1;

  if (n->n_data != NULL)
    free(n->n_data);

  n->n_data = data;

  return 0;
}

void print_avltree(struct katcp_dispatch *d, struct avl_node *n, int depth, void (*fn_print)(struct katcp_dispatch *, char *key, void *))
{
#if 1
#define SPACER "  "
  int i;
  i=0;

  if (n == NULL){
    fprintf(stderr,"%p\n", n);
    return;
  }

  fprintf(stderr,"in %s (%p) bal %d p(%p) data: (%p)\n", n->n_key, n, n->n_balance, n->n_parent, n->n_data);
  if(fn_print != NULL)
    (*fn_print)(d, n->n_key, n->n_data);

  for (i=0; i<depth; i++)
    fprintf(stderr,SPACER);
  fprintf(stderr," L ");
  print_avltree(d, n->n_left, depth+1, fn_print);

  for (i=0; i<depth; i++)
    fprintf(stderr, SPACER);
  fprintf(stderr," R ");
  print_avltree(d, n->n_right, depth+1, fn_print);
#undef SPACER   
#endif
}
#if 0
void print_inorder_avltree(struct katcp_dispatch *d, struct avl_node *n, void (*fn_print)(struct katcp_dispatch *,void *), int flags)
{
  if (n == NULL)
    return;
  
  print_inorder_avltree(d, n->n_left, fn_print, flags);
#if 0 
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "avlnode: %s",n->n_key);
  fprintf(stderr,"avltree: <%s>\n", n->n_key);
#endif
  
  if (flags){
    //log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", n->n_key);
    //prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_FIRST, "#%s", n->n_key);
    append_args_katcp(d, KATCP_FLAG_LAST, "with data %p", n->n_data);
  }

  if (fn_print != NULL)
    (*fn_print)(d, n->n_data);
  
  print_inorder_avltree(d, n->n_right, fn_print, flags);
}
#endif

struct avl_node *walk_inorder_avltree(struct avl_node *n)
{
  static struct katcp_stack *s = NULL;
  static int state = WALK_INIT;
  static struct avl_node *c;

  struct avl_node *rtn;

  while (1){

    switch (state) {

      case WALK_INIT:

#if DEBUG>2
        fprintf(stderr, "walk: about to init\n");
#endif

        s = create_stack_katcp();
        if (s == NULL)
          return NULL;

        c = n;

      case WALK_PUSH:

#if DEBUG>2
        fprintf(stderr, "walk: about to push\n");
#endif

        if (c != NULL){ 

          if (push_stack_katcp(s, c, NULL) < 0) {
            destroy_stack_katcp(s);
            s = NULL;
            state = WALK_INIT;
            return NULL;
          }

          c = c->n_left;
        } 
          
        if (c == NULL)
          state = WALK_POP;
        else
          state = WALK_PUSH;

        break;

      case WALK_POP:
#if DEBUG>2
        fprintf(stderr, "walk: about to pop\n");
#endif

        if (!is_empty_stack_katcp(s)){
          
          c = pop_data_stack_katcp(s);
          if (c != NULL){
            
            rtn = c;
            c = c->n_right;
            state = WALK_PUSH;

            return rtn;            
            
          } 

       } else {
         destroy_stack_katcp(s);
         s = NULL;
         state = WALK_INIT;
         return NULL;
       }


        break;

    }
  }
  
  return NULL;  
}

void *walk_data_inorder_avltree(struct avl_node *n)
{
  struct avl_node *c;

  c = walk_inorder_avltree(n);
  if (c == NULL)
    return NULL;

  return c->n_data;
}

void complex_inorder_traverse_avltree(struct katcp_dispatch *d, struct avl_node *n, void *global, int (*callback)(struct katcp_dispatch *d, void *global, char *key, void *data))
{
  struct avl_node *c;
#if 0
  while ((c = walk_inorder_avltree(n)) != NULL){

#ifdef DEBUG
    fprintf(stderr, "avl_tree: <%s>\n", c->n_key);
#endif
    if (flags){
      append_args_katcp(d, KATCP_FLAG_FIRST, "#%s", c->n_key);
      append_args_katcp(d, KATCP_FLAG_LAST, "with data %p", c->n_data);
    }

    if (fn_print != NULL)
      (*fn_print)(d, c->n_data);

  }
#endif
#if 1
  struct katcp_stack *s;

  if (n == NULL)
    return;
  
  s = create_stack_katcp();
  if (s == NULL)
    return;

  c = n;

  while (c != NULL) {
    if (push_stack_katcp(s, c, NULL) < 0){
#if DEBUG>2
      fprintf(stderr, "avl_tree: stack push error <%s>\n", c->n_key);
#endif
      destroy_stack_katcp(s);
      return;
    }
#if DEBUG>2
    fprintf(stderr, "avl_tree: stack 1 push  <%s>\n", c->n_key);
#endif
    c = c->n_left;
  }
  
#if DEBUG>2
  fprintf(stderr, "avl_tree: stack state: %d\n", is_empty_stack_katcp(s));
#endif

  while (!is_empty_stack_katcp(s)){
    c = pop_data_stack_katcp(s);
    if (c != NULL){

#if DEBUG>2
      fprintf(stderr, "avl_tree: <%s>\n", c->n_key);
#endif
      if (callback != NULL)
        (*callback)(d, global, c->n_key, c->n_data);

      c = c->n_right;

      while(c != NULL){
        if (push_stack_katcp(s, c, NULL) < 0){
#if DEBUG>2
          fprintf(stderr, "avl_tree: stack push error <%s>\n", c->n_key);
#endif
          destroy_stack_katcp(s);
          return;
        }
#if DEBUG>2
        fprintf(stderr, "avl_tree: stack 2 push  <%s>\n", c->n_key);
#endif
        c = c->n_left;
      }
      
    }
  }
  
  destroy_stack_katcp(s);
#endif
}

void print_inorder_avltree(struct katcp_dispatch *d, struct avl_node *n, void (*fn_print)(struct katcp_dispatch *d, char *key, void *data), int flags)
{
  /* TODO: this should just be a special case of complex_inorder_traverse_avltree */
  struct avl_node *c;
#if 0
  while ((c = walk_inorder_avltree(n)) != NULL){

#ifdef DEBUG
    fprintf(stderr, "avl_tree: <%s>\n", c->n_key);
#endif
    if (flags){
      append_args_katcp(d, KATCP_FLAG_FIRST, "#%s", c->n_key);
      append_args_katcp(d, KATCP_FLAG_LAST, "with data %p", c->n_data);
    }

    if (fn_print != NULL)
      (*fn_print)(d, c->n_data);

  }
#endif
#if 1
  struct katcp_stack *s;

  if (n == NULL)
    return;
  
  s = create_stack_katcp();
  if (s == NULL)
    return;

  c = n;

  while (c != NULL) {
    if (push_stack_katcp(s, c, NULL) < 0){
#if DEBUG>2
      fprintf(stderr, "avl_tree: stack push error <%s>\n", c->n_key);
#endif
      destroy_stack_katcp(s);
      return;
    }
#if DEBUG>2
    fprintf(stderr, "avl_tree: stack 1 push  <%s>\n", c->n_key);
#endif
    c = c->n_left;
  }
  
#if DEBUG>2
  fprintf(stderr, "avl_tree: stack state: %d\n", is_empty_stack_katcp(s));
#endif

  while (!is_empty_stack_katcp(s)){
    c = pop_data_stack_katcp(s);
    if (c != NULL){

#if DEBUG>2
      fprintf(stderr, "avl_tree: <%s>\n", c->n_key);
#endif
      if (flags){
        append_args_katcp(d, KATCP_FLAG_FIRST, "#%s", c->n_key);
        append_args_katcp(d, KATCP_FLAG_LAST, "with data %p", c->n_data);
      }

      if (fn_print != NULL)
        (*fn_print)(d, c->n_key, c->n_data);

      c = c->n_right;

      while(c != NULL){
        if (push_stack_katcp(s, c, NULL) < 0){
#if DEBUG>2
          fprintf(stderr, "avl_tree: stack push error <%s>\n", c->n_key);
#endif
          destroy_stack_katcp(s);
          return;
        }
#if DEBUG>2
        fprintf(stderr, "avl_tree: stack 2 push  <%s>\n", c->n_key);
#endif
        c = c->n_left;
      }
      
    }
  }
  
  destroy_stack_katcp(s);
#endif
}

int check_balances_avltree(struct avl_node *n, int depth)
{
  int l_bal, r_bal, bal;
  
  l_bal = 0;
  r_bal = 0;
  bal = 0;

  if (n == NULL)
    return 0;
  else {
    l_bal = check_balances_avltree(n->n_left, depth+1);
    r_bal = check_balances_avltree(n->n_right, depth+1);
    
#ifdef DEBUG 
    if ((r_bal-l_bal) != n->n_balance)
      fprintf(stderr,"avltree: ERROR %s\tn_bal: %d\t %d-%d=%d %s\n", n->n_key, n->n_balance, r_bal, l_bal, r_bal-l_bal, ((r_bal-l_bal)!=n->n_balance)?"ERROR":"OKAY");
#endif
    
    bal = (r_bal > l_bal) ? r_bal : l_bal;
    bal++;
  }

  return bal;
}

struct avl_node *rotate_rightright_avltree(struct avl_node *a)
{
  struct avl_node *f, *b;

  f = a->n_parent;
  b = a->n_right;

  if (a == NULL || b == NULL)
    return NULL;

  a->n_right = b->n_left;
  if (b->n_left != NULL)
    b->n_left->n_parent = a;
  b->n_left  = a;
  a->n_parent = b;
  b->n_parent = f;

  if (f != NULL) {
    if (f->n_right == a)
      f->n_right = b;
    else
      f->n_left = b;
  }

#if DEBUG >2
  fprintf(stderr,"avl_tree:\tRR a: <%s> bal %d\n", a->n_key, a->n_balance);
  fprintf(stderr,"avl_tree:\tRR b: <%s> bal %d\n", b->n_key, b->n_balance);
#endif
  if (b->n_balance == 0){
    a->n_balance = 1;
    b->n_balance = -1;
  } else {
    a->n_balance = 0;
    b->n_balance = 0;
  }
#if DEBUG >2
  fprintf(stderr,"avl_tree:\tRR a: <%s> bal %d\n", a->n_key, a->n_balance);
  fprintf(stderr,"avl_tree:\tRR b: <%s> bal %d\n", b->n_key, b->n_balance);
#endif

  return b;
}

struct avl_node *rotate_leftleft_avltree(struct avl_node *a)
{
  struct avl_node *f, *b;  

  f = a->n_parent;
  b = a->n_left;

  if (a == NULL || b == NULL)
    return NULL;

  a->n_left = b->n_right;
  if (b->n_right != NULL)
    b->n_right->n_parent = a;
  b->n_right  = a;
  a->n_parent = b;
  b->n_parent = f;

  if (f != NULL) {
    if (f->n_right == a)
      f->n_right = b;
    else
      f->n_left = b;
  }
#if DEBUG >2
  fprintf(stderr,"avl_tree:\tLL a: <%s> bal %d\n", a->n_key, a->n_balance);
  fprintf(stderr,"avl_tree:\tLL b: <%s> bal %d\n", b->n_key, b->n_balance);
#endif
  if (b->n_balance == 0){
    a->n_balance = -1;
    b->n_balance = 1;
  } else {
    a->n_balance = 0;
    b->n_balance = 0;
  }
#if DEBUG >2
  fprintf(stderr,"avl_tree:\tLL a: <%s> bal %d\n", a->n_key, a->n_balance);
  fprintf(stderr,"avl_tree:\tLL b: <%s> bal %d\n", b->n_key, b->n_balance);
#endif

  return b;
}

struct avl_node *rotate_leftright_avltree(struct avl_node *a)
{
  struct avl_node *f, *b, *c;  

  f = a->n_parent;
  b = a->n_left;
  c = b->n_right;

  if (a == NULL || b == NULL || c == NULL)
    return NULL;

  a->n_left  = c->n_right;
  if (c->n_right != NULL)
    c->n_right->n_parent = a; 
  
  b->n_right = c->n_left;
  if (c->n_left != NULL)
    c->n_left->n_parent = b;
  
  c->n_right = a;
  c->n_left  = b;
  
  b->n_parent = c;
  a->n_parent = c;
  c->n_parent = f;

  if (f != NULL){
    if (f->n_right == a)
      f->n_right = c;
    else
      f->n_left = c;
  }

#if DEBUG >2
  fprintf(stderr,"avl_tree:\tLR a: <%s> bal %d\n", a->n_key, a->n_balance);
  fprintf(stderr,"avl_tree:\tLR b: <%s> bal %d\n", b->n_key, b->n_balance);
  fprintf(stderr,"avl_tree:\tLR c: <%s> bal %d\n", c->n_key, c->n_balance);
#endif
  
  /*this is correct AFIK*/
  switch (c->n_balance){
    case 0:
      a->n_balance = 0;
      b->n_balance = 0;
      break;
    case -1:
      a->n_balance = 1;
      b->n_balance = 0;
      break;
    case 1:
      a->n_balance = 0;
      b->n_balance = -1;
      break;
  }
  c->n_balance = 0;

#if DEBUG >2
  fprintf(stderr,"avl_tree:\tLR a: <%s> bal %d\n", a->n_key, a->n_balance);
  fprintf(stderr,"avl_tree:\tLR b: <%s> bal %d\n", b->n_key, b->n_balance);
  fprintf(stderr,"avl_tree:\tLR c: <%s> bal %d\n", c->n_key, c->n_balance);
#endif

  return c;
}

struct avl_node *rotate_rightleft_avltree(struct avl_node *a)
{
  struct avl_node *f, *b, *c;  

  f = a->n_parent;
  b = a->n_right;
  c = b->n_left;

  if (a == NULL || b == NULL || c == NULL)
    return NULL;

  b->n_left  = c->n_right;
  if (c->n_right != NULL)
    c->n_right->n_parent = b;

  a->n_right = c->n_left;
  if (c->n_left != NULL)
    c->n_left->n_parent = a;

  c->n_right = b; 
  c->n_left  = a;
  
  b->n_parent = c;
  a->n_parent = c;
  c->n_parent = f;
  
  if (f != NULL) {
    if (f->n_right == a)
      f->n_right = c;
    else
      f->n_left = c;
  }

#if DEBUG >2
  fprintf(stderr,"avl_tree:\tRL a: <%s> bal %d\n", a->n_key, a->n_balance);
  fprintf(stderr,"avl_tree:\tRL b: <%s> bal %d\n", b->n_key, b->n_balance);
  fprintf(stderr,"avl_tree:\tRL c: <%s> bal %d\n", c->n_key, c->n_balance);
#endif
  
  switch (c->n_balance){
    case 0:
      a->n_balance = 0;
      b->n_balance = 0;
      break;
    case -1:
      a->n_balance = 0;
      b->n_balance = 1;
      break;
    case 1:
      a->n_balance = -1;
      b->n_balance = 0;
      break;
  }
  c->n_balance = 0;

#if DEBUG >2
  fprintf(stderr,"avl_tree:\tRL a: <%s> bal %d\n", a->n_key, a->n_balance);
  fprintf(stderr,"avl_tree:\tRL b: <%s> bal %d\n", b->n_key, b->n_balance);
  fprintf(stderr,"avl_tree:\tRL c: <%s> bal %d\n", c->n_key, c->n_balance);
#endif

  return c;
}

struct avl_node *rebalance_avltree(struct avl_node *n)
{
  struct avl_node *c;
  int rtype;

  if (n == NULL)
    return NULL;
#if 0
  c = (n->n_left == NULL) ? n->n_right : n->n_left;
#endif
  
  c = (n->n_balance == -2) ? n->n_left : n->n_right;
  
  rtype = ((n->n_balance == -2) ? AVL_LEFT : AVL_RIGHT ) & AVL_MASK;
  if (c->n_balance == 0){
    rtype = rtype | (rtype << 2);
  } else {
    rtype = rtype | ( ((c->n_balance == -1) ? (AVL_LEFT<<2) : (AVL_RIGHT<<2)) & AVL_MASK );
  }

#if DEBUG >1
  fprintf(stderr,"avl_tree:\trtype is 0x%X\n",rtype);
#endif

  switch (rtype){
    case AVL_LEFTRIGHT:
#if DEBUG > 1 
      fprintf(stderr,"avl_tree:\t LEFT RIGHT Rotation\n");
#endif
      c = rotate_leftright_avltree(n);
      break;
    case AVL_RIGHTLEFT:
#if DEBUG > 1
      fprintf(stderr,"avl_tree:\t RIGHT LEFT Rotation\n");
#endif
      c = rotate_rightleft_avltree(n);
      break;
    case AVL_LEFTLEFT:
#if DEBUG > 1
      fprintf(stderr,"avl_tree:\t LEFT LEFT Rotation\n");
#endif
      c = rotate_leftleft_avltree(n);
      break;
    case AVL_RIGHTRIGHT:
#if DEBUG > 1
      fprintf(stderr,"avl_tree:\t RIGHT RIGHT Rotation\n");
#endif
      c = rotate_rightright_avltree(n);
      break;
  }

#if DEBUG > 1
  fprintf(stderr,"avl_tree:\t post rot returning %s\n", c->n_key);
#endif
  return c;
}

int add_node_avltree(struct avl_tree *t, struct avl_node *n)
{
  struct avl_node *c;
  int cmp, run, flag;

  if (t == NULL)
    return -1;
  
  c = t->t_root;
  run = 1;
  flag = 0;
  
  if (c == NULL){
#if DEBUG > 3 
    fprintf(stderr,"avl_tree: root node is %s\n", n->n_key);
#endif
    t->t_root = n;
    return 0;
  }

  while (run){
    cmp = strcmp(c->n_key, n->n_key);
    if (cmp < 0){
      if (c->n_right == NULL){
        c->n_right = n;
        n->n_parent = c;
        run = 0;
#if DEBUG > 3
        fprintf(stderr,"avl_tree: add %s is right child of %s balance %d\n", n->n_key, c->n_key, c->n_balance);
#endif
        if ((c->n_balance + 1) == 0){
          flag = 1;
          c->n_balance++;
#if DEBUG > 3
          fprintf(stderr,"avl_tree:\t%s is now balanced\n", c->n_key);
#endif
        }
      }
      c = c->n_right;  
    } else if (cmp > 0) {
      if (c->n_left == NULL){
        c->n_left = n;
        n->n_parent = c;
        run = 0;
#if DEBUG > 3
        fprintf(stderr,"avl_tree: add %s is left child of %s balance %d\n", n->n_key, c->n_key, c->n_balance);
#endif  
        if ((c->n_balance - 1) == 0){
          flag = 1;
          c->n_balance--;
#if DEBUG > 3
          fprintf(stderr,"avl_tree:\t%s is now balanced\n", c->n_key);
#endif
        }
      }
      c = c->n_left;
    } else if (cmp == 0){
#ifdef DEBUG
      fprintf(stderr,"avl_tree: error node seems to match an existing node <%s>\n", c->n_key);
#endif
      run = 0;
      return -1;
    }
  }

  run = 1;
  while (run && !flag){
    
    if (c->n_parent == NULL){
#if DEBUG > 3
      fprintf(stderr,"avl_tree: found null parent %s ending\n", c->n_key);
#endif
      run = 0;
    } else {
      
      if (c->n_parent->n_left == c) {
        c->n_parent->n_balance--;
#if DEBUG > 3
        fprintf(stderr,"avl_tree: -- %s balance %d\n", c->n_parent->n_key, c->n_parent->n_balance);
#endif
      } else {
        c->n_parent->n_balance++;
#if DEBUG > 3
        fprintf(stderr,"avl_tree: ++ %s balance %d\n", c->n_parent->n_key, c->n_parent->n_balance);
#endif
      }
      
      c = c->n_parent;
      
      if (c->n_balance == 0){
#if DEBUG > 3
        fprintf(stderr,"avl_tree:\thave made %s balance 0 stop propergating cus subtree is balanced\n", c->n_key);
#endif
        run = 0;
      } else if (abs(c->n_balance) > 1){
        c = rebalance_avltree(c);
#if DEBUG > 3
        fprintf(stderr,"avl_tree:\tPOSTROT: %s (%p) p(%p) balance %d\n", c->n_key, c, c->n_parent, c->n_balance);
#endif
        run = 0; 
      }
    }
  } /*while*/

#if 1
  run = 1;
  while (run){
    if (c->n_parent == NULL)
      run = 0;
    else {
      c = c->n_parent;
#if DEBUG > 3
      fprintf(stderr,"avl_tree:\t%s balance is %d\n", c->n_key, c->n_balance);
#endif
    }
  }
#endif

  t->t_root = c;
#if DEBUG > 3
  fprintf(stderr,"avl_tree: new root node is %s\n", c->n_key);
#endif
#if DEBUG > 3 
  check_balances_avltree(t->t_root, 0);
#endif
  return 0;

}

/* WARNING: using a datastructure to hold a function pointer might be a bit excessive - *global could have been the function pointer too ... */

struct free_reducer{
  void (*reduced_free)(void *payload);
};
  
static void complex_to_reduced_free_avltree(void *global, char *key, void *datum)
{
  struct free_reducer *rs;

  if(global == NULL){
    abort();
  }
  
  rs = global;

  (*(rs->reduced_free))(datum);
}

void free_node_complex_avltree(struct avl_node *n, void *global, void (*complex_free)(void *global, char *key, void *payload))
{
  if(n == NULL){
    return;
  }

  if (n->n_parent != NULL){ 
    n->n_parent = NULL; 
  }

  if (n->n_left != NULL){ 
    n->n_left = NULL; 
  }

  if (n->n_right != NULL){ 
    n->n_right = NULL; 
  }

  n->n_balance = 0;

  if((n->n_data != NULL) && (complex_free != NULL)) { 
    (*complex_free)(global, n->n_key, n->n_data);
    n->n_data = NULL; 
  }

  if(n->n_key != NULL){ 
    free(n->n_key); 
    n->n_key = NULL; 
  }

  free(n);
}

void free_node_avltree(struct avl_node *n, void (*d_free)(void *))
{
  struct free_reducer reducer, *rs;

  if(d_free == NULL){
    free_node_complex_avltree(n, NULL, NULL);
    return;
  }

  rs = &reducer;
  rs->reduced_free = d_free;

  free_node_complex_avltree(n, rs, &complex_to_reduced_free_avltree);
}

int del_node_complex_avltree(struct avl_tree *t, struct avl_node *n, void *global, void (*complex_free)(void *global, char *key, void *payload))
{
  struct avl_node *c, *s;
  int run, flag;

  if (t == NULL)
    return -1;

  if (n == NULL)
    return -2;

  run = 1;
  flag = 0;
  if (n->n_right == NULL){

    c = n->n_left;

#if DEBUG > 1
    fprintf(stderr,"avl_tree: delete CASE 1 n has no right child replace with left child %p\n", c);
#endif
    if (n->n_parent){
      if (n->n_parent->n_balance == 0){
        flag = 1;
      }
      if (n->n_parent->n_left == n) {
        n->n_parent->n_left = c;
        n->n_parent->n_balance++;
#if DEBUG > 1
        fprintf(stderr,"avl_tree:\t++ balance %s %d\n", n->n_parent->n_key, n->n_parent->n_balance);   
#endif
      }
      else if (n->n_parent->n_right == n){
        n->n_parent->n_right = c;
        n->n_parent->n_balance--;
#if DEBUG > 1
        fprintf(stderr,"avl_tree:\t-- balance %s %d\n", n->n_parent->n_key, n->n_parent->n_balance);   
#endif
      }
    }
    if (c == NULL){
      c = n->n_parent;
      if (c == NULL)
        run = 0;
    } else if (c != NULL){
      c->n_parent = n->n_parent;
      if (c->n_parent != NULL)
        c = c->n_parent;
      else
        run = 0;
    }

    free_node_complex_avltree(n, global, complex_free);
  
  } else {
    
    c = n->n_right;
    
    if (c->n_left == NULL){
      
#if DEBUG > 1
      fprintf(stderr,"avl_tree: delete CASE 2 n's <%s> right child <%s> has no left replace with right child %s\n", n->n_key, c->n_key, c->n_key);
#endif
      c->n_parent = n->n_parent;
      if (n->n_parent){
       if (n->n_balance == 0){
#if DEBUG >1
          fprintf(stderr, "avl_tree:\tn balance is 0 set flag\n");
#endif
          flag = 1;
        }
        if (n->n_parent->n_left == n) {
          n->n_parent->n_left = c;
        }
        else if (n->n_parent->n_right == n){
          n->n_parent->n_right = c;
        }
      }
      c->n_left = n->n_left;
      if (n->n_left){
        n->n_left->n_parent  = c;
      }
      c->n_balance = n->n_balance;
      
      free_node_complex_avltree(n, global, complex_free);
      
      c->n_balance--;
#if DEBUG > 1
      fprintf(stderr,"avl_tree:\t-- balance %s %d\n", c->n_key, c->n_balance);   
#endif

    } else {
     
      for(;;) {
        s = c->n_left;
        if (s->n_left == NULL)
          break;
        c = s;
      }
#if DEBUG > 1
      fprintf(stderr,"avl_tree: delete CASE 3 replace with inorder successor %s %p\n", s->n_key, s);
#endif
      
      s->n_left = n->n_left;
      s->n_left->n_parent = s;

      c->n_left = s->n_right;
      if (c->n_left != NULL){
        c->n_left->n_parent = c;
      }
      
      s->n_right = n->n_right;
      if (s->n_right != NULL){
        s->n_right->n_parent = s;
      }

      s->n_balance = n->n_balance;
      
      s->n_parent = n->n_parent;
      if (n->n_parent){
        if (n->n_parent->n_left == n) {
          n->n_parent->n_left = s;
        }
        else if (n->n_parent->n_right == n){
          n->n_parent->n_right = s;
        }
      }

      free_node_complex_avltree(n, global, complex_free);
      
      if (c->n_balance == 0){
#if DEBUG > 1
        fprintf(stderr,"avl_tree:\tparent bal is 0 setting flag to stop\n");
#endif
        flag = 1;
      }

      c->n_balance++;
#if DEBUG > 1
      fprintf(stderr,"avl_tree:\t++ balance %s %d\n", c->n_key, c->n_balance);   
#endif

    }
  }

  while (run){
    
    if (flag){

      if (c->n_parent == NULL)
        run = 0;
      else
        c = c->n_parent;

    } else if (abs(c->n_balance) > 1){

#if DEBUG > 1
      fprintf(stderr,"avl_tree:\tneed to rebalance %s balance %d\n", c->n_key, c->n_balance);
#endif
      
      if (c->n_balance > 1 && c->n_right != NULL){
        if (c->n_right->n_balance == 0){
          flag = 1;
#if DEBUG > 1
          fprintf(stderr,"avl_tree:\tsetting flag to 0 cus right child of c has 0 bal\n");
#endif
        }
      }
      if (c->n_balance < -1 && c->n_left != NULL){
        if (c->n_left->n_balance == 0){
          flag = 1;
#if DEBUG > 1
          fprintf(stderr,"avl_tree:\tsetting flag to 0 cus right child of c has 0 bal\n");
#endif
        }
      }

      c = rebalance_avltree(c);
#if DEBUG > 1 
      fprintf(stderr,"avl_tree:\tPOSTROT: %s (%p) p(%p) balance %d\n", c->n_key, c, c->n_parent, c->n_balance);
#endif
      
    } else {

      if (c->n_parent == NULL){
        run = 0;
      } else {
        if (c->n_parent->n_balance == 0){
#if DEBUG > 1
          fprintf(stderr,"avl_tree:\tparent bal is 0 setting flag to stop\n");
#endif
          flag = 1;
        }
        if (c->n_parent->n_left == c){
          c->n_parent->n_balance++;
#if DEBUG > 1
          fprintf(stderr,"avl_tree:\t++ balance %s\n", c->n_parent->n_key);   
#endif
        } else if (c->n_parent->n_right == c){
          c->n_parent->n_balance--;
#if DEBUG > 1
          fprintf(stderr,"avl_tree:\t-- balance %s\n", c->n_parent->n_key);   
#endif
        }
        c = c->n_parent;
      }
    }
  } /*while*/

  t->t_root = c;

  return 0;
}

int del_node_avltree(struct avl_tree *t, struct avl_node *n, void (*d_free)(void *payload))
{
  struct free_reducer reducer, *rs;

  if(d_free == NULL){
    return del_node_complex_avltree(t, n, NULL, NULL);
  }

  rs = &reducer;
  rs->reduced_free = d_free;

  return del_node_complex_avltree(t, n, rs, &complex_to_reduced_free_avltree);
}

struct avl_node *find_name_node_avltree(struct avl_tree *t, char *key)
{
  struct avl_node *c;
  int run, cmp;
  
  if (t == NULL)
    return NULL;
  
  c = t->t_root;

  run = 1;
  while (run) {
    
    if (c == NULL){
      run = 0;
    } else {
      
      cmp = strcmp(key, c->n_key);

      if (cmp == 0){
#if DEBUG > 1
        fprintf(stderr,"avl_tree: FOUND %s (%p)\n",c->n_key, c);
#endif
        return c;
      } else if (cmp < 0) {
        c = c->n_left;
      } else if (cmp > 0) {
        c = c->n_right;
      }

    }

  } /*while*/

#if DEBUG > 1 
  fprintf(stderr,"avl_tree: NOT FOUND %s\n", key);
#endif

  return NULL;
}

void *find_data_avltree(struct avl_tree *t, char *key)
{
  struct avl_node *n;

  n = find_name_node_avltree(t, key);
  if (n == NULL)
    return NULL;

  return get_node_data_avltree(n);
}

int del_name_node_complex_avltree(struct avl_tree *t, char *key, void *global, void (*complex_free)(void *global, char *key, void *payload))
{
  struct avl_node *dn;
  
  if (t == NULL){
    return -1;
  }

  dn = find_name_node_avltree(t, key);

  if (dn == NULL){
    return -1;
  }

  return del_node_complex_avltree(t, dn, global, complex_free);
}

int del_name_node_avltree(struct avl_tree *t, char *key, void (*d_free)(void *))
{
  struct free_reducer reducer, *rs;

  if(d_free == NULL){
    return del_name_node_complex_avltree(t, key, NULL, NULL);
  }

  rs = &reducer;
  rs->reduced_free = d_free;

  return del_name_node_complex_avltree(t, key, rs, &complex_to_reduced_free_avltree);
}

void destroy_complex_avltree(struct avl_tree *t, void *global, void (*complex_free)(void *global, char *key, void *payload))
{
  struct avl_node *c, *dn;
  int run;
  
  if (t == NULL)
    return;

  c = t->t_root;
  
  run = 1;
  while (run){
    
    if (c == NULL){
#if DEBUG >2
      fprintf(stderr, "avl_tree: destroy c is NULL\n");
#endif
      run = 0;
    } else {
      
      if (c->n_left != NULL) {
#if DEBUG >2
        fprintf(stderr, "avl_tree: destroy c going left\n");
#endif
        c = c->n_left;
      } else if (c->n_right != NULL) {
#if DEBUG >2
        fprintf(stderr, "avl_tree: destroy c going right\n");
#endif
        c = c->n_right;
      } else {
        
#if DEBUG >1
        fprintf(stderr,"avl_tree: del %s (%p) ", c->n_key, c->n_data);
#endif
        dn = c;

        c = c->n_parent;
        if (c != NULL) { 
          if (c->n_left == dn)
            c->n_left = NULL;
          else if (c->n_right == dn)
            c->n_right = NULL;
        } 

        if (dn->n_data && complex_free) { 
          (*complex_free)(global, dn->n_key, dn->n_data);
          dn->n_data = NULL; 
        }

        if (dn->n_key) { 
          free(dn->n_key); 
          dn->n_key = NULL; 
        }

        dn->n_parent = NULL;

        free(dn);

#if DEBUG >1
        fprintf(stderr,"avl_tree: done\n");
#endif
      
      }
    }
  }

  if (t != NULL)
    free(t);
}

void destroy_avltree(struct avl_tree *t, void (*d_free)(void *))
{
  struct free_reducer reducer, *rs;

  if(d_free == NULL){
    destroy_complex_avltree(t, NULL, NULL);
    return;
  }

  rs = &reducer;
  rs->reduced_free = d_free;

  destroy_complex_avltree(t, rs, &complex_to_reduced_free_avltree);
}

/*********************************************************************************************************/

char *gen_id_avltree(char *prefix)
{
  struct timeval now;
  char *id;
  int len;
  
  id  = NULL;
  len = 0;

  gettimeofday(&now, NULL);

  while (id == NULL){
    if (id == NULL && len > 0){
      id = malloc(sizeof(char)*len);
#if 0
      def DEBUG
      fprintf(stderr, "gen_id_avltree: done malloc %p for len: %d\n", id, len);
#endif
    }
    len = snprintf(id, len,"%s.%lu.%06lu", prefix, now.tv_sec, now.tv_usec);
#if 0
    def DEBUG
    fprintf(stderr, "gen_id_avltree: len: %d\n", len);
#endif
  }

#if 0
  def DEBUG
  fprintf(stderr, "gen_id_avltree: %s\n", id);
#endif
  
  return id;
}

struct avl_node *store_exposed_node_avltree(struct avl_tree *t, char *key, void *data)
{
  struct avl_node *n;

  n = create_node_avltree(key, data);
  if(n == NULL){
    return NULL;
  }

  if(add_node_avltree(t, n) < 0){
    free_node_complex_avltree(n, NULL, NULL);
    return NULL;
  }

  return n;
}

int store_named_node_avltree(struct avl_tree *t, char *key, void *data)
{
  if(store_exposed_node_avltree(t, key, data) == NULL){
    return -1;
  }
  
  return 0;
}

#ifdef UNIT_TEST_AVL 

int add_file_words_to_avltree(struct avl_tree *t, char *buffer, int bsize)
{
  int i,j;
  char c;
  char *temp;
  
  if (t == NULL || buffer == NULL)
    return -1;

  temp = NULL;
  j = 0;

  for (i=0; i < bsize; i++){
    c = buffer[i];

    switch(c){
      case '\n':
        temp = strndup(buffer + j, i-j);
        add_node_avltree(t, create_node_avltree(temp, NULL));
        j = i+1;
        break;
    }

    if (temp){ 
      free(temp);
      temp = NULL;
    }
  }

  return 0;
}

int main(int argc, char *argv[])
{
  struct avl_tree *tree;
#if 0 
  int fd, fsize;
  struct stat file_stats;
  char *buffer;
#endif

#if 1 
  struct avl_node *a, *b, *c, *d, *e, *f, *g, *h, *i, *j, *k, *l, *m, *n, *o, *p, *q;
#endif

#if 0
  fd = open("/usr/share/dict/words", O_RDONLY);
  if (fd < 0){
#if DEBUG >0
    fprintf(stderr,"avl_tree: cannot open file\n");
#endif
    return 0;
  }
  
  if (fstat(fd, &file_stats) < 0){
#if DEBUG >0
    fprintf(stderr,"avl_tree: cannot stat file\n");
#endif
    close(fd);
    return 0;
  }
  
  fsize = file_stats.st_size;

  buffer = mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);
  
  if (buffer == MAP_FAILED){
#if DEBUG >0
    fprintf(stderr,"avl_tree: cannot mmap file\n");
#endif
    close(fd);
    return 0;
  }

  tree = create_avltree();
  
  if (add_file_words_to_avltree(tree, buffer, fsize) < 0){
#if DEBUG >0
    fprintf(stderr,"avl_tree: cannot populate file into tree\n");
#endif
    munmap(buffer, fsize);
    close(fd);
    destroy_avltree(tree);
    tree = NULL;
    return 0;
  }
  
 // print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);

#endif 

#if 1 
  tree = create_avltree();
  
  a = create_node_avltree("adam", NULL);
  b = create_node_avltree("ben", NULL);
  c = create_node_avltree("charlie", NULL);
  d = create_node_avltree("doug", NULL);
  e = create_node_avltree("eric", NULL);
  f = create_node_avltree("fred", NULL);
  g = create_node_avltree("gareth", NULL);
  h = create_node_avltree("dennis", NULL);
  i = create_node_avltree("thomas", NULL);
  j = create_node_avltree("mark", NULL);
  k = create_node_avltree("megan", NULL);
  l = create_node_avltree("zack", NULL);
  m = create_node_avltree("zack1", NULL);
  n = create_node_avltree("zack2", NULL);
  o = create_node_avltree("test", NULL);
  p = create_node_avltree("amy", NULL);
  q = create_node_avltree("alex", NULL);
  
  if (add_node_avltree(tree, h) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, i) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, j) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, c) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, a) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, b) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, d) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, e) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, f) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, g) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, k) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, l) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, m) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, n) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, o) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, p) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, q) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, create_node_avltree("andrea", NULL)) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, create_node_avltree("nicole", NULL)) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
 /* 
  if (add_node_avltree(tree, create_node_avltree("fred", NULL)) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, create_node_avltree("adam", NULL)) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, create_node_avltree("eric", NULL)) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, create_node_avltree("alan", NULL)) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, create_node_avltree("abe", NULL)) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, create_node_avltree("ben", NULL)) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, create_node_avltree("basil", NULL)) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, create_node_avltree("barry", NULL)) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  
  if (del_name_node_avltree(tree, "abe") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  
  if (del_name_node_avltree(tree, "fred") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  
  if (del_name_node_avltree(tree, "eric") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  
  if (del_name_node_avltree(tree, "ben") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  */

  print_avltree(NULL, tree->t_root, 0, NULL);
  //check_balances_avltree(tree->t_root, 0);
  
  
  while (0){
    if (del_node_avltree(tree, tree->t_root, NULL) < 0)
      break;
    else {
      print_avltree(NULL, tree->t_root, 0, NULL);
      check_balances_avltree(tree->t_root, 0);
    }
  }

#if 1 
  print_inorder_avltree(NULL, tree->t_root, NULL, 0);
  
  while ((a = walk_inorder_avltree(tree->t_root)) != NULL){
    
    if (a != NULL){
#ifdef DEBUG
      fprintf(stderr, "walk: <%s>\n", a->n_key);
#endif
    }

  }
#ifdef DEBUG
 fprintf(stderr, "walk round 2\n");
#endif
  
  while ((a = walk_inorder_avltree(tree->t_root)) != NULL){
    
    if (a != NULL){
#ifdef DEBUG
      fprintf(stderr, "walk: <%s>\n", a->n_key);
#endif
    }

  }


#endif

#if 0 

  if (del_name_node_avltree(tree, "test") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "charlie") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "ben") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "adam") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "mark") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "doug") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "nicole") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "eric") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "fred") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "andrea") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "thomas") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
  if (del_name_node_avltree(tree, "dennis") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
  print_avltree(tree->t_root, 0);
  check_balances_avltree(tree->t_root, 0);
  
#endif

#endif
  
  destroy_avltree(tree, NULL);
  tree = NULL;
  
  return 0;
}

#endif

