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

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  for(i = 0; i < s->s_amount; i++){
    v = s->s_versions[i];

    if(v->v_value && ((v->v_mode == 0) || (v->v_mode == s->s_mode))){
      append_string_katcp(d,   KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#version");
      append_string_katcp(d,                      KATCP_FLAG_STRING, v->v_label);
      if(v->v_build == NULL){
        append_string_katcp(d, KATCP_FLAG_LAST  | KATCP_FLAG_STRING, v->v_value);
      } else {
        append_string_katcp(d,                    KATCP_FLAG_STRING, v->v_value);
        append_string_katcp(d, KATCP_FLAG_LAST  | KATCP_FLAG_STRING, v->v_build);
      }
    }
  }

  return 0;
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
    print_versions_katcp(d, 0);
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

