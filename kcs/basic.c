#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <katcp.h>

#include "kcs.h"

int script_wildcard_resume(struct katcp_dispatch *d, struct katcp_notice *n)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "should extract status code at this point");

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_LAST, KATCP_OK);

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

  snprintf(path, len, "%s/%s.py", kb->b_scripts, name + 1);

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

  j = process_create_job_katcp(d, path, vector, n, NULL);

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

  if(kb->b_scripts == NULL){
    free(kb->b_scripts);
    kb->b_scripts = NULL;
  }

  free(kb);
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

  kb->b_scripts = NULL;

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
  if(result < 0){
    fprintf(stderr, "setup: unable to register command handlers for basic mode\n");
  }

  return 0;
}


