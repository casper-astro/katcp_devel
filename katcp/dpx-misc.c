#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

static char *scope_table[KATCP_MAX_SCOPE] = {
  "individual", "group", "global"
};

char *string_from_scope_katcp(unsigned int scope)
{
  if(scope >= KATCP_MAX_SCOPE){
    return NULL;
  }

  return scope_table[scope];
}

int code_from_scope_katcp(char *scope)
{
  unsigned int i;

  if(scope == NULL){
    return -1;
  }

  for(i = 0; i < KATCP_MAX_SCOPE; i++){
    if(!strcmp(scope_table[i], scope)){
      return i;
    }
  }

  return -1;
}

int fixup_timestamp_katcp(char *src, char *dst, int size)
{
  char *dot;
  int suffix, prefix, total;

#if KATCP_PROTOCOL_MAJOR_VERSION >= 5   

  dot = strchr(src, '.');
  if(dot){
    /* timestamp assumed to be v5, in seconds.fraction */

    if((src + 10) > dot){
      /* at the time of writing this code, this many seconds had already elapsed */
      return -1;
    }

    suffix = strlen(dot);
    prefix = dot - src;

    total = prefix + suffix;

    if(suffix >= 4){ /* all ok, no fixup */
      if(total >= size){
        if((prefix + 4) >= size){
          return -1;
        } else {
          memcpy(dst, src, size - 1);
          dst[size - 1] = '\0';
        }
      }

      memcpy(dst, src, total + 1);
      return 0;

    } else {
      /* pad out ... */

      if(((dot - src) + 4) >= size){
        return -1;
      }
      memcpy(dst, src, total);
      while(suffix < 4){
        dst[total++] = '0';
        suffix++;
      }
      dst[total] = '\0';

      return 1;
    }

  } else {
    /* we are v5 but talking to v4 */

    total = strlen(src);
    if(total < 13){
      /* unreasonable time */
      return -1;
    }

    if((total + 1) >= size){
      return -1;
    }

    prefix = total - 3;

    memcpy(dst, src, prefix);
    dst[prefix] = '.';
    memcpy(dst + prefix + 1, src + prefix, 4);

    return 1;
  }

#else

  /* we are configured to be old-style ... */

  dot = strchr(src, '.');
  if(dot){
    /* a new message, with seconds and fractions */

    prefix = dot - src;
    if(prefix < 10){
      return -1;
    }

    if((prefix + 3) >= size){
      return -1;
    }

    memcpy(dst, src, prefix);

    total = strlen(src);

    suffix = total - (prefix + 1);
    if(suffix > 3){
      suffix = 3;
    }

    if(suffix > 0){
      memcpy(dst + prefix, dot + 1, suffix);
    }
    while(suffix < 3){
      dst[prefix + suffix] = '0';
      suffix++;
    }

    dst[prefix + suffix] = '\0';

    return 1;

  } else {
    /* old style, no change other than length checks */

    total = strlen(src);
    if(total < 13){
      return -1;
    }

    if(total >= size){
      return -1;
    }

    strcpy(dst, src);

    return 0;
  }

#endif

}

#endif

#ifdef UNIT_TEST_DPX_MISC 

int main(int argc, char **argv)
{
#define BUFFER 32
  struct timeval tv;
  unsigned int milli;
  char in[BUFFER], out[BUFFER];

  gettimeofday(&tv, NULL);
  milli = tv.tv_usec / 1000;

  snprintf(in, BUFFER, "%lu.%03u", tv.tv_sec, milli);
  fixup_timestamp_katcp(in, out, BUFFER);
  printf("%s \t-> %s\n", in, out);

  snprintf(in, BUFFER, "%lu.%01u", tv.tv_sec, milli / 100);
  fixup_timestamp_katcp(in, out, BUFFER);
  printf("%s \t-> %s\n", in, out);

  snprintf(in, BUFFER, "%lu%03u", tv.tv_sec, milli);
  fixup_timestamp_katcp(in, out, BUFFER);
  printf("%s \t-> %s\n", in, out);

  snprintf(in, BUFFER, "%lu.", tv.tv_sec);
  fixup_timestamp_katcp(in, out, BUFFER);
  printf("%s \t-> %s\n", in, out);

  snprintf(in, BUFFER, "%lu.%03u", tv.tv_sec, milli);
  if(fixup_timestamp_katcp(in + 1, out, BUFFER) >= 0){
    fprintf(stderr, "fail: %s \t-> %s\n", in, out);
    return 1;
  } 

  return 0;

#undef BUFFER
}

#endif

