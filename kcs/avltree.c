/***
  An AVLTree Implementation
  with parent pointers 
***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void print_avltree(struct avl_node *n, int depth)
{
#define SPACER "  "
  int i;

  if (n == NULL){
#ifdef DEBUG
    fprintf(stderr,"%p\n", n);
#endif
    return;
  }

#ifdef DEBUG
  fprintf(stderr,"in %s (%p) bal %d p(%p)\n", n->n_key, n, n->n_balance, n->n_parent);
#endif

#ifdef DEBUG
  for (i=0; i<depth; i++)
    fprintf(stderr,SPACER);
  fprintf(stderr," L ");
#endif
  print_avltree(n->n_left, depth+1);

#ifdef DEBUG
  for (i=0; i<depth; i++)
    fprintf(stderr, SPACER);
  fprintf(stderr," R ");
#endif
  print_avltree(n->n_right, depth+1);
#undef SPACER   
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
  
  a->n_balance -= 2;
  b->n_balance--;

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
  
  a->n_balance += 2;
  b->n_balance++;

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
  
  a->n_balance += 2;
  b->n_balance--;

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
  
  a->n_balance -= 2;
  b->n_balance++;

  return c;
}

int add_node_avltree(struct avl_tree *t, struct avl_node *n)
{
  struct avl_node *c;
  int cmp, run;
  char rtype;
  
  c = t->t_root;
  run = 1;
  
  if (c == NULL){
#ifdef DEBUG
    fprintf(stderr,"avl_tree: root node is %s\n", n->n_key);
#endif
    t->t_root = n;
    print_avltree(t->t_root, 0);
    return 0;
  }

  while (run){
    
    cmp = strcmp(c->n_key, n->n_key);
    
    if (cmp < 0){

      if (c->n_right == NULL){
#ifdef DEBUG
        fprintf(stderr,"avl_tree: add %s is right child of %s\n", n->n_key, c->n_key);
#endif
        c->n_right = n;
        n->n_parent = c;
        run = 0;
      }
      
      c = c->n_right;  

    } else if (cmp > 0) {
      
      if (c->n_left == NULL){
#ifdef DEBUG
        fprintf(stderr,"avl_tree: add %s is left child of %s\n", n->n_key, c->n_key);
#endif
        c->n_left = n;
        n->n_parent = c;
        run = 0;
      }
      
      c = c->n_left;

    } else if (cmp == 0){

#ifdef DEBUG
      fprintf(stderr,"avl_tree: error none seems to match an existing node\n");
#endif
      run = 0;
      return -1;
    }
  }

  run = 1;
  rtype = 0;
  while (run){
    
    if (c->n_parent == NULL){
#ifdef DEBUG
      fprintf(stderr,"avl_tree: found null parent %s ending\n", c->n_key);
#endif
      run = 0;
    } else {

      if (c->n_parent->n_left == c) {
#ifdef DEBUG
        fprintf(stderr,"avl_tree: decrementing %s balance\n",c->n_parent->n_key);
#endif
        c->n_parent->n_balance--;
        rtype = rtype | (AVL_LEFT << abs(c->n_balance)*2);
      } else {
#ifdef DEBUG
        fprintf(stderr,"avl_tree: incrementing %s balance\n",c->n_parent->n_key);
#endif
        c->n_parent->n_balance++;
        rtype = rtype | (AVL_RIGHT << abs(c->n_balance)*2);
      }

      c = c->n_parent;

#ifdef DEBUG
      fprintf(stderr,"avl_tree:\t%s balance is %d rtype is 0x%X\n", c->n_key, c->n_balance, rtype);
#endif

      if (abs(c->n_balance) > 1){

        switch (rtype){

          case AVL_LEFTRIGHT:
#ifdef DEBUG
            fprintf(stderr,"avl_tree:\t LEFT RIGHT Rotation\n");
#endif
            c = rotate_leftright_avltree(c);
            break;
          
          case AVL_RIGHTLEFT:
#ifdef DEBUG
            fprintf(stderr,"avl_tree:\t RIGHT LEFT Rotation\n");
#endif
            c = rotate_rightleft_avltree(c);
            break;
          
          case AVL_LEFTLEFT:
#ifdef DEBUG
            fprintf(stderr,"avl_tree:\t LEFT LEFT Rotation\n");
#endif
            c = rotate_leftleft_avltree(c);
            break;
          
          case AVL_RIGHTRIGHT:
#ifdef DEBUG
            fprintf(stderr,"avl_tree:\t RIGHT RIGHT Rotation\n");
#endif
            c = rotate_rightright_avltree(c);
            break;

        }
        rtype = 0;
        if (c->n_parent != NULL)
          c = c->n_parent;
      }
    }
  } /*while*/

  t->t_root = c;
#ifdef DEBUG
  fprintf(stderr,"avl_tree: new root node is %s\n", c->n_key);
#endif
  
  print_avltree(t->t_root, 0);
  return 0;
}

#if 0
int del_node_avltree(struct avl_tree *t, struct avl_node *n)
{

}

struct avl_node *find_name_node_avltree(struct avl_tree *t, char *key)
{

}

int del_name_node_avltree(struct avl_tree *t, char *key)
{

}

void destroy_tree(struct avl_tree *t)
{
  struct avl_node *r;

  r = t->t_root;
#if 0
  if (r == NULL){
    return;
  }

  if (n->n_left)
    destroy_tree(n->n_left);
  
  if (n->n_right)
    destroy_tree(n->n_right);

  if (n->n_key)  { free(n->n_key); n->n_key = NULL; }
  if (n->n_data) { free(n->n_data); n->n_data = NULL; }
  if (n)         { free(n); n = NULL; }
#endif
}
#endif


#ifndef LIBRARY

int main(int argc, char *argv[])
{
  struct avl_tree *tree;
  struct avl_node *a, *b, *c, *d, *e, *f, *g, *h, *i, *j, *k;

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
  if (add_node_avltree(tree, h) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, i) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, j) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");
  if (add_node_avltree(tree, k) < 0)
    fprintf(stderr,"avl_tree: couldn't add\n");

  return 0;
}

#endif

