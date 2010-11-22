/* lower level parsing routes, arbitrarily having the katcl suffix */
/* these routines do their own IO buffering over file descriptors */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "katpriv.h"
#include "katcl.h"
#include "katcp.h"

/****************************************************************/

#ifdef DEBUG
static void sane_line_katcl(struct katcl_line *l)
{
  if(l == NULL){
    fprintf(stderr, "sane: line is null\n");
    abort();
  }
  if(l->l_next == NULL){
    fprintf(stderr, "sane: line has no valid next\n");
    abort();
  }
}
#else
#define sane_line_katcl(l)
#endif

/****************************************************************/

static void cache_spare_katcl(struct katcl_line *l, struct katcl_parse *p)
{
  clear_parse_katcl(p);

  p->p_next = l->l_spare;
  l->l_spare = p;
}

static struct katcl_parse *fetch_spare_katcl(struct katcl_line *l)
{
  struct katcl_parse *p;

  if(l->l_spare){
    p = l->l_spare;
    l->l_spare = p->p_next;
    p->p_next = NULL;
  } else {
    p = create_parse_katcl();
  }

  return p;
}

struct katcl_line *create_katcl(int fd)
{
  struct katcl_line *l;

  l = malloc(sizeof(struct katcl_line));
  if(l == NULL){
    return NULL;
  }

  l->l_fd = fd;

  l->l_ready = NULL;
  l->l_next = NULL; 

  l->l_head = NULL;
  l->l_tail = NULL;
  l->l_stage = NULL;

  l->l_pending = 0;
  l->l_arg = 0;
  l->l_offset = 0;

  l->l_spare = NULL;

  l->l_error = 0;

  l->l_next = create_parse_katcl(l); /* we require that next is always valid */
  if(l->l_next == NULL){
    destroy_katcl(l, 0);
    return NULL;
  }

  return l;
}

void destroy_katcl(struct katcl_line *l, int mode)
{
  if(l == NULL){
    return;
  }

  /* in */

  if(l->l_ready){
    destroy_parse_katcl(l->l_ready);
    l->l_ready = NULL;
  }
  if(l->l_next){
    destroy_parse_katcl(l->l_next);
    l->l_next = NULL;
  }

  /* out */

  if(l->l_stage){
    destroy_parse_katcl(l->l_stage);
    l->l_stage = NULL;
  }
  if(l->l_head){ /* WARNING: a linked list */
    destroy_parse_katcl(l->l_head);
    l->l_head = NULL;
  }
  l->l_tail = NULL; /* tail part of linked list */

  l->l_pending = 0;
  l->l_arg = 0;
  l->l_offset = 0;

  /* spares */

  if(l->l_spare){ /* WARNING: a linked list */
    destroy_parse_katcl(l->l_spare);
    l->l_spare = NULL;
  }

  if(mode){
    if(l->l_fd >= 0){
      close(l->l_fd);
      l->l_fd = (-1);
    }
  }

  free(l);
}

void exchange_katcl(struct katcl_line *l, int fd)
{
  struct katcl_parse *p;

  sane_line_katcl(l);

  /* WARNING: exchanging fds forces discarding of pending io */

#ifdef DEBUG
  if(l->l_head && (l->l_tail == NULL)){
    fprintf(stderr, "logic problem: line has a head but no tail\n");
    abort();
  }
  if(l->l_tail && (l->l_head == NULL)){
    fprintf(stderr, "logic problem: line has a tail but no head\n");
    abort();
  }
  if(l->l_tail && (l->l_tail->p_next != NULL)){
    fprintf(stderr, "logic problem: tail is not tail\n");
    abort();
  }
#endif

  if(l->l_fd >= 0){
    close(l->l_fd);
  }
  l->l_fd = fd;

  /* move ready to spare */
  if(l->l_ready){
    cache_spare_katcl(l, l->l_ready);
    l->l_ready = NULL;
  }

  if(l->l_next){
    clear_parse_katcl(l->l_next);
  }

  /* move all of head to spare */
  while(l->l_head){
    p = l->l_head;
    l->l_head = p->p_next;

    cache_spare_katcl(l, p);
  }
  l->l_tail = NULL;

  if(l->l_stage){
    cache_spare_katcl(l, l->l_stage);
    l->l_stage = NULL;
  }

  l->l_pending = 0;
  l->l_arg = 0;
  l->l_offset = 0;
}

int fileno_katcl(struct katcl_line *l)
{
  return l ? l->l_fd : (-1);
}

int error_katcl(struct katcl_line *l)
{
  int result;

  if(l == NULL){
    return 1;
  }

  result = l->l_error;

  l->l_error = 0;

  return result;
}

/***********************************************************************************/

int read_katcl(struct katcl_line *l)
{
  int rr;
  char *ptr;
  struct katcl_parse *p;

  sane_line_katcl(l);

#ifdef DEBUG
  if(l->l_next == NULL){
    fprintf(stderr, "read: logic problem: no next parse allocated\n");
    abort();
  }
#endif

  p = l->l_next;

  if(p->p_size <= p->p_have){
    ptr = realloc(p->p_buffer, p->p_size + KATCL_BUFFER_INC);
    if(ptr == NULL){
#ifdef DEBUG
      fprintf(stderr, "read: realloc to %d failed\n", p->p_size + KATCL_BUFFER_INC);
#endif
      l->l_error = ENOMEM;
      return -1;
    }

    p->p_buffer = ptr;
    p->p_size += KATCL_BUFFER_INC;
  }

  rr = read(l->l_fd, p->p_buffer + p->p_have, p->p_size - p->p_have);
  if(rr < 0){
    switch(errno){
      case EAGAIN :
      case EINTR  :
        return 0; /* more to be read */
      default : /* serious error */
        l->l_error = errno;
#ifdef DEBUG
        fprintf(stderr, "read: read error: %s\n", strerror(errno));
#endif
        return -1;
    }
  }

  if(rr == 0){ /* EOF */
#ifdef DEBUG
    fprintf(stderr, "read: end of file\n");
#endif
    return 1;
  }

  p->p_have += rr;
  return 0;
}

struct katcl_parse *ready_katcl(struct katcl_line *l) /* do we have a full line ? */
{
  return l->l_ready;
}

void clear_katcl(struct katcl_line *l) /* discard a full line */
{
  if(l->l_ready == NULL){
#ifdef DEBUG
    fprintf(stderr, "unusual: clearing something which is empty\n");
#endif
    return;
  }

  cache_spare_katcl(l, l->l_ready);
  l->l_ready = NULL;
}

int have_katcl(struct katcl_line *l)
{
  if(ready_katcl(l)){
#ifdef DEBUG
    fprintf(stderr, "clearing data buffer\n");
#endif
    clear_katcl(l);
  }

  return parse_katcl(l);
}

/***********************************************************************************/

unsigned int arg_count_katcl(struct katcl_line *l)
{
  if(l->l_ready == NULL){
    return 0;
  }

  return get_count_parse_katcl(l->l_ready);
}

int arg_tag_katcl(struct katcl_line *l)
{
  if(l->l_ready == NULL){
    return -1;
  }

  return get_tag_parse_katcl(l->l_ready);
}

static int arg_type_katcl(struct katcl_line *l, char mode)
{
  if(l->l_ready == NULL){
    return -1;
  }

  return is_type_parse_katcl(l->l_ready, mode);
}

int arg_request_katcl(struct katcl_line *l)
{
  return arg_type_katcl(l, KATCP_REQUEST);
}

int arg_reply_katcl(struct katcl_line *l)
{
  return arg_type_katcl(l, KATCP_REPLY);
}

int arg_inform_katcl(struct katcl_line *l)
{
  return arg_type_katcl(l, KATCP_INFORM);
}

int arg_null_katcl(struct katcl_line *l, unsigned int index)
{
  if(l->l_ready == NULL){
    return -1;
  }

  return is_null_parse_katcl(l->l_ready, index);
}

char *arg_string_katcl(struct katcl_line *l, unsigned int index)
{
  if(l->l_ready == NULL){
    return NULL;
  }

  return get_string_parse_katcl(l->l_ready, index);
}

char *arg_copy_string_katcl(struct katcl_line *l, unsigned int index)
{
  if(l->l_ready == NULL){
    return NULL;
  }

  return copy_string_parse_katcl(l->l_ready, index);
}

unsigned long arg_unsigned_long_katcl(struct katcl_line *l, unsigned int index)
{
  if(l->l_ready == NULL){
    return 0;
  }

  return get_unsigned_long_parse_katcl(l->l_ready, index);
}

#ifdef KATCP_USE_FLOATS
double arg_double_katcl(struct katcl_line *l, unsigned int index)
{
  if(l->l_ready == NULL){
    return 0.0; /* TODO: maybe NAN instead ? */
  }

  return get_double_parse_katcl(l->l_ready, index);
}
#endif

unsigned int arg_buffer_katcl(struct katcl_line *l, unsigned int index, void *buffer, unsigned int size)
{
  if(l->l_ready == NULL){
    return 0;
  }

  return get_buffer_parse_katcl(l->l_ready, index, buffer, size);
}

/******************************************************************/

static struct katcl_parse *before_append_katcl(struct katcl_line *l, int flags)
{
  if(l->l_stage == NULL){

    if(!(flags & KATCP_FLAG_FIRST)){
#ifdef DEBUG
      fprintf(stderr, "discarding appended data as stage empty\n");
#endif
      return NULL;
    }

    l->l_stage = fetch_spare_katcl(l);
  }

  return l->l_stage;
}

static int after_append_katcl(struct katcl_line *l, int flags, int result)
{
  if(result < 0){ /* things went wrong, throw away the entire line */
#ifdef DEBUG
    fprintf(stderr, "append failed, discarding stage\n");
#endif
    if(l->l_stage){
      cache_spare_katcl(l, l->l_stage);
      l->l_stage = NULL;
    }
    return result;
  }

  if(!(flags & KATCP_FLAG_LAST)){
#if DEBUG > 1
    fprintf(stderr, "after append: flag not last, not doing anything\n");
#endif
    return result;
  }

  /* probably redundant, before_append_katcl needs to check for this too */
  if(l->l_stage == NULL){
#ifdef DEBUG
    fprintf(stderr, "after append: no stage variable\n");
#endif
    return -1;
  }

#ifdef DEBUG
  if(l->l_stage->p_next){
    fprintf(stderr, "logic problem: stage %p should not have a next\n", l->l_stage); 
    abort();
  }
#endif

  /* things went ok, now queue the complete message */
  if(l->l_tail == NULL){
#ifdef DEBUG
    fprintf(stderr, "after append: adding first parse %p to queue\n", l->l_stage);
    if(l->l_head){
      fprintf(stderr, "logic problem: tail=%p, head=%p\n", l->l_tail, l->l_head);
      abort();
    }
#endif
    l->l_tail = l->l_stage;
    l->l_head = l->l_tail;
  } else {
#ifdef DEBUG
    fprintf(stderr, "after append: %p->p_next=%p\n", l->l_tail, l->l_stage);
    if(l->l_tail->p_next){
      fprintf(stderr, "logic problem: tail=%p has next=%p\n", l->l_tail, l->l_tail->p_next);
    }
#endif
    l->l_tail->p_next = l->l_stage;
    l->l_tail = l->l_stage;
  }

  l->l_stage = NULL;

  return result;
}

/******************************************************************/

int append_vargs_katcl(struct katcl_line *l, int flags, char *fmt, va_list args)
{
  struct katcl_parse *p;
  int result;

  p = before_append_katcl(l, flags);
  if(p == NULL){
    return -1;
  }

  result = add_vargs_parse_katcl(p, flags, fmt, args);

  return after_append_katcl(l, flags, result);
}

int append_args_katcl(struct katcl_line *l, int flags, char *fmt, ...)
{
  va_list args;
  struct katcl_parse *p;
  int result;

  p = before_append_katcl(l, flags);
  if(p == NULL){
    return -1;
  }

  va_start(args, fmt);
  result = add_vargs_parse_katcl(p, flags, fmt, args);
  va_end(args);

  return after_append_katcl(l, flags, result);
}

int append_string_katcl(struct katcl_line *l, int flags, char *buffer)
{
  struct katcl_parse *p;
  int result;

  p = before_append_katcl(l, flags);
  if(p == NULL){
    return -1;
  }

  result = add_string_parse_katcl(p, flags, buffer);

  return after_append_katcl(l, flags, result);
}

int append_unsigned_long_katcl(struct katcl_line *l, int flags, unsigned long v)
{
  struct katcl_parse *p;
  int result;

  p = before_append_katcl(l, flags);
  if(p == NULL){
    return -1;
  }

  result = add_unsigned_long_parse_katcl(p, flags, v);

  return after_append_katcl(l, flags, result);
}

int append_signed_long_katcl(struct katcl_line *l, int flags, unsigned long v)
{
  struct katcl_parse *p;
  int result;

  p = before_append_katcl(l, flags);
  if(p == NULL){
    return -1;
  }

  result = add_signed_long_parse_katcl(p, flags, v);

  return after_append_katcl(l, flags, result);
}

int append_hex_long_katcl(struct katcl_line *l, int flags, unsigned long v)
{
  struct katcl_parse *p;
  int result;

  p = before_append_katcl(l, flags);
  if(p == NULL){
    return -1;
  }

  result = add_hex_long_parse_katcl(p, flags, v);

  return after_append_katcl(l, flags, result);
}

#ifdef KATCP_USE_FLOATS
int append_double_katcl(struct katcl_line *l, int flags, double v)
{
  struct katcl_parse *p;
  int result;

  p = before_append_katcl(l, flags);
  if(p == NULL){
    return -1;
  }

  result = add_double_parse_katcl(p, flags, v);

  return after_append_katcl(l, flags, result);
}
#endif

int append_buffer_katcl(struct katcl_line *l, int flags, void *buffer, int len)
{
  struct katcl_parse *p;
  int result;

  p = before_append_katcl(l, flags);
  if(p == NULL){
#ifdef DEBUG
    fprintf(stderr, "append: before_append failed\n");
#endif
    return -1;
  }

  result = add_buffer_parse_katcl(p, flags, buffer, len);

  return after_append_katcl(l, flags, result);
}

int append_parse_katcl(struct katcl_line *l, struct katcl_parse *p, int move)
{
  struct katcl_parse *px;

#ifdef DEBUG
  if(p->p_state != KATCL_PARSE_DONE){
    fprintf(stderr, "problem: attempting to append a message in state %u\n", p->p_state);
    abort();
  }
#endif

  if(move){
#ifdef DEBUG
    fprintf(stderr, "append: grabbing %p (with next %p)\n", p, p->p_next);
#endif
    px = p;
  } else {
    px = copy_parse_katcl(NULL, p);
    if(px == NULL){
#ifdef DEBUG
      fprintf(stderr, "append: unable to duplicate parse\n");
#endif
      return -1;
    }
  }

  if(l->l_tail == NULL){
    l->l_tail = px;
    l->l_head = l->l_tail;
  } else {
    l->l_tail->p_next = px;
    l->l_tail = px;
  }

  return 0;
}

/**************************************************************/

int vsend_katcl(struct katcl_line *l, va_list args)
{
  int flags, result, check;
  char *string;
  void *buffer;
  unsigned long value;
  int len;
#ifdef KATCP_USE_FLOATS
  double dvalue;
#endif

  check = KATCP_FLAG_FIRST;
  
  do{
    flags = va_arg(args, int);
    if((check & flags) != check){
      /* WARNING: tests first arg for FLAG_FIRST */
      return -1;
    }
    check = 0;

    switch(flags & KATCP_TYPE_FLAGS){
      case KATCP_FLAG_STRING :
        string = va_arg(args, char *);
        result = append_string_katcl(l, flags & KATCP_ORDER_FLAGS, string);
        break;
      case KATCP_FLAG_SLONG :
        value = va_arg(args, unsigned long);
        result = append_signed_long_katcl(l, flags & KATCP_ORDER_FLAGS, value);
        break;
      case KATCP_FLAG_ULONG :
        value = va_arg(args, unsigned long);
        result = append_unsigned_long_katcl(l, flags & KATCP_ORDER_FLAGS, value);
        break;
      case KATCP_FLAG_XLONG :
        value = va_arg(args, unsigned long);
        result = append_hex_long_katcl(l, flags & KATCP_ORDER_FLAGS, value);
        break;
      case KATCP_FLAG_BUFFER :
        buffer = va_arg(args, void *);
        len = va_arg(args, int);
        result = append_buffer_katcl(l, flags & KATCP_ORDER_FLAGS, buffer, len);
        break;
#ifdef KATCP_USE_FLOATS
        case KATCP_FLAG_DOUBLE :
        dvalue = va_arg(args, double);
        result = append_double_katcl(l, flags & KATCP_ORDER_FLAGS, dvalue);
        break;
#endif
      default :
        result = (-1);
    }
#if DEBUG > 1
    fprintf(stderr, "vsend: appended: flags=0x%02x, result=%d\n", flags, result);
#endif
    if(result <= 0){
      return -1;
    }
  } while(!(flags & KATCP_FLAG_LAST));

  return 0;
}

/* This function is *NOT* a printf style function, instead
 * a format string, each value to be output is preceeded by
 * a flag field describing its type, as well as its position.
 * The first parameter needs to include the KATCP_FLAG_FIRST flag,
 * the last one has to contain the KATCP_FLAG_LAST. In the case
 * of binary output, it is necessary to include a further parameter
 * containing the length of the value. Consider this function to 
 * be a convenience which allows one perform multiple append_*_katcp 
 * calls in one go
 */

int send_katcl(struct katcl_line *l, ...)
{
  int result;
  va_list args;

  va_start(args, l);
  result = vsend_katcl(l, args);
  va_end(args);

  return result;
}

#if 0
int parsed_relay_katcl(struct katcl_parse *p, struct katcl_msg *m)
{
  int i, flag, need, len;
  int status;

  flag = KATCP_FLAG_FIRST;

  need = 0;
  status = 0;

  for(i = 0; i < p->p_got; i++){
    if((i + 1) >= p->p_got){
      flag |= KATCP_FLAG_LAST;
    }

    len = p->p_args[i].a_end - p->p_args[i].a_begin;
    need += len;

    if (queue_buffer_katcl(m, flag, p->p_buffer + p->p_args[i].a_begin, len) < 0){
      status = -1;
    }
    flag = 0;
  }

#ifdef DEBUG
  fprintf(stderr, "copied line of %d args from %p (%d arg bytes) to %p (status: %d)\n", i, p, need, m, status);
#endif

  return status;
}
#endif

int relay_katcl(struct katcl_line *lx, struct katcl_line *ly)
{
  if(lx->l_ready == NULL){
    return -1;
  }

  return append_parse_katcl(ly, lx->l_ready, 0);
}

/***************************/

static int escape_copy_katcl(char *dst, char *src, unsigned int want)
{
  /* returns amount actually consumed, assumes that dst has at least 2 + 2*want available */
  int i, j;
  char v;

  j = 0;

  for(j = 0, i = 0; i < want; i++){
    switch(src[i]){
      case  27  : v = 'e';  break;
      case '\n' : v = 'n';  break;
      case '\r' : v = 'r';  break;
      case '\0' : v = '0';  break;
      case '\\' : v = '\\'; break;
      case ' '  : v = '_';  break; 
      case '\t' : v = 't';  break;
      default   : 
        dst[j++] = src[i];
        continue; /* WARNING: restart loop */
    }
    dst[j++] = '\\';
    dst[j++] = v;
  }

  return j;
}

#define WRITE_STATE_DONE   0
#define WRITE_STATE_FILL   1
#define WRITE_STATE_NEXT   2
#define WRITE_STATE_SEND   3

int write_katcl(struct katcl_line *l)
{
  int wr;
  int state;
  unsigned int space, want, can, actual;
  struct katcl_parse *p;
  struct katcl_larg *la;
#define TMP_MARGIN 32

  p = NULL;

  for(state = WRITE_STATE_FILL; state != WRITE_STATE_DONE; ){
#ifdef DEBUG
    fprintf(stderr, "write: state=%d, parse=%p, arg=%u, offset=%u, pending=%u\n", state, p, l->l_arg, l->l_offset, l->l_pending);
#endif
    switch(state){

      case WRITE_STATE_FILL : 

        if(l->l_head == NULL){
          state = (l->l_pending > 0) ? WRITE_STATE_SEND : WRITE_STATE_DONE;
          continue; /* WARNING */
        } 
        p = l->l_head;

        if((l->l_pending + TMP_MARGIN) > KATCL_IO_SIZE){
          state = WRITE_STATE_SEND; /* drain if no margin */
          continue; /* WARNING */
        }

#ifdef DEBUG
        if(l->l_arg >= p->p_got){
          fprintf(stderr, "write: logic problem: arg=%u >= got=%u\n", l->l_arg, p->p_got);
          abort();
        }
#endif

        la = &(p->p_args[l->l_arg]);

#ifdef DEBUG
        if((la->a_begin + l->l_offset) > la->a_end){
          fprintf(stderr, "write: logic problem: offset=%u extends beyond argument %u (%u-%u)\n", l->l_offset, l->l_arg, la->a_begin, la->a_end);
          abort();
        }
#endif

        if((la->a_begin + l->l_offset) >= la->a_end){ /* done ? */
          if(l->l_offset == 0){ /* special case - null arg */
#ifdef DEBUG
            if(l->l_arg == 0){
              fprintf(stderr, "write: problem - arg0 is null\n");
              abort();
            }
#endif
            strcpy(l->l_buffer + l->l_pending, "\\@");
            l->l_pending += 2;
          }
          if(la->a_escape <= 1){ /* mark things which were thought to need escaping, but did not appropriately */
            la->a_escape = 0;
          }

          l->l_arg++;
          l->l_offset = 0;
          state = WRITE_STATE_NEXT;

          continue;
        }

        want = la->a_end - (la->a_begin + l->l_offset);
        space = KATCL_IO_SIZE - (l->l_pending + 1);

#ifdef DEBUG
        fprintf(stderr, "write: arg[%u] has %u more, space is %u\n", l->l_arg, want, space);
#endif

        if(la->a_escape){
          can = ((space / 2) >= want) ? want : space / 2;
          actual = escape_copy_katcl(l->l_buffer + l->l_pending, p->p_buffer + la->a_begin + l->l_offset, can);
          if(actual > can){
            la->a_escape = 2; /* record that we needed to escape */
          }
          l->l_pending += actual;
        } else {
          can = (space >= want) ? want : space;
          memcpy(l->l_buffer + l->l_pending, p->p_buffer + la->a_begin + l->l_offset, can);
          l->l_pending += can;
        }
        l->l_offset += can;

        break;

      case WRITE_STATE_NEXT : /* move onto next */
        /* gets a proposed position, updates to next parse or end if needed, adding separators and tags */

        if(l->l_head == NULL){ /* done */
          l->l_tail = NULL;
          l->l_offset = 0;
          l->l_arg = 0;
          state = WRITE_STATE_SEND;
          continue;
        }

        if(l->l_pending >= KATCL_IO_SIZE){
          fprintf(stderr, "write: logic problem: pending=%u, no space left\n", l->l_pending);
        }
      
        p = l->l_head;
 
        if((p->p_tag >= 0) && (l->l_arg == 1)){
          /* !#$ : TODO: enter tag printing state */
        }

        if(l->l_arg < p->p_got){ /* more args */
          state = WRITE_STATE_FILL;
          l->l_buffer[l->l_pending++] = ' ';
          continue;
        }

        l->l_arg = 0;
        l->l_buffer[l->l_pending++] = '\n';

        /* let go of processed item */
        l->l_head = p->p_next;
        cache_spare_katcl(l, p);

        if(l->l_head == NULL){
          l->l_tail = NULL;
        }

        state = l->l_head ? WRITE_STATE_FILL : WRITE_STATE_SEND;
        break;

      case WRITE_STATE_SEND : /* do io */

        if(l->l_pending <= 0){ /* nothing more to write ? */
          state = l->l_head ? WRITE_STATE_FILL : WRITE_STATE_DONE;
        }

        wr = send(l->l_fd, l->l_buffer, l->l_pending, MSG_DONTWAIT | MSG_NOSIGNAL);
        if(wr < 0){
          switch(errno){
            case EAGAIN :
            case EINTR  :
              return 0; /* returns zero if still more to do */
            default :
              l->l_error = errno;
              return -1;
          }
        }

        if(wr > 0){
          if(wr < l->l_pending){ /* a tad expensive, but we expect this to be rare */
            memmove(l->l_buffer, l->l_buffer + wr, l->l_pending - wr);
            l->l_pending -= wr;
          } else {
            l->l_pending = 0;
          }
        }

        break;
    }
  }

  /* done everything */
  return 1;
#undef TMP_MARGIN
}

#if 0
int write_katcl(struct katcl_line *l)
{
  int wr;
  struct katcl_parse *p;

  while(l->l_head){
    p = l->l_head;

    if(p->p_wrote < p->p_kept){
      wr = send(l->l_fd, p->p_buffer + p->p_wrote, p->p_kept - p->p_wrote, MSG_DONTWAIT | MSG_NOSIGNAL);
      if(wr < 0){
        switch(errno){
          case EAGAIN :
          case EINTR  :
            return 0; /* returns zero if still more to do */
          default :
            l->l_error = errno;
            return -1;
        }
      } else {
        p->p_wrote += wr;
      }
    }

    l->l_head = p->p_next;

    clear_parse_katcl(p);
    p->p_next = l->l_spare;
    l->l_spare = p;
  }

  /* done everything */
  l->l_tail = NULL;
  return 1;
}
#endif

int flushing_katcl(struct katcl_line *l)
{
#if DEBUG > 1
  fprintf(stderr, "flushing: %p->l_head=%p\n", l, l->l_head);
#endif
  return l->l_head ? 1 : 0;
}

/***************************/

#if 0
int error_katcl(struct katcl_line *l)
{
  return l->l_error;
}
#endif

void dump_katcl(struct katcl_line *l, FILE *fp)
{
  fprintf(fp, "input: fd=%d\n", l->l_fd);

  dump_parse_katcl(l->l_ready, "ready", fp);
  dump_parse_katcl(l->l_next, "next", fp);
  dump_parse_katcl(l->l_spare, "spare", fp);

  fflush(fp);

  append_parse_katcl(l, l->l_ready, 0);
  write_katcl(l);
}

#ifdef UNIT_TEST_LINE

#define MAX_ARG_COUNT     10
#define MAX_ARG_LEN     5120 /* for real test this should be bigger  */
#define TEST_RUNS        100 /* as should this */
#define INIT_BUFFER     1024

int fill_random_test(struct katcl_parse *p)
{
  int max, flags, j, i, size;
  char buffer[MAX_ARG_LEN];

  max = 1 + rand() % MAX_ARG_COUNT;
  size = rand() % MAX_ARG_LEN;

  flags = KATCP_FLAG_FIRST;

  for(i = 0; i < max; i++){

    if((i + 1) == max){
      flags |= KATCP_FLAG_LAST;
    }

    if(i == 0){
      size = 1 + (rand() % (MAX_ARG_LEN - 1));
      buffer[0] = '?';
      for(j = 1; j < size; j++){
        buffer[j] = 0x61 + rand() % 26;
      }
    } else {
      size = rand() % MAX_ARG_LEN;
      for(j = 0; j < size; j++){
        buffer[j] = rand() % 256;
      }
    }

    add_buffer_parse_katcl(p, flags, buffer, size);

    flags = 0;
  }

  return 0;
}

int echobuffer(int fd)
{
  char *ptr;
  int size, have, rr, wr, sr, done;
  fd_set fsr, fsw;

  size = INIT_BUFFER;
  have = 0;
  done = 0;

  ptr = malloc(INIT_BUFFER);
  if(ptr == NULL){
    return -1;
  }

  for(;;){ 
    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    FD_SET(fd, &fsr);
    if(have > 0){
      FD_SET(fd, &fsw);
    }

    if(have >= size){
      ptr = realloc(ptr, size * 2);
      if(ptr == NULL){
        fprintf(stderr, "malloc failed: with %d bytes\n", size * 2);
        return -1;
      }

      size *= 2;
    }

    sr = select(fd + 1, &fsr, &fsw, NULL, NULL);
    if(sr < 0){
      return -1;
    }

    if(FD_ISSET(fd, &fsw)){
      wr = write(fd, ptr + done, have);
      if(wr < 0){
        fprintf(stderr, "write failed: %s\n", strerror(errno));
        return -1;
      } else {
        done += wr;
        if(done > have / 2){
          if(done < have){
            memmove(ptr, ptr + done, have - done);
            have -= done;
          } else {
            have = 0;
          }
          done = 0;
        }
      }
    }

    if(FD_ISSET(fd, &fsr)){
      rr = read(fd, ptr + have, size - have);
      if(rr > 0){
        have += rr;
      } else if(rr == 0){
        return 0;
      } else {
        fprintf(stderr, "read failed: %s\n", strerror(errno));
        return -1;
      }
    }

  }
}

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

int main()
{
  struct katcl_line *l;
  struct katcl_parse *p;
  int count, seed, i, k, fds[2], result, al, bl;
  char alpha[MAX_ARG_LEN], beta[MAX_ARG_LEN];
  pid_t pid;

  seed = getpid();
  printf("line test: seed is %d\n", seed);
  srand(seed);

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0){
    fprintf(stderr, "line: unable to create socketpair\n");
    return 1;
  }

  pid = fork();
  if(pid < 0){
    fprintf(stderr, "line: unable to fork\n");
    return 1;
  }

  if(pid == 0){
    close(fds[0]);
    echobuffer(fds[1]);
    return 0;
  }

  close(fds[1]);

  l = create_katcl(fds[0]);
  if(l == NULL){
    fprintf(stderr, "main: unable to create katcl\n");
    return 1;
  }

  for(i = 0; i < TEST_RUNS; i++){
    p = create_parse_katcl();
    if(p == NULL){
      fprintf(stderr, "unable to create parse instance %d\n", i);
      return 1;
    }

    fill_random_test(p);
    dump_parse_katcl(p, "random", stderr);

    if(append_parse_katcl(l, p, 0) < 0){ /* leak */
      fprintf(stderr, "unable to add parse %d\n", i);
      return 1;
    }

    while((result = write_katcl(l)) == 0);

    if(result < 0){
      fprintf(stderr, "unable to write data\n");
      return 1;
    }

    do{
      result = read_katcl(l);
      if(result){
        fprintf(stderr, "read result is %d\n", result);
        return 1;
      }
    } while(have_katcl(l) == 0);

    count = arg_count_katcl(l);

    for(k = 0; k < count; k++){
      al = arg_buffer_katcl(l, k, alpha, MAX_ARG_LEN);
      bl = get_buffer_parse_katcl(p, k, beta, MAX_ARG_LEN);

      if((bl < 0) || (al < 0)){
        fprintf(stderr, "al=%d, bl=%d\n", al, bl);
        return 1;
      }

      if(al != bl){
        fprintf(stderr, "al=%d != bl=%d\n", al, bl);
        return 1;
      }

      if(memcmp(alpha, beta, al)){
        fprintf(stderr, "mismatch: round=%d, arg=%d\n", i, k); 
        return 1;
      }
    }

    fprintf(stderr, "parsed a line with %d words\n", count);
  }

  destroy_katcl(l, 1);

  printf("line test: ok\n");

  return 0;
}

#endif
