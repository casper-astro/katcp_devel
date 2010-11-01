#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "katpriv.h"
#include "katcp.h"

/****************************************************************/

#define STATE_FRESH      0
#define STATE_COMMAND    1
#define STATE_WHITESPACE 2 
#define STATE_ARG        3
#define STATE_TAG        4
#define STATE_ESCAPE     5
#define STATE_DONE       6

/****************************************************************/

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

/******************************************************************/

unsigned int parsed_count_katcl(struct katcl_parse *p)
{
  return p->p_got;
}

int parsed_tag_katcl(struct katcl_parse *p)
{
  return p->p_tag;
}

int parsed_type_katcl(struct katcl_parse *p, char mode)
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

/************************************************************************************/



/************************************************************************************/

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

