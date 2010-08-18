/* time comparison code grabbed from libloop */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>

void component_time_katcp(struct timeval *result, unsigned int ms)
{
  result->tv_sec  = ms / 1000;
  result->tv_usec = (ms % 1000) * 1000;
#ifdef DEBUG
  fprintf(stderr, "component time: %ums -> %lu.%06lds\n", ms, result->tv_sec, result->tv_usec);
#endif
}

int cmp_time_katcp(struct timeval *alpha, struct timeval *beta)
{
  if(alpha->tv_sec < beta->tv_sec){
    return -1;
  }

  if(alpha->tv_sec > beta->tv_sec){
    return 1;
  }

  if(alpha->tv_usec < beta->tv_usec){
    return -1;
  }

  if(alpha->tv_usec > beta->tv_usec){
    return 1;
  }

  return 0;
}

int add_time_katcp(struct timeval *sigma, struct timeval *alpha, struct timeval *beta)
{
  if(alpha->tv_usec + beta->tv_usec >= 1000000){
    sigma->tv_sec = alpha->tv_sec + beta->tv_sec + 1;
    sigma->tv_usec = (alpha->tv_usec + beta->tv_usec) - 1000000;
  } else {
    sigma->tv_sec = alpha->tv_sec + beta->tv_sec;
    sigma->tv_usec = alpha->tv_usec + beta->tv_usec;
  }
  return 0;
}

int sub_time_katcp(struct timeval *delta, struct timeval *alpha, struct timeval *beta)
{
  if(alpha->tv_usec < beta->tv_usec){
    if(alpha->tv_sec <= beta->tv_sec){
      delta->tv_sec  = 0;
      delta->tv_usec = 0;
      return -1;
    }
    delta->tv_sec  = alpha->tv_sec - (beta->tv_sec + 1);
    delta->tv_usec = (1000000 + alpha->tv_usec) - beta->tv_usec;
  } else {
    if(alpha->tv_sec < beta->tv_sec){
      delta->tv_sec  = 0;
      delta->tv_usec = 0;
      return -1;
    }
    delta->tv_sec  = alpha->tv_sec  - beta->tv_sec;
    delta->tv_usec = alpha->tv_usec - beta->tv_usec;
  }
#ifdef DEBUG
  if(delta->tv_usec >= 1000000){
    fprintf(stderr, "major logic failure: %lu.%06lu-%lu.%06lu yields %lu.%06lu\n", alpha->tv_sec, alpha->tv_usec, beta->tv_sec, beta->tv_usec, delta->tv_sec, delta->tv_usec);
    abort();
  }
#endif
  return 0;
}

