/***
  An AVLTree Implementation
  with parent pointers 
***/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

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
#if DEBUG > 0
    fprintf(stderr,"%p\n", n);
#endif
    return;
  }

#if DEBUG > 0
  fprintf(stderr,"in %s (%p) bal %d p(%p)\n", n->n_key, n, n->n_balance, n->n_parent);
#endif

#if DEBUG > 0
  for (i=0; i<depth; i++)
    fprintf(stderr,SPACER);
  fprintf(stderr," L ");
#endif
  print_avltree(n->n_left, depth+1);

#if DEBUG > 0
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

  if (t == NULL)
    return -1;
  
  c = t->t_root;
  run = 1;
  
  if (c == NULL){
#if DEBUG > 3 
    fprintf(stderr,"avl_tree: root node is %s\n", n->n_key);
#endif
    t->t_root = n;
#if DEBUG > 3
    print_avltree(t->t_root, 0);
#endif
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
      }
      c->n_balance++;
      c = c->n_right;  
    } else if (cmp > 0) {
      if (c->n_left == NULL){
        c->n_left = n;
        n->n_parent = c;
        run = 0;
#if DEBUG > 3
        fprintf(stderr,"avl_tree: add %s is left child of %s balance %d\n", n->n_key, c->n_key, c->n_balance);
#endif
      }
      c->n_balance--;
      c = c->n_left;
    } else if (cmp == 0){
#if DEBUG > 3
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
#if DEBUG > 3
      fprintf(stderr,"avl_tree: found null parent %s ending\n", c->n_key);
#endif
      run = 0;
    } else {
      
      if (c->n_parent->n_left == c) {
        rtype = ((rtype << (abs(c->n_balance)*2)) | AVL_LEFT) & AVL_MASK;
      } else {
        rtype = ((rtype << (abs(c->n_balance)*2)) | AVL_RIGHT) & AVL_MASK;
      }

      c = c->n_parent;
#if DEBUG > 3
      fprintf(stderr,"avl_tree:\t%s balance is %d rtype is 0x%X\n", c->n_key, c->n_balance, rtype);
#endif
  
      if (abs(c->n_balance) > 1){
#if DEBUG > 3
        fprintf(stderr,"avl_tree:\tneed to do a rotation rtype is 0x%X\n", rtype);
#endif
        switch (rtype){
          case AVL_LEFTRIGHT:
#if DEBUG > 3
            fprintf(stderr,"avl_tree:\t LEFT RIGHT Rotation\n");
#endif
            c = rotate_leftright_avltree(c);
            break;
          case AVL_RIGHTLEFT:
#if DEBUG > 3
            fprintf(stderr,"avl_tree:\t RIGHT LEFT Rotation\n");
#endif
            c = rotate_rightleft_avltree(c);
            break;
          case AVL_LEFTLEFT:
#if DEBUG > 3
            fprintf(stderr,"avl_tree:\t LEFT LEFT Rotation\n");
#endif
            c = rotate_leftleft_avltree(c);
            break;
          case AVL_RIGHTRIGHT:
#if DEBUG > 3
            fprintf(stderr,"avl_tree:\t RIGHT RIGHT Rotation\n");
#endif
            c = rotate_rightright_avltree(c);
            break;
        }
#if DEBUG > 3
        fprintf(stderr,"avl_tree:\tPOSTROT: %s (%p) p(%p) balance %d\n", c->n_key, c, c->n_parent, c->n_balance);
#endif
        rtype = 0;
        run = 0; 
      }
    }
  } /*while*/

  run = 1;
  while (run){
    if (c->n_parent == NULL)
      run = 0;
    else {
      if (c->n_parent->n_left == c) {
#if DEBUG > 3
        fprintf(stderr,"avl_tree: ++ %s balance\n", c->n_parent->n_key);
#endif
        c->n_parent->n_balance++;
      } else {
#if DEBUG > 3
        fprintf(stderr,"avl_tree: -- %s balance\n", c->n_parent->n_key);
#endif
        c->n_parent->n_balance--;
      }
      c = c->n_parent;
#if DEBUG > 3
      fprintf(stderr,"avl_tree:\t%s balance is %d\n", c->n_key, c->n_balance);
#endif
    }
  }

  t->t_root = c;
#if DEBUG > 3
  fprintf(stderr,"avl_tree: new root node is %s\n", c->n_key);
#endif
#if DEBUG > 3 
  print_avltree(t->t_root, 0);
#endif
  return 0;
}

struct avl_node *rebalance_avltree(struct avl_node *n)
{
  struct avl_node *c;
  int rtype;

  if (n == NULL)
    return NULL;

  c = (n->n_left == NULL) ? n->n_right : n->n_left;

  rtype = ((n->n_balance == -2) ? (AVL_LEFT << 2) : (AVL_RIGHT << 2)) & AVL_MASK;
  rtype = rtype | ( ((c->n_balance == -1) ? AVL_LEFT : AVL_RIGHT) & AVL_MASK );

#if DEBUG >1
  fprintf(stderr,"rtype is 0x%X\n",rtype);
#endif

  return n;
}

struct avl_node *swap_and_delete_node_avltree(struct avl_node *n, struct avl_node *c)
{
  struct avl_node *np, *cp;
  int npdir, cpdir;

  if (n == NULL || c == NULL)
    return NULL;

  npdir = 0;
  cpdir = 0;
  
  np = n->n_parent;
  cp = c->n_parent;
  
  if (cp == NULL)
    return NULL;

#if DEBUG >1
  fprintf(stderr,"avl_tree:\n\tnp: <%p> n: %s <%p>\n\tcp: <%p> c: %s <%p>\n", np, n->n_key, n, cp, c->n_key, c); 
#endif
  
  /*unlink n from parent and assign c*/
  if (np != NULL){
    if (np->n_left == n){
      np->n_left = c;
    } else if (np->n_right == n) {
      np->n_right = c;
    }
  }
  c->n_parent = np;
  
  if (cp != n){
    if (cp->n_left == c){
      cp->n_left = NULL;
      cp->n_balance++;
      cpdir = AVL_LEFT;
#if DEBUG >1
      fprintf(stderr,"avl_tree:\t++ balance in %s\n", cp->n_key);
#endif
    } else if (cp->n_right == c){
      cp->n_right = NULL;
      cp->n_balance--;
      cpdir = AVL_RIGHT;
#if DEBUG >1
      fprintf(stderr,"avl_tree:\t-- balance in %s\n", cp->n_key);
#endif
    }
  } 
#if 0 
  else if (cp == n) {
    cpdir = 0;
#if DEBUG >1
    fprintf(stderr,"avl_tree: EEEEKKKK IDK WTF\n");
#endif
  }
#endif
  
  /*if node to delete is the leaf node ie swap with itself*/
  if (n == c) {
    if (n->n_key != NULL) { free(n->n_key); n->n_key = NULL; }
    if (n->n_data != NULL) { free(n->n_data); n->n_data = NULL; }
    if (n != NULL) { free(n); n = NULL; }
    return cp;
  }
  
  if (c->n_left != NULL && c->n_right == NULL){
#if DEBUG >1
    fprintf(stderr,"avl_tree: %s has left child %s store in %s ", c->n_key, c->n_left->n_key, cp->n_key);
#endif
    switch (cpdir){
      case AVL_LEFT:
        cp->n_left = c->n_left;
        cp->n_left->n_parent = cp;
#if DEBUG >1
        fprintf(stderr,"left\n");
#endif
        break;
      case AVL_RIGHT:
        cp->n_right = c->n_left;
        cp->n_right->n_parent = cp;
#if DEBUG >1
        fprintf(stderr,"right\n");
#endif
        break;
    }
  } else if (c->n_left == NULL && c->n_right != NULL){
#if DEBUG >1
    fprintf(stderr,"avl_tree: %s has right child %s store in %s ", c->n_key, c->n_right->n_key, cp->n_key);
#endif
    switch (cpdir){
      case AVL_LEFT:
        cp->n_left = c->n_right;
        cp->n_left->n_parent = cp;
#if DEBUG >1
        fprintf(stderr,"left\n");
#endif
        break;
      case AVL_RIGHT:
        cp->n_right = c->n_right;
        cp->n_right->n_parent = cp;
#if DEBUG >1
        fprintf(stderr,"right\n");
#endif
        break;
    }
  }

  if (cp == n){
    if (cp->n_left == c){
      c->n_right = n->n_right;
      //c->n_balance++;
      if (c->n_right != NULL)
        c->n_right->n_parent = c;
    } else if (cp->n_right == c) {
      c->n_left = n->n_left;
      //c->n_balance--;
      if (c->n_left != NULL)
        c->n_left->n_parent = c;
    }
    cp = c;
#if DEBUG >1
    fprintf(stderr," case where cp == n\n");
#endif
  } else {
    c->n_left = n->n_left;
    c->n_right = n->n_right;  
    c->n_left->n_parent = c;
    c->n_right->n_parent = c;
    c->n_balance = n->n_balance;
  }
  
  if (n->n_key != NULL) { free(n->n_key); n->n_key = NULL; }
  if (n->n_data != NULL) { free(n->n_data); n->n_data = NULL; }
  if (n != NULL) { free(n); n = NULL; }
  
  return cp;
}
  
int del_node_avltree(struct avl_tree *t, struct avl_node *n)
{
  struct avl_node *c;
  int run, dir;

  if (t == NULL)
    return -1;

  if (n == NULL)
    return -2;

  run = 1;
  
  if (n->n_balance > 0) {
    c = n->n_right;
    dir = AVL_LEFT;
  } else if(n->n_balance < 0) {
    c = n->n_left;
    dir = AVL_RIGHT;
  } else {
    c = n->n_left;
    dir = AVL_RIGHT;
    if (c == NULL){
      run = 0;
      c = n;
    }
  }

  while (run){
    switch (dir){
      case AVL_RIGHT:
        if (c->n_right == NULL)
          run = 0;
        else
          c = c->n_right;
        break;
      case AVL_LEFT:
        if (c->n_left == NULL)
          run = 0;
        else 
          c = c->n_left;
        break;
    }
  }

#if DEBUG > 1
  fprintf(stderr,"avl_tree: swaping with %s (%p)\n", c->n_key, c);
#endif

  c = swap_and_delete_node_avltree(n, c);

  run = 1;
  while (run) {
    
    if (abs(c->n_balance) > 1) {
#if DEBUG > 1
      fprintf(stderr,"avl_tree: need to rebalance\n");
#endif
      c = rebalance_avltree(c);
    }

    if (c->n_parent != NULL) {
      if (c->n_parent->n_left == c){
        c->n_parent->n_balance++;
#if DEBUG > 1
        fprintf(stderr,"avl_tree:\t++ balance %s\n", c->n_parent->n_key);   
#endif
      } else if (c->n_parent->n_right == c) {
        c->n_parent->n_balance--;
#if DEBUG > 1
        fprintf(stderr,"avl_tree:\t-- balance %s\n", c->n_parent->n_key);   
#endif
      }
      c = c->n_parent;
#if DEBUG > 1
      fprintf(stderr,"avl_tree: delete in %s (%p) balance %d\n", c->n_key, c, c->n_balance);
#endif
    } else {
      run = 0;
    }

  }

  t->t_root = c;
  
  print_avltree(t->t_root, 0);

  return 0;
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
#if DEBUG > 0
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

#if DEBUG > 0 
  fprintf(stderr,"avl_tree: NOT FOUND %s\n", key);
#endif

  return NULL;
}

int del_name_node_avltree(struct avl_tree *t, char *key)
{
  struct avl_node *dn;
  
  if (t == NULL)
    return -1;

  dn = find_name_node_avltree(t, key);

  if (dn == NULL)
    return -1;

  return del_node_avltree(t, dn);
}

void destroy_avltree(struct avl_tree *t)
{
  struct avl_node *c, *dn;
  int run;
  
  if (t == NULL)
    return;

  c = t->t_root;
  
  run = 1;
  while (run){
    
    if (c == NULL){
      run = 0;
    } else {
      
      if (c->n_left != NULL) {
        c = c->n_left;
      } else if (c->n_right != NULL) {
        c = c->n_right;
      } else {
        
#if DEBUG > 2 
        fprintf(stderr,"avl_tree: del %s (%p) ", c->n_key, c);
#endif
        dn = c;

        c = c->n_parent;
        if (c != NULL) { 
          if (c->n_left == dn)
            c->n_left = NULL;
          else if (c->n_right == dn)
            c->n_right = NULL;
        } 

        if (dn->n_key) { free(dn->n_key); dn->n_key = NULL; }
        if (dn->n_data) { free(dn->n_data); dn->n_data = NULL; }
        dn->n_parent = NULL;

        free(dn);

#if DEBUG > 2
        fprintf(stderr,"done\n");
#endif
      
      }
    }
  }

  if (t != NULL)
    free(t);
  
}


#ifndef LIBRARY

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
#if 0
        DEBUG >0
        fprintf(stderr,"<%s> ",temp);
#endif
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
  int fd, fsize;
  struct stat file_stats;
  char *buffer;
  
#if 0 
  struct avl_node *a, *b, *c, *d, *e, *f, *g, *h, *i, *j, *k, *l, *m, *n, *o, *p, *q;
#endif

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

#if 0
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

  print_avltree(tree->t_root, 0);
 
  if (del_name_node_avltree(tree, "ben") < 0)
    fprintf(stderr,"avl_tree: couldn't delete\n");
#endif

  print_avltree(tree->t_root, 0);
  destroy_avltree(tree);
  
  tree = NULL;
  
  return 0;
}

#endif

