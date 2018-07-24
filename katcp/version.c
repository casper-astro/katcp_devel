/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/utsname.h>

#include <katpriv.h>
#include <katcp.h>

static int locate_version_katcp(struct katcp_dispatch *d, char *label)
{
  int i;
  struct katcp_shared *s;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  for(i = 0; i < s->s_amount; i++){
    if(!strcmp(label, s->s_versions[i]->v_label)){
      return i;
    }
  }

  return -1;
}

static void destroy_version_katcp(struct katcp_dispatch *d, struct katcp_version *v)
{
  if(v == NULL){
    return;
  }

  if(v->v_label){
    free(v->v_label);
    v->v_label = NULL;
  }

  if(v->v_value){
    free(v->v_value);
    v->v_value = NULL;
  }

  if(v->v_build){
    free(v->v_build);
    v->v_build = NULL;
  }

  free(v);
}

static struct katcp_version *allocate_version_katcp(struct katcp_dispatch *d, char *label)
{
  struct katcp_version *v;

  v = malloc(sizeof(struct katcp_version));
  if(v == NULL){
    return NULL;
  }

  v->v_label = strdup(label);
  v->v_mode = 0;
  v->v_value = NULL;
  v->v_build = NULL;

  if(v->v_label == NULL){
    destroy_version_katcp(d, v);
    return NULL;
  }

  return v;
}

static int set_version_katcp(struct katcp_dispatch *d, struct katcp_version *v, unsigned int mode, char *value, char *build)
{
  char *vt, *bt;

  if(build){
    bt = strdup(build);
    if(bt == NULL){
      return -1;
    }
  } else {
    bt = NULL;
  }

  if(value){
    vt = strdup(value);
    if(vt == NULL){
      if(bt){
        free(bt);
      }
      return -1;
    }
  } else {
    vt = NULL;
  }

#if 0
  if(value){
    if(prefix){
      len = strlen(prefix) + 1 + strlen(value) + 1;
      vt = malloc(len);
      if(vt){
        snprintf(vt, len,  "%s.%s", prefix, value);
        vt[len - 1] = '\0';
      }
    } else {
      vt = strdup(value);
    }
    if(vt == NULL){
      if(bt){
        free(bt);
      }
      return -1;
    }
  } else {
    vt = NULL;
  }
#endif

  if(v->v_value){
    free(v->v_value);
    v->v_value = NULL;
  }

  if(v->v_build){
    free(v->v_build);
    v->v_build = NULL;
  }

  v->v_build = bt;
  v->v_value = vt;
  v->v_mode = mode;

  return 0;
}

static int insert_version_katcp(struct katcp_dispatch *d, struct katcp_version *v)
{
  struct katcp_shared *s;
  struct katcp_version **tmp;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  tmp = realloc(s->s_versions, sizeof(struct katcp_version *) * (s->s_amount + 1));
  if(tmp == NULL){
    return -1;
  }

  s->s_versions = tmp;

  s->s_versions[s->s_amount] = v;
  s->s_amount++;

  return 0;
}


/***************************************************************************************/

void destroy_versions_katcp(struct katcp_dispatch *d)
{
  unsigned int i;
  struct katcp_shared *s;

  s = d->d_shared;
  if(s == NULL){
    return;
  }

  if(s->s_versions){

    for(i = 0; i < s->s_amount; i++){
      destroy_version_katcp(d, s->s_versions[i]);
      s->s_versions[i] = NULL;
    }

    s->s_amount = 0;
    free(s->s_versions);
  }
}

int remove_version_katcp(struct katcp_dispatch *d, char *label)
{
  struct katcp_shared *s;
  int pos;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  pos = locate_version_katcp(d, label);
  if(pos < 0){
    return -1;
  }

  destroy_version_katcp(d, s->s_versions[pos]);

  s->s_amount--;
  if(pos < s->s_amount){
    s->s_versions[pos] = s->s_versions[s->s_amount];
  }

  return 0;
}

int add_version_katcp(struct katcp_dispatch *d, char *label, unsigned int mode, char *value, char *build)
{
  int pos;
  struct katcp_version *v;
  struct katcp_shared *s;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  pos = locate_version_katcp(d, label);
  if(pos >= 0){
    /* existing case */
    v = s->s_versions[pos];
    return set_version_katcp(d, v, mode, value, build);
  }

  /* new option */
  v = allocate_version_katcp(d, label);
  if(v){
    if(set_version_katcp(d, v, mode, value, build) == 0){
      if(insert_version_katcp(d, v) == 0){
        return 0;
      }
    }
    destroy_version_katcp(d, v);
  }

  return -1;
}

/****************************************************************************************/

int print_versions_katcp(struct katcp_dispatch *d, int initial)
{
  unsigned int i;
  struct katcp_version *v;
  struct katcp_shared *s;
#if KATCP_PROTOCOL_MAJOR_VERSION >= 5
  char *prefix;
#endif
  int count;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

#if KATCP_PROTOCOL_MAJOR_VERSION >= 5
  switch(initial){
    case KATCP_PRINT_VERSION_CONNECT :
      prefix = KATCP_VERSION_CONNECT_INFORM;
      break;
    case KATCP_PRINT_VERSION_LIST :
      prefix = KATCP_VERSION_LIST_INFORM;
      break;
    case KATCP_PRINT_VERSION :
      prefix = KATCP_VERSION_INFORM;
      break;
    default :
      return -1;
  }
#endif

  count = 0;

  for(i = 0; i < s->s_amount; i++){
    v = s->s_versions[i];

#if KATCP_PROTOCOL_MAJOR_VERSION <= 4
    if(i == 0){
      switch(initial){
        case KATCP_PRINT_VERSION_CONNECT : 
        case KATCP_PRINT_VERSION : 

          append_string_katcp(d,   KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_VERSION_INFORM);
          append_string_katcp(d,   KATCP_FLAG_LAST  | KATCP_FLAG_STRING, v->v_value);
          if(v->v_build){
            append_string_katcp(d,   KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_BUILD_STATE_INFORM);
            append_string_katcp(d,   KATCP_FLAG_LAST  | KATCP_FLAG_STRING, v->v_build);
          }
          count++;
          break;
      }
    }
#endif

#if KATCP_PROTOCOL_MAJOR_VERSION >= 5
    switch(initial){
      case KATCP_PRINT_VERSION_CONNECT : 
      case KATCP_PRINT_VERSION_LIST : 
        if(v->v_value && ((v->v_mode == 0) || (v->v_mode == s->s_mode))){


          append_string_katcp(d,   KATCP_FLAG_FIRST | KATCP_FLAG_STRING, prefix);
          append_string_katcp(d,                      KATCP_FLAG_STRING, v->v_label);
          if(v->v_build == NULL){
            append_string_katcp(d, KATCP_FLAG_LAST  | KATCP_FLAG_STRING, v->v_value);
          } else {
            append_string_katcp(d,                    KATCP_FLAG_STRING, v->v_value);
            append_string_katcp(d, KATCP_FLAG_LAST  | KATCP_FLAG_STRING, v->v_build);
          }
          count++;
        }
        break;
    }
#endif
  }

  return count;
}

int add_kernel_version_katcp(struct katcp_dispatch *d)
{
  struct utsname u;

  if(uname(&u) < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to acquire host kernel version information");
    return -1;
  }

  return add_version_katcp(d, "kernel", 0, u.release, u.version);
}

int add_code_version_katcp(struct katcp_dispatch *d)
{
#define BUFFER 128
  struct katcp_shared *s;
  char buffer[BUFFER];
  int result;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  result = 0;

#ifdef VERSION
#ifdef BUILD
  result += add_version_katcp(d, KATCP_LIBRARY_LABEL, 0, VERSION, BUILD);
#else
  result += add_version_katcp(d, KATCP_LIBRARY_LABEL, 0, VERSION, NULL);
#endif
#endif

  if(s->s_count > 1){
    snprintf(buffer, BUFFER - 1, "%d.%d-%c", KATCP_PROTOCOL_MAJOR_VERSION, KATCP_PROTOCOL_MINOR_VERSION, 'M');
  } else {
    snprintf(buffer, BUFFER - 1, "%d.%d", KATCP_PROTOCOL_MAJOR_VERSION, KATCP_PROTOCOL_MINOR_VERSION);
  }
  buffer[BUFFER - 1] = '\0';

  result += add_version_katcp(d, KATCP_PROTOCOL_LABEL, 0, buffer, NULL);

  return result;
#undef BUFFER
}

int has_code_version_katcp(struct katcp_dispatch *d, char *label, char *value)
{
  int pos, result;
  struct katcp_version *v;
  struct katcp_shared *s;

  pos = locate_version_katcp(d, label);
  if(pos < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "no version information for module %s registered", label);
    return -1;
  }

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  v = s->s_versions[pos];

  result = strcmp(v->v_value, value);

  if(result){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "module %s does not have version %s but %s instead", label, value, v->v_value);
  } else {
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "module %s has desired version %s", label, value);
  }

  return (result == 0) ? 0 : 1;
}

int version_list_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  int count;

  s = d->d_shared;
  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  count = print_versions_katcp(d, KATCP_PRINT_VERSION_LIST);
  if(count < 0){
    return KATCP_RESULT_FAIL;
  }

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_unsigned_long_katcp(d, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, count);

  return KATCP_RESULT_OWN;
}

int version_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *op, *label, *value, *mode, *build;
  struct katcp_shared *s;
  int md;

  s = d->d_shared;
  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  op = arg_string_katcp(d, 1);
  if(op == NULL){
    print_versions_katcp(d, KATCP_PRINT_VERSION);
    return KATCP_RESULT_OK;
  }

  if(!strcmp(op, "add")){
    label = arg_string_katcp(d, 2);
    value = arg_string_katcp(d, 3);
    build = arg_string_katcp(d, 4);
    mode = arg_string_katcp(d, 5);

    if((label == NULL) || (value == NULL)){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "insufficient parameters for version add");
      return KATCP_RESULT_FAIL;
    }

    if(mode){
      md = query_mode_code_katcp(d, mode);
      if(md < 0){
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unknown mode %s", mode);
        return KATCP_RESULT_FAIL;
      }
    } else {
      md = 0;
#if 0
      md = s->s_mode;
#endif
    }

    if(add_version_katcp(d, label, md, value, build)){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to add version for module %s", label);
      return KATCP_RESULT_FAIL;
    }

    return KATCP_RESULT_OK;

  } else if(!strcmp(op, "remove")){
    label = arg_string_katcp(d, 2);

    if(label == NULL){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "need a label to remove version field");
      return KATCP_RESULT_FAIL;
    }

    if(remove_version_katcp(d, label) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to remove version for module %s", label);
      return KATCP_RESULT_FAIL;
    }

    return KATCP_RESULT_OK;
  } 

  return KATCP_RESULT_FAIL;
}

