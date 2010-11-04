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

/******************************************************************************/

#if 0
int queue_buffer_katcl(struct katcl_msg *m, int flags, void *buffer, int len)
{
  /* returns greater than zero on success */

  unsigned int newsize, had, want, need, result, i;
  char *s, *tmp, v;

#if 0 /* phase out FLAG_MORE */
  exclusive = KATCP_FLAG_MORE | KATCP_FLAG_LAST;
  if((flags & exclusive) == exclusive){
#ifdef DEBUG
    fprintf(stderr, "queue: usage problem: can not have last and more together\n");
#endif
    return -1;
  }
#endif

  if(len < 0){
    return -1;
  }

  s = buffer;
  want = buffer ? len : 0;

  /* extra checks */
  if(flags & KATCP_FLAG_FIRST){
    if(s == NULL){
      return -1;
    }
    if(m->m_complete != 1){
#ifdef DEBUG
      fprintf(stderr, "queue: usage problem: starting new message without having completed previous one");
#endif
      return -1;
    }
    switch(s[0]){
      case KATCP_INFORM  :
      case KATCP_REPLY   :
      case KATCP_REQUEST :
        break;
      default :
#ifdef DEBUG
        fprintf(stderr, "queue: usage problem: start of katcp message does not look valid\n");
#endif
        return -1;
    }
  }

  had = m->m_want;
  need = 1 + ((want > 0) ? (want * 2) : 2); /* a null token requires 2 bytes, all others a maximum of twice the raw value, plus one for trailing stuff */

  if((m->m_want + need) >= m->m_size){
    newsize = m->m_want + need;
    tmp = realloc(m->m_buffer, newsize);
    if(tmp == NULL){
#ifdef DEBUG
      fprintf(stderr, "queue: unable to resize output to <%u> bytes\n", newsize);
#endif
      return -1;
    }
    m->m_buffer = tmp;
    m->m_size = newsize;
  }

#ifdef DEBUG
  fprintf(stderr, "queue: length=%d, want=%d, need=%d, size=%d, had=%d\n", len, want, need, m->m_size, had);
#endif

  for(i = 0; i < want; i++){
    switch(s[i]){
      case  27  : v = 'e';  break;
      case '\n' : v = 'n';  break;
      case '\r' : v = 'r';  break;
      case '\0' : v = '0';  break;
      case '\\' : v = '\\'; break;
      case ' '  : v = '_';  break; 
#if 0
      case ' '  : v = ' ';  break; 
#endif
      case '\t' : v = 't';  break;
      default   : 
        m->m_buffer[m->m_want++] = s[i];
        continue; /* WARNING: restart loop */
    }
    m->m_buffer[m->m_want++] = '\\';
    m->m_buffer[m->m_want++] = v;
  }

  /* check if we are dealing with null args, if so put in null token */
#if 0
  if(!(flags & KATCP_FLAG_MORE) || (flags & KATCP_FLAG_LAST)){
#endif
    if((m->m_want > 0) && (m->m_buffer[m->m_want - 1] == ' ')){ /* WARNING: convoluted heuristic: add a null token if we haven't added anything sofar and more isn't comming */
#ifdef DEBUG
      fprintf(stderr, "queue: warning: empty argument considered bad form\n");
#endif
      m->m_buffer[m->m_want++] = '\\';
      m->m_buffer[m->m_want++] = '@';
    }
#if 0
  }
#endif

  if(flags & KATCP_FLAG_LAST){
    m->m_buffer[m->m_want++] = '\n';
    m->m_complete = 1;
  } else {
#if 0
    if(!(flags & KATCP_FLAG_MORE)){
      m->m_buffer[m->m_want++] = ' ';
    }
#else
    m->m_buffer[m->m_want++] = ' ';
#endif
    m->m_complete = 0;
  }

  result = m->m_want - had;

  return result;
}
#endif

/****************************************************************/

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
  l->l_pos = 0;

  l->l_spare = NULL;

  l->l_error = 0;

  l->l_next = create_parse_katcl(l);
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
  l->l_pos = 0;

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
    clear_parse_katcl(l->l_ready);
    l->l_ready->p_next = l->l_spare;
    l->l_spare = l->l_ready;
    l->l_ready = NULL;
  }

  if(l->l_next){
    clear_parse_katcl(l->l_next);
  }

  /* move all of head to spare */
  while(l->l_head){
    p = l->l_head;
    l->l_head = p->p_next;

    clear_parse_katcl(p);

    p->p_next = l->l_spare;
    l->l_spare = p;
  }
  l->l_tail = NULL;

  if(l->l_stage){
    clear_parse_katcl(l->l_stage);
    l->l_stage->p_next = l->l_spare;
    l->l_spare = l->l_stage;
    l->l_stage = NULL;
  }

  l->l_pending = 0;
  l->l_arg = 0;
  l->l_pos = 0;
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

int ready_katcl(struct katcl_line *l) /* do we have a full line ? */
{
  return (l->l_ready == NULL) ? 0 : 1;
}

void clear_katcl(struct katcl_line *l) /* discard a full line */
{
  struct katcl_parse *p;

  if(l->l_ready == NULL){
#ifdef DEBUG
    fprintf(stderr, "unusual: clearing something which is empty\n");
#endif
    return;
  }

  p = l->l_ready;
  l->l_ready = NULL;

  clear_parse_katcl(p);

  p->p_next = l->l_spare;
  l->l_spare = p;
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

    if(l->l_spare){

      l->l_stage = l->l_spare;
      l->l_spare = l->l_stage->p_next;
      l->l_stage->p_next = NULL;

    } else {

      l->l_stage = create_parse_katcl();
      if(l->l_stage == NULL){
        return NULL;
      }
    }
  }

  return l->l_stage;
}

static int after_append_katcl(struct katcl_line *l, int flags, int result)
{
  if(result < 0){ /* things went wrong, throw away the entire line */
    clear_parse_katcl(l->l_stage);
    l->l_stage->p_next = l->l_spare;
    l->l_spare = l->l_stage;
    l->l_stage = NULL;
  }

  if(!(flags & KATCP_FLAG_LAST)){
    return result;
  }

  /* probably redundant, before_append_katcl needs to check for this too */
  if(l->l_stage == NULL){
    return -1;
  }

#if 0
  deparse_katcl(l->l_stage); /* WARNING: assumed that will not fail */
#endif

  /* things went ok, now queue the complete message */
  if(l->l_tail == NULL){
    l->l_tail = l->l_stage;
    l->l_head = l->l_tail;
  } else {
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
    return -1;
  }

  result = add_buffer_parse_katcl(p, flags, buffer, len);

  return after_append_katcl(l, flags, result);
}

int append_parse_katcl(struct katcl_line *l, struct katcl_parse *p)
{
  /* WARNING: assumes ownership of parse */

  fprintf(stderr, "line grabbing ownership of parse\n");
  abort();

#ifdef DEBUG
  if(p->p_state != KATCL_PARSE_DONE){
    fprintf(stderr, "problem: attempting to append a message in state %u\n", p->p_state);
    abort();
  }
#endif

  if(l->l_tail == NULL){
    l->l_tail = p;
    l->l_head = l->l_tail;
  } else {
    l->l_tail->p_next = p;
    l->l_tail = p;
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

#ifdef TODO
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
  fprintf(stderr, "copyied line of %d args from %p (%d arg bytes) to %p (status: %d)\n", i, p, need, m, status);
#endif

  return status;
}

int relay_katcl(struct katcl_line *lx, struct katcl_line *ly)
{
  if(lx->l_ready == NULL){
    return -1;
  }

  return parsed_relay_katcl(lx->l_ready, ly->l_out);
}
#endif

/***************************/

#define WRITE_STATE_DONE   0
#define WRITE_STATE_FILL   1
#define WRITE_STATE_SEND   2

int escape_copy_katcl(char *dst, char *src, unsigned int want)
{
  /* returns amount actually consumed, presumes that dst has at least 2 + 2*want available */
  return -1;
}

int write_katcl(struct katcl_line *l)
{
  int wr;
  int state, want;
  unsigned int i, j;
  struct katcl_parse *p;
  struct katcl_larg *la;

  for(state = WRITE_STATE_FILL; state != WRITE_STATE_DONE; ){
    switch(state){

      case WRITE_STATE_FILL :
 
        if(l->l_head == NULL){
          state = (l->l_pending > 0) ? WRITE_STATE_SEND : WRITE_STATE_DONE;
        } else {
          p = l->l_head;

#ifdef DEBUG
          if((l->l_arg >= p->got) || (l->l_pos >= p->p_kept)){
            fprintf(stderr, "write: logic problem: arg=%u >= got=%u or pos=%u >= kept=%u\n", l->l_arg, p->p_got, l->l_pos, p->p_kept);
            abort();
          }
#endif

          la = &(p->p_args[l->l_arg]);

          if(la->a_escape){
          }

          i = la-
          want = la->a_end - la->a_begin;


          
        }

        break;

      case WRITE_STATE_SEND : /* do io */

        if(l->l_pending > 0){ /* do we have something in write buffer ? */
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
              state = l->l_head ? WRITE_STATE_FILL : WRITE_STATE_DONE;
            }
          }

          break;
        }
    }
  }

  while(l->l_head && (l->l_pending > 0)){

    if(l->l_pending < (KATCL_IO_SIZE / 2)){

      if(l->l_arg == 0){

      } else {

      }
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
  return l->l_head ? 1 : 0;
}

/***************************/

#if 0
int error_katcl(struct katcl_line *l)
{
  return l->l_error;
}
#endif

#ifdef UNIT_TEST_LINE

void dump_katcl(struct katcl_line *l, FILE *fp)
{
  fprintf(fp, "input: fd=%d\n", l->l_fd);

  dump_parse_katcl(l->l_ready, "ready", fp);
  dump_parse_katcl(l->l_next, "next", fp);
  dump_parse_katcl(l->l_spare, "spare", fp);
}

int main()
{
  struct katcl_line *l;
  int count;

  fprintf(stderr, "line.c test\n");

  l = create_katcl(STDIN_FILENO);
  if(l == NULL){
    fprintf(stderr, "main: unable to create katcl\n");
    return 1;
  }

  while(!read_katcl(l)){
    fprintf(stderr, "got data\n");
    while(have_katcl(l) > 0){

      count = arg_count_katcl(l);
      fprintf(stderr, "parsed a line with %d words\n", count);

      dump_katcl(l, stdout);
    }
  }
  
  destroy_katcl(l, 1);

  return 0;
}

#endif
