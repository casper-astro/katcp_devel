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

/* help */

void print_help_cmd_item(struct katcp_dispatch *d, char *key, void *v)
{
  struct katcp_cmd_item *i;

  i = v;

  if(i->i_flags & KATCP_MAP_FLAG_HIDDEN){ 
    return;
  }

  prepend_inform_katcp(d);

#if 0
  append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_HELP_INFORM);
#endif

  append_string_katcp(d, KATCP_FLAG_STRING, i->i_name);
  append_string_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_STRING, i->i_help);
}

int help_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcp_cmd_item *i;
  struct katcp_cmd_map *mx;
  char *name, *match;

  fx = require_flat_katcp(d);

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
      print_inorder_avltree(d, mx->m_tree->t_root, &print_help_cmd_item, 0);
    }
  } else {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "should provide help for %s", name);
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
      } else {
        print_help_cmd_item(d, NULL, (void *)i);
      }
#if 0
      if(i->i_flags & KATCP_MAP_FLAG_REQUEST){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "should print help message %s for %s", i->i_help, i->i_name);
      }
#endif
    }
  }

  return KATCP_RESULT_OK;
}

/* watchdog */

int watchdog_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "fielding %s ping request", is_inner_flat_katcp(d) ? "internal" : "remote");

  return KATCP_RESULT_OK;
}

/* client list */

static int print_client_list_katcp(struct katcp_dispatch *d, struct katcp_flat *fx)
{
  int result, r;

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
    case KATCP_SCOPE_GROUP :
      gx = this_group_katcp(d);
      if(gx){
        for(i = 0; i < gx->g_count; i++){
          fy = gx->g_flats[i];
          if(print_client_list_katcp(d, fy) > 0){
            total++;
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
            if(print_client_list_katcp(d, fy) > 0){
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

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", total);
}

#endif
