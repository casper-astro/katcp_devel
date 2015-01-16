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

int sensor_list_group_info_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcl_parse *px;
  struct katcp_vrbl *vx;
  struct katcp_endpoint *self, *remote, *origin;
  char *name, *description, *units, *type, *ptr, *strip;
  int len;

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
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "saw a sensor list from remote party, attempting to it");

    name = get_string_parse_katcl(px, 1);
    description = get_string_parse_katcl(px, 2);
    units = get_string_parse_katcl(px, 3);
    type = get_string_parse_katcl(px, 4);

    if((name == NULL) || (description == NULL) || (type == NULL)){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "deficient sensor declaration encountered");
      return -1;
    }

    /* WARNING: this is a bit of a mess, should be made part of the vrbl API */
    ptr = strchr(name, '*');
    if(ptr){

      ptr = strdup(name);
      if(ptr == NULL){
        return -1;
      }

    } else { /* would be case A, but since search not supported we transform it to case D */

      /* TODO: still work out how to use k7 relative/absolute variable names, noting that client renames can happen ... */
      if(name[0] == '.'){
        strip = name + 1;
      } else {
        strip = name;
      }

      len = strlen(strip);
      ptr = malloc(len + 3);
      if(ptr == NULL){
        return -1;
      }
      ptr[0] = '*';
      memcpy(ptr + 1, strip, len);
      ptr[len + 1] = '*';
      ptr[len + 2] = '\0';
    }

    vx = find_vrbl_katcp(d, ptr);
    if(vx != NULL){
      if(is_vrbl_sensor_katcp(d, vx)){
#if 0
      if(vx->v_flags & KATCP_VRF_SEN){ /* might be better choice: the extra checks in is_vrbl might be too fussy during set up ? */
#endif
        /* WARNING: unclear - maybe clobber the sensor entirely ... */
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "leaving old sensor definition unchanged");
      } else {
        /* unreasonable condition, up the severity */
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unwilling to transform existing variable %s into a sensor", ptr);
      }
      free(ptr);
      return 1; /* WARNING: is this a reasonable exit code ? */
    }

    /* here we can assume vx is a new variable */
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "defining new sensor %s as %s", name, ptr);

    vx = scan_vrbl_katcp(d, NULL, NULL, KATCP_VRC_SENSOR_VALUE, 1, KATCP_VRT_STRING);
    if(vx == NULL){
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable to declare sensor variable %s for %s", ptr, fx->f_name);
      free(ptr);
      return -1;
    }

    /* TODO: notice errors */
    scan_vrbl_katcp(d, vx, description, KATCP_VRC_SENSOR_HELP, 1, KATCP_VRT_STRING);
    scan_vrbl_katcp(d, vx, type, KATCP_VRC_SENSOR_TYPE, 1, KATCP_VRT_STRING);

    if(units){
      scan_vrbl_katcp(d, vx, units, KATCP_VRC_SENSOR_UNITS, 1, KATCP_VRT_STRING);
    }

    if(configure_vrbl_katcp(d, vx, KATCP_VRF_SEN, NULL, NULL, NULL, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to configure new variable %s as sensor", ptr);
      destroy_vrbl_katcp(d, ptr, vx);
      free(ptr);
      return -1;
    }

    if(update_vrbl_katcp(d, fx, ptr, vx, 0) == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to store new sensor variable %s for client %s", ptr, fx->f_name);
      destroy_vrbl_katcp(d, name, vx);
      free(ptr);
      return KATCP_RESULT_FAIL;
    }

    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "declared variable %s as %s", name, ptr);

    schedule_sensor_update_katcp(d, ptr);

    free(ptr);

    return 0;


  } else { /* we must have asked a sensor to send this to us, better relay it on */

    if(remote == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal problem, saw a sensor status but have nowhere to send it to");
      return -1;
    }

    send_message_endpoint_katcp(d, self, remote, px, 0);

    return 0;

  }

}

int sensor_status_group_info_katcp(struct katcp_dispatch *d, int argc)
{
#define TIMESTAMP_BUFFER 16
  struct katcp_flat *fx;
  struct katcl_parse *px;
  struct katcp_endpoint *self, *remote, *origin;
  struct katcp_vrbl *vx;
  char *name, *stamp, *value, *status;
  char buffer[TIMESTAMP_BUFFER];
  int unhide;

#ifdef DEBUG
  fprintf(stderr, "log: encountered a sensor-status message\n");
#endif

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "saw a sensor status message");

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

    stamp = get_string_parse_katcl(px, 1);
    name = get_string_parse_katcl(px, 3);
    status = get_string_parse_katcl(px, 4);
    value = get_string_parse_katcl(px, 5);

    if((stamp == NULL) || (name == NULL) || (status == NULL) || (value == NULL)){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "deficient sensor update encountered");
      return -1;
    }

    if(fixup_timestamp_katcp(stamp, buffer, TIMESTAMP_BUFFER) < 0){
      return -1;
    }
    buffer[TIMESTAMP_BUFFER - 1] = '\0';

    vx = find_vrbl_katcp(d, name);
    if(vx){
      if(is_vrbl_sensor_katcp(d, vx)){

        if(vx->v_flags & KATCP_VRF_HID){
          log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "variable %s is hidden", name);
          unhide = 0;
        } else {
          hide_vrbl_katcp(d, vx);
          unhide = 1;
        }

        scan_vrbl_katcp(d, vx, buffer,  KATCP_VRC_SENSOR_TIME,   1, KATCP_VRT_STRING);
        scan_vrbl_katcp(d, vx, status, KATCP_VRC_SENSOR_STATUS, 1, KATCP_VRT_STRING);

        if(unhide){
          show_vrbl_katcp(d, vx);
        }

        scan_vrbl_katcp(d, vx, value,  KATCP_VRC_SENSOR_VALUE,  1, KATCP_VRT_STRING);

      } else {
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "variable %s exists but not a sensor thus not propagating it", name);
      }
    } else {
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "no declaration for sensor %s thus not propagating it", name);
    }

  } else { /* we must have asked a sensor to send this to us, better relay it on */

    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "origin endpoint of message is %p, remote %p", origin, remote);

    if(remote == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal problem, saw a sensor status but have nowhere to send it to");
      return -1;
    }

    send_message_endpoint_katcp(d, self, remote, px, 0);
  }

  return 0;
#undef TIMESTAMP_BUFFER
}

#endif
