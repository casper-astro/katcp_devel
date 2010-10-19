#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include <errno.h>

#include <sys/types.h>

#include <katcp.h>
#include <katpriv.h>

#include "kcs.h"

int roachpool_greeting(struct katcp_dispatch *d){
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"add [roach hostname] [roach ip] [type]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"del [roach hostname] NOT IMPLEMENTED");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, "list");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"start [roach hostname]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"stop [roach hostname]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"start-pool");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"stop-pool");
  return KATCP_RESULT_OK;
}

void add_roach_to_node(struct kcs_node *cn, struct kcs_roach *nr){
#ifdef DEBUG
  fprintf(stderr,"Added roach %p to node %s\n",nr,cn->desc);
#endif
  cn->childroaches = realloc(cn->childroaches,sizeof(struct kcs_roach*)*(++cn->childcount));
  cn->childroaches[cn->childcount-1] = nr;
}

struct kcs_node * search_tree(struct kcs_node *n, char *s){
  int i;
  struct kcs_node *c;

  if (!strcmp(n->desc,s)){
    for (i=0;i<n->childcount;i++){
      c = n->childnodes[i];
      return search_tree(c,s);
    }
  } else
    return n;

  return NULL;
}

void place_node_in_tree(struct kcs_node **cn, struct kcs_roach *nr){
  struct kcs_node *node;
  node = *cn;
  if (node == NULL){ 
#ifdef DEBUG
    fprintf(stderr,"Current node is NULL\n");
#endif
    node = malloc(sizeof(struct kcs_node));
    node->parent       = NULL;
    node->childnodes   = NULL;
    node->childroaches = NULL;
    node->childcount   = 0;
    node->desc = malloc(sizeof(char)*strlen(nr->type));
    node->desc         = strcpy(node->desc,nr->type);
    add_roach_to_node(node,nr);
    *cn = node;
  } else {
    node = search_tree(node,nr->type);

    add_roach_to_node(node,nr);
    *cn = node;
  }

}

int roachpool_add(struct katcp_dispatch *d){

  struct kcs_basic *kb;
  //struct kcs_node *curnode;
  struct kcs_roach *kr;

  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);
  
  kr = malloc(sizeof(struct kcs_roach));

  if (kr == NULL)
    return KATCP_RESULT_FAIL;
  
  kr->parent   = NULL;
  kr->hostname = NULL;
  kr->ip       = NULL;
  kr->mac      = NULL;
  kr->type     = NULL;

  kr->hostname = arg_copy_string_katcp(d,2);
  kr->ip       = arg_copy_string_katcp(d,3);
  kr->type     = arg_copy_string_katcp(d,4);

#ifdef DEBUG
  fprintf(stderr,"NEW ROACH: %s %s %s\n",kr->hostname,kr->ip,kr->type);
#endif

  place_node_in_tree(&(kb->b_pool_head),kr);

 /* 
  if (kb->b_rpool == NULL){
    krp = malloc(sizeof(struct kcs_roach_pool*));
    krp = 
    krp->krr      = NULL;
    krp->krrcount = 0;
    kb->b_rpool   = krp; 
  } else {
    krp = kb->b_rpool;
  }
 
  kr = malloc(sizeof(struct kcs_roach));

  if (kr == NULL)
    return KATCP_RESULT_FAIL;

  kr->hostname = NULL;
  kr->ip       = NULL;
  kr->type     = NULL;
  kr->mac      = NULL;

  kr->hostname = arg_copy_string_katcp(d,2);
  kr->ip       = arg_copy_string_katcp(d,3);
  kr->type     = arg_copy_string_katcp(d,4);

  krp->krr = realloc(krp->krr,sizeof(struct kcs_roach*)*(++krp->krrcount));
  
  if (krp->krr == NULL)
    return KATCP_RESULT_FAIL;

  krp->krr[krp->krrcount-1] = kr;
*/
  return KATCP_RESULT_OK;
}

void traverse_tree(struct katcp_dispatch *d, struct kcs_node *n){
  
  int i;
  struct kcs_roach *r;
  struct kcs_node *c;
  
  if (n->childcount > 0){
    if (n->childnodes == NULL){
      for (i=0; i<n->childcount; i++){
        r = n->childroaches[i];
        prepend_inform_katcp(d);
        append_string_katcp(d,KATCP_FLAG_STRING, r->type);
        append_string_katcp(d,KATCP_FLAG_STRING, r->hostname);
        append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, r->ip);
      }
    } else if (n->childroaches == NULL){
#ifdef DEBUG
      fprintf(stderr,"Traversing children of node: %s\n",n->desc);
#endif
      prepend_inform_katcp(d);
      append_string_katcp(d,KATCP_FLAG_STRING, "Traversing node:");
      append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST, n->desc);
      for (i=0; i<n->childcount; i++) {
        c = n->childnodes[i];
        traverse_tree(d,c);
      }
    }
  }
#ifdef DEBUG
  else {
    fprintf(stderr,"NO children of node: %s\n",n->desc);
  }
#endif
}

int roachpool_list(struct katcp_dispatch *d){

  struct kcs_basic *kb;
  
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  traverse_tree(d,kb->b_pool_head);

  /*prepend_reply_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING,KATCP_OK);
  append_unsigned_long_katcp(d,KATCP_FLAG_ULONG | KATCP_FLAG_LAST, i);
  */
  return KATCP_RESULT_OWN;
}

void destroy_tree(struct kcs_node *n){
  
  int i;
  struct kcs_roach *r;
  struct kcs_node *c;
  
  if (n->childcount > 0){
    if (n->childnodes == NULL){
      for (i=0; i<n->childcount; i++){
        r = n->childroaches[i];
#ifdef DEBUG
        fprintf(stderr,"Freeing roach %s\n",r->hostname);
#endif  
        if (r->parent)   { free(r->parent);   r->parent = NULL; }
        if (r->hostname) { free(r->hostname); r->hostname = NULL; }
        if (r->ip)       { free(r->ip);       r->ip = NULL; }
        if (r->mac)      { free(r->mac);      r->mac = NULL; }
        if (r->type)     { free(r->type);     r->type = NULL; }
        free(r);
      }
      if (n->childroaches) { free(n->childroaches); n->childroaches = NULL; }
    } else if (n->childroaches == NULL){
      for (i=0; i<n->childcount; i++) {
        c = n->childnodes[i];
        destroy_tree(c);
      }
    }
  }
#ifdef DEBUG
  else {
    fprintf(stderr,"NO children of node: %s\n",n->desc);
  }
  fprintf(stderr,"Freeing node %s\n",n->desc);
#endif  
  free(n);
  n = NULL;
}

int roachpool_destroy(struct katcp_dispatch *d){
  
  struct kcs_basic *kb;
  
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  destroy_tree(kb->b_pool_head);

  return KATCP_RESULT_OK;
}

