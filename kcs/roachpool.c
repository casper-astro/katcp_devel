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

struct kcs_obj *new_kcs_obj(struct kcs_obj *parent, char *name, int tid, void *payload){
  struct kcs_obj *ko;
  ko = malloc(sizeof(struct kcs_obj));
  if (ko == NULL)
    return NULL;
  ko->tid     = tid;
  ko->parent  = parent;
  ko->name    = name;
  ko->payload = payload;
#ifdef DEBUG
  fprintf(stderr,"RP new kcs_obj %s (%p) with payload type:%d (%p)\n",name,ko,tid,payload);
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
  ko = new_kcs_obj(parent,name,KCS_ID_NODE,kn);
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
  /*kr->hostname = hostname;*/
  kr->ip       = ip;
  kr->mac      = mac;
  kr->jl       = NULL;
  kr->kurl     = kurl_create_url_from_string(url);
  kr->ksm      = NULL;
  kr->data     = NULL;
  if (kr->kurl == NULL)
    return NULL;
  ko = new_kcs_obj(parent,url,KCS_ID_ROACH,kr);
  return ko;
}
/*
char *create_str(char *s){
  char *nstr;
  if (!s)
    return NULL;
  nstr = NULL;
  nstr = malloc(sizeof(char)*(strlen(s)+1));
  if (!nstr)
    return NULL;
  nstr = strcpy(nstr,s);
  nstr[strlen(s)] = '\0';
  return nstr;
}
*/
struct kcs_obj *init_tree(){
  struct kcs_obj *root;
  root = new_kcs_node_obj(NULL,create_str("root"));
  return root;
}

struct kcs_obj *search_tree(struct kcs_obj *o, char *str){

  struct kcs_obj *co;
  struct kcs_node *n;
  int i;

  if (strcmp(o->name,str) == 0){
#ifdef DEBUG
    fprintf(stderr,"RP FOUND Match %s (%p) type:%d\n",o->name,o,o->tid);
#endif
    return o;
  }

  switch (o->tid){
    
    case KCS_ID_NODE:

      n = (struct kcs_node*) o->payload;
      if (!n) 
        return NULL;
      for (i=0;i<n->childcount;i++) {
#ifdef DEBUG
        fprintf(stderr,"Searching children of %s (%p) for %s\n",o->name,o,str);
#endif
        co = search_tree(n->children[i],str);
        if (co) 
          return co;
      }
      break;
  }

#ifdef DEBUG
  fprintf(stderr,"Not in %s (%p)\n",o->name,o);
#endif
  return NULL;
}

int add_obj_to_node(struct kcs_obj *pno, struct kcs_obj *cno){
  
  struct kcs_node *parent;

  parent = (struct kcs_node*) pno->payload;

  parent->children = realloc(parent->children,sizeof(struct kcs_obj*)*++(parent->childcount));
  parent->children[parent->childcount-1] = cno;

  cno->parent = pno;

  return KCS_OK;
}

int add_new_roach_to_tree(struct kcs_obj *root, char *poolname, char *url, char *ip, char *mac){
  
  struct kcs_obj *parent;
  struct kcs_obj *roach;
  
  parent = search_tree(root,poolname);
  
  if (!parent){
#ifdef DEBUG
    fprintf(stderr,"RP Parent pool doesn't exist so create a new one\n");
#endif
    parent = new_kcs_node_obj(root,poolname);
    if (add_obj_to_node(root,parent) == KCS_FAIL){
#ifdef DEBUG
      fprintf(stderr,"Could not add roach to node\n");
#endif
      return KCS_FAIL;
    }
  }
  else{
#ifdef DEBUG
    fprintf(stderr,"Freeing poolname since pool exists already\n");
#endif    
    free(poolname);
  }
  
  if (search_tree(root,url))
    return KCS_FAIL;

  roach = new_kcs_roach_obj(parent,url,ip,mac);
  if (!roach){
#ifdef DEBUG
    fprintf(stderr,"Could not create new roach\n");
#endif
    return KCS_FAIL;
  }
  
  if (add_obj_to_node(parent,roach) == KCS_FAIL){
#ifdef DEBUG
    fprintf(stderr,"Could not add roach to node\n");
#endif
    return KCS_FAIL;
  }

#ifdef DEBUG
  fprintf(stderr,"RP NEW ROACH\n");
#endif

  return KCS_OK;    
}

void show_pool(struct katcp_dispatch *d,struct kcs_obj *o,int depth){

  struct kcs_node *n;
  struct kcs_roach *r;
  int i;

#ifdef DEBUG
  for (i=0;i<depth;i++)
    fprintf(stderr,"\t");
  fprintf(stderr,"kcs_obj (%p) %s type:%d has\n",o,o->name,o->tid);
#endif

  switch (o->tid){
    
    case KCS_ID_NODE:
      
      n = (struct kcs_node*) o->payload;

#ifdef DEBUG
      for (i=0;i<depth;i++)
        fprintf(stderr,"\t");
      fprintf(stderr," |-> kcs_node (%p) cc:%d\n",n,n->childcount);
#endif

      for (i=0;i<n->childcount;i++){
        show_pool(d,n->children[i],depth+1);
      }

      break;

    case KCS_ID_ROACH:

      r = (struct kcs_roach*) o->payload;

#ifdef DEBUG
      for (i=0;i<depth;i++)
        fprintf(stderr,"\t");
      fprintf(stderr," |-> kcs_roach (%p) ip:%s m:%s\n",r,r->ip,r->mac);
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
      fprintf(stderr,"\tRP DESTROY in kcs_node (%p) cc:%d\n",n,n->childcount);
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
        fprintf(stderr,"\tRP DESTROY in kcs_roach (%p) h:%s ip:%s m:%s\n",r,o->name,r->ip,r->mac);
#endif
        /*if (r->hostname) { free(r->hostname); r->hostname = NULL; }*/
        if (r->ip) { free(r->ip); r->ip = NULL; }
        if (r->mac) { free(r->mac); r->mac = NULL; }
        if (r->kurl) { kurl_destroy(r->kurl); r->kurl = NULL; }
        if (r->ksm) { destroy_roach_ksm_kcs(r); }
        free(r);
      }

      break;
  }
  
#ifdef DEBUG
  fprintf(stderr,"RP DESTROY in kcs_obj (%p) %s type:%d\n",o,o->name,o->tid);
#endif
  if (o->name) { free(o->name); o->name = NULL; }
  free(o);
}

int remove_obj_from_current_pool(struct kcs_obj *ro){
  struct kcs_node *opn;
  int i;

  if (ro->parent->tid != KCS_ID_NODE){
#ifdef DEBUG
    fprintf(stderr,"The Parent obj is not NODE type\n");
#endif
    return KCS_FAIL;
  }

  opn = (struct kcs_node*) ro->parent->payload;

  for (i=0;i<opn->childcount;i++){
    if (opn->children[i] == ro){
      opn->children[i] = opn->children[opn->childcount-1];
      opn->children = realloc(opn->children,sizeof(struct kcs_obj*)*--(opn->childcount));
      break;
    }
  }
  
  ro->parent = NULL;

  return KCS_OK;
}

int mod_roach_to_new_pool(struct kcs_obj *root, char *pool, char *hostname){
  
  struct kcs_obj *ro;
  struct kcs_obj *po;
  
  ro = search_tree(root,hostname);

  if (!ro)
    return KCS_FAIL;
  
  po = search_tree(root,pool);

  if (!po){
#ifdef DEBUG
    fprintf(stderr,"New pool doesn't exist so create new one\n");
#endif
    po = new_kcs_node_obj(root,create_str(pool));
    if (!po)
      return KCS_FAIL;
    if (add_obj_to_node(root,po) == KCS_FAIL){
#ifdef DEBUG
      fprintf(stderr,"Could not add new pool to node\n");
#endif
      return KCS_FAIL;
    }
  }

  if (remove_obj_from_current_pool(ro) == KCS_FAIL){
#ifdef DEBUG
    fprintf(stderr,"Couldn't remove obj from pool\n");
#endif
    return KCS_FAIL;
  }
  
  if (add_obj_to_node(po,ro) == KCS_FAIL){
#ifdef DEBUG
    fprintf(stderr,"Couldn't add obj to new node\n");
#endif
    return KCS_FAIL;
  }

#ifdef DEBUG
  fprintf(stderr,"SUCCESS: %s is in pool %s\n",ro->name,po->name);
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
  
  url      = arg_copy_string_katcp(d,2);
  ip       = arg_copy_string_katcp(d,3);
  /*mac      = arg_copy_string_katcp(d,4);*/
  pool     = arg_copy_string_katcp(d,4);

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

int roachpool_halt_notice(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct kcs_basic *kb;
  struct kcs_obj *o;
  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);
  o = data;
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "halt notice %s",n->n_name);
  if (mod_roach_to_new_pool(kb->b_pool_head,"disconnected",o->name))
    return -1;
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Disconnected %s and moved to pool %s",o->name,"disconnected");
  return 0;
}

int roachpool_connect_pool(struct katcp_dispatch *d){
  
  struct katcp_job *j;
  struct katcp_notice *n;

  struct kcs_basic *kb;
  struct kcs_obj *root, *o;
  struct kcs_node *kn;
  struct kcs_roach *kr;

  int i, fd, count;
  char *pool, *dc_kurl;
  
  j = NULL; n = NULL; o = NULL; root = NULL; kn = NULL; kr = NULL, dc_kurl = NULL;
  count = 0;
  
  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);

  pool = arg_string_katcp(d,2);
  if (!pool)
    return KATCP_RESULT_FAIL;
  
  root = kb->b_pool_head;
  if (!root)
    return KATCP_RESULT_FAIL;

  o = search_tree(root,pool);
  if (!o)
    return KATCP_RESULT_FAIL;

#ifdef DEBUG
  fprintf(stderr,"Found roach pool: %s at %p\n",pool,o);
#endif
  
  kn = (struct kcs_node*) o->payload;
  i=0;
  //for (i=0; i<kn->childcount; i++){
  while (i < kn->childcount) {  
    o = kn->children[i];
    kr = (struct kcs_roach*) o->payload;

    if (!(dc_kurl = kurl_string(kr->kurl,"?disconnect")))
      dc_kurl = kurl_add_path(kr->kurl,"?disconnect");

#ifdef DEBUG 
    kurl_print(kr->kurl);
#endif
    //if (!find_notice_katcp(d,o->name)){
    if (!find_notice_katcp(d,dc_kurl)){
      fd = net_connect(kr->kurl->host,kr->kurl->port,0);
      if (fd < 0){
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to connect to %s",kr->kurl->str);
        /*net connect fail*/
        i++;
      } else {
        /*net connect success*/
        n = create_notice_katcp(d,dc_kurl,0);
        if (!n){
          /*notice fail*/
          log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to create 'halt' notice for %s",kr->kurl->str);
          i++;
        } else {
          /*notice success*/
          j = create_job_katcp(d,kr->kurl->str,0,fd,n);
          if (!j){
            /*job fail*/
            log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to create job for %s",kr->kurl->str);
            i++;
          } else {
            /*job success*/
            if (mod_roach_to_new_pool(root,"connected",o->name) == KCS_FAIL){
              log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Could not move roach %s to pool %s\n",kr->kurl->str,"connected");
              i++;
            } else {
              log_message_katcp(d,KATCP_LEVEL_INFO, NULL, "Success: roach %s moved to pool %s\n",kr->kurl->str,"connected");
              if (add_notice_katcp(d,n,&roachpool_halt_notice,o))
                log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Unable to add the halt function to notice");
              count++;
            }
            n = NULL;
            j = NULL;
          }
        }
      }
    }
    else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s already connected",o->name);
      i++;
    }
    if (dc_kurl) { free(dc_kurl); dc_kurl = NULL; }
  }

  log_message_katcp(d,KATCP_LEVEL_INFO, NULL, "Created network jobs for %d of %d roaches in pool %s\n",count,kn->childcount+count,pool);
  if (count == (kn->childcount+count))
    return KATCP_RESULT_OK;
  else
    return KATCP_RESULT_FAIL;
}

struct kcs_obj *roachpool_get_obj_by_name_kcs(struct katcp_dispatch *d, char *name){
  struct kcs_basic *kb;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  return search_tree(kb->b_pool_head,name);
}

int roachpool_greeting(struct katcp_dispatch *d){
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"add [kurl (katcp://roach.hostname:port/)] [roach ip] [pool type]");/*[roach hostname] [roach ip] [pool type]");*/
  //prepend_inform_katcp(d);
  //append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"del [roach hostname | pool type]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"mod [roach hostname] [new pool type]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "get-conf [config settings (servers_x / servers_f)]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "list");
  /*prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "connect [pool]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"start [roach hostname]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"stop [roach hostname]");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"start-pool");
  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"stop-pool");
  */
  return KATCP_RESULT_OK;
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


