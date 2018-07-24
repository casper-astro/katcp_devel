/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "katcp.h"
#include "katpriv.h"

int print_time_delta_katcm(char *buffer, unsigned int len, time_t delta)
{
  unsigned long a, r;
  char *unit, *minor;
  int result;

  if(buffer == NULL){
    return -1;
  }
  if(len <= 0){
    return -1;
  }

  minor = NULL;

  a = delta;
  unit = "second";
  minor = NULL;

  if(a > 60){
    r = a % 60;
    a = a / 60;
    minor = r ? unit : NULL;
    unit = "minute";
    if(a > 60){
      r = a % 60;
      a = a / 60;
      minor = r ? unit : NULL;
      unit = "hour";
      if(a > 24){
        r = a % 24;
        a = a / 24;
        minor = r ? unit : NULL;
        unit = "day";
      }
    }
  }

  if(minor){
    result = snprintf(buffer, len, "%lu %s%s and %lu %s%s", a, unit, (a == 1) ? "" : "s", r, minor, (r == 1) ? "" : "s");
  } else {
    result = snprintf(buffer, len, "%lu %s%s", a, unit, (a == 1) ? "" : "s");
  }

  buffer[len - 1] = '\0';

  return result;
}

static char *misc_result_table[] = { KATCP_OK, KATCP_FAIL, KATCP_INVALID, KATCP_PARTIAL, NULL };

char *code_to_name_katcm(int code)
{
#ifdef DEBUG
  if((KATCP_RESULT_OK > 0) || (code < KATCP_RESULT_PARTIAL)){
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

char *default_message_type_katcm(char *string, int type)
{
  int len;
  char *ptr;

  if(string == NULL){
    return NULL;
  }

  switch(string[0]){
    case KATCP_REQUEST : 
    case KATCP_REPLY   :
    case KATCP_INFORM  :
      return strdup(string);
  }

  len = strlen(string);

  ptr = malloc(len + 2);
  if(ptr == NULL){
    return NULL;
  }

  ptr[0] = type;
  memcpy(ptr + 1, string, len + 1);

  return ptr;
}
