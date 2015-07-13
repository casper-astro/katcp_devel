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

/* client stuff *******************************************************************/

int client_exec_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *label, *group;
  struct katcp_flat *fx;
  struct katcp_group *gx;
  int i, size;
  char **vector;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a command");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  label = arg_string_katcp(d, 1);
  if(label == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc > 2){
    group = arg_string_katcp(d, 2);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    } 
    gx = find_group_katcp(d, group);
    if(gx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate group called %s", group);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }
  } else {
    gx = this_group_katcp(d);
  }

  if(argc > 3){
    vector = malloc(sizeof(char *) * (argc - 2));
    if(vector == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %u element vector", argc - 2);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
    }
    size = argc - 3;
    for(i = 0; i < size; i++){
      vector[i] = arg_string_katcp(d, i + 3);
#ifdef DEBUG
      fprintf(stderr, "exec: vector[%u]=%s\n", i, vector[i]);
#endif
    }
    vector[i] = NULL;
  } else {
    vector = NULL;
  }

  fx = create_exec_flat_katcp(d, KATCP_FLAT_TOSERVER | KATCP_FLAT_TOCLIENT | KATCP_FLAT_PREFIXED, label, gx, vector);
  if(vector){
    free(vector);
  }

  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate client connection");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  return KATCP_RESULT_OK;
}

int client_connect_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *client, *group;
  struct katcp_group *gx;
  int fd;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a destination to connect to");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  client = arg_string_katcp(d, 1);
  if(client == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc > 2){
    group = arg_string_katcp(d, 2);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    } 
    gx = find_group_katcp(d, group);
    if(gx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate group called %s", group);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }
  } else {
    gx = this_group_katcp(d);
  }

  fd = net_connect(client, 0, NETC_ASYNC);
  if(fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to initiate connection to %s: %s", client, errno ? strerror(errno) : "unknown error");
    return KATCP_RESULT_FAIL;
  }

  fcntl(fd, F_SETFD, FD_CLOEXEC);

  if(create_flat_katcp(d, fd, KATCP_FLAT_CONNECTING | KATCP_FLAT_TOSERVER | KATCP_FLAT_PREFIXED, client, gx) == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate client connection");
    close(fd);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  return KATCP_RESULT_OK;
}

int client_config_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *option, *client;
  unsigned int mask, set;
  struct katcp_flat *fx, *fy;

  fy = this_flat_katcp(d);
  if(fy == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client scope available");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need an option");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  option = arg_string_katcp(d, 1);
  if(option == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire a flag");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }
  
  if(argc > 2){
    client = arg_string_katcp(d, 2);
    if(client == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire client name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    } 
    fx = scope_name_full_katcp(d, NULL, NULL, client);
    if(fx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate client %s", client);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }
  } else {
    fx = fy;
  }

  set  = 0;
  mask = 0;

  if(!strcmp(option, "duplex")){
    set   = KATCP_FLAT_TOSERVER | KATCP_FLAT_TOCLIENT;
  } else if(!strcmp(option, "server")){
    mask   = KATCP_FLAT_TOSERVER;
    set    = KATCP_FLAT_TOCLIENT;
  } else if(!strcmp(option, "client")){
    mask   = KATCP_FLAT_TOCLIENT;
    set    = KATCP_FLAT_TOSERVER;
  } else if(!strcmp(option, "hidden")){
    set    = KATCP_FLAT_HIDDEN;
  } else if(!strcmp(option, "visible")){
    mask   = KATCP_FLAT_HIDDEN;
  } else if(!strcmp(option, "prefixed")){
    set    = KATCP_FLAT_PREFIXED;
  } else if(!strcmp(option, "fixed")){
    mask   = KATCP_FLAT_PREFIXED;
  } else if(!strcmp(option, "translate")){
    mask   = KATCP_FLAT_RETAINFO;
  } else if(!strcmp(option, "native")){
    set    = KATCP_FLAT_RETAINFO;
  } else {
    /* WARNING: does not error out in an effort to be forward compatible */
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unknown configuration option %s", option);
    return KATCP_RESULT_OK;
  }

  if(reconfigure_flat_katcp(d, fx, (fx->f_flags & (~mask)) | set) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to change flags on client %s", fx->f_name);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int client_halt_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *client, *group;
  struct katcp_flat *fx;

  client = NULL;
  group = NULL;

  if(argc > 1){
    client = arg_string_katcp(d, 1);
    if(client == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
    if(argc > 2){
      group = arg_string_katcp(d, 2);
      if(group == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
      } 
    }
  }

  if(client){
    fx = scope_name_full_katcp(d, NULL, group, client);
    if(fx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client with name %s", client);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
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

int client_switch_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *client, *group;
  struct katcp_flat *fx;
  struct katcp_group *gx;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  group = arg_string_katcp(d, 1);
  if(group == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  gx = find_group_katcp(d, group);
  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group with name %s", group);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  if(argc > 2){
    client = arg_string_katcp(d, 2);
    if(client == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire client name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    } 
    fx = scope_name_full_katcp(d, NULL, NULL, client);
    if(fx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client with name %s", client);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }
  } else {
    client = NULL;
    fx = this_flat_katcp(d);
    if(fx == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
  }

  if(switch_group_katcp(d, fx, gx) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to transfer client to group %s", group);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  return KATCP_RESULT_OK;
}

int client_rename_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *from, *to, *group;
  struct katcp_flat *fx;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  to = arg_string_katcp(d, 1);
  if(to == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc > 2){
    from = arg_string_katcp(d, 2);
    if(from == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire previous name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
  } else {
    fx = require_flat_katcp(d);
    if(fx == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_API);
    }
    from = fx->f_name;
  }

  if(argc > 3){
    group = arg_string_katcp(d, 3);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
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

int group_create_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *group, *tmp;
  struct katcp_group *go, *gx;
  struct katcp_shared *s;
  int depth, i;

  s = d->d_shared;
  depth = 1;

  if(argc <= 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a new group name");
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a group name");
    return KATCP_RESULT_FAIL;
  }

  if(find_group_katcp(d, name)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "group %s already exists", name);
    return KATCP_RESULT_FAIL;
  }

  group = arg_string_katcp(d, 2);
  if(group == NULL){
    go = s->s_fallback;
  } else {
    go = find_group_katcp(d, group);
    if(go == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "template group %s not found", group);
      return KATCP_RESULT_FAIL;
    }
  }

  for(i = 3; i < argc; i++){
    tmp = arg_string_katcp(d, i);
    if(tmp){
      if(!strcmp(tmp, "linked")){
        depth = 0;
      }
    }
  }

  gx = duplicate_group_katcp(d, go, name, depth);
  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to duplicate group as %s", name);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int group_halt_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_group *gx;
  char *name;

  if(argc <= 1){
    gx = this_group_katcp(d);
    if(gx){
      if(terminate_group_katcp(d, gx, 1) == 0){
        return KATCP_RESULT_OK;
      } else {
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to terminate %s", gx->g_name);
      }
    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group available in this context");
    }
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to retrieve group name");
    return KATCP_RESULT_FAIL;
  }

  gx = find_group_katcp(d, name);
  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group %s found", name);
    return KATCP_RESULT_FAIL;
  }

  if(terminate_group_katcp(d, gx, 1)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to terminate group %s", name);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int group_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_group *gx;
  struct katcp_shared *s;
  struct katcp_cmd_map *mx;
  int j, count, i;
  char *ptr, *group, *name;

  s = d->d_shared;

  count = 0;

  if(argc >= 2){
    name = arg_string_katcp(d, 1);
    if(name == NULL){
      return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
    }
  } else {
    name = NULL;
  }

  for(j = 0; j < s->s_members; j++){
    gx = s->s_groups[j];

    if(gx->g_name){
      group = gx->g_name;
      if(name && strcmp(group, name)){
        /* WARNING */
        continue;
      }
    } else {
      group = "<anonymous>";
      if(name){
        /* WARNING */
        continue;
      }
    }

    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s has %d references", group, gx->g_use);
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s has %u members", group, gx->g_count);
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s will %s if not used", group, gx->g_autoremove ? "disappear" : "persist");

    if(gx->g_flags & KATCP_GROUP_OVERRIDE_SENSOR){
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s forces sensor names to be %s", group, (gx->g_flags & KATCP_FLAT_PREFIXED) ? "prefixed" : "without prefix");
    } else {
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s leaves sensor prefixing decision to client creation context", group);
    }

    ptr = log_to_string_katcl(gx->g_log_level);
    if(ptr){
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s sets client log level to %s", group, ptr);
    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "group %s has unreasonable log level", group);
    }

    ptr = string_from_scope_katcp(gx->g_scope);
    if(ptr){
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s sets %s client scope", group, ptr);
    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "group %s has invalid scope setting", group);
    }

    for(i = 0; i < KATCP_SIZE_MAP; i++){
      mx = gx->g_maps[i];
      if(mx){
        log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s command map %d has %s %s and is referenced %d times", group, i, mx->m_name ? "name" : "no", mx->m_name ? mx->m_name : "name", mx->m_refs);
      }
    }

    if(gx->g_name){
      prepend_inform_katcp(d);
      append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, gx->g_name);
      count++;
    }
  }

  if((count == 0) && (name != NULL)){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%d", count);
}

int group_config_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcp_group *gx;
  struct katcp_shared *s;
  char *option, *group;
  unsigned int mask, set;

  s = d->d_shared;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client scope available");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "prefixed option forces name prefix in sensors");
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "fixed option inhibits name prefix in sensors");
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "flexible option decides does not force sensor prefix");
    return KATCP_RESULT_FAIL;
  }

  option = arg_string_katcp(d, 1);
  if(option == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire a flag");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }
  
  if(argc > 2){
    group = arg_string_katcp(d, 2);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    } 
    gx = scope_name_group_katcp(d, group, fx);
  } else {
    gx = this_group_katcp(d);
    group = NULL;
  }

  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate group %s", group ? group : "of current client");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  set  = 0;
  mask = 0;

  if(!strcmp(option, "prefixed")){        /* prefix sensor paths */
    set    = (KATCP_GROUP_OVERRIDE_SENSOR | KATCP_FLAT_PREFIXED);
  } else if(!strcmp(option, "fixed")){    /* make sensor paths absolute */
    set    = KATCP_GROUP_OVERRIDE_SENSOR;
    mask   = KATCP_FLAT_PREFIXED;      
  } else if(!strcmp(option, "flexible")){ /* pick whatever the calling logic prefers */
    mask   = (KATCP_GROUP_OVERRIDE_SENSOR | KATCP_FLAT_PREFIXED);
  } else {
    /* WARNING: does not error out in an effort to be forward compatible */
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown configuration option %s", option);
    return KATCP_RESULT_OK;
  }

  gx->g_flags = (gx->g_flags & (~mask)) | set;

  return KATCP_RESULT_OK;
}

/* listener related commands ******************************************************/

int listener_create_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *group, *address;
  unsigned int port;
  struct katcp_group *gx;
  struct katcp_shared *s;

  s = d->d_shared;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(s->s_fallback == NULL){
    fprintf(stderr, "listen: expected an initialised fallback group\n");
    abort();
  }
#endif

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a label");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  if(find_type_arb_katcp(d, name, KATCP_ARB_TYPE_LISTENER)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "listener with name %s already exists", name);
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  port = 0;
  address = NULL;
  group = NULL;

  if(argc > 2){
    port = arg_unsigned_long_katcp(d, 2);
    if(argc > 3){
      address = arg_string_katcp(d, 3);
      if(argc > 4){
        group = arg_string_katcp(d, 4);
      }
    }
  }

  if(group){
    gx = find_group_katcp(d, group);
    if(gx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group with name %s found", group);
      return KATCP_RESULT_FAIL;
    }
  } else {
    gx = find_group_katcp(d, name);
    if(gx == NULL){
      gx = s->s_fallback;
      if(gx == NULL){
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no default group available");
        return KATCP_RESULT_FAIL;
      }
    }
  }

  if(create_listen_flat_katcp(d, name, port, address, gx) == NULL){
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
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  if(destroy_listen_flat_katcp(d, name) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to destroy listener instance %s which might not even exist", name);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "halted listener %s", name);

  return KATCP_RESULT_OK;
}

int print_listener_katcp(struct katcp_dispatch *d, struct katcp_arb *a, void *data)
{
  char *name, *extra;
  struct katcp_listener *kl;
  struct katcp_group *gx;

  name = name_arb_katcp(d, a);
  if(name == NULL){
    return -1;
  }

  kl = data_arb_katcp(d, a);
  if(kl == NULL){
    return -1;
  }

  prepend_inform_katcp(d);

  extra = NULL;

  if(kl->l_group){
    gx = kl->l_group;
    if(gx->g_name && strcmp(gx->g_name, name)){
      extra = gx->g_name;
    }
  }

  append_string_katcp(d, KATCP_FLAG_STRING, name);

  if(extra || kl->l_address){
    append_unsigned_long_katcp(d, KATCP_FLAG_ULONG, kl->l_port);
    append_string_katcp(d, KATCP_FLAG_STRING | (extra ? 0 : KATCP_FLAG_LAST), kl->l_address ? kl->l_address : "0.0.0.0");
    if(extra){
      append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, extra);
    }
  } else {
    append_unsigned_long_katcp(d, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, kl->l_port);
  }

  return 0;
}

int listener_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name;
  int count;
  struct katcp_arb *a;

  name = NULL;

  if(argc > 1){
    name = arg_string_katcp(d, 1);
    if(name == NULL){
      return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
    }

    /* a bit too "close" to the internals ... there should be find listener function */
    a = find_type_arb_katcp(d, name, KATCP_ARB_TYPE_LISTENER);
    if(a == NULL){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "no listener %s found", name);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }

    if(print_listener_katcp(d, a, NULL) < 0){
      count = (-1);
    } else {
      count = 0;
    }
  } else {
    count = foreach_arb_katcp(d, KATCP_ARB_TYPE_LISTENER, &print_listener_katcp, NULL);
  }

  if(count < 0){
    return KATCP_RESULT_FAIL;
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%d", count);
}

/* command/map related commands ***************************************************/

#define CMD_OP_REMOVE  0
#define CMD_OP_FLAG    1
#define CMD_OP_HELP    2

static int configure_cmd_group_katcp(struct katcp_dispatch *d, int argc, int op, unsigned int flags)
{
  struct katcp_cmd_map *mx;
  struct katcp_cmd_item *ix;
  struct katcp_flat *fx;
  char *name, *help;

  if(argc <= 1){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  fx = this_flat_katcp(d);
  if(fx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "interesting failure: not called within a valid duplex context\n");
    abort();
#endif
    return KATCP_RESULT_FAIL;
  }

  mx = map_of_flat_katcp(fx);
  if(mx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "interesting failure: no map available\n");
    abort();
#endif
    return KATCP_RESULT_FAIL;
  }

  switch(op){
    case CMD_OP_REMOVE :
      if(remove_cmd_map_katcp(mx, name) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "removal of command %s failed", name);
        return KATCP_RESULT_FAIL;
      } else {
        return KATCP_RESULT_OK;
      }
      break;
    case CMD_OP_HELP :
      ix = find_cmd_map_katcp(mx, name);
      if(ix == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no command %s found", name);
        return KATCP_RESULT_FAIL;
      }

      help = arg_string_katcp(d, 2);
      if(help == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a valid help string for %s", name);
        return KATCP_RESULT_FAIL;
      }

      if(set_help_cmd_item_katcp(ix, help) < 0){
        return KATCP_RESULT_FAIL;
      }

      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "updated help message for %s held %d times", name, ix->i_refs);

      return KATCP_RESULT_OK;
    case CMD_OP_FLAG :
      ix = find_cmd_map_katcp(mx, name);
      if(ix == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no command %s found", name);
        return KATCP_RESULT_FAIL;
      }

      set_flag_cmd_item_katcp(ix, flags);

      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "change on command used %d times", ix->i_refs);

      return KATCP_RESULT_OK;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal usage problem while configuring a command");
      return KATCP_RESULT_FAIL;
  }

}

int hide_cmd_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return configure_cmd_group_katcp(d, argc, CMD_OP_FLAG, KATCP_MAP_FLAG_HIDDEN);
}

int uncover_cmd_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return configure_cmd_group_katcp(d, argc, CMD_OP_FLAG, 0);
}

int delete_cmd_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return configure_cmd_group_katcp(d, argc, CMD_OP_REMOVE, 0);
}

int help_cmd_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{

  return configure_cmd_group_katcp(d, argc, CMD_OP_HELP, 0);
}

/* scope commands ***/

int scope_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *ptr;
  struct katcp_flat *fx;
  struct katcp_group *gx;
  int scope;

  if(argc <= 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  ptr = arg_string_katcp(d, 1);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "null parameters");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  scope = code_from_scope_katcp(ptr);
  if(scope < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown scope %s", ptr);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  /* WARNING: the *_scope variables should probably be changed using accessor functions, but not used sufficiently often */

  if(argc > 3){
    ptr = arg_string_katcp(d, 2);
    name = arg_string_katcp(d, 3);

    if((ptr == NULL) || (name == NULL)){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient or null parameters");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }

    if(!strcmp(ptr, "client")){
      fx = scope_name_full_katcp(d, NULL, NULL, name);
      if(fx == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "client %s not found", name);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      }

      fx->f_scope = scope;

    } else if(!strcmp(ptr, "group")){
      gx = find_group_katcp(d, name);
      if(gx == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "group %s not found", name);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      }

      gx->g_scope = scope;

    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown scope extent %s", ptr);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }

  } else {
    fx = this_flat_katcp(d);
    if(fx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no current client available");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_API);
    }

    fx->f_scope = scope;
  }

  return KATCP_RESULT_OK;
}

/*********************************************************************************/

int broadcast_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *inform, *group;
  struct katcl_parse *px, *py;
  struct katcp_flat *fx;
  struct katcp_group *gx;
  char *ptr;

  py = arg_parse_katcp(d);
  if(py == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "interesting internal problem: unable to acquire current parser message");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem: broadcast not run in flat scope");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }


  if(argc < 3){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a group and a message");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  group = arg_string_katcp(d, 1);
  if(group){

    gx = scope_name_group_katcp(d, group, fx);
    if(gx == NULL){
      if(strcmp(group, "*")){ /* WARNING: made up syntax - needs to be checked across everything */
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate group called %s", group);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      }
    }

    /* TODO: should check scope - if not global, restrict to current one ... */

  } else {
    /* group is null, assume everybody */
    switch(fx->f_scope){
      case KATCP_SCOPE_GROUP : 
        gx = this_group_katcp(d);
        break;
      case KATCP_SCOPE_GLOBAL : 
        gx = NULL;
        break;
      /* case KATCP_SCOPE_SINGLE :  */
      default :
        /* TODO: should spam ourselves ... in order not to give away that we are restricted */
        gx = NULL;
        return KATCP_RESULT_OK;
    }
  }

  inform = arg_string_katcp(d, 2);
  if(inform == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a non-null inform");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  switch(inform[0]){
    case KATCP_REPLY : 
    case KATCP_REQUEST :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "refusing to broadcast a message which is not an inform");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    /* case KATCP_INFORM : */
    default :
      break;
  }
  
  ptr = default_message_type_katcm(inform, KATCP_INFORM);
  if(ptr == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  px = create_parse_katcl();
  if(px == NULL){
    free(ptr);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  if(argc > 3){
    add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, ptr);

    add_trailing_parse_katcl(px, KATCP_FLAG_LAST, py, 3);

  } else {
    add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, ptr);
  }

  free(ptr);
  ptr = NULL;

  if(broadcast_flat_katcp(d, gx, px, NULL, NULL) < 0){
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

#endif
