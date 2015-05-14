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

/* logging related commands *****************************************/

#define LEVEL_EXTENT_DETECT     0
#define LEVEL_EXTENT_FLAT       1
#define LEVEL_EXTENT_GROUP      2
#define LEVEL_EXTENT_DEFAULT    3

int set_group_log_level_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, struct katcp_group *gx, unsigned int level, unsigned int immediate)
{
  struct katcp_flat *fy;
  unsigned int i;

  if(level >= KATCP_MAX_LEVELS){
    return -1;
  }

  if(fx){
    fx->f_log_level = level;
    return level;
  }

  if(gx){
    if(immediate){
      for(i = 0; i < gx->g_count; i++){
        fy = gx->g_flats[i];
        fy->f_log_level = level;
      }
    } 
    gx->g_log_level = level;
    return level;
  }

  return -1;
}

int generic_log_level_group_cmd_katcp(struct katcp_dispatch *d, int argc, unsigned int clue)
{
  struct katcp_flat *fx;
  struct katcp_group *gx;
  int level;
  unsigned int type;
  char *name, *requested, *extent;

  level = (-1);

  if(argc > 1){
    requested = arg_string_katcp(d, 1);

    if(requested){
      if(!strcmp(requested, "all")){
        level = KATCP_LEVEL_TRACE;
      } else {
        level = log_to_code_katcl(requested);
        if(level < 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown log level %s", requested);
          return KATCP_RESULT_FAIL;
        }
      }
    }
  }

  fx = NULL;
  gx = NULL;

  if(clue == LEVEL_EXTENT_DETECT){
    type = LEVEL_EXTENT_FLAT;
    if(argc > 2){
      extent = arg_string_katcp(d, 2);
      if(extent){
        if(!strcmp("client", extent)){
          type = LEVEL_EXTENT_FLAT;
        } else if(!strcmp("group", extent)){
          type = LEVEL_EXTENT_GROUP;
        } else if(!strcmp("default", extent)){
          type = LEVEL_EXTENT_DEFAULT;
        } else {
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown log extent %s", extent);
          return KATCP_RESULT_FAIL;
        }
      }
    }

    if(argc > 3){
      name = arg_string_katcp(d, 3);
      if(name == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no or null subject for log level supplied");
        return KATCP_RESULT_FAIL;
      }
      switch(type){
        case LEVEL_EXTENT_FLAT    :
          fx = scope_name_full_katcp(d, NULL, NULL, name);
          if(fx == NULL){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate a client with the name %s", name);
            return KATCP_RESULT_FAIL;
          }
          break;
        case LEVEL_EXTENT_GROUP   :
        case LEVEL_EXTENT_DEFAULT :
          gx = find_group_katcp(d, name);
          if(gx == NULL){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate a client group with the name %s", name);
            return KATCP_RESULT_FAIL;
          }
        break;
      }
    } 

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "decided to use log extent %u", type);
  } else {
    type = clue;
  }

  switch(type){
    case LEVEL_EXTENT_FLAT    :
      if(fx == NULL){
        fx = this_flat_katcp(d);
      }
      break;
    case LEVEL_EXTENT_GROUP   :
      if(level < 0){ /* request */
        if(fx == NULL){
          fx = this_flat_katcp(d);
        }
      } else { /* set */
        if(gx == NULL){
          gx = this_group_katcp(d);
        }
      }
      break;
    case LEVEL_EXTENT_DEFAULT :
      if(gx == NULL){
        gx = this_group_katcp(d);
      }
      break;
  }

  if((fx == NULL) && (gx == NULL)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to work out which entity should have its log level accessed for extent %u", type);
    return KATCP_RESULT_FAIL;
  }

  if(level < 0){
    switch(type){
      case LEVEL_EXTENT_FLAT    :
        if(fx){
          level = fx->f_log_level;
        }
        break;
      case LEVEL_EXTENT_GROUP   :
        if(fx){ /* WARNING: cheats, pick the current connection */
          level = fx->f_log_level;
        }
        break;
      case LEVEL_EXTENT_DEFAULT :
        if(gx){
          level = gx->g_log_level;
        }
        break;
    }
  } else {
    switch(type){
      case LEVEL_EXTENT_FLAT    :
        level = set_group_log_level_katcp(d, fx, NULL, level, 0);
        break;
      case LEVEL_EXTENT_GROUP   :
        level = set_group_log_level_katcp(d, NULL, gx, level, 1);
        break;
      case LEVEL_EXTENT_DEFAULT :
        level = set_group_log_level_katcp(d, NULL, gx, level, 0);
        break;
    }
  }

  if(level < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to retrieve or set a log level in this context");
    return KATCP_RESULT_FAIL;
  } 

  name = log_to_string_katcl(level);
  if(name == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "dpx: major logic problem: unable to convert %d to a symbolic log name", level);
    abort();
#endif
    return KATCP_RESULT_FAIL;
  }

#ifdef DEBUG
  if(extra_response_katcp(d, KATCP_RESULT_OK, name) < 0){
    fprintf(stderr, "dpx[%p]: unable to generate extended response\n", fx);
  }
  fprintf(stderr, "dpx[%p]: completed log logic with own response messages\n", fx);
#else
  extra_response_katcp(d, KATCP_RESULT_OK, name);
#endif

  return KATCP_RESULT_OWN;
}

int log_level_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return generic_log_level_group_cmd_katcp(d, argc, LEVEL_EXTENT_GROUP);
}

int log_local_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return generic_log_level_group_cmd_katcp(d, argc, LEVEL_EXTENT_FLAT);
}

int log_default_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return generic_log_level_group_cmd_katcp(d, argc, LEVEL_EXTENT_DEFAULT);
}

int log_override_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return generic_log_level_group_cmd_katcp(d, argc, LEVEL_EXTENT_DETECT);
}

/* help ********************************************************************/


int print_help_cmd_item(struct katcp_dispatch *d, void *global, char *key, void *v)
{
  struct katcp_cmd_item *i;
  unsigned int *cp;

  cp = global;

  i = v;

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "%s is %s with %d references and %s local data", i->i_name, i->i_flags & KATCP_MAP_FLAG_HIDDEN ? "hidden" : "visible", i->i_refs, i->i_data ? "own" : "no");

  if(i->i_flags & KATCP_MAP_FLAG_HIDDEN){ 
    return -1;
  }

  prepend_inform_katcp(d);

  append_string_katcp(d, KATCP_FLAG_STRING, i->i_name);
  append_string_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_STRING, i->i_help);

  *cp = (*cp) + 1;

  return 0;
}

int help_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcp_cmd_item *i;
  struct katcp_cmd_map *mx;
  char *name, *match;
  unsigned int count;

  fx = require_flat_katcp(d);
  count = 0;

  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "help group called outside expected path");
    return KATCP_RESULT_FAIL;
  }

  mx = map_of_flat_katcp(fx);
  if(mx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no map to be found for client %s", fx->f_name);
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "should generate list of commands");
    if(mx->m_tree){
      complex_inorder_traverse_avltree(d, mx->m_tree->t_root, (void *)(&count), &print_help_cmd_item);
    }
  } else {
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "should provide help for %s", name);
    switch(name[0]){
      case KATCP_REQUEST : 
      case KATCP_REPLY   :
      case KATCP_INFORM  :
        match = name + 1;
        break;
      default :
        match = name;
    }
    if(match[0] == '\0'){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to provide help on a null command");
      return KATCP_RESULT_FAIL;
    } else {
      i = find_data_avltree(mx->m_tree, match);
      if(i == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no match for %s found", name);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      } else {
        print_help_cmd_item(d, (void *)&count, match, (void *)i);
      }
    }
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", count);
}

/* watchdog */

int watchdog_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *source;
  struct katcp_flat *fx;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    source = "simplex";
  } else {
    if(remote_of_flat_katcp(d, fx) == sender_to_flat_katcp(d, fx)){
      source = "remote";
    } else {
      source = "internal";
    }
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "fielding %s ping request", source);

  return KATCP_RESULT_OK;
}

/* client list */

static int print_client_list_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, char *name)
{
  int result, r, pending;
  char *ptr;
  struct katcp_group *gx;

  if(fx->f_name == NULL){
    return 0;
  }

  if(name && strcmp(name, fx->f_name)){
    return 0;
  }

  if((fx->f_flags & KATCP_FLAT_HIDDEN) && (name == NULL)){
    return 0;
  }

  r = prepend_inform_katcp(d);
  if(r < 0){
#ifdef DEBUG
    fprintf(stderr, "print_client_list: prepend failed\n");
#endif
    return -1;
  }
  result = r;

  r = append_string_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_STRING, fx->f_name);
  if(r < 0){
#ifdef DEBUG
    fprintf(stderr, "print_client_list: append of %s failed\n", fx->f_name);
#endif
    return -1;
  }

  result += r;

  ptr = log_to_string_katcl(fx->f_log_level);
  if(ptr){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has log level <%s>", fx->f_name, ptr);
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "client %s has unreasonable log level", fx->f_name);
  }

  if(fx->f_flags & KATCP_FLAT_HIDDEN){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s will not be listed", fx->f_name);
  }

  if(fx->f_flags & KATCP_FLAT_TOSERVER){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s may issue requests as peer is a server", fx->f_name);
  }
  if(fx->f_flags & KATCP_FLAT_TOCLIENT){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s may field requests as peer is a client", fx->f_name);
  }

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s prefix its name to sensor definitions", fx->f_name, (fx->f_flags & KATCP_FLAT_PREFIXED) ? "will" : "will not");

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s translate relayed inform messages", fx->f_name, (fx->f_flags & KATCP_FLAT_RETAINFO) ? "will not" : "will");

  ptr = string_from_scope_katcp(fx->f_scope);
  if(ptr){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has %s scope", fx->f_name, ptr);
  } else {
    log_message_katcp(d, KATCP_LEVEL_WARN | KATCP_LEVEL_LOCAL, NULL, "client %s has invalid scope", fx->f_name);
  }

  switch((fx->f_stale & KATCP_STALE_MASK_SENSOR)){
    case KATCP_STALE_SENSOR_NAIVE :
      log_message_katcp(d, KATCP_LEVEL_DEBUG | KATCP_LEVEL_LOCAL, NULL, "client %s has not listed sensors", fx->f_name);
      break;
    case KATCP_STALE_SENSOR_CURRENT :
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has up to date sensor listing", fx->f_name);
      break;
    case KATCP_STALE_SENSOR_STALE :
      log_message_katcp(d, KATCP_LEVEL_WARN | KATCP_LEVEL_LOCAL, NULL, "client %s has sensor listing which is out of date", fx->f_name);
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_FATAL | KATCP_LEVEL_LOCAL, NULL, "client %s has corrupt sensor state tracking", fx->f_name);
      break;
  }

  log_message_katcp(d, ((fx->f_max_defer > 0) ? KATCP_LEVEL_WARN : KATCP_LEVEL_INFO) | KATCP_LEVEL_LOCAL, NULL, "client %s has had a maximum of %d requests outstanding", fx->f_name, fx->f_max_defer);

  if(flushing_katcl(fx->f_line)){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has output pending", fx->f_name);
  }

  gx = fx->f_group;
  if(gx && gx->g_name){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s member of group <%s>", fx->f_name, gx->g_name);
  }

  pending = pending_endpoint_katcp(d, fx->f_peer);
  if(pending > 0){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has %d %s in queue", fx->f_name, pending, (pending > 1) ? "commands" : "command");
  }

  if(flushing_katcl(fx->f_line)){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has output pending", fx->f_name);
  }

  return result;
}

int client_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_group *gx;
  struct katcp_flat *fx, *fy;
  unsigned int i, j, total;
  char *name;

  s = d->d_shared;
  total = 0;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client data available, probably called in incorrect context");
    return KATCP_RESULT_FAIL;
  }

  if(argc > 1){
    name = arg_string_katcp(d, 1);
  } else {
    name = NULL;
  }

  switch(fx->f_scope){
    case KATCP_SCOPE_SINGLE :
      /* WARNING: maybe a client can not be hidden from itself ? */
      if(print_client_list_katcp(d, fx, name) > 0){
        total++;
      }
      break;
    case KATCP_SCOPE_GROUP :
      gx = this_group_katcp(d);
      if(gx){
        for(i = 0; i < gx->g_count; i++){
          fy = gx->g_flats[i];
          if(print_client_list_katcp(d, fy, name) > 0){
            total++;
          }
        }
      }
      break;
    case KATCP_SCOPE_GLOBAL :
      for(j = 0; j < s->s_members; j++){
        gx = s->s_groups[j];
        if(gx){
          for(i = 0; i < gx->g_count; i++){
            fy = gx->g_flats[i];
            if(print_client_list_katcp(d, fy, name) > 0){
              total++;
            }
          }
        }
      }
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid client scope %d for %s\n", fx->f_scope, fx->f_name);
      return KATCP_RESULT_FAIL;
  }

  if((total == 0) && (name != NULL)){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", total);
}

/* halt and restart *************************************************/

int restart_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  /* halt and restart handlers could really be merged into one function */

  struct katcp_group *gx;
  struct katcp_flat *fx;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client data available, probably called in incorrect context");
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "restart request from %s", fx->f_name ? fx->f_name : "<unknown party>");

  switch(fx->f_scope){
    case KATCP_SCOPE_SINGLE :
      if(terminate_flat_katcp(d, fx) < 0){
        return KATCP_RESULT_FAIL;
      }
      return KATCP_RESULT_OK;
    case KATCP_SCOPE_GROUP :
      gx = fx->f_group; 
      if(gx){
        if(terminate_group_katcp(d, gx, 0) == 0){
          return KATCP_RESULT_OK;
        }
      }
      return KATCP_RESULT_FAIL;
    case KATCP_SCOPE_GLOBAL :
      terminate_katcp(d, KATCP_EXIT_RESTART);
      return KATCP_RESULT_OK;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid client scope %d for %s\n", fx->f_scope, fx->f_name);
      return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int halt_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_group *gx;
  struct katcp_flat *fx;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client data available, probably called in incorrect context");
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "halt request from %s", fx->f_name ? fx->f_name : "<unknown party>");

  switch(fx->f_scope){
    case KATCP_SCOPE_SINGLE :
      if(terminate_flat_katcp(d, fx) < 0){
        return KATCP_RESULT_FAIL;
      }
      return KATCP_RESULT_OK;
    case KATCP_SCOPE_GROUP :
      gx = fx->f_group;
      if(gx){
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "initiating halt for %s group", gx->g_name ? gx->g_name : "<unnamed>");
        if(terminate_group_katcp(d, gx, 1) == 0){
          return KATCP_RESULT_OK;
        }
      }
      return KATCP_RESULT_FAIL;
    case KATCP_SCOPE_GLOBAL :
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "initiating global halt");
      terminate_katcp(d, KATCP_EXIT_HALT);
      return KATCP_RESULT_OK;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid client scope %d for %s\n", fx->f_scope, fx->f_name);
      return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

/* sensor related functions ***************************************************/

int sensor_list_callback_katcp(struct katcp_dispatch *d, unsigned int *count, char *key, struct katcp_vrbl *vx)
{
  struct katcp_vrbl_payload *value, *help, *units, *type, *range;
  char *fallback;
  int result;
#if 0
  struct katcp_vrbl_payload *prefix;
  struct katcp_flat *fx;
  char *front;
#endif

  result = is_vrbl_sensor_katcp(d, vx);
  if(result <= 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    if(result < 0){
      fprintf(stderr, "sensor: sensor %s severely malformed\n", key);
      abort();
    }
#endif
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "subpressing listing of variable %s", key);
    return result;
  }

  if(vx->v_flags & KATCP_VRF_HID){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "subpressing listing of hidden sensor %s", key);
    return 0;
  }


#if 0
  vy = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_VALUE);
  uy = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_UNITS);
  hy = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_HELP);
  ty = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_TYPE);
#endif

  value = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_VALUE);
  if(value == NULL){
    return 0;
  }
  fallback = type_to_string_vrbl_katcp(d, value->p_type);
  if(fallback == NULL){
    return 0;
  }

#if 0
  prefix = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_PREFIX);
  if(prefix){
    /* TODO: should extract the prefix, convert it to a string (no vrbl API for that yet, then store it in front */
  }
#endif

#if 0
  front = NULL;
  fx = this_flat_katcp(d);
  if(fx){
    if(fx->f_flags & KATCP_FLAT_PREFIXED){
      front = fx->f_name;
    }
  }
#endif

  prepend_inform_katcp(d);

#if 0
  if(front){
    append_args_katcp(d, KATCP_FLAG_STRING, "%s.%s", front, key);
  } else {
#endif
    append_string_katcp(d, KATCP_FLAG_STRING, key);
#if 0
  }
#endif

  help = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_HELP);
  if(help && (help->p_type == KATCP_VRT_STRING)){
    append_payload_vrbl_katcp(d, 0, vx, help);
  } else {
    append_string_katcp(d, KATCP_FLAG_STRING, "undocumented");
  }

  units = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_UNITS);
  if(units && (units->p_type == KATCP_VRT_STRING)){
    append_payload_vrbl_katcp(d, 0, vx, units);
  } else {
    append_string_katcp(d, KATCP_FLAG_STRING, "none");
  }

  range = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_RANGE);

  type = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_TYPE);
  if(type && (type->p_type == KATCP_VRT_STRING)){
    /* allows us to override the native type to something in sensor world */
    append_payload_vrbl_katcp(d, (range ? 0 : KATCP_FLAG_LAST), vx, type);
  } else {
    append_string_katcp(d, KATCP_FLAG_STRING | (range ? 0 : KATCP_FLAG_LAST), fallback);
  }

  if(range){
    append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, range);
  }

  if(count){
    *count = (*count) + 1;
  }

  return 0;
}

int sensor_list_void_callback_katcp(struct katcp_dispatch *d, void *state, char *key, void *data)
{
  struct katcp_vrbl *vx;
  unsigned int *count;

  vx = data;
  count = state;

  return sensor_list_callback_katcp(d, count, key, vx);
}

int sensor_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *key;
  unsigned int i, count;
  int result;
  struct katcp_vrbl *vx;
  struct katcp_flat *fx;

  result = 0;
  count = 0;

  if(argc > 1){
    /* One of the few commands which accepts lots of parameters */
    for(i = 1 ; i < argc ; i++){
      key = arg_string_katcp(d, i);
      if(key == NULL){
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
      }
      vx = find_vrbl_katcp(d, key);
      if(vx == NULL){
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "%s not found", key);
        result = (-1);
      } else {
        if(sensor_list_callback_katcp(d, &count, key, vx) < 0){
          result = (-1);
        }
      }
    }
  } else {
    result = traverse_vrbl_katcp(d, (void *)&count, &sensor_list_void_callback_katcp);
    fx = this_flat_katcp(d);
    if(fx){
      fx->f_stale = (fx->f_stale & (~KATCP_STALE_MASK_SENSOR)) | KATCP_STALE_SENSOR_CURRENT;
    }
  }

  if(result < 0){
    return KATCP_RESULT_FAIL;
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", count);
}

int sensor_sampling_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  int result;
  char *key, *strategy;
  struct katcp_vrbl *vx;
  struct katcp_flat *fx;
  int stg;

  if(argc <= 1){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  key = arg_string_katcp(d, 1);
  if(key == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  vx = find_vrbl_katcp(d, key);
  if(vx == NULL){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "%s not found", key);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  result = is_vrbl_sensor_katcp(d, vx);
  if(result <= 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    if(result < 0){
      fprintf(stderr, "sensor: sensor %s severely malformed\n", key);
      abort();
    }
#endif
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "not using variable %s", key);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  if(vx->v_flags & KATCP_VRF_HID){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "not using hidden sensor %s", key);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "sensor sampling not available outside duplex context");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc <= 2){
    stg = current_strategy_sensor_katcp(d, vx, fx);
    /* WARNING: will have to get more information for period sensors ... */

    strategy = strategy_to_string_sensor_katcp(d, stg);

    if(strategy == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    } 

  } else {

    strategy = arg_string_katcp(d, 2);
    if(strategy == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }

    stg = strategy_from_string_sensor_katcp(d, strategy);
    switch(stg){
      case KATCP_STRATEGY_EVENT :
        if(monitor_event_variable_katcp(d, vx, fx) < 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register event based sampling for sensor %s", key);
          return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
        }
        break;

      case KATCP_STRATEGY_OFF :
        if(forget_event_variable_katcp(d, vx, fx) < 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to release event based sampling for sensor %s", key);
          return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
        }
        break;

      /* TODO */
      case KATCP_STRATEGY_PERIOD :
      case KATCP_STRATEGY_DIFF :
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "sampling strategy %s not implemented for sensor %s", strategy, key);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);

      default :
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid sensor sampling strategy %s", strategy);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }
  }

  /* WARNING: all ERROR paths should have exited above ... can safely report now */

#ifdef KATCP_CONSISTENCY_CHECKS
  if((key == NULL) || (strategy == NULL)){
    fprintf(stderr, "dpx: sensor-sampling: logic problem: essential display field left out\n");
    abort();
  }
#endif

  prepend_reply_katcp(d);

  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_string_katcp(d, KATCP_FLAG_STRING, key);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, strategy);

  return KATCP_RESULT_OWN;
}

/********************************************************************************/

int sensor_value_callback_katcp(struct katcp_dispatch *d, unsigned int *count, char *key, struct katcp_vrbl *vx)
{
  struct katcl_parse *px;
  int result;

  result = is_vrbl_sensor_katcp(d, vx);
  if(result <= 0){
#ifdef DEBUG
    fprintf(stderr, "sensor value: %p not a sensor\n", vx);
#endif
    return result;
  }

  if(vx->v_flags & KATCP_VRF_HID){
#ifdef DEBUG
    fprintf(stderr, "sensor value: %p is hidden\n", vx);
#endif
    return 0;
  }

  px = make_sensor_katcp(d, key, vx, KATCP_SENSOR_VALUE_INFORM);
  if(px == NULL){
#ifdef DEBUG
    fprintf(stderr, "sensor value: unable to assemble output for %p\n", vx);
#endif
    return -1;
  }

#if 1
  /* WARNING: this is a bit ugly - we need prepend_inform for future tags, but the parse layer doesn't know about tags ... */

  /* TODO: there should probably be a add_tagged_string call which can be used to build up a tagged prefix on a parse structure */

  prepend_inform_katcp(d);

  append_trailing_katcp(d, KATCP_FLAG_LAST, px, 1);
#else
  /* this won't work for tags */

  append_parse_katcp(d, px);
#endif

  destroy_parse_katcl(px);

  if(count){
    *count = (*count) + 1;
  }

  return 0;
}

int sensor_value_void_callback_katcp(struct katcp_dispatch *d, void *state, char *key, void *data)
{
  struct katcp_vrbl *vx;
  unsigned int *count;

  vx = data;
  count = state;

  return sensor_value_callback_katcp(d, count, key, vx);
}

int sensor_value_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *key;
  unsigned int i, count;
  int result;
  struct katcp_vrbl *vx;

  result = 0;
  count = 0;

  if(argc > 1){
    /* One of the few commands which accepts lots of parameters */
    for(i = 1 ; i < argc ; i++){
      key = arg_string_katcp(d, i);
      if(key == NULL){
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
      }
      vx = find_vrbl_katcp(d, key);
      if(vx == NULL){
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "%s not found", key);
        result = (-1);
      } else {
        if(sensor_value_callback_katcp(d, &count, key, vx) < 0){
          result = (-1);
        }
      }
    }
  } else {
    result = traverse_vrbl_katcp(d, (void *)&count, &sensor_value_void_callback_katcp);
  }

  if(result < 0){
    return KATCP_RESULT_FAIL;
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", count);
}

/* version related function ***************************************************/

#if 0
int version_list_callback_katcp(struct katcp_dispatch *d, void *state, char *key, struct katcp_vrbl *vx)
{
  struct katcp_payload *pversion, *pbuild:
  unsigned int *cp;
  int type;

  cp = state;

#ifdef DEBUG
  fprintf(stderr, "version: about to consider variable %p\n", vx);
#endif

  if(vx == NULL){
    return -1;
  }

  if((vx->v_flags & KATCP_VRF_VER) == 0){
#ifdef DEBUG
    fprintf(stderr, "version: %p not a version variable\n", vx);
#endif
    return 0;
  }

  /* WARNING: this duplicates too much from version_connect, but needs the prepend_inform */

  type = find_type_vrbl_katcp(d, vx, NULL);
  switch(type){
    case KATCP_VRT_STRING :
      prepend_inform_katcp(d);
      append_string_katcp(d, KATCP_FLAG_STRING, key);
      append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, NULL);
      *cp = (*cp) + 1;
      return 0;
    case KATCP_VRT_TREE :

      pversion = find_payload_katcp(d, vx, KATCP_VRC_VERSION_VERSION);
      pbuild = find_payload_katcp(d, vx, KATCP_VRC_VERSION_BUILD);

      if(pversion){
        if(type_payload_vrbl_katcp(d, vx, pversion) != KATCP_VRT_STRING){
          pversion = NULL;
        }
      }
      if(pbuild){
        if(type_payload_vrbl_katcp(d, vx, pbuild) != KATCP_VRT_STRING){
          pbuild = NULL;
        }
      }

      if(pversion || pbuild){
        prepend_inform_katcp(d);
        append_string_katcp(d, KATCP_FLAG_STRING, key);
        if(pbuild){
          if(pversion){
            append_payload_vrbl_katcp(d, 0, vx, pversion);
          } else {
            append_string_katcp(d, KATCP_FLAG_STRING, "unknown");
          }
          append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, pbuild);
        } else {
          append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, pversion);
        }
        *cp = (*cp) + 1;
      }

      return 0;

    default :
      return 0;
  }
}
#endif

int version_list_void_callback_katcp(struct katcp_dispatch *d, void *state, char *key, void *data)
{
  struct katcp_vrbl *vx;

  vx = data;

  return version_generic_callback_katcp(d, state, key, vx);
}

int version_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *key;
  unsigned int i;
  int result;
  struct katcp_vrbl *vx;
  unsigned int count;

  result = 0;
  count = 0;

#ifdef DEBUG
  fprintf(stderr, "version: invoking listing with %d parameters\n", argc);
#endif

  if(argc > 1){
    for(i = 1 ; i < argc ; i++){
      key = arg_string_katcp(d, i);
      if(key == NULL){
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
      }
      vx = find_vrbl_katcp(d, key);
      if(vx == NULL || ((vx->v_flags & KATCP_VRF_VER) == 0)){
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "%s not found", key);
        result = (-1);
      } else {
        if(version_generic_callback_katcp(d, (void *)&count, key, vx) < 0){
          result = (-1);
        }
      }
    }
  } else {
    result = traverse_vrbl_katcp(d, (void *)&count, &version_list_void_callback_katcp);
  }

  if(result < 0){
    return KATCP_RESULT_FAIL;
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", count);
}


#endif
