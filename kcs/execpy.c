#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <katcp.h>

#include "kcs.h"




int execpy_exec(char *filename){

  int fds[2], dups;
  pid_t pid,wpid;
  int status=0;
  //int i;

  if (socketpair(AF_UNIX, SOCK_STREAM,0, fds) < 0) {
      fprintf(stderr,"socketpair failed: %s\n",strerror(errno));  
      return KATCP_RESULT_FAIL;
  }
  
  fprintf(stderr,"parent_pid = %d\n",getpid());
 
  pid = fork();
 
  if (pid == 0){
    /*This is the child*/
    fprintf(stderr,"In Child Process (PID=%d)\n",getpid());
    
    dups=0;

    close(fds[1]);
    if (fds[0] != STDOUT_FILENO){
      if (dup2(fds[0], STDOUT_FILENO) != STDOUT_FILENO){
        fprintf(stderr,"Could not DUP2 chid socket pair to child stdout fd err: %s\n",strerror(errno));        
        exit(EX_OSERR);
      }
      dups++;
    }
    if (fds[0] != STDIN_FILENO){
      if (dup2(fds[0], STDIN_FILENO) != STDIN_FILENO){
        fprintf(stderr,"Could not DUP2 child socket pair to child stdin fd err: %s\n",strerror(errno));
        exit(EX_OSERR);
      }
      dups++;
    }
    if (dups >= 2)
      fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    
    char *args[] = {"/bin/ls","-t","-l",(char*)0};
    execvp("ls -tl",args);
    
    fprintf(stderr,"EXECVP FAIL: %s\n",strerror(errno));
    exit(EX_OSERR);
    //exit(1);
  }
  else if (pid < 0){
    close(fds[0]);
    close(fds[1]);
    fprintf(stderr,"fork failed: %s\n",strerror(errno));
    return KATCP_RESULT_FAIL;
  }

  /*This is the parent*/
  close(fds[0]);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);



  while ((wpid = wait(&status)) > 0){
    fprintf(stderr,"EXIT STATUS of %d was %d (%s)\n",(int)wpid,status,strerror(status));
  }

  return KATCP_RESULT_OK;
}




#ifdef STANDALONE

int greeting(char *app) {
  fprintf(stderr,"ROACH ExecPY\n\n\tUsage:\t%s -f [filename] [args] [...]\n\n",app);
  return EX_OK;
}

int main(int argc, char **argv){
  
  int i,j,c;
  char *param;
  char *filename;

  i=j=1;
  param=NULL;

  if (argc == i) return greeting(argv[0]);

  while (i<argc) {
  
    if (argv[i][0] == '-') {
      c=argv[i][j];
      switch (c) {
        default:

          if (argv[i][2] == '\0') {
            param = argv[i+1];
            i+=2;
          }
          else {
            param = argv[i] + 2;
            i+=1;
          }
          
          if (i > argc) return greeting(argv[0]);
          else if (param[0] == '\0') return greeting(argv[0]);
          else if (param[0] == '-') return greeting(argv[0]);

          switch (c) {
            case 'f':
              filename = param;
              break;
            default:
              return greeting(argv[0]);
              break;
          }

          break;
      }
    }
    else
      return greeting(argv[0]);

  }
  
  fprintf(stderr,"filename: %s\n",filename);

  execpy_exec(filename);

  return EX_OK;
}
#endif
