/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "katpriv.h"
#include "katcl.h"
#include "katcp.h"

#define PARSE_MAGIC 0xff7f1273

/****************************************************************/

static int check_array_parse_katcl(struct katcl_parse *p);

/****************************************************************/

#ifdef KATCP_CONSISTENCY_CHECKS
static void sane_parse_katcl(struct katcl_parse *p)
{
  if(p == NULL){
    fprintf(stderr, "sane: parse is null\n");
    abort();
  }
  if(p->p_magic != KATCL_PARSE_MAGIC){
    fprintf(stderr, "sane: bad parse magic for %p (magic 0x%0x, expected 0x%x)\n", p, p->p_magic, KATCL_PARSE_MAGIC);
    abort();
  }
}
#else
#define sane_parse_katcl(p)
#endif

struct katcl_parse *create_parse_katcl()
{
  struct katcl_parse *p;

  p = malloc(sizeof(struct katcl_parse));
  if(p == NULL){
    return NULL;
  }

  p->p_magic = KATCL_PARSE_MAGIC;
  p->p_state = KATCL_PARSE_FRESH;

  p->p_buffer = NULL;
  p->p_size = 0;
  p->p_have = 0;
  p->p_used = 0;
  p->p_kept = 0;

  p->p_args = NULL;
  p->p_current = NULL;

  p->p_refs = 0; 
  p->p_tag = (-1);

  p->p_count = 0;
  p->p_got = 0;

  return p;
}

struct katcl_parse *create_referenced_parse_katcl()
{
  struct katcl_parse *p;

  p = create_parse_katcl();
  if(p == NULL){
    return NULL;
  }

  p->p_refs++;

  return p;
}

struct katcl_parse *copy_parse_katcl(struct katcl_parse *p)
{
  if(p == NULL){
    return NULL;
  }

  sane_parse_katcl(p);

  if(p->p_state != KATCL_PARSE_DONE){
#ifdef DEBUG
    fprintf(stderr, "warning: expected complete source parse in copy\n");
#endif
    return NULL;
  }

  p->p_refs++;

  return p;
}

static void clear_parse_katcl(struct katcl_parse *p)
{
  sane_parse_katcl(p);

#ifdef KATCP_CONSISTENCY_CHECKS
  if(p->p_refs != 1){
    fprintf(stderr, "logic problem: clearing parse used in multiple places\n");
    abort();
  }
#endif

  p->p_state = KATCL_PARSE_FRESH;

  p->p_have = 0;
  p->p_used = 0;
  p->p_kept = 0;
  p->p_tag = (-1);

  p->p_got = 0;
  p->p_current = NULL;

  p->p_tag = (-1);
}

struct katcl_parse *reuse_parse_katcl(struct katcl_parse *p)
{
  struct katcl_parse *px;
  
  if((p != NULL) && (p->p_refs <= 1)){
    clear_parse_katcl(p);
    return p;
  }

  px = create_referenced_parse_katcl();
  destroy_parse_katcl(p);

  return px;
}

void destroy_parse_katcl(struct katcl_parse *p)
{
  sane_parse_katcl(p);

#if DEBUG > 1
  fprintf(stderr, "destroy parse: %p with refs %d\n", p, p->p_refs);
#endif

  p->p_refs--;

  if(p->p_refs <= 0){
#if DEBUG > 1
    fprintf(stderr, "destroyed parse: %p: no more references\n", p);
#endif
    p->p_magic = 0xdead;
    p->p_state = (-1);

    if(p->p_buffer){
      free(p->p_buffer);
      p->p_buffer = NULL;
    }
    p->p_size = 0;
    p->p_have = 0;
    p->p_used = 0;
    p->p_kept = 0;

    if(p->p_args){
      free(p->p_args);
      p->p_args = NULL;
    }
    p->p_current = NULL;
    p->p_count = 0;
    p->p_got = 0;

    p->p_refs = (-1);
    p->p_tag = (-1);

    free(p);
  }
}

/******************************************************************/

int buffer_from_parse_katcl(struct katcl_parse *p, char *buffer, unsigned int len)
{
  struct katcl_larg *la;
  unsigned int i;
  int offset, size, space;

  sane_parse_katcl(p);

  switch(p->p_state){
    case KATCL_PARSE_FAKE :
    case KATCL_PARSE_DONE :
      break;
    default :
      return -1;
  }

  if((buffer == NULL) || (len == 0)){
    return -1;
  }

  offset = (-1); /* WARNING, trickery */
  space = len;

  if(p->p_got == 0){
    offset++;
  }

  for(i = 0; i < p->p_got; i++){

    offset++;

    la = &(p->p_args[i]);

    size = la->a_end - la->a_begin;
#ifdef KATCP_CONSISTENCY_CHECKS
    if(size < 0){
      fprintf(stderr, "buffer from parse: bad element size %d for item %u of %p", size, i, p);
      abort();
    }
#endif

    if(size >= space){

      if(len < 4){
        buffer[0] = '\0';
        return -1;
      }

      memcpy(buffer + offset, p->p_buffer + la->a_begin, space);
      memcpy(buffer + len - 4, "...", 4);

      return len; /* indicate overflow, primitively */

    } else {

      memcpy(buffer + offset, p->p_buffer + la->a_begin, size);

      offset += size;
      space -= size;

      buffer[offset] = ' ';
      space--;

      /* WARNING: offset gets updated unconventionally */
    }
  }

  buffer[offset] = '\0';

  return offset;
}

/******************************************************************/
/* logic to populate the parse ************************************/

static int before_add_parse_katcl(struct katcl_parse *p, unsigned int flags)
{
  sane_parse_katcl(p);

  if(flags & KATCP_FLAG_FIRST){
#ifdef KATCP_CONSISTENCY_CHECKS
    if(p->p_got > 0){
      fprintf(stderr, "usage problem: can not add first field to one which already has %u fields\n", p->p_got);
      abort();
    }
    if(p->p_state != KATCL_PARSE_FRESH){
      fprintf(stderr, "usage problem: need a fresh parse structure to populate\n");
      abort();
    }
#endif
    p->p_state = KATCL_PARSE_FAKE; /* not really parsed, inserted manually */
  } else {
#ifdef KATCP_CONSISTENCY_CHECKS
    if(p->p_got == 0){
      fprintf(stderr, "first field should be flagged as first\n");
      abort();
    }
#endif
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(p->p_state != KATCL_PARSE_FAKE){
    fprintf(stderr, "usage problem: parse structure in state %u, wanted state %u\n", p->p_state, KATCL_PARSE_FAKE);
    abort();
  }
#endif

  if(check_array_parse_katcl(p) < 0){
    return -1;
  }

  p->p_current->a_begin = p->p_kept;
  p->p_current->a_end = p->p_kept;
  p->p_current->a_escape = 0;

  if(flags & KATCP_FLAG_LAST){
    /* WARNING: risky: if request_space fails, we are still marked as good */
    p->p_state = KATCL_PARSE_DONE;
  }

  return 0;
}

int finalize_parse_katcl(struct katcl_parse *p)
{
  if (p == NULL)
    return -1;

  p->p_state = KATCL_PARSE_DONE;
  
  return 0;
}

static int after_add_parse_katcl(struct katcl_parse *p, unsigned int data, unsigned int escape)
{
#ifdef DEBUG
  if(p->p_size < p->p_kept + data + 1){
    fprintf(stderr, "add parse: over allocated parse (size=%u, added=%u+%u+1)\n", p->p_size, p->p_kept, data);
  }
#endif

  p->p_current->a_end = p->p_kept + data;
  p->p_current->a_escape = escape;

  p->p_buffer[p->p_kept + data] = '\0';

  p->p_kept += data + 1;

  p->p_have = p->p_kept;
  p->p_used = p->p_kept;

  p->p_got++;

  p->p_current = NULL; /* force crash */

  return data + 1;
}

static unsigned int actual_space_parse_katcl(struct katcl_parse *p)
{
  sane_parse_katcl(p);
#ifdef DEBUG
  if(p->p_size < p->p_kept){
    fprintf(stderr, "logic problem: size smaller than data kept\n");
  }
#endif
  return p->p_size - p->p_kept;
}

static char *request_space_parse_katcl(struct katcl_parse *p, unsigned int amount)
{
  char *tmp;
  unsigned int need;

  need = p->p_kept + amount;
  if(need >= p->p_size){

    need++; /* TODO: could try to guess harder */

    tmp = realloc(p->p_buffer, need);

    if(tmp == NULL){
      return NULL;
    }

    p->p_buffer = tmp;
    p->p_size = need;
  }

  return p->p_buffer + p->p_kept;
}

/*********************************************************************/

static int add_unsafe_parse_katcl(struct katcl_parse *p, int flags, char *string)
{
  unsigned int len;
  char *ptr;

  if(before_add_parse_katcl(p, flags) < 0){
    return -1;
  }

  len = strlen(string);

  ptr = request_space_parse_katcl(p, len + 1);
  if(ptr == NULL){
    return -1;
  }
  memcpy(ptr, string, len);

  return after_add_parse_katcl(p, len, 0);
}

/*********************************************************************/

int add_buffer_parse_katcl(struct katcl_parse *p, int flags, void *buffer, unsigned int len)
{
  char *src, *dst;

  src = buffer;

  if(before_add_parse_katcl(p, flags) < 0){
    return -1;
  }

#if DEBUG > 1
  fprintf(stderr, "adding %u bytes to parse %p\n", len, p);
#endif

  if(len > 0){
    dst = request_space_parse_katcl(p, len + 1);
    if(dst == NULL){
      return -1;
    }
    memcpy(dst, src, len);
  } /* else single \@ case */

  return after_add_parse_katcl(p, len, 1);
}

int add_parameter_parse_katcl(struct katcl_parse *pd, int flags, struct katcl_parse *ps, unsigned int index)
{
  char *src, *dst;
  unsigned int len;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(ps->p_state != KATCL_PARSE_DONE){
    fprintf(stderr, "warning: copy argument %u from incomplete parse (state=%u)\n", index, ps->p_state);
  }
#endif

  if(index >= ps->p_got){
    return -1;
  }

  if(ps->p_args[index].a_begin >= ps->p_args[index].a_end){
    len = 0; 
    src = NULL;
  } else {
    len = ps->p_args[index].a_end - ps->p_args[index].a_begin;
    src = ps->p_buffer + ps->p_args[index].a_begin;
  }

  if(before_add_parse_katcl(pd, flags) < 0){
    return -1;
  }

#if DEBUG > 1
  fprintf(stderr, "adding %u bytes to parse %p\n", len, pd);
#endif

  if(src){
    dst = request_space_parse_katcl(pd, len + 1);
    if(dst == NULL){
      return -1;
    }
    memcpy(dst, src, len);
  } /* else single \@ case */

  return after_add_parse_katcl(pd, len, 1);
}

/*********************************************************************/

int add_plain_parse_katcl(struct katcl_parse *p, int flags, char *string)
{
  return add_unsafe_parse_katcl(p, flags, string);
}

int add_string_parse_katcl(struct katcl_parse *p, int flags, char *buffer)
{
  if(buffer){
    return add_buffer_parse_katcl(p, flags, buffer, strlen(buffer));
  } else {
    return add_buffer_parse_katcl(p, flags, NULL, 0);
  }
}

int add_unsigned_long_parse_katcl(struct katcl_parse *p, int flags, unsigned long v)
{
#define TMP_BUFFER 32
  int result;
  char *ptr;

  if(before_add_parse_katcl(p, flags) < 0){
    return -1;
  }

  ptr = request_space_parse_katcl(p, TMP_BUFFER);
  if(ptr == NULL){
    return -1;
  }

  result = snprintf(ptr, TMP_BUFFER, "%lu", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return after_add_parse_katcl(p, result, 0);
#undef TMP_BUFFER
}

int add_signed_long_parse_katcl(struct katcl_parse *p, int flags, unsigned long v)
{
#define TMP_BUFFER 32
  int result;
  char *ptr;

  if(before_add_parse_katcl(p, flags) < 0){
    return -1;
  }

  ptr = request_space_parse_katcl(p, TMP_BUFFER);
  if(ptr == NULL){
    return -1;
  }

  result = snprintf(ptr, TMP_BUFFER, "%ld", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return after_add_parse_katcl(p, result, 0);
#undef TMP_BUFFER
}

int add_hex_long_parse_katcl(struct katcl_parse *p, int flags, unsigned long v)
{
#define TMP_BUFFER 32
  int result;
  char *ptr;

  if(before_add_parse_katcl(p, flags) < 0){
    return -1;
  }

  ptr = request_space_parse_katcl(p, TMP_BUFFER);
  if(ptr == NULL){
    return -1;
  }

  result = snprintf(ptr, TMP_BUFFER, "0x%lx", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return after_add_parse_katcl(p, result, 0);
#undef TMP_BUFFER
}

#ifdef KATCP_USE_FLOATS
int add_double_parse_katcl(struct katcl_parse *p, int flags, double v)
{
#define TMP_BUFFER 48
  int result;
  char *ptr;

  if(before_add_parse_katcl(p, flags) < 0){
    return -1;
  }

  ptr = request_space_parse_katcl(p, TMP_BUFFER);
  if(ptr == NULL){
    return -1;
  }

#if 0
  result = snprintf(ptr, TMP_BUFFER, "%f", v);
#endif
  result = snprintf(ptr, TMP_BUFFER, "%g", v);
  if((result <= 0) || (result >= TMP_BUFFER)){
    return -1;
  }

  return after_add_parse_katcl(p, result, 0);
#undef TMP_BUFFER
}
#endif

int add_vargs_parse_katcl(struct katcl_parse *p, int flags, char *fmt, va_list args)
{
#define TMP_BUFFER 8
#define TMP_LIMIT  10 /* in case of antique snprintf, start failing after TMP_BUFFER * 2^TMP_LIMIT */
  char *ptr;
  va_list copy;
  int want, got, x;

  if(before_add_parse_katcl(p, flags) < 0){
    return -1;
  }

#if DEBUG > 1
  fprintf(stderr, "add vargs: my fmt string is <%s>\n", fmt);
#endif

  got = TMP_BUFFER;

  for(x = 1; x < TMP_LIMIT; x++){ /* paranoid nutter check */
    ptr = request_space_parse_katcl(p, got);
    if(ptr == NULL){
#ifdef DEBUG
      fprintf(stderr, "add vargs: unable to request %d tmp bytes\n", got);
#endif
      return -1;
    }

    got = actual_space_parse_katcl(p);

    /* WARNING: check va_ logic: */
    va_copy(copy, args);
    want = vsnprintf(ptr, got, fmt, copy);
    va_end(copy);
#if DEBUG > 1
    fprintf(stderr, "add vargs: printed <%s> (iteration=%d, want=%d, got=%d)\n", ptr, x, want, got);
#endif

    if((want >= 0) && ( want < got)){
      return after_add_parse_katcl(p, want, 1);
    }

    if(want >= got){
      got = want + 1;
    } else {
      /* old style return codes, with x termination check */
      got *= 2;
    }
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  fprintf(stderr, "add vargs: sanity failure with %d bytes\n", got);
  abort();
#endif

  return -1;
#undef TMP_BUFFER
#undef TMP_LIMIT
}

int add_args_parse_katcl(struct katcl_parse *p, int flags, char *fmt, ...)
{
  va_list args;
  int result;

  va_start(args, fmt);
  result = add_vargs_parse_katcl(p, flags, fmt, args);
  va_end(args);

  return result;
}

struct katcl_parse *vturnaround_extra_parse_katcl(struct katcl_parse *p, int code, char *fmt, va_list args)
{
  char *string;
  struct katcl_parse *px;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(p->p_state != KATCL_PARSE_DONE){
    fprintf(stderr, "logic problem: attempting to turn around incomplete parse\n");
    abort();
  }
#endif

  string = copy_string_parse_katcl(p, 0);
  if(string == NULL){
    return NULL;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(string[0] != KATCP_REQUEST){
    fprintf(stderr, "logic problem: attempting to turn around <%s>\n", string);
    abort();
  }
#endif

  px = reuse_parse_katcl(p);
  if(px == NULL){
    destroy_parse_katcl(p);
    return NULL;
  }

  string[0] = KATCP_REPLY;
  add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, string);
  free(string);

  string = code_to_name_katcm(code);

  if(fmt){
    add_plain_parse_katcl(px, KATCP_FLAG_STRING, string ? string : KATCP_FAIL);

#if 0
    add_string_parse_katcl(px, KATCP_FLAG_LAST | KATCP_FLAG_STRING, fmt);
#endif

    add_vargs_parse_katcl(px, KATCP_FLAG_LAST | KATCP_FLAG_STRING, fmt, args);

  } else {
    add_plain_parse_katcl(px, KATCP_FLAG_LAST | KATCP_FLAG_STRING, string ? string : KATCP_FAIL);
  }

  return px;
}

struct katcl_parse *turnaround_extra_parse_katcl(struct katcl_parse *p, int code, char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  p = vturnaround_extra_parse_katcl(p, code, fmt, args);
  va_end(args);

  return p;
}

struct katcl_parse *turnaround_parse_katcl(struct katcl_parse *p, int code)
{
  return turnaround_extra_parse_katcl(p, code, NULL);
}

/******************************************************************/
/* end of adding to parse, now functions to get things out of it **/

unsigned int get_count_parse_katcl(struct katcl_parse *p)
{
#ifdef KATCP_CONSISTENCY_CHECKS
  if(p->p_state != KATCL_PARSE_DONE){
    fprintf(stderr, "warning: extracting argument count from incomplete parse (state=%u)\n", p->p_state);
  }
#endif
  return p->p_got;
}

int get_tag_parse_katcl(struct katcl_parse *p)
{
  return p->p_tag;
}

int is_type_parse_katcl(struct katcl_parse *p, char type)
{
  if(p->p_got <= 0){
    return 0;
  }

  if(p->p_buffer[p->p_args[0].a_begin] == type){
    return 1;
  }

  return 0;
}

int is_request_parse_katcl(struct katcl_parse *p)
{
  return is_type_parse_katcl(p, KATCP_REQUEST);
}

int is_reply_parse_katcl(struct katcl_parse *p)
{
  return is_type_parse_katcl(p, KATCP_REPLY);
}

int is_inform_parse_katcl(struct katcl_parse *p)
{
  return is_type_parse_katcl(p, KATCP_INFORM);
}

int is_null_parse_katcl(struct katcl_parse *p, unsigned int index)
{
  if(index >= p->p_got){
    return 1;
  } 

  if(p->p_args[index].a_begin >= p->p_args[index].a_end){
    return 1;
  }

  return 0;
}

char *get_string_parse_katcl(struct katcl_parse *p, unsigned int index)
{
#ifdef KATCP_CONSISTENCY_CHECKS
  if(p->p_state != KATCL_PARSE_DONE){
    fprintf(stderr, "warning: extracting string argument %u from incomplete parse (state=%u)\n", index, p->p_state);
  }
#endif

  if(index >= p->p_got){
    return NULL;
  }

  if(p->p_args[index].a_begin >= p->p_args[index].a_end){
    return NULL;
  }

  return p->p_buffer + p->p_args[index].a_begin;
}

char *copy_string_parse_katcl(struct katcl_parse *p, unsigned int index)
{
  char *ptr;

  ptr = get_string_parse_katcl(p, index);
  if(ptr){
    return strdup(ptr);
  } else {
    return NULL;
  }
}

unsigned long get_unsigned_long_parse_katcl(struct katcl_parse *p, unsigned int index)
{
  unsigned long value;

  if(index >= p->p_got){
    return 0;
  } 

  value = strtoul(p->p_buffer + p->p_args[index].a_begin, NULL, 0);

  return value;
}

long get_signed_long_parse_katcl(struct katcl_parse *p, unsigned int index)
{
  long value;

  if(index >= p->p_got){
    return 0;
  } 

  value = strtol(p->p_buffer + p->p_args[index].a_begin, NULL, 0);

  return value;
}

int get_bb_parse_katcl(struct katcl_parse *p, unsigned int index, struct katcl_byte_bit *b)
{
  char *string, *end;
  unsigned long byte, bit; 

  if(b == NULL){
    return -1;
  }

  string = get_string_parse_katcl(p, index);
  if(string == NULL){
    return -1;
  }

  if(string[0] != ':'){
    byte = strtoul(string, &end, 0);
    if(end[0] == ':'){
      string = end;
    }
  } else {
    byte = 0;
  }

  if(string[0] == ':'){
    bit = strtoul(string + 1, NULL, 0);
  } else {
    bit = 0;
  }

  return make_bb_katcl(b, byte, bit);
}

#ifdef KATCP_USE_FLOATS
double get_double_parse_katcl(struct katcl_parse *p, unsigned int index)
{
  double value;

  if(index >= p->p_got){
    return 0.0; /* WARNING: should probably be NaN */
  } 

  value = strtod(p->p_buffer + p->p_args[index].a_begin, NULL);

  return value;
}
#endif

unsigned int get_buffer_parse_katcl(struct katcl_parse *p, unsigned int index, void *buffer, unsigned int size)
{
  unsigned int got, done;

  sane_parse_katcl(p);

  if(index >= p->p_got){
    return 0;
  } 

  got = p->p_args[index].a_end - p->p_args[index].a_begin;
  done = (got > size) ? size : got;

  if(buffer && (got > 0)){
    memcpy(buffer, p->p_buffer + p->p_args[index].a_begin, done);
  }

  return got;
}

/******************************************************************/
/* end of getting this out a parse, now logic to read from line ***/

static int check_array_parse_katcl(struct katcl_parse *p)
{
  struct katcl_larg *tmp;

  sane_parse_katcl(p);

  if(p->p_got < p->p_count){
    p->p_current = &(p->p_args[p->p_got]);
    return 0;
  }

  tmp = realloc(p->p_args, sizeof(struct katcl_larg) * (p->p_count + KATCL_ARGS_INC));
  if(tmp == NULL){
    return -1;
  }

  p->p_args = tmp;
  p->p_count += KATCL_ARGS_INC;
  p->p_current = &(p->p_args[p->p_got]);

  return 0;
}

static int stash_remainder_parse_katcl(struct katcl_parse *p, char *buffer, unsigned int len)
{
  char *tmp;
  unsigned int need;

  sane_parse_katcl(p);

#if DEBUG>2
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

#if 0
int deparse_katcl(struct katcl_parse *p)
{
  int i;
  struct katcl_larg *la;

  switch(p->p_state){
    case KATCL_PARSE_DONE :
      break;
    case KATCL_PARSE_FLAT :
      p->p_wrote = 0;
      return 0;
    default :
#ifdef DEBUG
      fprintf(stderr, "deparse: unable to flatten a message in state %u\n", p->p_state);
#endif
      return -1;
  }

  la = NULL;

  for(i = 0; i < p->p_got; i++){
    la = &(p->p_args[i]);
    if(la->a_escapes > 0){
      /* TODO: escape characters, testing that next argument is perfectly contiguous (tags ? ) */
    } 
    p->p_buffer[la->a_end] = ' ';
  }

  if(la){
    p->p_buffer[la->a_end] = '\n';
  }

  p->p_state = KATCL_PARSE_FLAT;
  p->p_wrote = 0;

  return 0;
}
#endif

int parse_katcl(struct katcl_line *l) /* transform buffer -> args */
{
  int increment;
  struct katcl_parse *p;

  p = l->l_next;
#ifdef KATCP_CONSISTENCY_CHECKS
  if(p == NULL){
    fprintf(stderr, "logic failure: expected a valid next entry in parse\n");
    abort();
  }
#endif

#if DEBUG>2
  fprintf(stderr, "invoking parse (state=%d, have=%d, used=%d, kept=%d)\n", p->p_state, p->p_have, p->p_used, p->p_kept);
#endif

  sane_parse_katcl(p);

  while((p->p_used < p->p_have) && (p->p_state != KATCL_PARSE_DONE)){

    increment = 0; /* what to do to keep */

#if DEBUG > 1
    fprintf(stderr, "parse: state=%d, char=%c\n", p->p_state, p->p_buffer[p->p_used]);
#endif

    switch(p->p_state){

      case KATCL_PARSE_FAKE :
#ifdef KATCP_CONSISTENCY_CHECKS
        fprintf(stderr, "major logic problem: attempting to parse a locally generated message\n");
        abort();
#endif
        break;

      case KATCL_PARSE_FRESH : 
        switch(p->p_buffer[p->p_used]){
          case '#' :
          case '!' :
          case '?' :
            if(check_array_parse_katcl(p) < 0){
              l->l_error = ENOMEM;
              return -1;
            }

#ifdef DEBUG
            if(p->p_kept != p->p_used){
              fprintf(stderr, "logic issue: kept=%d != used=%d\n", p->p_kept, p->p_used);
            }
#endif
            p->p_current->a_begin = p->p_kept;
            p->p_current->a_escape= 0;
            p->p_state = KATCL_PARSE_COMMAND;

            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            increment = 1;
            break;

          default :
            break;
        }
        break;

      case KATCL_PARSE_COMMAND :
        increment = 1;
        switch(p->p_buffer[p->p_used]){
          case '[' : 
            p->p_tag = 0;
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = KATCL_PARSE_TAG;
            break;

          case ' '  :
          case '\t' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = KATCL_PARSE_WHITESPACE;
            break;

          case '\n' :
          case '\r' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = KATCL_PARSE_DONE;
            break;

          default   :
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            break;
            
        }
        break;

      case KATCL_PARSE_TAG : 
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
            p->p_state = KATCL_PARSE_WHITESPACE;
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

      case KATCL_PARSE_WHITESPACE : 
        switch(p->p_buffer[p->p_used]){
          case ' '  :
          case '\t' :
            break;
          case '\n' :
          case '\r' :
            p->p_state = KATCL_PARSE_DONE;
            break;
          case '\\' :
            if(check_array_parse_katcl(p) < 0){
              l->l_error = ENOMEM;
              return -1;
            }
            p->p_current->a_begin = p->p_kept; /* token begins with an escape */
            p->p_current->a_escape = 0; /* gets set in escape state */
            p->p_state = KATCL_PARSE_ESCAPE;
            break;
          default   :
            if(check_array_parse_katcl(p) < 0){
              l->l_error = ENOMEM;
              return -1;
            }
            p->p_current->a_begin = p->p_kept; /* token begins with a normal char */
            p->p_current->a_escape = 0;
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            increment = 1;
            p->p_state = KATCL_PARSE_ARG;
            break;
        }
        break;

      case KATCL_PARSE_ARG :
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
            p->p_state = KATCL_PARSE_WHITESPACE;
            break;

          case '\n' :
          case '\r' :
            p->p_buffer[p->p_kept] = '\0';
            p->p_current->a_end = p->p_kept;
            p->p_got++;
            p->p_state = KATCL_PARSE_DONE;
            break;
            
          case '\\' :
            p->p_state = KATCL_PARSE_ESCAPE;
            increment = 0;
            break;

          default   :
            p->p_buffer[p->p_kept] = p->p_buffer[p->p_used];
            break;
        }
        
        break;

      case KATCL_PARSE_ESCAPE :
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
        p->p_current->a_escape = 1;
        p->p_state = KATCL_PARSE_ARG;
        break;
    }

    p->p_used++;
    p->p_kept += increment;
  }

  if(p->p_state != KATCL_PARSE_DONE){ /* not finished, run again later */
    return 0; 
  }

#ifdef DEBUG 
  l->l_next = NULL; /* value kept in p */
#endif

  /* we now need a next entry */
  l->l_next = create_referenced_parse_katcl();
  if(l->l_next == NULL){
    l->l_error = ENOMEM;
    return -1; /* is it safe to call parse with a next which is DONE ? */
  }

  /* ready is about to be replaced, delete the old copy */
  if(l->l_ready){
    destroy_parse_katcl(l->l_ready);
    l->l_ready = NULL;
  }

  /* stash any unparsed io for the next round */
  if(p->p_used < p->p_have){
    stash_remainder_parse_katcl(l->l_next, p->p_buffer + p->p_used, p->p_have - p->p_used);
    p->p_have = p->p_used;
  }

  l->l_ready = p;

  return 1;
}

/******************************************************************/
/* end of core library logic, now debug and test routines *********/

#include <ctype.h>

int dump_parse_katcl(struct katcl_parse *p, char *prefix, FILE *fp)
{
  unsigned int i, j;
  struct katcl_larg *la;

  if(p == NULL){
    fprintf(fp, "parse %s is null\n", prefix);
    return -1;
  }

  fprintf(fp, "parse %s: state=%d\n", prefix, p->p_state);
  fprintf(fp, "parse %s: buffer size=%u, have=%u, used=%u, kept=%u\n", prefix, p->p_size, p->p_have, p->p_used, p->p_kept);
  fprintf(fp, "parse %s: count=%u, got=%u\n", prefix, p->p_count, p->p_got);

  sane_parse_katcl(p);

  for(i = 0; i < p->p_got; i++){
    la = &(p->p_args[i]);
#if 0
    fprintf(fp, "parse[%u]: (%s) <", i, get_string_parse_katcl(p, i)); 
#endif
    fprintf(fp, "parse[%u]: <", i);
    for(j = la->a_begin; j < la->a_end; j++){
      if(isprint(p->p_buffer[j])){
        fprintf(fp, "%c", p->p_buffer[j]);
      } else {
        fprintf(fp, "[%02x]", 0xff & ((unsigned int)(p->p_buffer[j])));
      }
    }
    fprintf(fp, ">, begin=%u, end=%u, [%s]\n", la->a_begin, la->a_end, la->a_escape ? "may need escaping" : "no escaping needed");
  }

  return 0;
}

#ifdef UNIT_TEST_PARSE

int main()
{
#define BUFFER 32
  struct katcl_parse *p, *pc;
  char *ptr;
  char buffer[BUFFER];

  p = create_referenced_parse_katcl();
  if(p == NULL){
    fprintf(stderr, "unable to create parse structure\n");
    return 1;
  }

  dump_parse_katcl(p, "empty", stderr);
  add_plain_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "?foobar");
  add_string_parse_katcl(p, KATCP_FLAG_STRING, "froz\nbozz");

  add_unsigned_long_parse_katcl(p, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, 42UL);

  dump_parse_katcl(p, "test", stderr);

  ptr = get_string_parse_katcl(p, 0);
  if((ptr == NULL) || strcmp(ptr, "?foobar")){
    fprintf(stderr, "bad argument 0: %s\n", ptr);
    return 1;
  }
  ptr = get_string_parse_katcl(p, 1);
  if((ptr == NULL) || strcmp(ptr, "froz\nbozz")){
    fprintf(stderr, "bad argument 1: %s\n", ptr);
    return 1;
  }

  pc = copy_parse_katcl(p);
  if(pc == NULL){
    fprintf(stderr, "unable to copy parse\n");
    return 1;
  }

  dump_parse_katcl(pc, "copy", stderr);

  buffer_from_parse_katcl(p, buffer, BUFFER);
  fprintf(stderr, "buffer of parse: <%s>\n", buffer);

  destroy_parse_katcl(p);
  destroy_parse_katcl(pc);

  printf("parse test: ok\n");

  return 0;
#undef BUFFER
}
  
#endif
