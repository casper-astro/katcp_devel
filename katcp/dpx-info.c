#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

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

  if(mask & KATCP_LEVEL_LOCAL){ /* message never visible outside current connection */
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

#define FIXUP_TABLE 256 /* has to be size of char ... */

static unsigned char fixup_remap_table[FIXUP_TABLE];
static unsigned int fixup_remap_ready = 0;

static void fixup_remap_init()
{
  unsigned int i;

  for(i = 0; i < FIXUP_TABLE; i++){
    fixup_remap_table[i] = i;
    if(isupper(fixup_remap_table[i])){
      fixup_remap_table[i] = tolower(fixup_remap_table[i]);
    }
    if(!isprint(fixup_remap_table[i])){
      fixup_remap_table[i] = KATCP_VRBL_DELIM_SPACER;
    }
  }

  /* strip out things that trigger variable substitution */
  fixup_remap_table[KATCP_VRBL_DELIM_GROUP] = KATCP_VRBL_DELIM_LOGIC;
  fixup_remap_table[KATCP_VRBL_DELIM_TREE ] = KATCP_VRBL_DELIM_LOGIC;
  fixup_remap_table[KATCP_VRBL_DELIM_ARRAY] = KATCP_VRBL_DELIM_LOGIC;

  /* and clean up things */
  fixup_remap_table[KATCP_VRBL_DELIM_FORBID] = KATCP_VRBL_DELIM_SPACER;
}
#undef FIXUP_TABLE

static char *make_child_field_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, char *name, int locator)
{
  /* WARNING: in a way this is a bit of a mess, some of this should be subsumed into the variable API, however, the client prefix doesn't belong there either ... it is complicated */
  char *copy, *strip;
  int suffix, prefix, size, i, j;

  if(fixup_remap_ready == 0){
    fixup_remap_init();
    fixup_remap_ready = 1;
  }

  if(strchr(name, KATCP_VRBL_DELIM_GROUP)){
    /* WARNING: special case - if variable name includes * we assume the child knows about our internals */
    return strdup(name);
  }

  size = 1;

  if(locator){
    size += 2;
  }

  if((fx != NULL) && (fx->f_flags & KATCP_FLAT_PREFIXED) && (name[0] != KATCP_VRBL_DELIM_LOGIC)){ 
    /* prefix name to things */
    if(fx->f_name == NULL){
      /* eh ? */
      return NULL;
    }

    prefix = strlen(fx->f_name);
  } else {
    prefix = 0;
  }
  size += (prefix + 1);

  if(name[0] == KATCP_VRBL_DELIM_LOGIC){
    strip = name + 1;
  } else {
    strip = name;
  }
  suffix = strlen(strip);
  if(suffix <= 0){
    return NULL;
  }
  size += suffix;

  copy = malloc(size);
  if(copy == NULL){
    return NULL;
  }

  i = 0;
  if(locator){
    copy[i++] = KATCP_VRBL_DELIM_GROUP;
  }
  if(prefix){
    for(j = 0; fx->f_name[j] != '\0'; j++){
      copy[i++] = fixup_remap_table[((unsigned char *)fx->f_name)[j]];
    }
    copy[i++] = KATCP_VRBL_DELIM_LOGIC;
  }

  for(j = 0; j < suffix; j++){
    copy[i++] = fixup_remap_table[((unsigned char *)strip)[j]];
  }

  if(locator){
    copy[i++] = KATCP_VRBL_DELIM_GROUP;
  }

  copy[i++] = '\0';

  return copy;
}

int version_group_info_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcl_parse *px;
  struct katcp_vrbl *vx;
  struct katcp_endpoint *self, *remote, *origin;
  char *name, *version, *build, *ptr;
  unsigned int count;

#ifdef DEBUG
  fprintf(stderr, "version: encountered peer version message\n");
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

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "saw a version message (origin=%p, remote=%p, self=%p)", origin, remote, self);

  if(origin != remote){ /* version information must have originated internally ... */
    if(remote){ /* ... better relay it on, if somebody requested it */
      send_message_endpoint_katcp(d, self, remote, px, 0);
    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal problem, saw a version message but have nowhere to send it to");
    }

    return 0;
  }

  count = get_count_parse_katcl(px);
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "saw a version field with %u args - attempting to process it", count);

  if(count < 2){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "insufficient parameters for usable version report - only got %u", count);
    return 0;
  }

  name = get_string_parse_katcl(px, 1);
  version = get_string_parse_katcl(px, 2);

  if(count > 2){
    build = get_string_parse_katcl(px, 3);
  } else {
    build = NULL;
  }

  ptr = make_child_field_katcp(d, fx, name, 1);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable fixup version field %s", name);
    return KATCP_RESULT_FAIL;
  }

  vx = find_vrbl_katcp(d, ptr);
  if(vx != NULL){
    if(is_ver_sensor_katcp(d, vx)){
      /* WARNING: unclear - maybe clobber the sensor entirely ... */
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "leaving old version definition unchanged");
    } else {
      /* unreasonable condition, up the severity */
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unwilling to transform existing variable %s into a version description", ptr);
    }
    free(ptr);
    return KATCP_RESULT_OWN; /* WARNING: is this a reasonable exit code ? */
  }

  /* here we can assume vx is a new variable */
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "defining new version string %s as %s", name, ptr);

  vx = scan_vrbl_katcp(d, NULL, version, KATCP_VRC_VERSION_VERSION, 1, KATCP_VRT_STRING);
  if(vx == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable to declare version variable %s for %s", ptr, fx->f_name);
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  /* TODO: notice errors */
  if(build){
    scan_vrbl_katcp(d, vx, build, KATCP_VRC_VERSION_BUILD, 1, KATCP_VRT_STRING);
  }

  if(configure_vrbl_katcp(d, vx, KATCP_VRF_VER, NULL, NULL, NULL, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to configure new variable %s as version field", ptr);
    destroy_vrbl_katcp(d, ptr, vx);
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  if(update_vrbl_katcp(d, fx, ptr, vx, 0) == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to store new version variable %s for client %s", ptr, fx->f_name);
    destroy_vrbl_katcp(d, name, vx);
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "declared version variable %s as %s", name, ptr);

  free(ptr);

  return KATCP_RESULT_OK;
}

int sensor_list_group_info_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcl_parse *px;
  struct katcp_vrbl *vx;
  struct katcp_endpoint *self, *remote, *origin;
  char *name, *description, *units, *type, *ptr;

#ifdef DEBUG
  fprintf(stderr, "log: encountered a sensor-list message\n");
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

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "saw a sensor list message (origin=%p, remote=%p, self=%p)", origin, remote, self);

  if(origin != remote){ /* request must have originated internally, so ... */
    if(remote){ /* ... better relay it on, if somebody requested it */
      send_message_endpoint_katcp(d, self, remote, px, 0);
    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal problem, saw a sensor list but have nowhere to send it to");
    }
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "saw a sensor list - attempting to load it");

  name = get_string_parse_katcl(px, 1);
  description = get_string_parse_katcl(px, 2);
  units = get_string_parse_katcl(px, 3);
  type = get_string_parse_katcl(px, 4);

  if((name == NULL) || (description == NULL) || (type == NULL)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "deficient sensor declaration encountered");
    return KATCP_RESULT_FAIL;
  }

  ptr = make_child_field_katcp(d, fx, name, 1);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable fixup sensor name %s", name);
    return KATCP_RESULT_FAIL;
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
    return KATCP_RESULT_OWN; /* WARNING: is this a reasonable exit code ? */
  }

  /* here we can assume vx is a new variable */
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "defining new sensor %s as %s", name, ptr);

  vx = scan_vrbl_katcp(d, NULL, NULL, KATCP_VRC_SENSOR_VALUE, 1, KATCP_VRT_STRING);
  if(vx == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable to declare sensor variable %s for %s", ptr, fx->f_name);
    free(ptr);
    return KATCP_RESULT_FAIL;
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
    return KATCP_RESULT_FAIL;
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

  return KATCP_RESULT_OK;
}

int sensor_status_group_info_katcp(struct katcp_dispatch *d, int argc)
{
#define TIMESTAMP_BUFFER 20
  struct katcp_flat *fx;
  struct katcl_parse *px;
  struct katcp_endpoint *self, *remote, *origin;
  struct katcp_vrbl *vx;
  char *name, *stamp, *value, *status, *ptr;
  char buffer[TIMESTAMP_BUFFER];
  int unhide;

#ifdef DEBUG
  fprintf(stderr, "log: encountered a sensor-status message\n");
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

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "saw a sensor status message (origin=%p, remote=%p, self=%p)", origin, remote, self);

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
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "timestamp %s not reasonable", stamp);
      return -1;
    }
    buffer[TIMESTAMP_BUFFER - 1] = '\0';

    ptr = make_child_field_katcp(d, fx, name, 0);
    if(ptr == NULL){
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable fixup sensor name %s", name);
      return -1;
    }

    vx = find_vrbl_katcp(d, ptr);
    if(vx){
      if(is_vrbl_sensor_katcp(d, vx)){

        if(vx->v_flags & KATCP_VRF_HID){
          log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "variable %s is hidden", ptr);
          unhide = 0;
        } else {
          hide_vrbl_katcp(d, vx);
          unhide = 1;
        }

        scan_vrbl_katcp(d, vx, buffer, KATCP_VRC_SENSOR_TIME,   1, KATCP_VRT_STRING);
        scan_vrbl_katcp(d, vx, status, KATCP_VRC_SENSOR_STATUS, 1, KATCP_VRT_STRING);

        if(unhide){
          show_vrbl_katcp(d, vx);
        }

        scan_vrbl_katcp(d, vx, value,  KATCP_VRC_SENSOR_VALUE,  1, KATCP_VRT_STRING);

      } else {
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "variable %s exists but not a sensor thus not propagating it", ptr);
      }
    } else {
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "no declaration for sensor %s thus not propagating it", ptr);
    }

    free(ptr);

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
