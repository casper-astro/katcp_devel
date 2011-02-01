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


int func1(int rtn){
#ifdef DEBUG
  fprintf(stderr,"Called Function 1 going to return: %d\n",rtn);
#endif
  return rtn;
}

int func2(int rtn){
#ifdef DEBUG
  fprintf(stderr,"Called Function 2 going to return: %d\n",rtn);
#endif
  return rtn;
}

int func3(int rtn){
#ifdef DEBUG
  fprintf(stderr,"Called Function 3 going to return: %d\n",rtn);
#endif
  return rtn;
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
