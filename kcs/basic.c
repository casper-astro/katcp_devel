#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <sys/socket.h>
#include <fcntl.h>
#include <sysexits.h>
#include <errno.h>
#include <katcl.h>

#include <katcp.h>
#include <katpriv.h>

#include "kcs.h"

struct katcp_job *wrapper_process_create_job_katcp(struct katcp_dispatch *d, char *file, char **argv, struct katcp_notice *halt)
{
  int fds[2];
  pid_t pid;
  int copies;
  struct katcl_line *xl;
  struct katcp_job *j;

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0){
    return NULL;
  }

  pid = fork();
  if(pid < 0){
    close(fds[0]);
    close(fds[1]);
    return NULL;
  }

  if(pid > 0){
    close(fds[0]);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    j = create_job_katcp(d, file, pid, fds[1], halt);
    if(j == NULL){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to allocate job logic so terminating child process");
      kill(pid, SIGTERM);
      close(fds[1]);
    }

    return j;
  }

  /* WARNING: now in child, do not call return, use exit */

  xl = create_katcl(fds[0]);

  close(fds[1]);

  copies = 0;
  if(fds[0] != STDOUT_FILENO){
    if(dup2(fds[0], STDOUT_FILENO) != STDOUT_FILENO){
      sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to set up standard output for child process %u (%s)", getpid(), strerror(errno)); 
      exit(EX_OSERR);
    }
    copies++;
  }
  if(fds[0] != STDIN_FILENO){
    if(dup2(fds[0], STDIN_FILENO) != STDIN_FILENO){
      sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to set up standard input for child process %u (%s)", getpid(), strerror(errno)); 
      exit(EX_OSERR);
    }
    copies++;
  }
  if(copies >= 2){
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  }

  //execvp(file, argv);
  execpy_do(file, argv);
  sync_message_katcl(xl, KATCP_LEVEL_ERROR, NULL, "unable to run command %s (%s)", file, strerror(errno)); 

  destroy_katcl(xl, 0);

  exit(EX_OSERR);
  return NULL;
}

int script_wildcard_resume(struct katcp_dispatch *d, struct katcp_notice *n)
{
  char *ptr;
  struct katcl_parse *p;

  p = parse_notice_katcp(d, n);
  if(p){
    ptr = get_string_parse_katcl(p, 1);
  } else {
    ptr = NULL;
  }

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_LAST, ptr ? ptr : KATCP_FAIL);

  resume_katcp(d);

  return 0;
}

int script_wildcard_cmd(struct katcp_dispatch *d, int argc)
{
  struct katcp_notice *n;
  struct katcp_job *j;

  struct kcs_basic *kb;

  struct stat st;
  char *name, *path, **vector;
  int len, i;

#ifdef DEBUG
  fprintf(stderr, "script cmd: dispatch is %p\n", d);
#endif

  kb = need_current_mode_katcp(d, KCS_MODE_BASIC);

  name = arg_string_katcp(d, 0);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "logic problem, unable to acquire command name");
    return KATCP_RESULT_FAIL;
  }

  len = strlen(name);
  if(len <= 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "command unreasonably short");
    return KATCP_RESULT_FAIL;
  }

  len += strlen(kb->b_scripts);
  len += 5;

  path = malloc(len);
  if(path == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to allocate %d bytes", len);
    return KATCP_RESULT_FAIL;
  }

#if 0
  snprintf(path, len, "%s/%s.py", kb->b_scripts, name + 1);
#endif
  snprintf(path,len, "%s/%s",kb->b_scripts, name + 1);

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "checking if %s exists", path);

  if(stat(path, &st) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to stat %s", path);
    free(path);
    return KATCP_RESULT_FAIL;
  }

  n = find_notice_katcp(d, KCS_NOTICE_PYTHON);
  if(n != NULL){
    free(path);
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "it appears another task of type %s is already operational", KCS_NOTICE_PYTHON);
    return KATCP_RESULT_FAIL;
  }

  n = create_notice_katcp(d, KCS_NOTICE_PYTHON, 0);
  if(n == NULL){
    free(path);
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create notice for type %s", KCS_NOTICE_PYTHON);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "allocating vector of %d arguments", argc + 1);

  vector = malloc(sizeof(char *) * (argc + 1));
  if(vector == NULL){
    free(path);
#if 0
    destroy_notice_katcp(d, n);
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate vector for %d arguments", argc + 1);
    return KATCP_RESULT_FAIL;
  }

  for(i = 0; i < argc; i++){
    vector[i] = arg_string_katcp(d, i);
  }
  vector[i] = NULL;

#if 0 
  j = process_create_job_katcp(d, path, vector, n);
#endif
#if 1 
  j = wrapper_process_create_job_katcp(d, path, vector, n);
#endif
  free(vector);
  free(path);

  if(j == NULL){
#if 0
    destroy_notice_katcp(d, n);
#endif
    return KATCP_RESULT_FAIL;
  }

  if(add_notice_katcp(d, n, &script_wildcard_resume)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to watch notice %s", KCS_NOTICE_PYTHON);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_PAUSE;
}

int enter_basic_kcs(struct katcp_dispatch *d, char *flags, unsigned int from)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "operating in basic mode");

  return KCS_MODE_BASIC;
}

void destroy_basic_kcs(struct katcp_dispatch *d)
{
  struct kcs_basic *kb;

  kb = get_mode_katcp(d, KCS_MODE_BASIC);
  if(kb == NULL){
    return;
  }

  if(kb->b_scripts != NULL){
    free(kb->b_scripts);
    kb->b_scripts = NULL;
  }

  if (kb->b_parser != NULL){
    fprintf(stderr,"FREE the Parser object\n");
    parser_destroy(d);
    kb->b_parser = NULL;
  }

  if (kb->b_pool_head != NULL){
    roachpool_destroy(d);
    kb->b_pool_head = NULL;
  }

  free(kb);
}



int parser_cmd(struct katcp_dispatch *d, int argc){

  char *p_cmd;

  if (argc == 1){
    prepend_inform_katcp(d);
    append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"load [filename]");
    prepend_inform_katcp(d);
    append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"save [filename]"); 
    prepend_inform_katcp(d);
    append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"get [label] [setting] [value index]");
    prepend_inform_katcp(d);
    append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"set [label] [setting] [value index] [new value]");
    prepend_inform_katcp(d);
    append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST ,"list");
    return KATCP_RESULT_OK;
  }
  else if (argc == 3) {
    p_cmd = arg_string_katcp(d,1);

    if (strcmp("load",p_cmd) == 0){
      return parser_load(d,arg_string_katcp(d,2)); 
    }
    else if (strcmp("save",p_cmd) == 0){
      return parser_save(d,arg_string_katcp(d,2),0);
    }

  }
  else if (argc == 2){
    p_cmd = arg_string_katcp(d,1);
    if (strcmp("list",p_cmd) == 0){
      return parser_list(d);
    }
    else if (strcmp("save",p_cmd) == 0){
      return parser_save(d,NULL,0);
    }
    else if (strcmp("forcesave",p_cmd) ==0){
      return parser_save(d,NULL,1);
    }
  }
  else if (argc == 5){
    p_cmd = arg_string_katcp(d,1);
    if (strcmp("get",p_cmd) == 0){
      struct p_value *tval;
      tval = parser_get(d,arg_string_katcp(d,2),arg_string_katcp(d,3),arg_unsigned_long_katcp(d,4));
      if (tval != NULL){
        log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"%s",tval->str);
        return KATCP_RESULT_OK;
      }
      else
        return KATCP_RESULT_FAIL;
    }
  }
  else if (argc == 6){
    p_cmd = arg_string_katcp(d,1);
    if (strcmp("set",p_cmd) == 0){
      return parser_set(d,arg_string_katcp(d,2),arg_string_katcp(d,3),arg_unsigned_long_katcp(d,4),arg_string_katcp(d,5));
    }
  }
  
  return KATCP_RESULT_FAIL;
}


int roach_cmd(struct katcp_dispatch *d, int argc){
  char *p_cmd;

  switch (argc){
    case 1:
      return roachpool_greeting(d);        
    case 2:
      p_cmd = arg_string_katcp(d,1);
      if (strcmp("start-pool",p_cmd) == 0){
      
      } else if(strcmp("stop-pool",p_cmd) == 0){
        
      } else if (strcmp("list",p_cmd) == 0){
        return roachpool_list(d); 
      }
      return KATCP_RESULT_FAIL;
    case 3:
      p_cmd = arg_string_katcp(d,1);
      if (strcmp("del",p_cmd) == 0){
 
      } else if (strcmp("start",p_cmd) == 0){

      } else if (strcmp("stop",p_cmd) == 0){

      } else if (strcmp("get-conf",p_cmd) == 0){
        return roachpool_getconf(d);
      }
      return KATCP_RESULT_FAIL;
    case 4:
      p_cmd = arg_string_katcp(d,1);
      if (strcmp("mod",p_cmd) == 0){
        return roachpool_mod(d);
      }
      return KATCP_RESULT_FAIL;
    case 5:
      p_cmd = arg_string_katcp(d,1);
      if (strcmp("add",p_cmd) == 0){
        return roachpool_add(d);
      }
      return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_FAIL;
}

int k7_resume_job(struct katcp_dispatch *d, struct katcp_notice *n){
  struct katcl_parse *p;
  char *ptr;

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL,"remote has responed on job via notice %p", n);

  p = parse_notice_katcp(d,n);

  if (p) {
    ptr = get_string_parse_katcl(p,1);
#ifdef DEBUG
    fprintf(stderr,"k7-resume: parameter %d is %s\n", 1, ptr);
#endif    
  } else {
    ptr = NULL;
  }
  
  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_LAST, ptr ? ptr : KATCP_FAIL);

  resume_katcp(d);
  return 0;

}

int k7_snap_shot_cmd(struct katcp_dispatch *d, int argc){
#define JOBLABEL "localhost"
  struct katcp_job *j;
  struct katcl_parse *p;

  if (argc != 2)
    return KATCP_RESULT_FAIL;
  
  j = find_job_katcp(d, JOBLABEL);
  if (j == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to find job labelled %s", JOBLABEL);
    return KATCP_RESULT_FAIL;
  }
  
  p = create_parse_katcl();
  if (p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create message");
    return KATCP_RESULT_FAIL;
  }
    
  if (add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, "?k7-snap-shot") < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to assemble message");
    destroy_parse_katcl(p);
    return KATCP_RESULT_FAIL;
  }

  if (submit_to_job_katcp(d,j,p, &k7_resume_job) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to submit message to job");
    destroy_parse_katcl(p);
    return KATCP_RESULT_FAIL;
  }
  
  return KATCP_RESULT_PAUSE;
#undef JOBLABEL
}

int setup_basic_kcs(struct katcp_dispatch *d, char *scripts)
{
  struct kcs_basic *kb;
  int result;

  if(scripts == NULL){
    return -1;
  }

  kb = malloc(sizeof(struct kcs_basic));
  if(kb == NULL){
    return -1;
  }

  kb->b_scripts    = NULL;
  kb->b_parser     = NULL;
  kb->b_pool_head  = NULL;
  
  kb->b_scripts = strdup(scripts);
  if(kb->b_scripts == NULL){
    free(kb);
    return -1;
  }

  /* TODO: trim out trailing / to make things look neater */
  if(store_full_mode_katcp(d, KCS_MODE_BASIC, KCS_MODE_BASIC_NAME, &enter_basic_kcs, NULL, kb, &destroy_basic_kcs) < 0){
    fprintf(stderr, "setup: unable to register basic mode\n");
    destroy_basic_kcs(d);
    return -1;
  }

  result = 0;

  result += register_flag_mode_katcp(d, NULL, "python script handler", &script_wildcard_cmd, KATCP_CMD_HIDDEN | KATCP_CMD_WILDCARD, KCS_MODE_BASIC);
  result += register_flag_mode_katcp(d, "?parser" , "ROACH Configuration file parser (?parser [load|save|get|set|list])", &parser_cmd, 0, KCS_MODE_BASIC);
  result += register_flag_mode_katcp(d, "?roach" , "Control the pool of roaches (?roach [add|del|start|stop|start-pool|stop-pool])", &roach_cmd, 0, KCS_MODE_BASIC);
  result += register_flag_mode_katcp(d, "?k7-snap-shot" , "Grab a snap shot (?k7-snap-shot [antenna polarisation])", &k7_snap_shot_cmd, 0, KCS_MODE_BASIC);
  
  if(result < 0){
    fprintf(stderr, "setup: unable to register command handlers for basic mode\n");
    return result;
  }

  return 0;
}
