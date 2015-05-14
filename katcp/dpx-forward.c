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

#ifdef KATCP_CONSISTENCY_CHECKS 
#define FORWARD_STATE_MAGIC 0xfa34df12
#endif

#define FOP_LITTERAL        0
#define FOP_SINGLE          1
#define FOP_TRAIL           2
#define FOP_VAR             3
#define FOP_SPECIAL_GROUP   4
#define FOP_SPECIAL_CLIENT  5

#define MAX_FOP             6

struct forward_symbolic_parm
{
  unsigned short p_op;
  unsigned short p_num;
  char *p_str;
};

struct forward_symbolic_state
{
#ifdef KATCP_CONSISTENCY_CHECKS 
  unsigned int f_magic;
#endif
  char *f_peer;
  struct forward_symbolic_parm *f_vector;
  unsigned int f_count;
};

/************************************************************************/

/************************************************************************/

int load_parm_forward(struct katcp_dispatch *d, struct forward_symbolic_state *fs, unsigned int op, unsigned int number, char *string)
{
  struct forward_symbolic_parm *pf, *pv;
  char *copy;

  pv = realloc(fs->f_vector, (fs->f_count + 1) * (sizeof(struct forward_symbolic_parm)));
  if(pv == NULL){
    return -1;
  }
  fs->f_vector = pv;

  if(string){
    copy = strdup(string);
    if(copy == NULL){
      return -1;
    }
  } else {
    copy = NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "forwarding: loading parameter %u: op=%d, number=%u, ptr=%p\n", fs->f_count, op, number, copy);
#endif

  pf = &(fs->f_vector[fs->f_count]);

  pf->p_op = op;
  pf->p_num = number;
  pf->p_str = copy;

  fs->f_count = fs->f_count + 1;

  return 0;
}

/*************************/

/* %0 %1 ... %N */
/* %0+ %1+ ...  */
/* %g %c - group and client */
/* $variable */

/*************************/

int parse_parm_foward(struct katcp_dispatch *d, struct forward_symbolic_state *fs, char *ptr)
{
  unsigned int op, num;
  char *scan, *end;

  if(ptr == NULL){
    return -1;
  }

  op = MAX_FOP;
  num = 0;
  scan = NULL;

  if(ptr[0] == '%'){
    num = strtoul(ptr + 1, &end, 10);
    switch(end[0]){
      case '+' :
        op = FOP_TRAIL;
        break;
      case '\n' :
      case '\r' :
      case '\0' :
        op = FOP_SINGLE;
        break;
      case 'g' :
        if(end == (ptr + 1)){
          op = FOP_SPECIAL_GROUP;
          break;
        }
        /* WARNING: extra stupid FALL, should be a goto default */
      case 'c' :
        if(end == (ptr + 1)){
          op = FOP_SPECIAL_CLIENT;
          break;
        }
        /* CONDITIONAL FALL */
      default :
        return -1;
        break;
    }
  } else if(ptr[0] == '$'){
    scan = ptr + 1;
    op = FOP_VAR;
  } else {
    op = FOP_LITTERAL;
    scan = ptr;
  }

  return load_parm_forward(d, fs, op, num, scan);
}

struct katcl_parse *generate_relay_forward(struct katcp_dispatch *d, struct forward_symbolic_state *fs, char *name)
{
  struct katcl_parse *px, *po;
  struct forward_symbolic_parm *pf;
  struct katcp_vrbl *vx;
  struct katcp_flat *fx;
  struct katcp_vrbl_payload *py;
  struct katcp_group *gx;
  unsigned int size;
  int flags, run, result, i, limit;

  px = create_parse_katcl();
  if(px == NULL){
    return NULL;
  }

  add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?relay");
  add_string_parse_katcl(px, KATCP_FLAG_STRING, name);

  po = arg_parse_katcp(d);
  if(po == NULL){
    destroy_parse_katcl(px);
    return NULL;
  }
  size = get_count_parse_katcl(po);

  if(fs->f_count <= 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "internal error where a message triggering a relay is entirely empty");
    destroy_parse_katcl(px);
    return NULL;
  }

  limit = fs->f_count - 1;
  for(run = 1; run > 0;){

    pf = &(fs->f_vector[limit]);
    switch(pf->p_op){
      case FOP_SINGLE :
      case FOP_TRAIL :
        if(pf->p_num >= size){
          /* TODO - print insufficient parameters message, if that is always desirable ? */ 
          /* log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "internal error where a message triggering a relay is entirely empty"); */
#ifdef DEBUG
          fprintf(stderr, "forwarding: ignoring trailing parameter %u (want %u, currently have %u)\n", limit, pf->p_num, size);
#endif
          limit--;
        } else {
          run = 0;
        }
        break;
      case FOP_VAR :
#ifdef DEBUG
        fprintf(stderr, "forwarding: attempting to look up variable %s\n", pf->p_str);
#endif
        vx = find_vrbl_katcp(d, pf->p_str);
        if(vx == NULL){
          limit--;
        } else {
          py = find_payload_katcp(d, vx, pf->p_str);
          if(py == NULL){
            limit--;
          } else {
            run = 0;
          }
        }
        break;
      default :
        run = 0;
        break;
    }

    if(limit < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "malformed relay where nothing can be forwarded");
      destroy_parse_katcl(px);
      return NULL;
    }
  }

#ifdef DEBUG
  fprintf(stderr, "forwarding: intend to process %u positions\n", limit);
#endif

  for(i = 0; i <= limit; i++){
    pf = &(fs->f_vector[i]);
    flags = (i == limit) ? KATCP_FLAG_LAST : 0;
    result = 0;

#ifdef DEBUG
    fprintf(stderr, "forwarding: adding op %u with field %p\n", pf->p_op, pf->p_str);
#endif

    switch(pf->p_op){
      case FOP_LITTERAL :
        result = add_string_parse_katcl(px, KATCP_FLAG_STRING | flags, pf->p_str);
        break;
      case FOP_SINGLE :
      case FOP_TRAIL :
        if(pf->p_num < size){
          if(pf->p_op == FOP_SINGLE){
            result = add_parameter_parse_katcl(px, flags, po, pf->p_num);
          } else {
            result = add_trailing_parse_katcl(px, flags, po, pf->p_num);
          }
        } else {
          if(pf->p_op == FOP_SINGLE){
            result = add_buffer_parse_katcl(px, flags, NULL, 0);
          } else {
            result = (-1);
          }
        }
        break;
      case FOP_VAR :
        vx = find_vrbl_katcp(d, pf->p_str);
#ifdef DEBUG
        fprintf(stderr, "forwarding: look up of variable %s yields %p\n", pf->p_str, vx);
#endif
        if(vx){
          py = find_payload_katcp(d, vx, pf->p_str);
          /* WARNING: this test is for the not found case, rather than just a string variable */
          if((py == NULL) && (path_suffix_vrbl_katcp(d, pf->p_str) != NULL)){
            result = (-1);
          } else {
            result = add_payload_vrbl_katcp(d, px, flags, vx, py);
          }
        } else {
          result = (-1);
        }
        break;
      case FOP_SPECIAL_GROUP :
        gx = this_group_katcp(d);
        if(gx && gx->g_name){
          result = add_string_parse_katcl(px, flags, gx->g_name);
        } else {
          result = (-1);
        }
        break;
      case FOP_SPECIAL_CLIENT :
        fx = this_flat_katcp(d);
        if(fx && fx->f_name){
          result = add_string_parse_katcl(px, flags, fx->f_name);
        } else {
          result = (-1);
        }
        break;
      default :
#ifdef KATCP_CONSISTENCY_CHECKS 
        fprintf(stderr, "major logic problem: unknown op %u at %p while relaying request\n", pf->p_op, pf);
        abort();
#endif
        result = (-1);
    }

    if(result < 0){
      destroy_parse_katcl(px);
      return NULL;
    }
  }

  return px;
}

/************************************************************************/

struct forward_symbolic_state *create_forward_symbolic_state(struct katcp_dispatch *d)
{
  struct forward_symbolic_state *fs;
 
  fs = malloc(sizeof(struct forward_symbolic_state));
  if(fs == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate symbolic state");
    return NULL;
  }

#ifdef KATCP_CONSISTENCY_CHECKS 
  fs->f_magic = FORWARD_STATE_MAGIC;
#endif

  fs->f_peer = NULL;

  fs->f_vector = NULL;
  fs->f_count = 0;

  return fs;
}

void clear_forward_symbolic_state(void *data)
{
  struct forward_symbolic_state *fs;
  struct forward_symbolic_parm *pf;
  unsigned int i;

  fs = data;
  if(fs == NULL){
    return;
  }

#ifdef KATCP_CONSISTENCY_CHECKS 
  if(fs->f_magic != FORWARD_STATE_MAGIC){
    fprintf(stderr, "logic problem: expected %p to be a symbolic state structure, but magic 0x%x\n", fs, fs->f_magic);
    abort();
  }
  fs->f_magic = 0;
#endif

  if(fs->f_peer){
    free(fs->f_peer);
    fs->f_peer = NULL;
  }

  if(fs->f_vector){
    for(i = 0; i < fs->f_count; i++){
      pf = &(fs->f_vector[i]);
      if(pf->p_str){
        free(pf->p_str);
        pf->p_str = NULL;
      }
    }
    free(fs->f_vector);
    fs->f_vector = NULL;
  }

  fs->f_count = 0;

  free(fs);
}

int complete_relay_generic_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcl_parse *px;

#ifdef KATCP_CONSISTENCY_CHECKS
  fx = require_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not retrive current session detail");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_API);
  }
#endif

#ifdef DEBUG
  fprintf(stderr, "complete relay: got %d parameters\n", argc);
#endif

#if 0
  /* testing nonsense */
  return extra_response_katcp(d, KATCP_RESULT_FAIL, "just because");
#endif

#if 0
  cmd = arg_string_katcp(d, 0);
  if(cmd == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, "internal/usage");
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "triggering generic completion logic for %s (%d args)", cmd, argc);
#endif

  px = arg_parse_katcp(d);
  if(px == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to retrieve message from relay");
    return KATCP_RESULT_FAIL;
  }

#ifdef DEBUG
  dump_parse_katcl(px, "complete-relay", stderr);
#endif

#ifdef KATCP_CONSISTENCY_CHECKS
  if(is_request_parse_katcl(px)){
    fprintf(stderr, "complete-relay: did not expect to see a request message\n");
    abort();
  }
#endif

  if(is_inform_parse_katcl(px) && (fx->f_flags & KATCP_FLAT_RETAINFO)){
    append_parse_katcp(d, px);
  } else {
    if(is_inform_parse_katcl(px)){
      prepend_inform_katcp(d);
    } else {
      prepend_reply_katcp(d);
    }

    if(get_count_parse_katcl(px) > 1){
      append_trailing_katcp(d, KATCP_FLAG_LAST, px, 1);
    } else {
      if(is_inform_parse_katcl(px)){
        append_end_katcp(d);
      } else {
        append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, KATCP_FAIL);
      }
    }
  }

  return KATCP_RESULT_OWN;
}

int relay_generic_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *cmd, *ptr;
  unsigned int len, flags;
  struct katcp_flat *fx, *fy;
  struct katcl_parse *px, *po;
  struct katcp_endpoint *source, *target;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters (%d) to relay logic", argc);
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  name = arg_string_katcp(d, 1);
  cmd = arg_string_katcp(d, 2);

  if((name == NULL) || (cmd == NULL)){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  fx = require_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not retrive current session detail");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_API);
  }
  source = handler_of_flat_katcp(d, fx);

  fy = find_name_flat_katcp(d, NULL, name, 0);
  if(fy == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not look up name %s", name);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }
  if(fy == fx){
    target = remote_of_flat_katcp(d, fx);
  } else {
    target = handler_of_flat_katcp(d, fy);
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "sending %s ... from endpoint %p to endpoint %p", cmd, source, target);
#ifdef DEBUG
  fprintf(stderr, "dpx[%p]: sending %s ...(%d) from endpoint %p to endpoint %p at dpx[%p]\n", fx, cmd, argc, source, target, fy);
#endif

  px = create_parse_katcl();
  if(px == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  if(argc > 3){
    flags = KATCP_FLAG_FIRST;
  } else {
    flags = KATCP_FLAG_FIRST | KATCP_FLAG_LAST;
  }

  if(cmd[0] == KATCP_REQUEST){
    add_string_parse_katcl(px, flags | KATCP_FLAG_STRING, cmd);
  } else {
    len = strlen(cmd);
    ptr = malloc(len + 2);
    if(ptr == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
    }

    ptr[0] = KATCP_REQUEST;
    strcpy(ptr + 1, cmd);

    add_string_parse_katcl(px, flags | KATCP_FLAG_STRING, ptr);
    free(ptr);
  }

  if(argc > 3){
    po = arg_parse_katcp(d);
    add_trailing_parse_katcl(px, KATCP_FLAG_LAST, po, 3);
  }

  if(send_message_endpoint_katcp(d, source, target, px, 1) < 0){
    destroy_parse_katcl(px);
    return KATCP_RESULT_FAIL;
  }

  if(cmd[0] == KATCP_REQUEST){
    ptr = cmd + 1;
  } else {
    ptr = cmd;
  }

  /* TODO: use wrappers to access fx members */

  if(callback_flat_katcp(d, fx->f_current_endpoint, fx->f_rx, target, &complete_relay_generic_group_cmd_katcp, ptr, KATCP_REPLY_HANDLE_REPLIES | KATCP_REPLY_HANDLE_INFORMS)){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "unable to register callback for %s", ptr);
    return KATCP_RESULT_FAIL;
  }

  /* WARNING: complications: how do we know we have stalled handling a request ? */

  return KATCP_RESULT_OWN;
}

int perform_forward_symbolic_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_cmd_item *ix;
  struct katcp_flat *fx, *fy;
  struct forward_symbolic_state *fs;
  struct katcp_endpoint *target, *source, *origin;
  struct katcl_parse *px;
  char *req;

  req = arg_string_katcp(d, 0);
  if(req == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  ix = this_cmd_katcp(d);
  if(ix == NULL){
    return KATCP_RESULT_FAIL;
  }

  fs = ix->i_data;
  if(fs == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no state associated with %s: possibly incorrect callback", req);
    return KATCP_RESULT_FAIL;
  }

#ifdef KATCP_CONSISTENCY_CHECKS 
  if(fs->f_magic != FORWARD_STATE_MAGIC){
    fprintf(stderr, "major memory corruption: bad forward state magic 0x%x in structure %p, expected 0x%x\n", fs->f_magic, fs, FORWARD_STATE_MAGIC);
    abort();
  }
  if(fs->f_peer == NULL){
    fprintf(stderr, "major memory corruption: expected a peer name, not null in structure %p\n", fs);
    abort();
  }
#endif

  fx = require_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not retrive current session detail");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_API);
  }
  source = handler_of_flat_katcp(d, fx);

  fy = find_name_flat_katcp(d, NULL, fs->f_peer, 0);
  if(fy == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no peer matching %s available to process request %s", fs->f_peer, req);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "about to send %s to peer %s", req, fy->f_name);

  if(fy == fx){
    target = remote_of_flat_katcp(d, fx);
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "currently not able to handle requests to self");
    return KATCP_RESULT_FAIL;
  } else {
    target = handler_of_flat_katcp(d, fy);
  }

  px = generate_relay_forward(d, fs, fy->f_name);
  if(px == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  if(send_message_endpoint_katcp(d, source, target, px, 1) < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to send relay message to target %s", fy->f_name);
    destroy_parse_katcl(px);
    return KATCP_RESULT_FAIL;
  }

#if 0
  /* TODO: use wrappers to access fx members */
  /* this code path is just added, presumed nodefective, defer testing it for some other time */
  origin = sender_to_flat_katcp(d, fx);

  if(callback_flat_katcp(d, origin, po, target, &complete_relay_feneric_group_cmd_katcp, "relay", KATCP_REPLY_HANDLE_REPLIES | KATCP_REPLY_HANDLE_INFORMS)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register relay callback for %s", req);
    return KATCP_RESULT_FAIL;
  }
#else

  if(callback_flat_katcp(d, fx->f_current_endpoint, fx->f_rx, target, &complete_relay_generic_group_cmd_katcp, "relay", KATCP_REPLY_HANDLE_REPLIES | KATCP_REPLY_HANDLE_INFORMS)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register relay callback for %s", req);
    return KATCP_RESULT_FAIL;
  }
#endif

  return KATCP_RESULT_OWN;
}

int forward_symbolic_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *cmd, *ptr, *str;
  struct forward_symbolic_state *fs;
  struct katcp_cmd_map *mx;
  struct katcp_flat *fx;
  unsigned int i;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "forward logic only available within duplex handlers");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  mx = map_of_flat_katcp(fx);
  if(mx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no map associated with current connection, unable to register callback");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters, need a command and remote party");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  cmd = arg_string_katcp(d, 1);
  if(cmd == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "null command parameter");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  fs = create_forward_symbolic_state(d);
  if(fs == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate symbolic state");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  fs->f_peer = arg_copy_string_katcp(d, 2);
  if(fs->f_peer == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no valid destination could be found or copied");
    clear_forward_symbolic_state(fs);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  for(i = 3; i < argc; i++){
    str = arg_string_katcp(d, i);
    if(str == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "refusing to relay a null parameter at %u", i);
      clear_forward_symbolic_state(fs);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }
    if(parse_parm_foward(d, fs, str) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse %s into a parameter", str);
      clear_forward_symbolic_state(fs);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }
  }

  if(argc == 3){
    if(load_parm_forward(d, fs, FOP_TRAIL, 0, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to generate simple forwarding state for %s", cmd);
      clear_forward_symbolic_state(fs);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
  }

  /* this isn't quite ideal, we could end up handling a request handler within an inform map. Maybe eliminate the ptr code completely, not needed by add_full_cmd_map anyway */
  ptr = default_message_type_katcm(cmd, KATCP_REQUEST);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to duplicate %s", cmd);
    clear_forward_symbolic_state(fs);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "about to register request %s to %s", ptr, fs->f_peer);

  if(add_full_cmd_map_katcp(mx, ptr, "user requested relay", 0, &perform_forward_symbolic_group_cmd_katcp, fs, &clear_forward_symbolic_state) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register handler for %s", ptr);
    free(ptr);
    clear_forward_symbolic_state(fs);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "registered forwarding handler for %s to %s", ptr, fs->f_peer);
  free(ptr);

  return KATCP_RESULT_OK;
}

#endif
