#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <netc.h>
#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

int client_halt_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *client, *group;
  struct katcp_flat *fx;

  if(argc < 2){
    client = NULL;
  } else {
    client = arg_string_katcp(d, 1);
    if(client == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
    }
  }

  if(argc > 2){
    group = arg_string_katcp(d, 3);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
    } 
  } else {
    group = NULL;
  }

  if(client){
    fx = find_name_flat_katcp(d, group, client);
    if(fx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client with name %s", client);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, "usage");
    }
  } else {
    fx = require_flat_katcp(d);
  }

  if(terminate_flat_katcp(d, fx) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to terminate client");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int client_rename_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *from, *to, *group;
  struct katcp_flat *fx;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "usage");
  }

  to = arg_string_katcp(d, 1);
  if(to == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
  }

  if(argc > 2){
    from = arg_string_katcp(d, 2);
    if(from == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire previous name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
    }
  } else {
    fx = require_flat_katcp(d);
    if(fx == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, "usage");
    }
    from = fx->f_name;
  }

  if(argc > 3){
    group = arg_string_katcp(d, 3);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
    } 
  } else {
    group = NULL;
  }

  if(rename_flat_katcp(d, group, from, to) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to rename from %s to %s in within %s group", from, to, group ? group : "any");
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "instance previously called %s now is %s", from, to);

  return KATCP_RESULT_OK;
}

/* group related commands *********************************************************/

int group_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_group *gx;
  struct katcp_shared *s;
  int j, count;

  s = d->d_shared;

  count = 0;

  for(j = 0; j < s->s_members; j++){
    gx = s->s_groups[j];

    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "group %s has %d references and %u members", gx->g_name ? gx->g_name : "<anonymous>", gx->g_use, gx->g_count);

    if(gx->g_name){
      prepend_inform_katcp(d);
      append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, gx->g_name);
      count++;
    }
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%d", count);

}

/* listener related commands ******************************************************/

int listener_create_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *group;
  struct katcp_group *gx;
  struct katcp_shared *s;

  s = d->d_shared;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(s->s_fallback == NULL){
    fprintf(stderr, "listen: expected an initialised fallback group\n");
    abort();
  }
#endif

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  group = arg_string_katcp(d, 2);
  if(group == NULL){
    gx = s->s_fallback;
  } else {
    gx = find_group_katcp(d, group);
  }

  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to find a group to associate with listen on %s", name);
    return KATCP_RESULT_FAIL;
  }

  if(create_listen_flat_katcp(d, name, gx) == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to listen on %s: %s", name, strerror(errno));
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int listener_halt_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name;

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
  }

  if(destroy_listen_flat_katcp(d, name) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to destroy listener instance %s  which might not even exist", name);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "halted listener %s", name);

  return KATCP_RESULT_OK;
}

int print_listener_katcp(struct katcp_dispatch *d, struct katcp_arb *a)
{
  char *name;

  name = name_arb_katcp(d, a);
  if(name == NULL){
    return -1;
  }

  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, name);

  return 0;
}

int listener_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name;
  int count;
  struct katcp_arb *a;

  if(argc > 1){
    name = arg_string_katcp(d, 1);
    if(name == NULL){
      return extra_response_katcp(d, KATCP_RESULT_INVALID, "usage");
    }

    /* a bit too "close" to the internals ... there should be find listener function */
    a = find_type_arb_katcp(d, name, KATCP_ARB_TYPE_LISTENER);
    if(a == NULL){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "no listener %s found", name);
      return KATCP_RESULT_FAIL;
    }

    count = print_listener_katcp(d, a);
  } else {
    count = foreach_arb_katcp(d, KATCP_ARB_TYPE_LISTENER, &print_listener_katcp);
  }

  if(count < 0){
    return KATCP_RESULT_FAIL;

  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%d", count);
}

#endif
