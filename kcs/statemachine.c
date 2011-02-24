#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#include <katcp.h>
#include <katcl.h>
#include <katpriv.h>

#include "kcs.h"

/*******************************************************************************************************/
/*Statemachine lookup function usually called from the statemachine notice*/
/*******************************************************************************************************/

int run_statemachine(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_statemachine *ksm;
  int rtn;
  ko = data;
  if (!ko)
    return 0;
  kr = ko->payload;
  if (!kr)
    return 0;
  ksm = kr->ksm;
#ifdef DEBUG
  fprintf(stderr,"SM: run_statemachine fn %s\n",(!n)?ko->name:n->n_name);
#endif
  if (ksm->state != 0)
    rtn = (*ksm->sm[ksm->state])(d,n,ko);
  else {  
    log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Destroying kcs_statemachine %p",ksm);
    free(ksm);
    kr->ksm = NULL;
    return 0;
  }
  ksm->state = rtn;
  return rtn;
}

/*******************************************************************************************************/
/*PING*/
/*******************************************************************************************************/
int kcs_sm_ping_s1(struct katcp_dispatch *d,struct katcp_notice *n, void *data){
  struct katcp_job *j;
  struct katcl_parse *p;
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_statemachine *ksm;
  char * p_kurl;

  ko = data;
  kr = ko->payload;
  ksm = kr->ksm;
 
#ifdef DEBUG
  fprintf(stderr,"SM: kcs_sm_ping_s1 %s\n",(!n)?ko->name:n->n_name);
#endif

  j = find_job_katcp(d,ko->name);
  if (j == NULL){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Couldn't find job labeled %s",ko->name);
    return KCS_SM_PING_STOP;
  }

  p = create_parse_katcl();
  if (p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create message");
    return KCS_SM_PING_STOP;
  }

  if (add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, "?watchdog") < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    return KCS_SM_PING_STOP;
  }

  if (!n){
    if (!(p_kurl = kurl_string(kr->kurl,"?ping")))
      p_kurl = kurl_add_path(kr->kurl,"?ping");
    n = create_parse_notice_katcp(d, p_kurl, 0, p);
    if (!n){
      log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create notice %s",p_kurl);
      free(p_kurl);
      return KCS_SM_PING_STOP;
    }
    
    ksm->n = n;
    
    if (p_kurl) { free(p_kurl); p_kurl = NULL; }
    
    if (add_notice_katcp(d, n, &run_statemachine, ko) != 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to add to notice %s",n->n_name);
      return KCS_SM_PING_STOP;
    }
  }
  else { 
    update_notice_katcp(d, n, p, 0, 0);
  } 

  gettimeofday(&kr->lastnow, NULL);
  
  if (notice_to_job_katcp(d, j, n) != 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to notice_to_job %s",n->n_name);
    return KCS_SM_PING_STOP;
  }

  return KCS_SM_PING_S2;
}

int time_ping_wrapper_call(struct katcp_dispatch *d, void *data){
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_statemachine *ksm; 
  struct katcp_notice *n;

  ko = data;
  kr = ko->payload;
  ksm = kr->ksm;
  n = ksm->n;
#ifdef DEBUG
  fprintf(stderr, "SM: running from timer, waking notice %p\n", n);
#endif
  wake_notice_katcp(d,n,NULL);
  return KCS_SM_PING_S2;
}

int kcs_sm_ping_s2(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct katcl_parse *p;
  char *ptr;
  struct timeval when, now, delta;

#ifdef DEBUG
  fprintf(stderr,"SM: kcs_sm_ping_s2 %s\n",n->n_name);
#endif

  delta.tv_sec  = 1;
  delta.tv_usec = 0;

  gettimeofday(&now,NULL);
  add_time_katcp(&when,&now,&delta);

  p = get_parse_notice_katcp(d,n);
  if (p){
    ptr = get_string_parse_katcl(p,1);
    sub_time_katcp(&delta,&now,&((struct kcs_roach *)((struct kcs_obj *)data)->payload)->lastnow);
    log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"%s reply in %dms returns: %s",n->n_name,(delta.tv_sec*1000)+(delta.tv_usec/1000),ptr);
    /*prepend_reply_katcp(d);
    append_string_katcp(d, KATCP_FLAG_LAST, ptr);*/
    if (strcmp(ptr,"fail") == 0)
      return KCS_SM_PING_STOP;
  }

  if (register_at_tv_katcp(d, &when, &time_ping_wrapper_call, data) < 0)
    return KCS_SM_PING_STOP;

  return KCS_SM_PING_S1;
}

/*******************************************************************************************************/
/*CONNECT*/
/*******************************************************************************************************/
int disconnected_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct katcp_job *j;
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  char *dc_kurl;
  int fd;

  ko = data;
  kr = ko->payload;
  
  if (!n){
    if (!(dc_kurl = kurl_string(kr->kurl,"?disconnect")))
      dc_kurl = kurl_add_path(kr->kurl,"?disconnect");
    if(!(n = fine_notice_katcp(d,dc_kurl))){
      fd = net_connect(kr->kurl->host,kr->kurl->port,0);
      if (fd < 0){
        free(dc_kurl);
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to connect to %s",kr->kurl->str);
        return KCS_SM_CONNECT_STOP;     
      } else{
        //n = create_notice_katcp(d,
      }
    } else {
      free(dc_kurl);
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s already connected",ko->name);
      return KCS_SM_CONNECT_STOP;
    }
  }



  return KCS_SM_CONNECT_CONNECTED;
}

int connected_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data){

  return KCS_SM_CONNECT_STOP;
}


/*******************************************************************************************************/
/*StateMachine Setups*/
/*******************************************************************************************************/

struct kcs_statemachine *get_sm_ping_kcs(){
  struct kcs_statemachine *ksm;
  ksm = malloc(sizeof(struct kcs_statemachine));
  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  ksm->state = KCS_SM_PING_S1;
  ksm->sm[KCS_SM_PING_STOP] = NULL;
  ksm->sm[KCS_SM_PING_S1]   = &kcs_sm_ping_s1;
  ksm->sm[KCS_SM_PING_S2]   = &kcs_sm_ping_s2;
#ifdef DEBUG
  fprintf(stderr,"SM: pointer to ping sm: %p\n",ksm->sm);
#endif
  return ksm;
}

struct kcs_statemachine *get_sm_connect_kcs(){
  struct kcs_statemachine *ksm;
  ksm = malloc(sizeof(struct kcs_statemachine));
  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  ksm->state = KCS_SM_CONNECT_DISCONNECTED;
  ksm->state[KCS_SM_CONNECT_STOP]         = NULL;
  ksm->state[KCS_SM_CONNECT_DISCONNECTED] = &disconnected_sm_kcs;
  ksm->state[KCS_SM_CONNECT_CONNECTED]    = &connected_sm_kcs;
#ifdef DEBUG
  fprintf(stderr,"SM: pointer to connect sm: %p\n",ksm->sm);
#endif
  return ksm;
}


/*******************************************************************************************************/
/*API Functions*/
/*******************************************************************************************************/

int statemachine_greeting(struct katcp_dispatch *d){
  prepend_inform_katcp(d);
 // append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"katcp://roach:port/?ping | katcp://*roachpool/?ping");
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"ping [roachpool|roachurl]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"connect [roachpool|roachurl]");
  /*prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"progdev [roachpool|roach]");
  */
  return KATCP_RESULT_OK;
}

int statemachine_ping(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_node *kn;
  struct katcp_notice *n;
  int i;
  n = NULL;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
#ifdef DEBUG
  fprintf(stderr,"SM ping param: %s\n",arg_string_katcp(d,2));
#endif
  ko = search_tree(kb->b_pool_head,arg_string_katcp(d,2));
  if (!ko)
    return KATCP_RESULT_FAIL;
  switch (ko->tid){
    case KCS_ID_ROACH:
      kr = (struct kcs_roach *) ko->payload;
      kr->ksm = get_sm_ping_kcs();
      run_statemachine(d,n,ko);
      log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a roach",ko->name);
      break;
    case KCS_ID_NODE:
      kn = (struct kcs_node *) ko->payload;
      for (i=0; i<kn->childcount; i++){
        kr = kn->children[i]->payload;
        kr->ksm = get_sm_ping_kcs();
        run_statemachine(d,n,kn->children[i]);
        log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a roach",kn->children[i]->name);
      }
      log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a node with %d children",ko->name,kn->childcount);
      break;
  }
  return KATCP_RESULT_OK;
}

int statemachine_connect(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_node *kn;
  struct katcp_notice *n;
  int i;
  n = NULL;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
#ifdef DEBUG
  fprintf(stderr,"SM connect param: %s\n",arg_string_katcp(d,2));
#endif
  ko = search_tree(kb->b_pool_head,arg_string_katcp(d,2));
  if (!ko)
    return KATCP_RESULT_FAIL;
  switch (ko->tid){
    case KCS_ID_ROACH:
      kr = (struct kcs_roach *) ko->payload;
      kr->ksm = get_sm_connect_kcs();
      run_statemachine(d,n,ko);
      log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a roach",ko->name);
      break;
    case KCS_ID_NODE:
      kn = (struct kcs_node *) ko->payload;
      for (i=0; i<kn->childcount; i++){
        kr = kn->children[i]->payload;
        /*kr->ksm = get_sm_connect_kcs();
        run_statemachine(d,n,kn->children[i]);
        */log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a roach",kn->children[i]->name);
      }
      log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a node with %d children",ko->name,kn->childcount);
      break;
  }
  return KATCP_RESULT_OK;
}

void ksm_destroy(struct kcs_statemachine *ksm){
  if (ksm->n != NULL) { ksm->n = NULL; };
  if (ksm->sm != NULL) { free(ksm->sm); ksm->sm = NULL; }
  if (ksm != NULL) { free(ksm); ksm = NULL; }
}




#ifdef STANDALONE
int main(int argc, char **argv){
  
  int i;
  int cf;
  int (*statemachine[])(int) = {
    &func1,
    &func2,
    &func3,
    &func1,
    NULL
  };

  for (i=0; statemachine[i]; i++)
  {
    
    cf = (*statemachine[i])(i);
    
#ifdef DEBUG
      fprintf(stderr,"function returned: %d\n",cf);
#endif
  
  }

  return EX_OK;
}
#endif
