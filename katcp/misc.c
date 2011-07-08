/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "katcp.h"
#include "katpriv.h"

static char *misc_result_table[] = { KATCP_OK, KATCP_FAIL, KATCP_INVALID, NULL };

char *code_to_name_katcm(int code)
{
#ifdef DEBUG
  if((code > 0) || (code < -2)){
    return NULL;
  }
#endif
  return misc_result_table[code * (-1)];
}


char **copy_vector_katcm(char **vector, unsigned int size)
{
  char **base;
  unsigned int i;

  base = malloc(sizeof(char *) * size);
  if(base == NULL){
    return NULL;
  }

  for(i = 0; i < size; i++){
    if(vector[i]){
      base[i] = strdup(vector[i]);
      if(base[i] == NULL){
        delete_vector_katcm(base, i);
        return NULL;
      }
    } else {
      base[i] = NULL;
    }
  }

  return base;
}

void delete_vector_katcm(char **vector, unsigned int size)
{
  unsigned int i;

  if(vector == NULL){
    return;
  }

  for(i = 0; i < size; i++){
    if(vector[i]){
      free(vector[i]);
      vector[i] = NULL;
    }
  }

  free(vector);
}
