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


struct katcl_msg *create_msg_katcl(struct katcl_line *l)
{
  struct katcl_msg *m;

  m = malloc(sizeof(struct katcl_msg));
  if(m == NULL){
    return NULL;
  }

  m->m_line = l;

  m->m_buffer = NULL;
  m->m_size = 0;

  m->m_want = 0;
  m->m_tag = (-1);
  m->m_complete = 0;

  return m;
}

void destroy_msg_katcl(struct katcl_msg *m)
{
  if(m == NULL){
    return;
  }

  if(m->m_buffer){
    free(m->m_buffer);
    m->m_buffer = NULL;
  }

  m->m_size = 0;
  m->m_want = 0;

  free(m);
}

/******************************************************************************/

int queue_vargs_katcl(struct katcl_msg *m, int flags, char *fmt, va_list args)
{
  char *tmp, *buffer;
  va_list copy;
  int want, got, x, result;

  got = strlen(fmt) + 16;
#if DEBUG > 1
  fprintf(stderr, "queue vargs: my fmt string is <%s>\n", fmt);
#endif

  /* in an ideal world this would be an insitu copy to save the malloc */
  buffer = NULL;

  for(x = 1; x < 8; x++){ /* paranoid nutter check */
    tmp = realloc(buffer, sizeof(char) * got);
    if(tmp == NULL){
#ifdef DEBUG
      fprintf(stderr, "queue vargs: unable to allocate %d tmp bytes\n", got);
#endif
      free(buffer);
      return -1;
    }

    buffer = tmp;

    va_copy(copy, args);
    want = vsnprintf(buffer, got, fmt, copy);
    va_end(copy);
#if DEBUG > 1
    fprintf(stderr, "queue vargs: printed <%s> (iteration=%d, want=%d, got=%d)\n", buffer, x, want, got);
#endif

    if((want >= 0) && ( want < got)){
      result = queue_buffer_katcl(m, flags, buffer, want);
      free(buffer);
      return result;
    }

    if(want >= got){
      got = want + 1;
    } else {
      /* old style return codes, with x termination check */
      got *= 2;
    }

  }

#ifdef DEBUG
  fprintf(stderr, "queue vargs: sanity failure with %d bytes\n", got);
  abort();
#endif

  return -1;
}

int queue_args_katcl(struct katcl_msg *m, int flags, char *fmt, ...)
{
  va_list args;
  int result;

  va_start(args, fmt);

  result = queue_vargs_katcl(m, flags, fmt, args);

  va_end(args);

  return result;
}

int queue_string_katcl(struct katcl_msg *m, int flags, char *buffer)
{
  return queue_buffer_katcl(m, flags, buffer, strlen(buffer));
}

int queue_unsigned_long_katcl(struct katcl_msg *m, int flags, unsigned long v)
{
#define TMP_BUFFER 32
  char buffer[TMP_BUFFER];
  int result;

  result = snprintf(buffer, TMP_BUFFER, "%lu", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return queue_buffer_katcl(m, flags, buffer, result);
#undef TMP_BUFFER
}

int queue_signed_long_katcl(struct katcl_msg *m, int flags, unsigned long v)
{
#define TMP_BUFFER 32
  char buffer[TMP_BUFFER];
  int result;

  result = snprintf(buffer, TMP_BUFFER, "%ld", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return queue_buffer_katcl(m, flags, buffer, result);
#undef TMP_BUFFER
}

int queue_hex_long_katcl(struct katcl_msg *m, int flags, unsigned long v)
{
#define TMP_BUFFER 32
  char buffer[TMP_BUFFER];
  int result;

  result = snprintf(buffer, TMP_BUFFER, "0x%lx", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return queue_buffer_katcl(m, flags, buffer, result);
#undef TMP_BUFFER
}

#ifdef KATCP_USE_FLOATS
int queue_double_katcl(struct katcl_msg *m, int flags, double v)
{
#define TMP_BUFFER 32
  char buffer[TMP_BUFFER];
  int result;

  result = snprintf(buffer, TMP_BUFFER, "%e", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return queue_buffer_katcl(m, flags, buffer, result);
#undef TMP_BUFFER
}
#endif

int queue_buffer_katcl(struct katcl_msg *m, int flags, void *buffer, int len)
{
  /* returns greater than zero on success */

  unsigned int newsize, had, want, need, result, i, problem;
  char *s, *tmp, v;

  problem = KATCP_FLAG_MORE | KATCP_FLAG_LAST;
  if((flags & problem) == problem){
#ifdef DEBUG
    fprintf(stderr, "queue: usage problem: can not have last and more together\n");
#endif
    return -1;
  }

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
  if(!(flags & KATCP_FLAG_MORE) || (flags & KATCP_FLAG_LAST)){
    if((m->m_want > 0) && (m->m_buffer[m->m_want - 1] == ' ')){ /* WARNING: convoluted heuristic: add a null token if we haven't added anything sofar and more isn't comming */
#ifdef DEBUG
      fprintf(stderr, "queue: warning: empty argument considered bad form\n");
#endif
      m->m_buffer[m->m_want++] = '\\';
      m->m_buffer[m->m_want++] = '@';
    }
  }

  if(flags & KATCP_FLAG_LAST){
    m->m_buffer[m->m_want++] = '\n';
    m->m_complete = 1;
  } else {
    if(!(flags & KATCP_FLAG_MORE)){
      m->m_buffer[m->m_want++] = ' ';
    }
    m->m_complete = 0;
  }

  result = m->m_want - had;

  return result;
}

void clear_msg_katcl(struct katcl_msg *m)
{
  m->m_want = 0;
  m->m_complete = 1;
}


/****************************************************************/

#define STATE_FRESH      0
#define STATE_COMMAND    1
#define STATE_WHITESPACE 2 
#define STATE_ARG        3
#define STATE_TAG        4
#define STATE_ESCAPE     5
#define STATE_DONE       6

int problem_katcl(struct katcl_line *l)
{
  int result;

  if(l == NULL){
    return 1;
  }

  result = l->l_problem;

  l->l_problem = 0;

  return result;
}

void destroy_katcl(struct katcl_line *l, int mode)
{
  if(l == NULL){
    return;
  }

  if(l->l_input){
    free(l->l_input);
    l->l_input = NULL;
  }
  l->l_isize = 0;

  if(l->l_args){
    free(l->l_args);
    l->l_args = NULL;
  }
  l->l_asize = 0;
  l->l_current = NULL;

  if(l->l_out){
    destroy_msg_katcl(l->l_out);
    l->l_out = NULL;
  }

  if(mode){
    if(l->l_fd >= 0){
      close(l->l_fd);
      l->l_fd = (-1);
    }
  }

  free(l);
}

struct katcl_line *create_katcl(int fd)
{
  struct katcl_line *l;

  l = malloc(sizeof(struct katcl_line));
  if(l == NULL){
    return NULL;
  }

  l->l_fd = fd;

  l->l_input = NULL;
  l->l_isize = 0;
  l->l_ihave = 0;
  l->l_iused = 0;
  l->l_ikept = 0;
  l->l_itag = (-1);

  l->l_args = NULL;
  l->l_current = NULL;
  l->l_asize = 0;
  l->l_ahave = 0;

  l->l_out = NULL;
  l->l_odone = 0;

  l->l_error = 0;
  l->l_problem = 0;
  l->l_state = STATE_FRESH;

  l->l_out = create_msg_katcl(l);
  if(l->l_out == NULL){
    destroy_katcl(l, 0); 
  }

  return l;
}

int fileno_katcl(struct katcl_line *l)
{
  return l ? l->l_fd : (-1);
}

void exchange_katcl(struct katcl_line *l, int fd)
{
  if(l->l_fd >= 0){
    close(l->l_fd);
  }
  l->l_fd = fd;

  /* WARNING: exchanging fds forces discarding of pending io */
  l->l_ihave = 0;
  l->l_iused = 0;
  l->l_ikept = 0;
  l->l_itag = (-1);

  l->l_odone = 0;
  if(l->l_out){
    clear_msg_katcl(l->l_out);
  }

  /* WARNING: shouldn't we also forget parsed stuff ? */
}

int read_katcl(struct katcl_line *l)
{
  int rr;
  char *ptr;

  if(l->l_isize <= l->l_ihave){
    ptr = realloc(l->l_input, l->l_isize + KATCP_BUFFER_INC);
    if(ptr == NULL){
#ifdef DEBUG
      fprintf(stderr, "read: realloc to %d failed\n", l->l_isize + KATCP_BUFFER_INC);
#endif
      l->l_error = ENOMEM;
      return -1;
    }

    l->l_input = ptr;
    l->l_isize += KATCP_BUFFER_INC;
  }

  rr = read(l->l_fd, l->l_input + l->l_ihave, l->l_isize - l->l_ihave);
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

  l->l_ihave += rr;
  return 0;
}

int ready_katcl(struct katcl_line *l) /* do we have a full line ? */
{
  return (l->l_state == STATE_DONE) ? 1 : 0;
}

void clear_katcl(struct katcl_line *l) /* discard a full line */
{
#ifdef DEBUG
  if(l->l_state != STATE_DONE){
    fprintf(stderr, "unusual: clearing line which is not ready\n");
  }
#endif

  if(l->l_iused >= l->l_ihave){
    l->l_ihave = 0;
  } else if(l->l_iused > 0){
    memmove(l->l_input, l->l_input + l->l_iused, l->l_ihave - l->l_iused);
    l->l_ihave -= l->l_iused;
  }

  l->l_iused = 0;
  l->l_ikept = 0;
  l->l_itag = (-1);

  l->l_ahave = 0;
  l->l_current = NULL;

  l->l_state = STATE_FRESH;
}

static int space_katcl(struct katcl_line *l)
{
  struct katcl_larg *tmp;

  if(l->l_ahave < l->l_asize){
    l->l_current = &(l->l_args[l->l_ahave]);
    return 0;
  }

  tmp = realloc(l->l_args, sizeof(struct katcl_larg) * (l->l_asize + KATCP_ARGS_INC));
  if(tmp == NULL){
    l->l_error = ENOMEM;
    return -1;
  }

  l->l_args = tmp;
  l->l_asize += KATCP_ARGS_INC;
  l->l_current = &(l->l_args[l->l_ahave]);

  return 0;
}

int parse_katcl(struct katcl_line *l) /* transform buffer -> args */
{
  int increment;

#ifdef DEBUG
  fprintf(stderr, "invoking parse (have=%d, used=%d, kept=%d)\n", l->l_ihave, l->l_iused, l->l_ikept);
#endif

  while((l->l_iused < l->l_ihave) && (l->l_state != STATE_DONE)){

    increment = 0; /* what to do to keep */

#if DEBUG > 1
    fprintf(stderr, "parse: state=%d, char=%c\n", l->l_state, l->l_input[l->l_iused]);
#endif

    switch(l->l_state){

      case STATE_FRESH : 
        switch(l->l_input[l->l_iused]){
          case '#' :
          case '!' :
          case '?' :
            if(space_katcl(l) < 0){
              return -1;
            }

#ifdef DEBUG
            if(l->l_ikept != l->l_iused){
              fprintf(stderr, "logic issue: kept=%d != used=%d\n", l->l_ikept, l->l_iused);
            }
#endif
            l->l_current->a_begin = l->l_ikept;
            l->l_state = STATE_COMMAND;

            l->l_input[l->l_ikept] = l->l_input[l->l_iused];
            increment = 1;
            break;

          default :
            break;
        }
        break;

      case STATE_COMMAND :
        increment = 1;
        switch(l->l_input[l->l_iused]){
          case '[' : 
            l->l_itag = 0;
            l->l_input[l->l_ikept] = '\0';
            l->l_current->a_end = l->l_ikept;
            l->l_ahave++;
            l->l_state = STATE_TAG;
            break;

          case ' '  :
          case '\t' :
            l->l_input[l->l_ikept] = '\0';
            l->l_current->a_end = l->l_ikept;
            l->l_ahave++;
            l->l_state = STATE_WHITESPACE;
            break;

          case '\n' :
          case '\r' :
            l->l_input[l->l_ikept] = '\0';
            l->l_current->a_end = l->l_ikept;
            l->l_ahave++;
            l->l_state = STATE_DONE;
            break;

          default   :
            l->l_input[l->l_ikept] = l->l_input[l->l_iused];
            break;
            
        }
        break;

      case STATE_TAG : 
        switch(l->l_input[l->l_iused]){
          case '0' :
          case '1' :
          case '2' :
          case '3' :
          case '4' :
          case '5' :
          case '6' :
          case '7' :
          case '8' :
          case '9' :
            l->l_itag = (l->l_itag * 10) + (l->l_input[l->l_iused] - '0');
            break;
          case ']' : 
#if DEBUG > 1
            fprintf(stderr, "extract: found tag %d\n", l->l_itag);
#endif
            l->l_state = STATE_WHITESPACE;
            break;
          default :
#ifdef DEBUG
            fprintf(stderr, "extract: invalid tag char %c\n", l->l_input[l->l_iused]);
#endif
            /* WARNING: error handling needs to be improved here */
            l->l_error = EINVAL;
            return -1;
        }
        break;

      case STATE_WHITESPACE : 
        switch(l->l_input[l->l_iused]){
          case ' '  :
          case '\t' :
            break;
          case '\n' :
          case '\r' :
            l->l_state = STATE_DONE;
            break;
          case '\\' :
            if(space_katcl(l) < 0){
              return -1;
            }
            l->l_current->a_begin = l->l_ikept; /* token begins with a whitespace */
            l->l_state = STATE_ESCAPE;
            break;
          default   :
            if(space_katcl(l) < 0){
              return -1;
            }
            l->l_current->a_begin = l->l_ikept; /* token begins with a normal char */
            l->l_input[l->l_ikept] = l->l_input[l->l_iused];
            increment = 1;
            l->l_state = STATE_ARG;
            break;
        }
        break;

      case STATE_ARG :
        increment = 1;
#ifdef DEBUG
        if(l->l_current == NULL){
          fprintf(stderr, "parse logic failure: entered arg state without having allocated entry\n");
        }
#endif
        switch(l->l_input[l->l_iused]){
          case ' '  :
          case '\t' :
            l->l_input[l->l_ikept] = '\0';
            l->l_current->a_end = l->l_ikept;
            l->l_ahave++;
            l->l_state = STATE_WHITESPACE;
            break;

          case '\n' :
          case '\r' :
            l->l_input[l->l_ikept] = '\0';
            l->l_current->a_end = l->l_ikept;
            l->l_ahave++;
            l->l_state = STATE_DONE;
            break;
            
          case '\\' :
            l->l_state = STATE_ESCAPE;
            increment = 0;
            break;

          default   :
            l->l_input[l->l_ikept] = l->l_input[l->l_iused];
            break;
        }
        
        break;

      case STATE_ESCAPE :
        increment = 1;
        switch(l->l_input[l->l_iused]){
          case 'e' :
            l->l_input[l->l_ikept] = 27;
            break;
          case 'n' :
            l->l_input[l->l_ikept] = '\n';
            break;
          case 'r' :
            l->l_input[l->l_ikept] = '\r';
            break;
          case 't' :
            l->l_input[l->l_ikept] = '\t';
            break;
          case '0' :
            l->l_input[l->l_ikept] = '\0';
            break;
          case '_' :
            l->l_input[l->l_ikept] = ' ';
            break;
          case '@' :
            increment = 0;
            break;
            /* case ' ' : */
            /* case '\\' : */
          default :
            l->l_input[l->l_ikept] = l->l_input[l->l_iused];
            break;
        }
        l->l_state = STATE_ARG;
        break;
    }

    l->l_iused++;
    l->l_ikept += increment;
  }

  return (l->l_state == STATE_DONE) ? 1 : 0;
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

/******************************************************************/

unsigned int arg_count_katcl(struct katcl_line *l)
{
  return l->l_ahave;
}

int arg_tag_katcl(struct katcl_line *l)
{
  return l->l_itag;
}

static int arg_type_katcl(struct katcl_line *l, char mode)
{
  if(l->l_ahave <= 0){
    return 0;
  }

  if(l->l_input[l->l_args[0].a_begin] == mode){
    return 1;
  }

  return 0;
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
  if(index >= l->l_ahave){
    return 1;
  } 

  if(l->l_args[index].a_begin >= l->l_args[index].a_end){
    return 1;
  }

  return 0;
}

char *arg_string_katcl(struct katcl_line *l, unsigned int index)
{
  if(index >= l->l_ahave){
    return NULL;
  }

  if(l->l_args[index].a_begin >= l->l_args[index].a_end){
    return NULL;
  }

  return l->l_input + l->l_args[index].a_begin;
}

char *arg_copy_string_katcl(struct katcl_line *l, unsigned int index)
{
  char *ptr;

  ptr = arg_string_katcl(l, index);
  if(ptr){
    return strdup(ptr);
  } else {
    return NULL;
  }
}

unsigned long arg_unsigned_long_katcl(struct katcl_line *l, unsigned int index)
{
  unsigned long value;

  if(index >= l->l_ahave){
    return 0;
  } 

  value = strtoul(l->l_input + l->l_args[index].a_begin, NULL, 0);

  return value;
}

#ifdef KATCP_USE_FLOATS
double arg_double_katcl(struct katcl_line *l, unsigned int index)
{
  double value;

  if(index >= l->l_ahave){
    return 0;
  } 

  value = strtod(l->l_input + l->l_args[index].a_begin, NULL);

  return value;
}
#endif

unsigned int arg_buffer_katcl(struct katcl_line *l, unsigned int index, void *buffer, unsigned int size)
{
  unsigned int want, done;

  if(index >= l->l_ahave){
    return 0;
  } 

  want = l->l_args[index].a_end - l->l_args[index].a_begin;
  done = (want > size) ? size : want;

  if(buffer && (want > 0)){
    memcpy(buffer, l->l_input + l->l_args[index].a_begin, done);
  }

  return want;
}

/******************************************************************/

int append_vargs_katcl(struct katcl_line *l, int flags, char *fmt, va_list args)
{
  return queue_vargs_katcl(l->l_out, flags, fmt, args);
}

int append_args_katcl(struct katcl_line *l, int flags, char *fmt, ...)
{
  va_list args;
  int result;

  va_start(args, fmt);
  result =  queue_vargs_katcl(l->l_out, flags, fmt, args);
  va_end(args);

  return result;
}

int append_string_katcl(struct katcl_line *l, int flags, char *buffer)
{
  return queue_string_katcl(l->l_out, flags, buffer);
}

int append_unsigned_long_katcl(struct katcl_line *l, int flags, unsigned long v)
{
  return queue_unsigned_long_katcl(l->l_out, flags, v);
}

int append_signed_long_katcl(struct katcl_line *l, int flags, unsigned long v)
{
  return queue_signed_long_katcl(l->l_out, flags, v);
}

int append_hex_long_katcl(struct katcl_line *l, int flags, unsigned long v)
{
  return queue_hex_long_katcl(l->l_out, flags, v);
}

#ifdef KATCP_USE_FLOATS
int append_double_katcl(struct katcl_line *l, int flags, double v)
{
  return queue_double_katcl(l->l_out, flags, v);
}
#endif

int append_buffer_katcl(struct katcl_line *l, int flags, void *buffer, int len)
{
  return queue_buffer_katcl(l->l_out, flags, buffer, len);
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
/* vprint and print are deprecated */
int vprint_katcl(struct katcl_line *l, int full, char *fmt, va_list args)
{
  unsigned int newsize, delta;
  int actual;
  char *tmp;
  va_list copy;

  /* Warning: no string escaping happens in here, would need to 
   * provide other routines to escape data */

  for(;;){

#ifdef DEBUG
    if(l->l_osize < l->l_owant){
      fprintf(stderr, "vprint: logic problem: want(%u) to clobber maximum(%u)\n", l->l_owant, l->l_osize);
      abort();
    }
#endif

    va_copy(copy, args);

    delta = l->l_osize - l->l_owant; 
    actual = vsnprintf(l->l_output + l->l_owant, delta, fmt, copy);

    va_end(copy);

    if((actual >= 0) && (actual < delta)){
      if(full){
        switch(l->l_output[l->l_owant]){
          case KATCP_INFORM  :
          case KATCP_REPLY   :
          case KATCP_REQUEST :
            break;
          default :
            return -1;
        }
      }
      l->l_owant += actual;
      if(full){
        if(l->l_output[l->l_owant - 1] != '\n'){
          l->l_output[l->l_owant++] = '\n';
        }
      }
#ifdef DEBUG
      fprintf(stderr, "vprint: new buffer <%s>\n", l->l_output);
#endif
      return 0;
    } 

#if 0
    newsize = (actual >= 0) ? (l->l_owant + actual + 2) : (l->l_osize + KATCP_BUFFER_INC);
#endif

    newsize = l->l_osize + KATCP_BUFFER_INC;
    tmp = realloc(l->l_output, newsize);
    if(tmp == NULL){
#ifdef DEBUG
      fprintf(stderr, "vprint: unable to resize output to <%u> bytes\n", newsize);
#endif
      return -1;
    } 

    l->l_output = tmp;
    l->l_osize = newsize;
  }
}

int print_katcl(struct katcl_line *l, int full, char *fmt, ...)
{
  va_list args;
  int result;

  va_start(args, fmt);

  result = vprint_katcl(l, full, fmt, args);

  va_end(args);

  return result;
}
#endif

#if 1 
int relay_katcl(struct katcl_line *lx, struct katcl_line *ly)
{
  int i, flag, need, len;
  int status;

  flag = KATCP_FLAG_FIRST;

  need = 0;
  status = 0;

  for(i = 0; i < lx->l_ahave; i++){
    if((i + 1) >= lx->l_ahave){
      flag |= KATCP_FLAG_LAST;
    }

    len = lx->l_args[i].a_end - lx->l_args[i].a_begin;
    need += len;

    if (append_buffer_katcl(ly, flag, lx->l_input + lx->l_args[i].a_begin, len) < 0)
      status = -1;
    flag = 0;
  }

#ifdef DEBUG
  fprintf(stderr, "copyied line of %d args from %p (%d arg bytes) to %p (status: %d)\n", i, lx, need, ly, status);
#endif

  return status;
}
#endif

/***************************/

#define FLUSH_LIMIT (1024 * 1024)

int write_katcl(struct katcl_line *l)
{
  int wr;
  struct katcl_msg *m;

  m = l->l_out;

  if(l->l_odone < m->m_want){
    wr = send(l->l_fd, m->m_buffer + l->l_odone, m->m_want - l->l_odone, MSG_DONTWAIT | MSG_NOSIGNAL);
    if(wr < 0){
      switch(errno){
        case EAGAIN :
        case EINTR  :
          return 0;
        default :
          l->l_error = errno;
          return -1;
      }
    } else {
      l->l_odone += wr;
    }
  }

  if(l->l_odone >= m->m_want){
    l->l_odone = 0;
    m->m_want = 0;
    return 1;
  }

  if(l->l_odone > FLUSH_LIMIT){
    memmove(m->m_buffer, m->m_buffer + l->l_odone, m->m_want - l->l_odone);
    m->m_want -= l->l_odone;
    l->l_odone = 0;
  }

  /* returns zero if still more to do */

  return 0;
}

int flushing_katcl(struct katcl_line *l)
{
  struct katcl_msg *m;

  m = l->l_out;

  return (l->l_odone < m->m_want) ? (m->m_want - l->l_odone) : 0;
}

/***************************/

int error_katcl(struct katcl_line *l)
{
  return l->l_error;
}

void dump_katcl(struct katcl_line *l, FILE *fp)
{
  unsigned int i, j;

  fprintf(stderr, "input: kept=%u, used=%u, have=%u\n", l->l_ikept, l->l_iused, l->l_ihave);

  for(i = 0; i < l->l_ahave; i++){
    fprintf(fp, "arg[%u]: (%s) ", i, arg_string_katcl(l, i));
    for(j = l->l_args[i].a_begin; j < l->l_args[i].a_end; j++){
      if(isprint(l->l_input[j])){
        fprintf(fp, "%c", l->l_input[j]);
      } else {
        fprintf(fp, "\\%02x", l->l_input[j]);
      }
    }
    fprintf(fp, "\n");
  }
}

#ifdef UNIT_TEST_LINE

int main()
{
  struct katcl_line *l;
  char *ptr;
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
