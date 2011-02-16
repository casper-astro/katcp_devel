#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <katcp.h>
#include <katcl.h>
#include <katpriv.h>

#include "kcs.h"

int kcs_sm_ping_s1(struct katcp_dispatch *d,struct katcp_notice *n){
  struct katcp_job *j;
  struct katcl_parse *p;
  struct kcs_basic *kb;
  struct kcs_statemachine *ksm;
  struct kcs_obj *o;

  j = find_job_katcp(d,n->n_name);
  if (j == NULL){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Couldn't find job labeled %s",n->n_name);
    return KCS_FAIL;
  }

  p = create_parse_katcl();
  if (p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create message");
    return KCS_FAIL;
  }

  if (add_string_parse_katcl(p,KATCP_FLAG_FIRST|KATCP_FLAG_LAST|KATCP_FLAG_STRING,"?watchdog")<0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    return KCS_FAIL;
  }

  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  o = search_tree(kb->b_pool_head,n->n_name);
  ksm = ((struct kcs_roach*) o->payload)->ksm;

  if (submit_to_job_katcp(d,j,p,n->n_name,ksm->sm[KCS_SM_PING_S2]) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to submit message to job");
    destroy_parse_katcl(p);
  }
  
  return KCS_SM_PING_S2;
}

int kcs_sm_ping_s2(struct katcp_dispatch *d, struct katcp_notice *n){
  
#ifdef DEBUG
  fprintf(stderr,"GREAT SUCCESS we are in the 2nd state %s\n",n->n_name);
#endif

 

  return KCS_SM_PING_S1;
}

struct kcs_statemachine *get_kcs_sm_ping(){
  struct kcs_statemachine *ksm;
  
  ksm = malloc(sizeof(struct kcs_statemachine));
  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, struct katcp_notice *))*3);
  ksm->state = KCS_SM_PING_S1;
  ksm->sm[KCS_SM_PING_S1] = &kcs_sm_ping_s1;
  ksm->sm[KCS_SM_PING_S2] = &kcs_sm_ping_s2;
  ksm->sm[KCS_SM_PING_STOP] = NULL;

#ifdef DEBUG
  fprintf(stderr,"pointer to ping sm: %p\n",ksm->sm);
#endif

  return ksm;
}

int statemachine_greeting(struct katcp_dispatch *d){
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"ping [roachpool|roach]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"connect [roachpool|roach]");
  prepend_inform_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,"progdev [roachpool|roach]");
  return KATCP_RESULT_OK;
}

void run_statemachine(int (**sm)(struct katcp_dispatch *, struct katcp_notice *), int state, struct katcp_dispatch *d, struct katcp_notice *n){
  int rtn; 
  
  rtn = (*sm[state])(d,n);
  
}
/*
int init_statemachines(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  struct kcs_statemachines *ksms;

  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);

  if (!kb->b_sms){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"Cannot run init_statemachines, statemachines already exist");
    return KATCP_RESULT_FAIL;
  }

  ksms = malloc(sizeof(struct kcs_statemachines));
  ksms->machines = NULL;
  ksms->mcount = 0;

  ksms->machines = malloc(sizeof(struct kcs_statemachine*)*1);
  ksms->mcount++;

  ksms->machines[KCS_SM_PING] = get_kcs_sm_ping();
 
  kb->b_sms = ksms;
  
  return KATCP_RESULT_OK;
}
*/

int statemachine_ping(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  struct kcs_obj *ko;
  struct kcs_roach *kr;
  struct kcs_node *kn;
  struct katcp_notice *n;
  //struct kcs_statemachine *ksm;

  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  
#ifdef DEBUG
  fprintf(stderr,"sm ping param: %s\n",arg_string_katcp(d,2));
#endif
  
  ko = search_tree(kb->b_pool_head,arg_string_katcp(d,2));
  if (!ko)
    return KATCP_RESULT_FAIL;

  switch (ko->tid){
    case KCS_ID_ROACH:
      
      kr = (struct kcs_roach *) ko->payload;
      
      kr->ksm = get_kcs_sm_ping();
      
      n = create_notice_katcp(d,ko->name,0);

      run_statemachine(kr->ksm->sm,kr->ksm->state,d,n);


      log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a roach",ko->name);
      break;
    case KCS_ID_NODE:
      
      kn = (struct kcs_node *) ko->payload;

      log_message_katcp(d, KATCP_LEVEL_INFO,NULL,"Found %s it is a node with %d children",ko->name,kn->childcount);
      break;
  }

  

  //ksm = get_kcs_sm_ping();
  
  //run_statemachine(ksm->sm,KCS_SM_START);

  //free(ksm);

  return KATCP_RESULT_OK;
}

void ksm_destroy(struct kcs_statemachine *ksm){
  if (ksm->sm != NULL) { free(ksm->sm); ksm->sm = NULL; }
  if (ksm != NULL) { free(ksm); ksm = NULL; }
}
/*
void statemachine_destroy(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  struct kcs_statemachines *ksms;
  int i,j;

  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  ksms = kb->b_sms;
  
  for (i=0;i<ksms->mcount;i++){
    free(ksms->machines[i]->sm);
    ksms->machines[i]->sm = NULL;
    free(ksms->machines[i]);
  }
  free(ksms->machines);
  ksms->machines = NULL;
  ksms->mcount   = 0;

  free(ksms);
}
*/

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
