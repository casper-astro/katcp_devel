#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "kcs.h"


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
