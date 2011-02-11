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

#include "kcs.h"

int kcs_sm_ping_s1(struct katcp_dispatch *d,char *kurl){
  struct katcp_job *j;
  struct katcp_parse *p;
  struct kcs_basic *kb;
  struct kcs_statemachines *ksms;

  j = find_job_katcp(d,kurl);
  if (j == NULL){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"Couldn't find job labeled %s",kurl);
    return KCS_FAIL;
  }

  p = create_parse_katcl();
  if (p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to create message");
    return KCS_FAIl;
  }

  if (add_string_parse_katcl(p,KATCP_FLAG_FIRST|KATCP_FLAG_LAST|KATCP_FLAG_STRING,"?watchdog")<0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to assemble message");
    return KCS_FAIl;
  }

  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  ksms = kb->b_sms;

  if (submit_to_job_katcp(d,j,p,ksms->machines[KCS_SM_PING]->sm[KCS_SM_PING_S2]) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR,NULL,"unable to submit message to job");
    destroy_parse_katcl(p);
  }
  
  return KCS_SM_PING_S2;
}

int kcs_sm_ping_s2(struct katcp_dispatch *d, char *kurl){
  
  return KCS_SM_PING_S1;
}

struct kcs_statemachine *get_kcs_sm_ping(){
  struct kcs_statemachine *ksm;
  
  ksm = malloc(sizeof(struct kcs_statemachine));
  ksm->sm = malloc(sizeof(int (*)(struct katcp_dispatch *, char *))*3);
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

void run_statemachine(int (**sm)(struct katcp_dispatch *, char *),int state){
  int i;
  int rtn;

  for(i=0;sm[i];i++){
    //rtn = (*sm[i])(555);
  }
  
}

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

int statemachine_ping(struct katcp_dispatch *d){
  struct kcs_basic *kb;
  //struct kcs_statemachine *ksm;

  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  
#ifdef DEBUG
  fprintf(stderr,"sm ping param: %s\n",arg_string_katcp(d,2));
#endif

  //ksm = get_kcs_sm_ping();
  
  //run_statemachine(ksm->sm,KCS_SM_START);

  //free(ksm);

  return KATCP_RESULT_OK;
}

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
