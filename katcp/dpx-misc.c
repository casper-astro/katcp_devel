#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

static char *scope_table[KATCP_MAX_SCOPE] = {
  "individual", "group", "global"
};

char *string_from_scope_katcp(unsigned int scope)
{
  if(scope >= KATCP_MAX_SCOPE){
    return NULL;
  }

  return scope_table[scope];
}

int code_from_scope_katcp(char *scope)
{
  unsigned int i;

  if(scope == NULL){
    return -1;
  }

  for(i = 0; i < KATCP_MAX_SCOPE; i++){
    if(!strcmp(scope_table[i], scope)){
      return i;
    }
  }

  return -1;
}


#endif

