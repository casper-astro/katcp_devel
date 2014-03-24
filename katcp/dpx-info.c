#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <errno.h>
#include <sysexits.h>

#include <sys/stat.h>

#include <netc.h>
#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

static int each_log_parse_katcp(struct katcl_line *l, int count, unsigned int limit, unsigned int level, struct katcl_parse *px)
{
  int result;

#if DEBUG > 1
  fprintf(stderr, "log: considering logging %p to %p (if %u >= %u)\n", px, l, level, limit);
#endif

  if(level < limit){
    return count;
  }

  result = append_parse_katcl(l, px);
  if(count < 0){
    return -1;
  }
  if(result < 0){
    return -1;
  }

  return count + 1;
}

int log_parse_katcp(struct katcp_dispatch *d, int level, struct katcl_parse *px)
{

  /* WARNING: assumption if level < 0, then a relayed log message ... this probably should be a flag in its own right */

  int limit, count;
  unsigned int mask;
  int i, j;
  char *ptr;
  struct katcp_flat *fx, *ft;
  struct katcp_group *gx, *gt;
  struct katcp_shared  *s;

  count = 0;

  if(px == NULL){
    return -1;
  }

  s = d->d_shared;
  if(s == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "log: no shared state available\n");
    abort();
#endif
    return -1;
  }

  ft = this_flat_katcp(d);
  gt = this_group_katcp(d);

  ptr = get_string_parse_katcl(px, 0);
  if(ptr == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "log: empty message type\n");
#endif
    return -1;
  }
  if(strcmp(ptr, KATCP_LOG_INFORM)){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "log: expected message %s, not %s\n", KATCP_LOG_INFORM, ptr);
#endif
    return -1;
  }

  if(level < 0){
    ptr = get_string_parse_katcl(px, 1);
    if(ptr == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "log: null priority value for log message\n");
#endif
      return -1;
    }

    limit = log_to_code_katcl(ptr);
    mask = 0;
    if(limit < 0){
#ifdef KATCP_CONSISTENCY_CHECKS
      fprintf(stderr, "log: invalid log priority value %s for log message\n", ptr);
#endif
      return -1;
    }
  } else {
    mask = (~KATCP_MASK_LEVELS) & ((unsigned int)level);
    limit = KATCP_MASK_LEVELS & ((unsigned int)level);
  }

  /* WARNING: ft and gt may be NULL if outside flat context */
  /* WARNING: won't echo a negative level back to its sender */

  if(mask & KATCP_LEVEL_LOCAL){ /* message never visible to outside same connection */
    fx = ft;
    if(fx && (level >= 0)){
      count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
    }
  } else if(mask & KATCP_LEVEL_GROUP){ /* message stays within the same group, at most */
    gx = gt;
    if(gx){
      for(i = 0; i < gx->g_count; i++){
        fx = gx->g_flats[i];
        if(fx){
          if(fx == ft){
            if(level >= 0){
              count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
            }
          } else {
            if(fx->f_scope != KATCP_SCOPE_SINGLE){
              count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
            }
          }
        }
      }
    }
  } else { /* message goes everywhere, subject to scope filter */
    if(s->s_groups){
      for(j = 0; j < s->s_members; j++){
        gx = s->s_groups[j];
        for(i = 0; i < gx->g_count; i++){
          fx = gx->g_flats[i];
          if(fx && ((fx != ft) || (level >= 0))){
            switch(fx->f_scope){
              case KATCP_SCOPE_SINGLE :
                if(fx == ft){
                  count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
                }
                break;
              case KATCP_SCOPE_GROUP  :
                if(gx == gt){
                  count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
                }
                break;
              case KATCP_SCOPE_GLOBAL :
                count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
                break;
#ifdef KATCP_CONSISTENCY_CHECKS
              default :
                fprintf(stderr, "log: invalid scope %d for %p\n", fx->f_scope, fx);
                abort();
                break;
#endif
            }
          }
        }
      }
    }
  }

#ifdef DEBUG
  fprintf(stderr, "log: message %p reported %d times\n", px, count);
#endif

  return count;
}

int log_group_info_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcl_parse *px;

#ifdef DEBUG
  fprintf(stderr, "log: encountered a log message\n");
#endif

  fx = this_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

#if 0
  /* WARNING: probably the best way to inhibit this is to provide a means to deregister this handler */
  if(fx->f_flags & KATCP_FLAT_TOCLIENT){
#ifdef DEBUG
    fprintf(stderr, "log: ingnoring log message from client\n");
#endif
    return 0;
  }
#endif

  px = arg_parse_katcp(d);
  if(px == NULL){
    return -1;
  }

  if(log_parse_katcp(d, -1, px) < 0){
    return -1;
  }

  return 0;
}

/* TODO: sensor logic: sensor-list, sensor-status ? */

int sensor_status_group_info_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcl_parse *px;
  struct katcp_endpoint *self, *remote, *origin;

#ifdef DEBUG
  fprintf(stderr, "log: encountered a log message\n");
#endif

  fx = this_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = arg_parse_katcp(d);
  if(px == NULL){
    return -1;
  }

  origin = sender_to_flat_katcp(d, fx);
  remote = remote_of_flat_katcp(d, fx);
  self = handler_of_flat_katcp(d, fx);


  if(origin == remote){ /* ... remote party is sending us a status update ... */

    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "saw a sensor status update from remote party, should capture this");

  } else { /* we must have asked a sensor to send this to us, better relay it on */

    if(remote == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal problem, saw a sensor status but have nowhere to send it to");
      return -1;
    }

    send_message_endpoint_katcp(d, self, remote, px, 0);
  }

  return 0;
}

#endif
