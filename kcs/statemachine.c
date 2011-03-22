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
#include <netc.h>

#include "kcs.h"

int is_active_sm_kcs(struct kcs_roach *kr){
  if (kr->ksm == NULL)
    return 0;
  return 1;
}

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
  
  ksm = kr->ksm[kr->ksmactive];
  if (!ksm)
    return 0;

#ifdef DEBUG
  fprintf(stderr,"SM: run_statemachine (%p) fn %s current state:%d\n",ksm,(!n)?ko->name:n->n_name,ksm->state);
#endif

  if (ksm->sm[ksm->state]){
#ifdef DEBUG
    fprintf(stderr,"SM: running state: (%p)\n",ksm->sm[ksm->state]);
#endif
    rtn = (*ksm->sm[ksm->state])(d,n,ko);
  }
  else {  
    wake_single_name_notice_katcp(d,KCS_SCHEDULER_NOTICE,NULL,ko);
#ifdef DEBUG
    fprintf(stderr,"SM: cleaning up: (%p)\n",ksm);
#endif
    log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Destroying kcs_statemachine %p",ksm);
    /*free(ksm);
    kr->ksm = NULL;*/
    //destroy_roach_ksm_kcs(kr);
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
  ksm = kr->ksm[kr->ksmactive];
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
    if (!(p_kurl = copy_kurl_string_katcp(kr->kurl,"?ping")))
      p_kurl = add_kurl_path_copy_string_katcp(kr->kurl,"?ping");
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
    /*notice already exists so update it with new parse but dont wake it*/
    update_notice_katcp(d, n, p, KATCP_NOTICE_TRIGGER_OFF, 0, NULL);
  } 
  gettimeofday(&kr->lastnow, NULL);
  if (notice_to_job_katcp(d, j, n) != 0){
    /*send the notice to the job this adds it to the bottom of the list of thinngs the job must do*/
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
  if (!is_active_sm_kcs(kr))
    return KCS_SM_PING_STOP;
  ksm = kr->ksm[kr->ksmactive];
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
    log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"%s reply in %4.3fms returns: %s",n->n_name,(float)(delta.tv_sec*1000)+((float)delta.tv_usec/1000),ptr);
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
int connect_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct kcs_basic *kb;
  struct katcp_job *j;
  struct katcl_parse *p;

  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_obj *root;
  
  char *dc_kurl, *newpool;
  int fd;

  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  if (!kb)
    return KCS_SM_CONNECT_STOP;
  
  root = kb->b_pool_head;
  if (!root)
    return KCS_SM_CONNECT_STOP;

  ko = data;
  kr = ko->payload;
  newpool = kr->ksm[kr->ksmactive]->data;

  if (n != NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "expected null notice but got (%p)", n);
    return KCS_SM_CONNECT_STOP;
  }

  if (!(dc_kurl = copy_kurl_string_katcp(kr->kurl,"?disconnect")))
    dc_kurl = add_kurl_path_copy_string_katcp(kr->kurl,"?disconnect");
  
  n = find_notice_katcp(d, dc_kurl);
  if (n != NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "find notice katcp returns (%p) %s is already connected", n, ko->name);
    free(dc_kurl);
    return KCS_SM_CONNECT_STOP;
  }
  
  fd = net_connect(kr->kurl->u_host, kr->kurl->u_port, NETC_ASYNC);
  if (fd < 0) {
    log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to connect to %s",kr->kurl->u_str);
    free(dc_kurl);
    return KCS_SM_CONNECT_STOP;     
  } 
  
  n = create_notice_katcp(d, dc_kurl, 0);
  if (n == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create halt notice %s",dc_kurl);
    free(dc_kurl);
    return KCS_SM_CONNECT_STOP;
  } 

  free(dc_kurl);
  
  kr->ksm[kr->ksmactive]->n = n;

  j = create_job_katcp(d, kr->kurl, 0, fd, 1, n); /*dispatch, katcp_url, pid, fd, async, notice*/
  if (j == NULL){
    log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to create job for %s",kr->kurl->u_str);
    return KCS_SM_CONNECT_STOP;
  } 
  
  if (add_notice_katcp(d, n, kr->ksm[kr->ksmactive]->sm[KCS_SM_CONNECT_CONNECTED], ko)) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "Unable to add the halt function to notice");
    zap_job_katcp(d, j);
    return KCS_SM_CONNECT_STOP;
  }

  if (mod_roach_to_new_pool(root, newpool, ko->name) == KCS_FAIL){
    log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Could not move roach %s to pool %s\n", kr->kurl->u_str, newpool);
    zap_job_katcp(d, j);
    return KCS_SM_CONNECT_STOP;
  } 
  
  log_message_katcp(d,KATCP_LEVEL_INFO, NULL, "Success: roach %s moved to pool %s", kr->kurl->u_str, newpool);
  
  p = create_parse_katcl();
  if (p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create message");
    return KCS_SM_CONNECT_STOP;
  }
  if (add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_STRING | KATCP_FLAG_LAST, "!sm connect ok") < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    return KCS_SM_CONNECT_STOP;
  }
  
  //update notice with new parse

  wake_single_name_notice_katcp(d, KCS_SCHEDULER_NOTICE, NULL, ko);
  
  return KCS_SM_CONNECT_CONNECTED;
}

int disconnect_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct kcs_roach *kr;
  struct kcs_obj *ko;
  struct kcs_basic *kb;
  char *newpool;

  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);
  ko = data;
  kr = ko->payload;

  newpool = kr->data?kr->data:"disconnected";
  
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "Halt notice (%p) %s",n,n->n_name);
  if (mod_roach_to_new_pool(kb->b_pool_head,newpool,ko->name) == KCS_FAIL){
    log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Could not move roach %s to pool %s\n",kr->kurl->u_str,newpool);
  } else { 
    log_message_katcp(d,KATCP_LEVEL_INFO, NULL, "Success: roach %s moved to pool %s",kr->kurl->u_str,newpool);
  }
  /*if (kr->io_ksm){
    destroy_ksm_kcs(kr->io_ksm);
    kr->io_ksm = NULL;
  }*/
  //kr->data = NULL; 
  return KCS_SM_CONNECT_STOP;
}

/*******************************************************************************************************/
/*PROGDEV*/
/*******************************************************************************************************/
int try_progdev_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct katcp_job *j;
  struct katcl_parse *p;
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_statemachine *ksm;
  char * p_kurl;
  struct p_value *conf_bs;
  ko = data;
  kr = ko->payload;
  ksm = kr->ksm[kr->ksmactive];
  conf_bs = kr->data;
  j = find_job_katcp(d,ko->name);
  if (j == NULL){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Couldn't find job labeled %s",ko->name);
    return KCS_SM_PROGDEV_STOP;
  }
  p = create_parse_katcl();
  if (p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create message");
    return KCS_SM_PROGDEV_STOP;
  }
  if (add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?progdev") < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    return KCS_SM_PROGDEV_STOP;
  }
  if (add_string_parse_katcl(p,KATCP_FLAG_LAST | KATCP_FLAG_STRING,conf_bs->str) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    return KCS_SM_PROGDEV_STOP;
  }
  kr->data = NULL;
  if (!n){
    if (!(p_kurl = copy_kurl_string_katcp(kr->kurl,"?progdev")))
      p_kurl = add_kurl_path_copy_string_katcp(kr->kurl,"?progdev");
    n = create_parse_notice_katcp(d, p_kurl, 0, p);
    if (!n){
      log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create notice %s",p_kurl);
      free(p_kurl);
      return KCS_SM_PROGDEV_STOP;
    }
    ksm->n = n;
    if (p_kurl) { free(p_kurl); p_kurl = NULL; }
    if (add_notice_katcp(d, n, &run_statemachine, ko) != 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to add to notice %s",n->n_name);
      return KCS_SM_PROGDEV_STOP;
    }
  }
  else { 
    /*notice already exists so update it with new parse but dont wake it*/
    update_notice_katcp(d, n, p, KATCP_NOTICE_TRIGGER_OFF, 0, NULL);
  } 
  if (notice_to_job_katcp(d, j, n) != 0){
    /*send the notice to the job this adds it to the bottom of the list of thinngs the job must do*/
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to notice_to_job %s",n->n_name);
    return KCS_SM_PROGDEV_STOP;
  }
  /*okay*/
  //log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "done try progdev %s",n->n_name);
  return KCS_SM_PROGDEV_OKAY;
}

int okay_progdev_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct katcl_parse *p;
  char *ptr;
  p = get_parse_notice_katcp(d,n);
  if (p){
    ptr = get_string_parse_katcl(p,1);
    log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"%s replies: %s",n->n_name,ptr);
    if (strcmp(ptr,"fail") == 0)
      return KCS_SM_PROGDEV_STOP;
  }
#ifdef DEBUG
  fprintf(stderr,"SM: about to run wake_notice_katcp\n");
#endif
  //wake_notice_katcp(d,n,p);
  wake_notice_katcp(d,n,NULL);
  //log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "done okay progdev %s",n->n_name);
  return KCS_SM_PROGDEV_STOP;
}

/*******************************************************************************************************/
/*StateMachine Setups*/
/*******************************************************************************************************/

struct kcs_statemachine *get_sm_ping_kcs(void *data){
  struct kcs_statemachine *ksm;
  
  ksm = malloc(sizeof(struct kcs_statemachine));
  if (ksm == NULL)
    return NULL;

  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  if (ksm->sm == NULL) {
    free(ksm);
    return NULL;
  }

  ksm->state = KCS_SM_PING_S1;
  ksm->n = NULL;
  ksm->sm[KCS_SM_PING_STOP] = NULL;
  ksm->sm[KCS_SM_PING_S1]   = &kcs_sm_ping_s1;
  ksm->sm[KCS_SM_PING_S2]   = &kcs_sm_ping_s2;
  ksm->data = data;

#ifdef DEBUG
  fprintf(stderr,"SM: pointer to ping sm: %p\n",ksm);
#endif

return ksm;
}

struct kcs_statemachine *get_sm_connect_kcs(void *data){
  struct kcs_statemachine *ksm;
  
  ksm = malloc(sizeof(struct kcs_statemachine));
  if (ksm == NULL)
    return NULL;

  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  if (ksm->sm == NULL) {
    free(ksm);
    return NULL;
  }

  ksm->state = KCS_SM_CONNECT_DISCONNECTED;
  ksm->n = NULL;
  ksm->sm[KCS_SM_CONNECT_STOP]         = NULL;
  ksm->sm[KCS_SM_CONNECT_DISCONNECTED] = &connect_sm_kcs;
  ksm->sm[KCS_SM_CONNECT_CONNECTED]    = &disconnect_sm_kcs;
  ksm->data = data;

#ifdef DEBUG
  fprintf(stderr,"SM: pointer to connect sm: %p\n",ksm);
#endif

  return ksm;
}

struct kcs_statemachine *get_sm_progdev_kcs(void *data){
  struct kcs_statemachine *ksm;
  
  ksm = malloc(sizeof(struct kcs_statemachine));
  if (ksm == NULL)
    return NULL;

  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  if (ksm->sm == NULL) {
    free(ksm);
    return NULL;
  }

  ksm->state = KCS_SM_PROGDEV_TRY;
  ksm->n = NULL;
  ksm->sm[KCS_SM_PROGDEV_TRY]   = &try_progdev_sm_kcs;
  ksm->sm[KCS_SM_PROGDEV_OKAY]  = &okay_progdev_sm_kcs;
  ksm->sm[KCS_SM_PROGDEV_STOP]  = NULL;
  ksm->data = data;

#ifdef DEBUG
  fprintf(stderr,"SM: pointer to progdev sm: %p\n",ksm);
#endif
  
  return ksm;
}

/*******************************************************************************************************/
/*KCS Scheduler Stuff*/
/* ******************************************************************************************************/
int tick_scheduler_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  int i;

  ko = data;
  kr = ko->payload;

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "scheduler tick function");
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "scheduler roach    :%s",ko->name);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "scheduler ksmcount :%d",kr->ksmcount);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "scheduler ksmactive:%d",kr->ksmactive);
  for (i=0;i<kr->ksmcount;i++){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "scheduler ksm[%d]  :%p",i,kr->ksm[i]);
  }
  
  kr->ksmactive++;

#ifdef DEBUG
  fprintf(stderr,"SM: scheduler tick with data: %p\n",data);
#endif
  
  return KCS_SCHEDULER_TICK;
}
struct katcp_notice *add_scheduler_notice_kcs(struct katcp_dispatch *d, void *data){
  struct katcp_notice *n;

  n = find_notice_katcp(d,KCS_SCHEDULER_NOTICE);

#ifdef DEBUG
  fprintf(stderr,"SM: scheduler: add_scheduler_notice to: %p\n",n);
#endif

  if(n == NULL){
    log_message_katcp(d,KATCP_LEVEL_INFO, NULL, "creating kcs scheduler and attaching tick callback");
    n = create_notice_katcp(d,KCS_SCHEDULER_NOTICE,0);
    if (n == NULL){
      log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "unable to create kcs scheduler notice");
      return NULL;
    }
#ifdef DEBUG
    fprintf(stderr,"SM: scheduler: needed to create notice %s: %p\n",KCS_SCHEDULER_NOTICE,n);
#endif
  }
  if (add_notice_katcp(d, n, &tick_scheduler_kcs, data)<0){
    log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "unable to add function to notice");
    return NULL; 
  }
#ifdef DEBUG
  fprintf(stderr,"SM: scheduler added tick callback with data: %p\n",data);
#endif

  return n;  
}

/*******************************************************************************************************/
/*API Functions*/
/*******************************************************************************************************/
int api_prototype_sm_kcs(struct katcp_dispatch *d, struct kcs_obj *ko, struct kcs_statemachine *(*call)(void *),void *data){
  struct kcs_roach *kr;
  struct kcs_node *kn;
  struct katcp_notice *n;
  int i, oldcount;
  
  n = NULL;
  i=0;

  switch (ko->tid){
    case KCS_ID_ROACH:
      kr = (struct kcs_roach *) ko->payload;
      
      if (add_scheduler_notice_kcs(d,ko) == NULL)
        return KATCP_RESULT_FAIL;
      
      kr->ksm = realloc(kr->ksm,sizeof(struct kcs_statemachine *)*++kr->ksmcount);
      if (kr->ksm == NULL)
        return KATCP_RESULT_FAIL;
      
      kr->ksm[kr->ksmcount-1] = (*call)(data);
      //kr->ksm[kr->ksmcount-1]->data = data;
      
      run_statemachine(d,n,ko);
      break;
    
    case KCS_ID_NODE:
      kn = (struct kcs_node *) ko->payload;
      while (i < kn->childcount){  
        kr = kn->children[i]->payload;
        
        if (add_scheduler_notice_kcs(d,kn->children[i]) == NULL)
          return KATCP_RESULT_FAIL;
        
        kr->ksm = realloc(kr->ksm,sizeof(struct kcs_statemachine *)*++kr->ksmcount);
        if (kr->ksm == NULL)
          return KATCP_RESULT_FAIL;
        
        kr->ksm[kr->ksmcount-1] = (*call)(data);
        //kr->data = data;
        
        oldcount = kn->childcount;
        run_statemachine(d,n,kn->children[i]);
        if (kn->childcount == oldcount)
          i++; 
      }
      break;
  }
  return KATCP_RESULT_OK;
}

int statemachine_stop(struct katcp_dispatch *d){
  struct kcs_obj *ko;
  struct kcs_node *kn;
  struct kcs_roach *kr;
  int state,i;
  ko = roachpool_get_obj_by_name_kcs(d,arg_string_katcp(d,2));
  if (!ko)
    return KATCP_RESULT_FAIL;
/*
  switch (ko->tid){
    case KCS_ID_ROACH:
      kr = (struct kcs_roach*) ko->payload;
      if (is_active_sm_kcs(kr)){
        for (state=0; kr->ksm->sm[state]; state++);
        log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"%s is in state: %d going to stop state: %d",kr->ksm->n->n_name,kr->ksm->state,state);
        kr->ksm->state = state;
        rename_notice_katcp(d,kr->ksm->n,"<zombie>");
        wake_notice_katcp(d,kr->ksm->n,NULL);
      }
      else
        return KATCP_RESULT_FAIL;
      break;
    case KCS_ID_NODE:
      kn = (struct kcs_node *) ko->payload;
      for (i=0; i<kn->childcount; i++){
        kr = kn->children[i]->payload;
        if (is_active_sm_kcs(kr)){
          for (state=0; kr->ksm->sm[state]; state++);
          log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"%s is in state: %d going to stop state: %d",kr->ksm->n->n_name,kr->ksm->state,state);
          kr->ksm->state = state;
          rename_notice_katcp(d,kr->ksm->n,"<zombie>");
          wake_notice_katcp(d,kr->ksm->n,NULL);
        }
        else
          return KATCP_RESULT_FAIL;
      }
      break;
  }
  */
  return KATCP_RESULT_OK;
}

int statemachine_ping(struct katcp_dispatch *d){
  struct kcs_obj *ko;
  char *obj;

  obj = arg_string_katcp(d,2);

  ko = roachpool_get_obj_by_name_kcs(d,obj);
  if (!ko)
    return KATCP_RESULT_FAIL;

  return api_prototype_sm_kcs(d,ko,&get_sm_ping_kcs,NULL);
}

int statemachine_connect(struct katcp_dispatch *d){
  struct kcs_obj *ko;
  char *obj, *pool;

  obj = arg_string_katcp(d,2);
  pool = arg_string_katcp(d,3);

  ko = roachpool_get_obj_by_name_kcs(d,obj);
  if (!ko)
    return KATCP_RESULT_FAIL;

  return api_prototype_sm_kcs(d,ko,&get_sm_connect_kcs,pool);
}
int statemachine_disconnect(struct katcp_dispatch *d){
  struct kcs_obj *ko;
  struct kcs_node *kn;
  struct kcs_roach *kr;
  struct katcp_job *j;
  int i;
  char *obj, newpool;

  obj = arg_string_katcp(d,2);
  newpool = arg_string_katcp(d,3);

  ko = roachpool_get_obj_by_name_kcs(d,obj);
  if (!ko)
    return KATCP_RESULT_FAIL;
  
  switch (ko->tid) {
    case KCS_ID_ROACH:
      j = find_job_katcp(d,ko->name);
      if (!j){
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to find job %s",ko->name);
        return KATCP_RESULT_OK;
      }
      
      kr = ko->payload;
      //kr->data = arg_string_katcp(d,3);
      kr->ksm[kr->ksmactive]->data = newpool;
      zap_job_katcp(d,j);
      break;
    
    case KCS_ID_NODE:
      kn = ko->payload;
      
      for (i=0;i<kn->childcount;i++){
        j = find_job_katcp(d,kn->children[i]->name);
        if (!j){
          log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to find job %s",kn->children[i]->name);
          return KATCP_RESULT_OK;
        }
        
        kr = kn->children[i]->payload;
        //kr->data = arg_string_katcp(d,3);
        kr->ksm[kr->ksmactive]->data = newpool;
        zap_job_katcp(d,j);
      }
      break;
  }
  return KATCP_RESULT_OK;
}

int statemachine_progdev(struct katcp_dispatch *d){
  struct kcs_obj *ko;
  struct p_value *conf_bitstream;
  char *label, *setting, *value;

  label   = arg_string_katcp(d,2);
  setting = arg_string_katcp(d,3);
  value   = arg_string_katcp(d,4);

  conf_bitstream = NULL;
  
  ko = roachpool_get_obj_by_name_kcs(d,arg_string_katcp(d,5));
  if (!ko)
    return KATCP_RESULT_FAIL;
  
  conf_bitstream = parser_get(d, label, setting, atoi(value));
  if (!conf_bitstream)
    return KATCP_RESULT_FAIL;
  
  return api_prototype_sm_kcs(d,ko,&get_sm_progdev_kcs,conf_bitstream);
}

void destroy_ksm_kcs(struct kcs_statemachine *ksm){
  if (ksm){
    if (ksm->n != NULL) { ksm->n = NULL; }
    if (ksm->sm != NULL) { free(ksm->sm); ksm->sm = NULL; }
    free(ksm); 
  }
}
/*
void destroy_roach_ksm_kcs(struct kcs_roach *kr){
  if (kr->ksm){
#ifdef DEBUG
    fprintf(stderr,"SM: about to free (%p)\n",kr->ksm);
#endif
    destroy_ksm_kcs(kr->ksm);
    kr->ksm = NULL; 
  }
}
*/

int statemachine_greeting(struct katcp_dispatch *d){
  prepend_inform_katcp(d);
 // append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"katcp://roach:port/?ping | katcp://*roachpool/?ping");
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"ping [roachpool|roachurl]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"connect [roachpool|roachurl] [newpool]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"disconnect [roachpool|roachurl] [newpool]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"progdev [conf-label] [conf-setting] [conf-value] [roachpool|roach]");
  return KATCP_RESULT_OK;
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
