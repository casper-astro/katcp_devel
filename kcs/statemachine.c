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
  fprintf(stderr,"SM: run_statemachine (%p) fn %s current state:%d\n",ksm,(!n)?ko->name:n->n_name,ksm->state);
#endif
  if (ksm->sm[ksm->state]){
#ifdef DEBUG
    fprintf(stderr,"SM: running state: (%p)\n",ksm->sm[ksm->state]);
#endif
    rtn = (*ksm->sm[ksm->state])(d,n,ko);
  }
  else {  
#ifdef DEBUG
    fprintf(stderr,"SM: cleaning up: (%p)\n",ksm);
#endif
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
    /*notice already exists so update it with new parse but dont wake it*/
    update_notice_katcp(d, n, p, 0, 0);
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
int disconnected_connect_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data){
  //struct katcp_job *j;
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  char *dc_kurl;
  int fd;

  ko = data;
  kr = ko->payload;
  
  if (!n){
    if (!(dc_kurl = kurl_string(kr->kurl,"?disconnect")))
      dc_kurl = kurl_add_path(kr->kurl,"?disconnect");
    if(!(n = find_notice_katcp(d,dc_kurl))){
      fd = net_connect(kr->kurl->host,kr->kurl->port,0);
      if (fd < 0){
        free(dc_kurl);
        log_message_katcp(d,KATCP_LEVEL_ERROR, NULL, "Unable to connect to %s",kr->kurl->str);
        return KCS_SM_CONNECT_STOP;     
      } else {
        /*
        n = create_notice_katcp(d,dc_kurl);
        if (!n){
          log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"unable to create halt notice %s",dc_kurl);
          return KCS_SM_CONNECT_STOP;
        } else {
          //j = create_job_katcp(
        }
        */
      }
    } else {
      free(dc_kurl);
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%s already connected",ko->name);
      return KCS_SM_CONNECT_STOP;
    }
  }



  return KCS_SM_CONNECT_CONNECTED;
}

int connected_connect_sm_kcs(struct katcp_dispatch *d, struct katcp_notice *n, void *data){

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
  ksm = kr->ksm;
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
  if (!n){
    if (!(p_kurl = kurl_string(kr->kurl,"?progdev")))
      p_kurl = kurl_add_path(kr->kurl,"?progdev");
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
    update_notice_katcp(d, n, p, 0, 0);
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
  wake_notice_katcp(d,n,NULL);
  //log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "done okay progdev %s",n->n_name);
  return KCS_SM_PROGDEV_STOP;
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
  fprintf(stderr,"SM: pointer to ping sm: %p\n",ksm);
#endif
  return ksm;
}

struct kcs_statemachine *get_sm_connect_kcs(){
  struct kcs_statemachine *ksm;
  ksm = malloc(sizeof(struct kcs_statemachine));
  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  ksm->state = KCS_SM_CONNECT_DISCONNECTED;
  ksm->sm[KCS_SM_CONNECT_DISCONNECTED] = &disconnected_connect_sm_kcs;
  ksm->sm[KCS_SM_CONNECT_CONNECTED]    = &connected_connect_sm_kcs;
  ksm->sm[KCS_SM_CONNECT_STOP]         = NULL;
#ifdef DEBUG
  fprintf(stderr,"SM: pointer to connect sm: %p\n",ksm);
#endif
  return ksm;
}

struct kcs_statemachine *get_sm_progdev_kcs(){
  struct kcs_statemachine *ksm;
  ksm = malloc(sizeof(struct kcs_statemachine));
  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  ksm->state = KCS_SM_PROGDEV_TRY;
  ksm->sm[KCS_SM_PROGDEV_TRY]   = &try_progdev_sm_kcs;
  ksm->sm[KCS_SM_PROGDEV_OKAY]  = &okay_progdev_sm_kcs;
  ksm->sm[KCS_SM_PROGDEV_STOP]  = NULL;
#ifdef DEBUG
  fprintf(stderr,"SM: pointer to progdev sm: %p\n",ksm);
#endif
  return ksm;
}

/*******************************************************************************************************/
/*API Functions*/
/*******************************************************************************************************/
int is_active_sm_kcs(struct kcs_roach *kr){
  if (kr->ksm == NULL)
    return 0;
  return 1;
}

int api_prototype_sm_kcs(struct katcp_dispatch *d, struct kcs_obj *ko, struct kcs_statemachine *(*call)(),void *data){
  struct kcs_roach *kr;
  struct kcs_node *kn;
  struct katcp_notice *n;
  int i;
  n = NULL;
  switch (ko->tid){
    case KCS_ID_ROACH:
      kr = (struct kcs_roach *) ko->payload;
      if (is_active_sm_kcs(kr))
        return KATCP_RESULT_FAIL;
      kr->ksm = (*call)();
      kr->data = data;
      run_statemachine(d,n,ko);
      //log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a roach",ko->name);
      break;
    case KCS_ID_NODE:
      kn = (struct kcs_node *) ko->payload;
      for (i=0; i<kn->childcount; i++){
        kr = kn->children[i]->payload;
        if (is_active_sm_kcs(kr))
          return KATCP_RESULT_FAIL;
        kr->ksm = (*call)();
        kr->data = data;
        run_statemachine(d,n,kn->children[i]);
        //log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a roach",kn->children[i]->name);
      }
      //log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a node with %d children",ko->name,kn->childcount);
      break;
  }
  return KATCP_RESULT_OK;
}

int statemachine_ping(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  struct kcs_obj *ko;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  ko = search_tree(kb->b_pool_head,arg_string_katcp(d,2));
  if (!ko)
    return KATCP_RESULT_FAIL;
  return api_prototype_sm_kcs(d,ko,&get_sm_ping_kcs,NULL);
}

int statemachine_connect(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  struct kcs_obj *ko;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  ko = search_tree(kb->b_pool_head,arg_string_katcp(d,2));
  if (!ko)
    return KATCP_RESULT_FAIL;
  return api_prototype_sm_kcs(d,ko,&get_sm_connect_kcs,NULL);
}

int statemachine_progdev(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  struct kcs_obj *ko;
  struct p_value *conf_bitstream;
  conf_bitstream = NULL;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  ko = search_tree(kb->b_pool_head,arg_string_katcp(d,5));
  if (!ko)
    return KATCP_RESULT_FAIL;
  conf_bitstream = parser_get(d,
                              arg_string_katcp(d,2),
                              arg_string_katcp(d,3),
                              atoi(arg_string_katcp(d,4)));
  if (!conf_bitstream)
    return KATCP_RESULT_FAIL;
  return api_prototype_sm_kcs(d,ko,&get_sm_progdev_kcs,conf_bitstream);
}



void ksm_destroy(struct kcs_statemachine *ksm){
#ifdef DEBUG
  fprintf(stderr,"SM: about to free (%p)\n",ksm);
#endif 
  if (ksm->n != NULL) { ksm->n = NULL; };
  if (ksm->sm != NULL) { free(ksm->sm); ksm->sm = NULL; }
  if (ksm != NULL) { free(ksm); ksm = NULL; }
}


int statemachine_greeting(struct katcp_dispatch *d){
  prepend_inform_katcp(d);
 // append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"katcp://roach:port/?ping | katcp://*roachpool/?ping");
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"ping [roachpool|roachurl]");
  //prepend_inform_katcp(d);
  //append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"connect [roachpool|roachurl]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"progdev conf-label conf-setting conf-value [roachpool|roach]");
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
