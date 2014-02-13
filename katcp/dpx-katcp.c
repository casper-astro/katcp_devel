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
  struct katcp_shared *s;
  struct katcp_flat *fx;
  struct katcp_group *gx;
  int level;
  unsigned int type;
  char *name, *requested, *extent;

  s = d->d_shared;

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
          fx = find_name_flat_katcp(d, NULL, name);
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
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "fielding %s ping request", is_inner_flat_katcp(d) ? "internal" : "remote");

  return KATCP_RESULT_OK;
}

/* client list */

static int print_client_list_katcp(struct katcp_dispatch *d, struct katcp_flat *fx)
{
  int result, r, pending;
  char *ptr;
  struct katcp_group *gx;

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
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "client %s will not be listed");
  }

  if(fx->f_flags & KATCP_FLAT_TOSERVER){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "client %s may issue requests as peer is a server");
  }
  if(fx->f_flags & KATCP_FLAT_TOCLIENT){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "client %s may field requests as peer is a client");
  }

  ptr = string_from_scope_katcp(fx->f_scope);
  if(ptr){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has %s scope", fx->f_name, ptr);
  } else {
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has invalid scope", fx->f_name);
  }

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

  s = d->d_shared;
  total = 0;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client data available, probably called in incorrect context");
    return KATCP_RESULT_FAIL;
  }

  switch(fx->f_scope){
    case KATCP_SCOPE_SINGLE :
      /* WARNING: maybe a client can not be hidden from itself ? */
      if((fx->f_flags & KATCP_FLAT_HIDDEN) == 0){
        if(print_client_list_katcp(d, fx) > 0){
          total++;
        }
      }
      break;
    case KATCP_SCOPE_GROUP :
      gx = this_group_katcp(d);
      if(gx){
        for(i = 0; i < gx->g_count; i++){
          fy = gx->g_flats[i];
          if((fy->f_flags & KATCP_FLAT_HIDDEN) == 0){
            if(print_client_list_katcp(d, fy) > 0){
              total++;
            }
          }
        }
      }
      break;
    case KATCP_SCOPE_GLOBAL :
      for(j = 0; j < s->s_members; j++){
        gx = s->s_groups[i];
        if(gx){
          for(i = 0; i < gx->g_count; i++){
            fy = gx->g_flats[i];
            if((fy->f_flags & KATCP_FLAT_HIDDEN) == 0){
              if(print_client_list_katcp(d, fy) > 0){
                total++;
              }
            }
          }
        }
      }
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid client scope %d for %s\n", fx->f_scope, fx->f_name);
      return KATCP_RESULT_FAIL;
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", total);
}

/* halt and restart *************************************************/

int restart_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  /* halt and restart handlers could really be merged into one function */

  struct katcp_shared *s;
  struct katcp_group *gx;
  struct katcp_flat *fx;

  s = d->d_shared;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client data available, probably called in incorrect context");
    return KATCP_RESULT_FAIL;
  }

  switch(fx->f_scope){
    case KATCP_SCOPE_SINGLE :
      if(terminate_flat_katcp(d, fx) < 0){
        return KATCP_RESULT_FAIL;
      }
      return KATCP_RESULT_OK;
    case KATCP_SCOPE_GROUP :
      gx = this_group_katcp(d);
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
  struct katcp_shared *s;
  struct katcp_group *gx;
  struct katcp_flat *fx;

  s = d->d_shared;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client data available, probably called in incorrect context");
    return KATCP_RESULT_FAIL;
  }

  switch(fx->f_scope){
    case KATCP_SCOPE_SINGLE :
      if(terminate_flat_katcp(d, fx) < 0){
        return KATCP_RESULT_FAIL;
      }
      return KATCP_RESULT_OK;
    case KATCP_SCOPE_GROUP :
      gx = this_group_katcp(d);
      if(gx){
        if(terminate_group_katcp(d, gx, 1) == 0){
          return KATCP_RESULT_OK;
        }
      }
      return KATCP_RESULT_FAIL;
    case KATCP_SCOPE_GLOBAL :
      terminate_katcp(d, KATCP_EXIT_HALT);
      return KATCP_RESULT_OK;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid client scope %d for %s\n", fx->f_scope, fx->f_name);
      return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

/* sensor related functions ***************************************************/

int sensor_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  /* TODO */
  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", 0);
}

int sensor_sampling_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  /* TODO */

  if(argc <= 1){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
}

int sensor_value_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  /* TODO */

  if(argc <= 1){
    return extra_response_katcp(d, KATCP_RESULT_OK, "%d", 0);
  }

  return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_NOT_FOUND);

}

/* version related function ***************************************************/

/* WARNING: this is somewhat similar to the version connect logic ... */

int version_list_callback_katcp(struct katcp_dispatch *d, void *state, char *key, struct katcp_vrbl *vx)
{
  unsigned int *cp;

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

  prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, key);
  append_vrbl_katcp(d, KATCP_FLAG_LAST, vx);

  *cp = (*cp) + 1;

  return 0;
}

int version_list_void_callback_katcp(struct katcp_dispatch *d, void *state, char *key, void *data)
{
  struct katcp_vrbl *vx;

  vx = data;

  return version_list_callback_katcp(d, state, key, vx);
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
        if(version_list_callback_katcp(d, (void *)&count, key, vx) < 0){
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
