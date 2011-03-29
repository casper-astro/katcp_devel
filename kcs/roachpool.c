#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include <errno.h>

#include <sys/types.h>

#include <katcp.h>
#include <katpriv.h>
#include <netc.h>

#include "kcs.h"

void destroy_roach_kcs(struct kcs_roach *kr){
  int i;
  if (kr){
    if (kr->ip)     { free(kr->ip);                 kr->ip     = NULL; }
    if (kr->mac)    { free(kr->mac);                kr->mac    = NULL; } 
    if (kr->kurl)   { destroy_kurl_katcp(kr->kurl); kr->kurl   = NULL; }
    if (kr->ksm){ 
      for (i=0;i<kr->ksmcount;i++){
        destroy_ksm_kcs(kr->ksm[i]);
      }
      free(kr->ksm);
      kr->ksm    = NULL;
    }
    //if (kr->io_ksm) { destroy_ksm_kcs(kr->io_ksm);  kr->io_ksm = NULL; }
    free(kr);
  }
}

struct kcs_obj *new_kcs_obj(struct kcs_obj *parent, char *name, int tid, void *payload){
  struct kcs_obj *ko;
  ko = malloc(sizeof(struct kcs_obj));
  if (ko == NULL)
    return NULL;
  ko->tid     = tid;
  ko->parent  = parent;
  ko->name    = strdup(name);
  ko->payload = payload;
#ifdef DEBUG
  fprintf(stderr,"roachpool: new kcs_obj %s (%p) with payload type:%d (%p)\n",name,ko,tid,payload);
#endif
  return ko;
}

struct kcs_obj *new_kcs_node_obj(struct kcs_obj *parent, char *name){
  struct kcs_obj *ko;
  struct kcs_node *kn;
  ko = NULL;
  kn = NULL;
  kn = malloc(sizeof(struct kcs_node));
  if (kn == NULL)
    return NULL;
  kn->children   = NULL;
  kn->childcount = 0;
  ko = new_kcs_obj(parent, name, KCS_ID_NODE, kn);
  return ko;
}

struct kcs_obj *new_kcs_roach_obj(struct kcs_obj *parent, char *url, char *ip, char *mac){
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  ko = NULL;
  kr = NULL;
  kr = malloc(sizeof(struct kcs_roach));
  if (kr == NULL)
    return NULL;
  if (ip)
    kr->ip     = strdup(ip);
  else 
    kr->ip     = NULL;
  if (mac)
    kr->mac    = strdup(mac);
  else
    kr->mac    = NULL;
  kr->kurl     = create_kurl_from_string_katcp(url);
  kr->ksm      = NULL;
  kr->ksmcount = 0;
  kr->ksmactive= 0;
  //kr->io_ksm   = NULL;
  //kr->data     = NULL;
  
  if (kr->kurl == NULL){
    destroy_roach_kcs(kr);
    return NULL;
  }
  ko = new_kcs_obj(parent, url, KCS_ID_ROACH, kr);
  if (ko == NULL){
    destroy_roach_kcs(kr);
    return NULL;
  }
  kr->kurl->u_use++;
  return ko;
}
struct kcs_obj *init_tree(){
  struct kcs_obj *root;
  root = new_kcs_node_obj(NULL,"root");
  return root;
}

struct kcs_obj *search_tree(struct kcs_obj *o, char *str){

  struct kcs_obj *co;
  struct kcs_node *n;
  int i;

  if (strcmp(o->name,str) == 0){
#ifdef DEBUG
    fprintf(stderr,"roachpool: found match %s (%p) type:%d\n",o->name, o, o->tid);
#endif
    return o;
  }

  switch (o->tid){
    
    case KCS_ID_NODE:

      n = (struct kcs_node*) o->payload;
      if (!n) 
        return NULL;
      for (i=0; i < n->childcount; i++) {
#if 0
#ifdef DEBUG
        fprintf(stderr,"Searching children of %s (%p) for %s\n",o->name,o,str);
#endif
#endif
        co = search_tree(n->children[i], str);
        if (co) 
          return co;
      }
      break;
  }

#ifdef DEBUG
  fprintf(stderr,"roachpool: not in %s (%p)\n", o->name, o);
#endif
  return NULL;
}

int add_obj_to_node(struct kcs_obj *pno, struct kcs_obj *cno){
  /*mac      = arg_copy_string_katcp(d,4);*/
  
  struct kcs_node *parent;

  parent = (struct kcs_node*) pno->payload;

  parent->children = realloc(parent->children, sizeof(struct kcs_obj *) * ++(parent->childcount));
  parent->children[parent->childcount-1] = cno;

  cno->parent = pno;

  return KCS_OK;
}

int add_new_roach_to_tree(struct kcs_obj *root, char *poolname, char *url, char *ip, char *mac){
  
  struct kcs_obj *parent;
  struct kcs_obj *roach;
  
  parent = search_tree(root, poolname);
  
  if (!parent){
#ifdef DEBUG
    fprintf(stderr,"roachpool: parent pool doesn't exist so create\n");
#endif
    parent = new_kcs_node_obj(root, poolname);
    if (add_obj_to_node(root, parent) == KCS_FAIL){
#ifdef DEBUG
      fprintf(stderr,"roachpool: could not add roach to node\n");
#endif
      return KCS_FAIL;
    }
  }
/*  else{
#ifdef DEBUG
    fprintf(stderr,"Freeing poolname since pool exists already\n");
#endif    
    free(poolname);
  }*/
  
  if (search_tree(root, url))
    return KCS_FAIL;

  roach = new_kcs_roach_obj(parent, url, ip, mac);
  if (!roach){
#ifdef DEBUG
    fprintf(stderr,"roachpool: could not create new roach\n");
#endif
    return KCS_FAIL;
  }
  
  if (add_obj_to_node(parent, roach) == KCS_FAIL){
#ifdef DEBUG
    fprintf(stderr,"roachpool: could not add roach to node\n");
#endif
    return KCS_FAIL;
  }
#if 0
#ifdef DEBUG
  fprintf(stderr,"roachpool: \n");
#endif
#endif

  return KCS_OK;    
}

int add_roach_to_pool_kcs(struct katcp_dispatch *d, char *pool, char *url, char *ip){
  struct kcs_basic *kb;
  struct kcs_obj *root;
  int rtn;

  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);
  
  root = kb->b_pool_head;
  if (root == NULL){
    root = init_tree();
    kb->b_pool_head = root;
  }
  rtn = add_new_roach_to_tree(root, pool, url, ip, NULL);

  switch (rtn){
    case KCS_FAIL:
#ifdef DEBUG
      fprintf(stderr,"roachpool: error adding roach <%s> to pool <%s>\n", url, pool); 
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "roachpool: error adding roach <%s> to pool <%s>\n", url, pool); 
#endif
      break;
    case KCS_OK:
#ifdef DEBUG
      fprintf(stderr,"roachpool: added roach <%s> to pool <%s>\n", url, pool); 
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "roachpool: added roach <%s> to pool <%s>\n", url, pool); 
#endif
      break;
  }
  return rtn;
}

void show_pool(struct katcp_dispatch *d, struct kcs_obj *o, int depth){

  struct kcs_node *n;
  struct kcs_roach *r;
  int i;

#ifdef DEBUG
  for (i=0;i<depth;i++)
    fprintf(stderr, "\t");
  fprintf(stderr, "roachpool: kcs_obj (%p) %s type:%d has\n", o, o->name, o->tid);
#endif

  switch (o->tid){
    
    case KCS_ID_NODE:
      
      n = (struct kcs_node*) o->payload;

#ifdef DEBUG
      for (i=0;i<depth;i++)
        fprintf(stderr, "\t");
      fprintf(stderr," |-> kcs_node (%p) cc:%d\n", n, n->childcount);
#endif

      for (i=0;i<n->childcount;i++){
        show_pool(d, n->children[i], depth+1);
      }

      break;

    case KCS_ID_ROACH:

      r = (struct kcs_roach*) o->payload;

#ifdef DEBUG
      for (i=0;i<depth;i++)
        fprintf(stderr,"\t");
      fprintf(stderr," |-> kcs_roach (%p) ip:%s m:%s\n", r, r->ip, r->mac);
#endif
#ifndef STANDALONE
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "%s %s %s %s",o->parent->name,o->name,r->ip,r->mac);
#endif


      break;
  }
  
}

void destroy_tree(struct kcs_obj *o){
  
  struct kcs_node *n;
  struct kcs_roach *r;
  int i;
  
  n = NULL;
  r = NULL;
  
  if (!o) return;
  

  switch (o->tid){
    
    case KCS_ID_NODE:

      n = (struct kcs_node*) o->payload;

#ifdef DEBUG
      fprintf(stderr,"\troachpool: destory in kcs_node (%p) cc:%d\n", n, n->childcount);
#endif

      for (i=0;i<n->childcount;i++){
        destroy_tree(n->children[i]);
        n->children[i] = NULL;
      }
      if (n->children) { free(n->children); n->children = NULL; }
      if (n) { free(n); n = NULL; }

      break;

    case KCS_ID_ROACH:

      r = (struct kcs_roach*) o->payload;

      if (r) {
#ifdef DEBUG
        fprintf(stderr,"\troachpool: destroy in kcs_roach (%p) h:%s ip:%s m:%s\n", r, o->name, r->ip, r->mac);
#endif
        /*if (r->hostname) { free(r->hostname); r->hostname = NULL; }*/
        destroy_roach_kcs(r);
        r = NULL;
      }

      break;
  }
  
#ifdef DEBUG
  fprintf(stderr,"roachpool: destroy in kcs_obj (%p) %s type:%d\n", o, o->name, o->tid);
#endif
  if (o->name) { free(o->name); o->name = NULL; }
  if (o) free(o);
}

int remove_obj_from_current_pool(struct kcs_obj *ro){
  struct kcs_node *opn;
  int i;

  if (ro->parent->tid != KCS_ID_NODE){
#ifdef DEBUG
    fprintf(stderr,"roachpool: the parent obj is not NODE type\n");
#endif
    return KCS_FAIL;
  }

  opn = (struct kcs_node*) ro->parent->payload;

  for (i=0;i<opn->childcount;i++){
    if (opn->children[i] == ro){
      opn->children[i] = opn->children[opn->childcount-1];
      opn->children = realloc(opn->children, sizeof(struct kcs_obj *) * --(opn->childcount));
      break;
    }
  }
  
  ro->parent = NULL;

  return KCS_OK;
}

int mod_roach_to_new_pool(struct kcs_obj *root, char *pool, char *hostname){
  
  struct kcs_obj *ro;
  struct kcs_obj *po;
  
  ro = search_tree(root, hostname);

  if (!ro)
    return KCS_FAIL;
  
  po = search_tree(root, pool);

  if (!po){
#ifdef DEBUG
    fprintf(stderr,"roachpool: new pool doesn't exist so create\n");
#endif
    po = new_kcs_node_obj(root, pool);
    if (!po)
      return KCS_FAIL;
    if (add_obj_to_node(root, po) == KCS_FAIL){
#ifdef DEBUG
      fprintf(stderr,"roachpool: could not add new pool to node\n");
#endif
      return KCS_FAIL;
    }
  }

  if (remove_obj_from_current_pool(ro) == KCS_FAIL){
#ifdef DEBUG
    fprintf(stderr,"roachpool: couldn't remove obj from pool\n");
#endif
    return KCS_FAIL;
  }
  
  if (add_obj_to_node(po, ro) == KCS_FAIL){
#ifdef DEBUG
    fprintf(stderr,"roachpool: couldn't add obj to new node\n");
#endif
    return KCS_FAIL;
  }

#ifdef DEBUG
  fprintf(stderr,"roachpool: success %s is in pool %s\n", ro->name, po->name);
#endif
 
  return KCS_OK;
}

#ifndef STANDALONE
int roachpool_getconf(struct katcp_dispatch *d){
  char *confsetting;
  struct kcs_basic *kb;
  struct p_value **pv;
  int pvc,i,errc;

  pv   = NULL;
  pvc  = 0;
  errc = 0;

  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);

  confsetting = arg_string_katcp(d,2);
  
  if (!kb->b_parser){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Config file has not been parsed");
    return KATCP_RESULT_FAIL;
  }
  pv = parser_get_values(kb->b_parser,confsetting,&pvc);

  if (!pv){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Cannot get values for setting: %s",confsetting);
    return KATCP_RESULT_FAIL;
  }

#ifdef DEBUG
  fprintf(stderr,"%s has %d values\n",confsetting,pvc);
#endif
  
  if (!kb->b_pool_head){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Roach pool has not been created");
    return KATCP_RESULT_FAIL;
  }
  
  for (i=0;i<pvc;i++){
    if (mod_roach_to_new_pool(kb->b_pool_head,confsetting,pv[i]->str) == KCS_FAIL){
      log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"ROACH: %s is not available",pv[i]->str);
      errc++;
    }
  }
  
  errc = pvc-errc;
  if (errc == 0){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"None of the roaches from the config are available in the pool");
    return KATCP_RESULT_FAIL;
  }
  
  log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"%d of %d ROACHES moved to %s pool",errc,pvc,confsetting);

  return KATCP_RESULT_OK;
}

int roachpool_add(struct katcp_dispatch *d){

  struct kcs_basic *kb;
  struct kcs_obj   *root;
  char *url, *ip, *mac, *pool;

  url      = NULL;
  ip       = NULL;
  mac      = NULL;
  pool     = NULL;

  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);
  
  root = kb->b_pool_head;
  if (root == NULL){
    root = init_tree();
    kb->b_pool_head = root;
  }
  
  url      = arg_string_katcp(d,2);
  ip       = arg_string_katcp(d,3);
  pool     = arg_string_katcp(d,4);

  if (add_new_roach_to_tree(root,pool,url,ip,mac) == KCS_FAIL)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_OK;
}

int roachpool_mod(struct katcp_dispatch *d){
  
  struct kcs_basic *kb;
  struct kcs_obj *root;
  char *hostname, *newpool;

  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);

  root = kb->b_pool_head;
  if (!root)
    return KATCP_RESULT_FAIL;

  hostname = arg_string_katcp(d,2);
  newpool  = arg_string_katcp(d,3);

  if (mod_roach_to_new_pool(root,newpool,hostname) == KCS_FAIL)
    return KATCP_RESULT_FAIL;
  
  return KATCP_RESULT_OK;
}
int roachpool_del(struct katcp_dispatch *d){
  return KATCP_RESULT_OK;
}
int roachpool_list(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);
  if (kb->b_pool_head == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"The roach pool is uninitialized");
    return KATCP_RESULT_FAIL;
  }
  show_pool(d,kb->b_pool_head,0);
  return KATCP_RESULT_OK;
}
int roachpool_destroy(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);
  destroy_tree(kb->b_pool_head);
  return KATCP_RESULT_OK;
}

struct kcs_obj *roachpool_get_obj_by_name_kcs(struct katcp_dispatch *d, char *name){
  struct kcs_basic *kb;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  if (name == NULL)
    return NULL;
  return search_tree(kb->b_pool_head,name);
}

int roachpool_count_kcs(struct katcp_dispatch *d)
{
  struct kcs_obj *ko;
  struct kcs_node *kn;
  char *obj;
  int count;
  
  obj = arg_string_katcp(d, 2); 
  count = 0;

  ko = roachpool_get_obj_by_name_kcs(d, obj);
  if (ko == NULL)
    return KATCP_RESULT_FAIL;

  switch (ko->tid){
    case KCS_ID_ROACH:
        count = 1;
      break;

    case KCS_ID_NODE:
        kn = ko->payload;
        if (kn == NULL)
          return KATCP_RESULT_FAIL;
        count = kn->childcount;
      break;
  }
  
  prepend_reply_katcp(d);
  append_unsigned_long_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, count);

  return KATCP_RESULT_OK;
}

int roachpool_greeting(struct katcp_dispatch *d){
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"add [kurl (katcp://roach.hostname:port/)] [roach ip] [pool type]");
  //prepend_inform_katcp(d);
  //append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"del [roach hostname | pool type]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"mod [roach hostname] [new pool type]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"count [roachpool object]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "get-conf [config settings (servers_x / servers_f)]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "list");
  return KATCP_RESULT_OK;
}

int roach_cmd(struct katcp_dispatch *d, int argc)
{
  char *p_cmd;

  switch (argc){
    case 1:
      return roachpool_greeting(d);        
    
    case 2:
      p_cmd = arg_string_katcp(d,1);
      if (strcmp("list", p_cmd) == 0)
        return roachpool_list(d); 
      return KATCP_RESULT_FAIL;
    
    case 3:
      p_cmd = arg_string_katcp(d,1);
      if (strcmp("del", p_cmd) == 0){
 
      }
      else if (strcmp("get-conf", p_cmd) == 0){
        return roachpool_getconf(d);
      }
      else if (strcmp("count", p_cmd) == 0){
        return roachpool_count_kcs(d);
      } 
      return KATCP_RESULT_FAIL;
    
    case 4:
      p_cmd = arg_string_katcp(d, 1);
      if (strcmp("mod", p_cmd) == 0){
        return roachpool_mod(d);
      }
      return KATCP_RESULT_FAIL;

    case 5:
      p_cmd = arg_string_katcp(d, 1);
      if (strcmp("add", p_cmd) == 0){
        return roachpool_add(d);
      }
      return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_FAIL;
}



#endif

#ifdef STANDALONE
struct kcs_basic *tkb;

void *need_current_mode_katcp(struct katcp_dispatch *d, unsigned int mode){
  return tkb;
}

int main(int argc, char **argv){

  tkb = malloc(sizeof(struct kcs_basic));
  tkb->b_pool_head = init_tree();
 
  add_new_roach_to_tree(tkb->b_pool_head,
                        create_str("xport"),
                        create_str("roach1.roachnet"),
                        create_str("10.0.0.1"),
                        NULL);
  
  add_new_roach_to_tree(tkb->b_pool_head,
                        create_str("spare"),
                        create_str("roach1.roachnet"),
                        create_str("10.0.0.1"),
                        NULL);

  add_new_roach_to_tree(tkb->b_pool_head,
                        create_str("xport"),
                        create_str("roach2.roachnet"),
                        create_str("10.0.0.2"),
                        NULL);
  
  add_new_roach_to_tree(tkb->b_pool_head,
                        create_str("spare"),
                        create_str("roach2.roachnet"),
                        create_str("10.0.0.2"),
                        NULL);
  
  add_new_roach_to_tree(tkb->b_pool_head,
                        create_str("xport"),
                        create_str("roach3.roachnet"),
                        create_str("10.0.0.3"),
                        NULL);
  
  show_pool(NULL,tkb->b_pool_head,0);
  
  destroy_tree(tkb->b_pool_head);
  free(tkb);

  return 0;
}

#endif


