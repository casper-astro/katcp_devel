#include <stdio.h>
#include <stdlib.h>

#include "katcp.h"

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

