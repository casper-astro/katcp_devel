#ifndef _AVTREE_H_
#define _AVTREE_H_

#include "katcp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AVL_LEFT        0x2 
#define AVL_RIGHT       0x1
#define AVL_MASK        0xF

#define AVL_LEFTRIGHT   0x6
#define AVL_RIGHTLEFT   0x9
#define AVL_LEFTLEFT    0xA
#define AVL_RIGHTRIGHT  0X5

#define WALK_INIT       0
#define WALK_PUSH       1
#define WALK_POP        2

struct avl_tree {
  struct avl_node *t_root;
};

struct avl_node {
  struct avl_node *n_parent;
  struct avl_node *n_left;
  struct avl_node *n_right;
  int n_balance;
  
  char *n_key;
  
  void *n_data;
};

/*this can be used as a set of nodes*/
struct avl_node_list {
  struct avl_node **l_n;
  int l_count;
};

struct avl_tree *create_avltree();
struct avl_node *create_node_avltree(char *key, void *data);
int add_node_avltree(struct avl_tree *t, struct avl_node *n);
int del_node_avltree(struct avl_tree *t, struct avl_node *n, void (*d_free)(void *));
struct avl_node *find_name_node_avltree(struct avl_tree *t, char *key);
void *find_data_avltree(struct avl_tree *t, char *key);
int del_name_node_avltree(struct avl_tree *t, char *key, void (*d_free)(void *));
void free_node_avltree(struct avl_node *n, void (*d_free)(void *));
void destroy_avltree(struct avl_tree *t, void (*d_free)(void *));

struct avl_node *walk_inorder_avltree(struct avl_node *n);
void *walk_data_inorder_avltree(struct avl_node *n);

/*node api*/

char *get_node_name_avltree(struct avl_node *n);
void *get_node_data_avltree(struct avl_node *n);
int update_node_data_avltree(struct avl_node *n, void *data);

int store_named_node_avltree(struct avl_tree *t, char *key, void *data);

/*testing api*/
void print_avltree(struct katcp_dispatch *d, struct avl_node *n, int depth, void (*fn_print)(struct katcp_dispatch *, char *key, void *));
void print_inorder_avltree(struct katcp_dispatch *d, struct avl_node *n, void (*fn_print)(struct katcp_dispatch *, char *key, void *), int flags);

int check_balances_avltree(struct avl_node *n, int depth);

/*extra*/

char *gen_id_avltree(char *prefix);

#ifdef __cplusplus
}
#endif

#endif
