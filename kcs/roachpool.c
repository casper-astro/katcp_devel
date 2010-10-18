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
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"del [roach hostname]");
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

int roachpool_add(struct katcp_dispatch *d){

  struct kcs_basic *kb;
  struct kcs_roach_pool *krp;
  struct kcs_roach *kr;

  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);
  
  if (kb->b_rpool == NULL){
    krp = malloc(sizeof(struct kcs_roach_pool));
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

  return KATCP_RESULT_OK;
}

int roachpool_list(struct katcp_dispatch *d){

  struct kcs_basic *kb;
  struct kcs_roach_pool *krp;
  struct kcs_roach *kr;
  int i;

  i = 0;

  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);

  krp = kb->b_rpool;

  if (krp != NULL){
    for (; i<krp->krrcount; i++){
      kr = krp->krr[i];
      prepend_inform_katcp(d);
      append_string_katcp(d,KATCP_FLAG_STRING,kr->type);
      append_string_katcp(d,KATCP_FLAG_STRING,kr->hostname);
      append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,kr->ip);
    }
  }

  prepend_reply_katcp(d);
  append_string_katcp(d,KATCP_FLAG_STRING,KATCP_OK);
  append_unsigned_long_katcp(d,KATCP_FLAG_ULONG | KATCP_FLAG_LAST, i);
  
  return KATCP_RESULT_OWN;
}

int roachpool_destroy(struct katcp_dispatch *d){
  
  struct kcs_basic *kb;
  struct kcs_roach_pool *krp;
  struct kcs_roach *kr;
  int i;
  
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  
  krp = kb->b_rpool;

  if (krp == NULL)
    return KATCP_RESULT_OK;

  if (krp->krr != NULL){
    for (i=0;i<krp->krrcount;i++){
      kr = krp->krr[i];
      if (kr != NULL){
        if (kr->hostname != NULL){
          free(kr->hostname);
          kr->hostname = NULL;
        }
        if (kr->ip != NULL){
          free(kr->ip);
          kr->ip = NULL;
        }
        if (kr->mac != NULL){
          free(kr->mac);
          kr->mac = NULL;
        }
        if (kr->type != NULL){
          free(kr->type);
          kr->type = NULL;
        }
        free(kr);
        kr = NULL;
        krp->krr[i] = kr;
      }
    }
    free(krp->krr);
    krp->krr = NULL;
  }
  free(krp);
  
  return KATCP_RESULT_OK;
}

