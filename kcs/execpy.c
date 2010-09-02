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
#include <katcl.h>

#include "kcs.h"


static volatile int sawchild=1;

void handle(int signum){
  sawchild=0;
}

void build_socket_set(struct e_state *e){
  FD_ZERO(&e->insocks);
  FD_SET(e->fd,&e->insocks);
  e->highsock = e->fd;
}

void run_child(int *fds){
  int dups;
  char *args[] = {"/bin/ls","-t","-l",NULL};

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

  execvp("/bin/ls",args);

  fprintf(stderr,"EXECVP FAIL: %s\n",strerror(errno));
  exit(EX_OSERR);
}

#define TEMPBUFFERSIZE 100
int parent_process_data(struct e_state *e, char *buf, int size){
  
  int gotline;
  gotline = 0;
  
   
  
  return 0;  
   
}

int run_parent(struct e_state *e,int *fds){

  pid_t wpid;
  struct sigaction sa;
  int rtn, run=1, status=0, recvb;
 
  char temp_buffer[TEMPBUFFERSIZE];

  close(fds[0]);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  
  fprintf(stderr,"Parent fd: %d\n",fds[1]);
  
  e->fd       = fds[1];
  e->highsock = 0;

  sigset_t emptyset, blockset;
  sigemptyset(&blockset);
  sigaddset(&blockset, SIGCHLD);
  sigprocmask(SIG_BLOCK,&blockset, NULL);
  sa.sa_flags=0;
  sa.sa_handler=handle;
  sigemptyset(&(sa.sa_mask));
  sigaction(SIGCHLD,&sa,NULL);
  sigemptyset(&emptyset);

  while (run){
    build_socket_set(e);
    rtn = pselect(e->highsock+1,&e->insocks,NULL,NULL,NULL,&emptyset);
    if (rtn < 0){
      fprintf(stderr,"PSelect returned an error: %s\n",strerror(errno));
      switch (errno){
        case EAGAIN :
        case EINTR  :
          break;
        default :
          return KATCP_RESULT_FAIL;
      }
    }
    else {
      if (FD_ISSET(e->fd,&e->insocks)){
        fprintf(stderr,"PARENT got data\n");
        recvb = read(e->fd,temp_buffer,TEMPBUFFERSIZE);
        
        if (recvb == 0){
          run=0;
          fprintf(stderr,"Read EOF from client\n");
        }
        else {
          fprintf(stderr,"Parent has %d bytes process returned: %d\n",recvb,parent_process_data(e,temp_buffer,recvb));
        }
      }
    }

    if (sawchild) {
      if (e->pid == waitpid(e->pid,&status,WNOHANG)){
        fprintf(stderr,"In waitpid with WNOHANG\n");
      }
    }
  }
        
  if (!sawchild){
    fprintf(stderr,"about to wait for SIGCHLD\n");
    wpid = waitpid(e->pid,&status,0);
  }
  
  close(e->fd);
  return status;
}


struct e_state * execpy_exec(char *filename, int *status){

  int fds[2];
  pid_t pid;

  struct e_state *e;
  e = malloc(sizeof(struct e_state));
  e->fd       = 0;
  e->highsock = 0;
  e->kl       = NULL;

  e->kl = create_katcl(STDOUT_FILENO);

  if (e->kl == NULL){
    fprintf(stderr,"create_katcl fail\n");
    return NULL;
  }

  if (socketpair(AF_UNIX, SOCK_STREAM,0, fds) < 0) {
    fprintf(stderr,"socketpair failed: %s\n",strerror(errno));  
    return NULL;
  }
  
  fprintf(stderr,"parent_pid = %d\n",getpid());
 
  pid = fork();
 
  if (pid == 0){
    /*This is the child*/
    run_child(fds);
  }
  else if (pid < 0){
    close(fds[0]);
    close(fds[1]);
    fprintf(stderr,"fork failed: %s\n",strerror(errno));
    return NULL;
  }

  /*This is the parent*/
  e->pid = pid;
  *status = run_parent(e,fds);
  return e;
}

int execpy_destroy(struct e_state *e){
  if (e != NULL){
    fprintf(stderr,"Freeing state struct\n");
    
    destroy_katcl(e->kl,0);
    
    free(e);
    return KATCP_RESULT_OK;
  }
  return KATCP_RESULT_FAIL;
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

  int status;
  struct e_state *e;
 
  e = execpy_exec(filename, &status);

  if (e ==NULL)
    return EX_SOFTWARE;
  
  if(execpy_destroy(e) == KATCP_RESULT_OK){
    fprintf(stderr,"Mem cleaned\n"); 
  }
  else {
    fprintf(stderr,"Mem not cleaned\n"); 
  }

  if (WIFEXITED(status)) {
    fprintf(stderr,"End of program\n");
    exit(WEXITSTATUS(status));
  }
  else if (WIFSIGNALED(status)){
#ifdef WCOREDUMP
    if (WCOREDUMP(status)){
      fprintf(stderr,"CHild Core DUMPed!!\n");
    }
#endif
    kill(getpid(),WTERMSIG(status));
  }

  return EX_OK;
}
#endif
