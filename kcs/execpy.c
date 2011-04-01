/* (c) 2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

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
 // int klfd;

  FD_ZERO(&e->insocks);
  FD_SET(e->fd,&e->insocks);
  if (e->highsock < e->fd)
    e->highsock = e->fd;
/*
  FD_ZERO(&e->outsocks);
  if (flushing_katcl(e->kl)) {
    klfd = fileno_katcl(e->kl);
    FD_SET(klfd,&e->outsocks);
    if(e->highsock < klfd)
      e->highsock = klfd;
  }*/
}

void run_child(int *fds,char *filename, char **argv){
  int dups;
  /*char *args[] = {filename,"-t","-l",NULL};*/

  fprintf(stderr,"EXECPY CHILD: In Child Process (PID=%d)\n",getpid());

  dups=0;

  close(fds[1]);
  if (fds[0] != STDOUT_FILENO){
    if (dup2(fds[0], STDOUT_FILENO) != STDOUT_FILENO){
      fprintf(stderr,"EXECPY CHILD: Could not DUP2 child socket pair to child stdout fd err: %s\n",strerror(errno));        
      exit(EX_OSERR);
    }
    dups++;
  }
  if (fds[0] != STDIN_FILENO){
    if (dup2(fds[0], STDIN_FILENO) != STDIN_FILENO){
      fprintf(stderr,"EXECPY CHILD: Could not DUP2 child socket pair to child stdin fd err: %s\n",strerror(errno));
      exit(EX_OSERR);
    }
    dups++;
  }
  if (dups >= 2)
    //fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    close(fds[0]);

  execvp(filename,argv);

  fprintf(stderr,"EXECVP CHILD FAIL: %s\n",strerror(errno));
  exit(EX_OSERR);
}

#define TEMPBUFFERSIZE 4096 
int parent_process_data(struct e_state *e, char *buf, int size){
  
  char *gotline,*linestart;
  int klfd, lastpos, len;
  gotline   = NULL;
  linestart = NULL;
  lastpos   = 0;
  len       = 0;
  
  //fprintf(stderr,"buff------%s------\n",buf);
  
  do {
    gotline = strchr(buf+lastpos,'\n');
    if (gotline == NULL){
      fprintf(stderr,"EXECPY: End of data -- line continues in next read!! ");
      
      linestart = buf + lastpos;
      len = TEMPBUFFERSIZE - lastpos;

      fprintf(stderr,"EXECPY: len: %d ",len);

      e->cdb = realloc(e->cdb,sizeof(char) * (len+1));
      e->cdb = strncpy(e->cdb,linestart,len);
      e->cdb[len] = '\0';
      e->cdbsize = len;

      fprintf(stderr,"EXECPY PARENT: %s\n",e->cdb);
      fflush(stderr);

      break;
    }
    linestart = buf + lastpos;
    lastpos = (int)(gotline-buf)+1;
    len = (int)(gotline-linestart);

    fprintf(stderr,"EXECPY _%d_ len: %d ",lastpos,len);
   
    if (e->cdbsize > 0) 
      len += e->cdbsize;

    e->cdb = realloc(e->cdb,sizeof(char) * (len+1)); 
    strncpy(e->cdb + e->cdbsize,linestart,len-e->cdbsize);
    e->cdb[len] = '\0';
    e->cdbsize = 0;

    fprintf(stderr,"EXECPY PARENT IO: line: %s\n",e->cdb);
    fflush(stderr);

    log_message_katcl(e->kl,KATCP_LEVEL_INFO,e->filename,"%s",e->cdb);

    klfd = fileno_katcl(e->kl);
    FD_ZERO(&e->outsocks);
    FD_SET(klfd,&e->outsocks);
    if(e->highsock < klfd)
      e->highsock = klfd;

  } while (lastpos != strlen(buf));

  //fprintf(stderr,"\nDone searching no more NLs lastpos:%d\n",lastpos);

  return 0;  
}

int run_parent(struct e_state *e,int *fds){

  pid_t wpid;
  struct sigaction sa;
  int rtn, run=1, status=0, recvb;
 
  char temp_buffer[TEMPBUFFERSIZE];

  close(fds[0]);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  
  fprintf(stderr,"EXECPY PARENT: Parent fd: %d\n",fds[1]);
  
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

  FD_ZERO(&e->outsocks);
  while (run){
    build_socket_set(e);
    rtn = pselect(e->highsock+1,&e->insocks,&e->outsocks,NULL,NULL,&emptyset);
    if (rtn < 0){
      fprintf(stderr,"EXECPY PARENT: PSelect returned an error: %s\n",strerror(errno));
      switch (errno){
        case EAGAIN :
        case EINTR  :
          break;
        default :
          return KATCP_RESULT_FAIL;
      }
    }
    else {
      if (FD_ISSET(fileno_katcl(e->kl),&e->outsocks)){
        fprintf(stderr,"EXECPY PARENT: writing katcl\n");
        rtn = write_katcl(e->kl);  
        FD_CLR(fileno_katcl(e->kl),&e->outsocks);
      }
      if (FD_ISSET(e->fd,&e->insocks)){
        //fprintf(stderr,"\nPARENT got data\n");
        memset(temp_buffer,'\0',TEMPBUFFERSIZE);
        recvb = read(e->fd,temp_buffer,TEMPBUFFERSIZE);
        if (recvb == 0){
          run=0;
          fprintf(stderr,"EXECPY PARENT: Read EOF from client\n");
        }
        else {
          rtn = parent_process_data(e,temp_buffer,recvb);
          //fprintf(stderr,"Parent has %d bytes process returned: %d\n",recvb,rtn);
        }
      }
    }

    if (sawchild) {
      if (e->pid == waitpid(e->pid,&status,WNOHANG)){
        fprintf(stderr,"EXECPY PARENT: In waitpid with WNOHANG\n");
      }
    }
  }
        
  if (!sawchild){
    //fprintf(stderr,"about to wait for SIGCHLD\n");
    wpid = waitpid(e->pid,&status,0);
  }
  
  close(e->fd);
  return status;
}


struct e_state * execpy_exec(char *filename, char **argv, int *status){

  int fds[2];
  pid_t pid;

  struct e_state *e;
  e = malloc(sizeof(struct e_state));
  e->fd       = 0;
  e->highsock = 0;
  e->kl       = NULL;
  e->cdb      = NULL;
  e->cdbsize  = 0;
  e->filename = filename;

  e->kl = create_katcl(STDOUT_FILENO);

  if (e->kl == NULL){
    fprintf(stderr,"EXECPY PARENT: create_katcl fail\n");
    return e; 
  }

  if (socketpair(AF_UNIX, SOCK_STREAM,0, fds) < 0) {
    fprintf(stderr,"EXECPY PARENT: socketpair failed: %s\n",strerror(errno));  
    return e;
  }
  
  fprintf(stderr,"EXECPY PARENT: parent_pid = %d\n",getpid());
 
  pid = fork();
 
  if (pid == 0){
    /*This is the child*/
    run_child(fds,filename,argv);
  }
  else if (pid < 0){
    close(fds[0]);
    close(fds[1]);
    fprintf(stderr,"EXECPY PARENT: fork failed: %s\n",strerror(errno));
    return NULL;
  }

  /*This is the parent*/
  e->pid = pid;
  *status = run_parent(e,fds);
  return e;
}

int execpy_destroy(struct e_state *e){
  if (e != NULL){
    fprintf(stderr,"EXECPY PARENT: Freeing state struct\n");
    
    destroy_katcl(e->kl,0);
    
    if (e->cdb != NULL)
      free(e->cdb);
   
    free(e);
    return KATCP_RESULT_OK;
  }
  return KATCP_RESULT_FAIL;
}


void execpy_do(char * filename, char **argv){
  int status;
  struct e_state *e;
  
  e = execpy_exec(filename, argv, &status);
  
  if (e ==NULL)
    return exit(1);
  
  if(execpy_destroy(e) == KATCP_RESULT_OK){
    fprintf(stderr,"EXECPY PARENT: Mem cleaned\n"); 
  }
  else {
    fprintf(stderr,"EXECPY PARENT: Mem not cleaned\n"); 
  }

  if (WIFEXITED(status)) {
    fprintf(stderr,"EXECPY PARENT: End of program\n");
    exit(WEXITSTATUS(status));
  }
  else if (WIFSIGNALED(status)){
#ifdef WCOREDUMP
    if (WCOREDUMP(status)){
      fprintf(stderr,"EXECPY PARENT: Child Core DUMPed!!\n");
    }
#endif
    kill(getpid(),WTERMSIG(status));
  }
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
  
  fprintf(stderr,"EXECPY MAIN: filename: %s\n",filename);
  
  char *args[] = {filename,NULL};

  execpy_do(filename,args);

  return EX_OK;
}
#endif
