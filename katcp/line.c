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
  m->m_complete = 1;

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

struct katcl_parse *create_parse_katcl()
{
  struct katcl_parse *p;

  p = malloc(sizeof(struct katcl_parse));
  if(p == NULL){
    return NULL;
  }

  p->p_state = STATE_FRESH;

  p->p_buffer = NULL;
  p->p_size = 0;
  p->p_have = 0;

  p->p_refs = 0;

  p->p_tag = (-1);

  p->p_args = NULL;
  p->p_current = NULL;

  p->p_count = 0;
  p->p_got = 0;

  return p;
}

void destroy_parse_katcl(struct katcl_parse *p)
{
  if(p == NULL){
    return;
  }

  if(p->p_buffer){
    free(p->p_buffer);
    p->p_buffer = NULL;
  }
  p->p_size = 0;
  p->p_have = 0;

  if(p->p_args){
    free(p->p_args);
    p->p_args = NULL;
  }
  p->p_current = NULL;
  p->p_count = 0;
  p->p_got = 0;

  p->p_refs = 0;
  p->p_tag = (-1);
}

static int stash_parse_katcl(struct katcl_parse *p, char *buffer, unsigned int len)
{
  char *tmp;
  unsigned int need;

#ifdef DEBUG
  fprintf(stderr, "stashing %u bytes starting with <%c...> for next line\n", len, buffer[0]);
#endif
  
  need = p->p_used + len;

  if(need > p->p_size){
    tmp = realloc(p->p_buffer, need);
    if(tmp == NULL){
      return -1;
    }
    p->p_buffer = tmp;
    p->p_size = need;
  }

  memcpy(p->p_buffer + p->p_used, buffer, len);
  p->p_have = need;

  return 0;
}

void clear_parse_katcl(struct katcl_parse *p)
{
  p->p_state = STATE_FRESH;

  p->p_have = 0;
  p->p_used = 0;
  p->p_kept = 0;
  p->p_tag = (-1);

  p->p_got = 0;
  p->p_current = NULL;
}

/****************************************************************/

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

void destroy_katcl(struct katcl_line *l, int mode)
{
  if(l == NULL){
    return;
  }

#if 0
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
#endif

  if(l->l_ready){
    destroy_parse_katcl(l->l_ready);
    l->l_ready = NULL;
  }
  if(l->l_next){
    destroy_parse_katcl(l->l_next);
    l->l_next = NULL;
  }
  if(l->l_spare){
    destroy_parse_katcl(l->l_spare);
    l->l_spare = NULL;
  }

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

#if 0
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
#endif

  l->l_ready = NULL;
  l->l_next = NULL; 
  l->l_spare = NULL;

  l->l_out = NULL;
  l->l_odone = 0;

  l->l_error = 0;

  /* WARNING: only allocate next parse instance */

  l->l_next = create_parse_katcl(l);
  if(l->l_next == NULL){
    destroy_katcl(l, 0);
    return NULL;
  }

  l->l_out = create_msg_katcl(l);
  if(l->l_out == NULL){
    destroy_katcl(l, 0); 
    return NULL;
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
#if 0
  l->l_ihave = 0;
  l->l_iused = 0;
  l->l_ikept = 0;
  l->l_itag = (-1);
  /* WARNING: shouldn't we also forget parsed stuff ? */
#endif

  if(l->l_ready){
    clear_parse_katcl(l->l_ready);
  }
  if(l->l_next){
    clear_parse_katcl(l->l_next);
  }
  /* WARNING: ensure that spare is handled consistently */

  l->l_odone = 0;
  if(l->l_out){
    clear_msg_katcl(l->l_out);
  }
}

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
    ptr = realloc(p->p_buffer, p->p_size + KATCP_BUFFER_INC);
    if(ptr == NULL){
#ifdef DEBUG
      fprintf(stderr, "read: realloc to %d failed\n", p->p_size + KATCP_BUFFER_INC);
#endif
      l->l_error = ENOMEM;
      return -1;
    }

    p->p_buffer = ptr;
    p->p_size += KATCP_BUFFER_INC;
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

#ifdef DEBUG
  if(p->p_state != STATE_DONE){
    fprintf(stderr, "unusual: clearing line which is not ready\n");
  }
#endif

  if(l->l_spare == NULL){
    clear_parse_katcl(l->l_ready);
    l->l_spare = l->l_ready;
  } else {
    destroy_parse_katcl(l->l_ready);
  }
  l->l_ready = NULL;

#if 0
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
#endif
}

static int space_katcl(struct katcl_parse *p)
{
  struct katcl_larg *tmp;

  if(p->p_got < p->p_count){
    p->p_current = &(p->p_args[p->p_got]);
    return 0;
  }

  tmp = realloc(p->p_args, sizeof(struct katcl_larg) * (p->p_count + KATCP_ARGS_INC));
  if(tmp == NULL){
    return -1;
  }

  p->p_args = tmp;
  p->p_count += KATCP_ARGS_INC;
  p->p_current = &(p->p_args[p->p_got]);

  return 0;
}

int parse_katcl(struct katcl_line *l) /* transform buffer -> args */
{
  int increment;
  struct katcl_parse *p;

  p = l->l_next;

#ifdef DEBUG
  fprintf(stderr, "invoking parse (state=%d, have=%d, used=%d, kept=%d)\n", p->p_state, p->p_have, p->p_used, p->p_kept);
#endif

  while((p->p_used < p->p_have) && (p->p_state != STATE_DONE)){

    increment = 0; /* what to do to keep */

#if DEBUG > 1
    fprintf(stderr, "parse: state=%d, char=%c\n", p->p_state, p->p_buffer[p->p_used]);
#endif

    switch(p->p_state){

      case STATE_FRESH : 
        switch(p->p_buffer[p->p_used]){
          case '#' :
          case '!' :
          case '?' :
            if(space_katcl(p) < 0){
              l->l_error = ENOMEM;
              return -1;
            }

#ifdef DEBUG
            if(p->p_kept != p->p_used){
              fprintf(stderr, "logic issue: kept=%d != used=%d\n", p->p_kept, p->p_used);
            }
#endif
            p->p_current->a_begin = p->p_kept;
            p->p_state = STATE_COMMAND;

            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            increment = 1;
            break;

          default :
            break;
        }
        break;

      case STATE_COMMAND :
        increment = 1;
        switch(p->p_buffer[p->p_used]){
          case '[' : 
            p->p_tag = 0;
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = STATE_TAG;
            break;

          case ' '  :
          case '\t' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = STATE_WHITESPACE;
            break;

          case '\n' :
          case '\r' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = STATE_DONE;
            break;

          default   :
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            break;
            
        }
        break;

      case STATE_TAG : 
        switch(p->p_buffer[p->p_used]){
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
            p->p_tag = (p->p_tag * 10) + (p->p_buffer[p->p_used] - '0');
            break;
          case ']' : 
#if DEBUG > 1
            fprintf(stderr, "extract: found tag %d\n", p->p_tag);
#endif
            p->p_state = STATE_WHITESPACE;
            break;
          default :
#ifdef DEBUG
            fprintf(stderr, "extract: invalid tag char %c\n", p->p_buffer[p->p_used]);
#endif
            /* WARNING: error handling needs to be improved here */
            l->l_error = EINVAL;
            return -1;
        }
        break;

      case STATE_WHITESPACE : 
        switch(p->p_buffer[p->p_used]){
          case ' '  :
          case '\t' :
            break;
          case '\n' :
          case '\r' :
            p->p_state = STATE_DONE;
            break;
          case '\\' :
            if(space_katcl(p) < 0){
              l->l_error = ENOMEM;
              return -1;
            }
            p->p_current->a_begin = p->p_kept; /* token begins with a whitespace */
            p->p_state = STATE_ESCAPE;
            break;
          default   :
            if(space_katcl(p) < 0){
              l->l_error = ENOMEM;
              return -1;
            }
            p->p_current->a_begin = p->p_kept; /* token begins with a normal char */
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            increment = 1;
            p->p_state = STATE_ARG;
            break;
        }
        break;

      case STATE_ARG :
        increment = 1;
#ifdef DEBUG
        if(p->p_current == NULL){
          fprintf(stderr, "parse logic failure: entered arg state without having allocated entry\n");
        }
#endif
        switch(p->p_buffer[p->p_used]){
          case ' '  :
          case '\t' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = STATE_WHITESPACE;
            break;

          case '\n' :
          case '\r' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = STATE_DONE;
            break;
            
          case '\\' :
            p->p_state = STATE_ESCAPE;
            increment = 0;
            break;

          default   :
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            break;
        }
        
        break;

      case STATE_ESCAPE :
        increment = 1;
        switch(p->p_buffer[p->p_used]){
          case 'e' :
            p->p_buffer[p->p_kept] = 27;
            break;
          case 'n' :
            p->p_buffer[p->p_kept] = '\n';
            break;
          case 'r' :
            p->p_buffer[p->p_kept] = '\r';
            break;
          case 't' :
            p->p_buffer[p->p_kept] = '\t';
            break;
          case '0' :
            p->p_buffer[p->p_kept] = '\0';
            break;
          case '_' :
            p->p_buffer[p->p_kept] = ' ';
            break;
          case '@' :
            increment = 0;
            break;
            /* case ' ' : */
            /* case '\\' : */
          default :
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            break;
        }
        p->p_state = STATE_ARG;
        break;
    }

    p->p_used++;
    p->p_kept += increment;
  }

  if(p->p_state != STATE_DONE){ /* not finished, run again later */
    return 0; 
  }

  /* got a new line, move it to ready position */

  if(l->l_spare == NULL){
    l->l_spare = create_parse_katcl();
    if(l->l_spare == NULL){
      l->l_error = ENOMEM;
      return -1; /* is it safe to call parse with a next which is DONE ? */
    }
  } else {
    clear_parse_katcl(l->l_spare);
  }

  /* at this point we have a cleared spare which we can make the next entry */
  l->l_next = l->l_spare;

  /* move ready out of the way */
  if(l->l_ready){
#ifdef DEBUG
    fprintf(stderr, "unsual: expected ready to be cleared before attempting a new parse\n");
#endif
    clear_parse_katcl(l->l_ready);
    l->l_spare = l->l_ready;
  } else {
    l->l_spare = NULL;
  }

  /* stash any unparsed io for the next round */
  if(p->p_used < p->p_have){
    stash_parse_katcl(l->l_next, p->p_buffer + p->p_used, p->p_have - p->p_used);
    p->p_have = p->p_used;
  }

  l->l_ready = p;

  return 1;
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

unsigned int parsed_count_katcl(struct katcl_parse *p)
{
  return p->p_got;
}

int parsed_tag_katcl(struct katcl_parse *p)
{
  return p->p_tag;
}

static int parsed_type_katcl(struct katcl_parse *p, char mode)
{
  if(p->p_got <= 0){
    return 0;
  }

  if(p->p_buffer[p->p_args[0].a_begin] == mode){
    return 1;
  }

  return 0;
}

int parsed_request_katcl(struct katcl_parse *p)
{
  return parsed_type_katcl(p, KATCP_REQUEST);
}

int parsed_reply_katcl(struct katcl_parse *p)
{
  return parsed_type_katcl(p, KATCP_REPLY);
}

int parsed_inform_katcl(struct katcl_parse *p)
{
  return parsed_type_katcl(p, KATCP_INFORM);
}

int parsed_null_katcl(struct katcl_parse *p, unsigned int index)
{
  if(index >= p->p_got){
    return 1;
  } 

  if(p->p_args[index].a_begin >= p->p_args[index].a_end){
    return 1;
  }

  return 0;
}

char *parsed_string_katcl(struct katcl_parse *p, unsigned int index)
{
  if(index >= p->p_got){
    return NULL;
  }

  if(p->p_args[index].a_begin >= p->p_args[index].a_end){
    return NULL;
  }

  return p->p_buffer + p->p_args[index].a_begin;
}

char *parsed_copy_string_katcl(struct katcl_parse *p, unsigned int index)
{
  char *ptr;

  ptr = parsed_string_katcl(p, index);
  if(ptr){
    return strdup(ptr);
  } else {
    return NULL;
  }
}

unsigned long parsed_unsigned_long_katcl(struct katcl_parse *p, unsigned int index)
{
  unsigned long value;

  if(index >= p->p_got){
    return 0;
  } 

  value = strtoul(p->p_buffer + p->p_args[index].a_begin, NULL, 0);

  return value;
}

#ifdef KATCP_USE_FLOATS
double parsed_double_katcl(struct katcl_parse *p, unsigned int index)
{
  double value;

  if(index >= p->p_got){
    return 0;
  } 

  value = strtod(p->p_buffer + p->p_args[index].a_begin, NULL);

  return value;
}
#endif

unsigned int parsed_buffer_katcl(struct katcl_parse *p, unsigned int index, void *buffer, unsigned int size)
{
  unsigned int want, done;

  if(index >= p->p_got){
    return 0;
  } 

  want = p->p_args[index].a_end - p->p_args[index].a_begin;
  done = (want > size) ? size : want;

  if(buffer && (want > 0)){
    memcpy(buffer, p->p_buffer + p->p_args[index].a_begin, done);
  }

  return want;
}

/******************************************************************/

unsigned int arg_count_katcl(struct katcl_line *l)
{
  if(l->l_ready == NULL){
    return 0;
  }

  return parsed_count_katcl(l->l_ready);
}

int arg_tag_katcl(struct katcl_line *l)
{
  if(l->l_ready == NULL){
    return -1;
  }

  return parsed_tag_katcl(l->l_ready);
}

static int arg_type_katcl(struct katcl_line *l, char mode)
{
  if(l->l_ready == NULL){
    return -1;
  }

  return parsed_type_katcl(l->l_ready, mode);
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

  return parsed_null_katcl(l->l_ready, index);
}

char *arg_string_katcl(struct katcl_line *l, unsigned int index)
{
  if(l->l_ready == NULL){
    return NULL;
  }

  return parsed_string_katcl(l->l_ready, index);
}

char *arg_copy_string_katcl(struct katcl_line *l, unsigned int index)
{
  if(l->l_ready == NULL){
    return NULL;
  }

  return parsed_copy_string_katcl(l->l_ready, index);
}

unsigned long arg_unsigned_long_katcl(struct katcl_line *l, unsigned int index)
{
  if(l->l_ready == NULL){
    return 0;
  }

  return parsed_unsigned_long_katcl(l->l_ready, index);
}

#ifdef KATCP_USE_FLOATS
double arg_double_katcl(struct katcl_line *l, unsigned int index)
{
  if(l->l_ready == NULL){
    return 0.0; /* TODO: maybe NAN instead ? */
  }

  return parsed_double_katcl(l->l_ready, index);
}
#endif

unsigned int arg_buffer_katcl(struct katcl_line *l, unsigned int index, void *buffer, unsigned int size)
{
  if(l->l_ready == NULL){
    return 0;
  }

  return parsed_buffer_katcl(l->l_ready, index, buffer, size);
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

int append_msg_katcl(struct katcl_line *l, struct katcl_msg *m)
{
  struct katcl_msg *mx;
  char *buffer;
  unsigned int size, want;

  mx = l->l_out;

  if((mx->m_complete == 0) || (m->m_complete == 0)){
    fprintf(stderr, "logic problem: attempting to queue messages to line while incomplete\n");
    return -1;
  }
#ifdef DEBUG
  if((m->m_want <= 0) || (m->m_size < m->m_want)){
    fprintf(stderr, "logic problem: malproportioned message: want=%u, size=%u\n", m->m_want, m->m_size);
    abort();
  }
#endif

  if(mx->m_want == 0){ /* the line buffer is empty, so exchange it */
    buffer = mx->m_buffer;
    size = mx->m_size;
    want = mx->m_want;

    mx->m_buffer = m->m_buffer;
    mx->m_size = m->m_size;
    mx->m_want = m->m_want;

    m->m_buffer = buffer;
    m->m_size = size;
    m->m_want = want;
  } else { /* still something in buffer, append message */

    want = mx->m_want + m->m_want;
    if(want > mx->m_size){
      buffer = realloc(mx->m_buffer, want);
      if(buffer == NULL){
        l->l_error = ENOMEM;
        return -1;
      }
      mx->m_buffer = buffer;
    }

    memcpy(mx->m_buffer + mx->m_want, m->m_buffer, m->m_want);
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

#if 0
int error_katcl(struct katcl_line *l)
{
  return l->l_error;
}
#endif

#ifdef UNIT_TEST_LINE

void dump_parse_katcl(struct katcl_parse *p, char *prefix, FILE *fp)
{
  unsigned int i, j;

  if(p == NULL){
    fprintf(fp, "input %s is null\n", prefix);
    return;
  }

  fprintf(fp, "input %s@%p: state=%d, kept=%u, used=%u, have=%u\n", prefix, p, p->p_state, p->p_kept, p->p_used, p->p_have);

  for(i = 0; i < p->p_got; i++){
    fprintf(fp, "arg[%u]: (%s) ", i, parsed_string_katcl(p, i));
    for(j = p->p_args[i].a_begin; j < p->p_args[i].a_end; j++){
      if(isprint(p->p_buffer[j])){
        fprintf(fp, "%c", p->p_buffer[j]);
      } else {
        fprintf(fp, "\\%02x", p->p_buffer[j]);
      }
    }
    fprintf(fp, "\n");
  }
}

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
