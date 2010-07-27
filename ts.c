#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "katcp.h"
#include "katpriv.h"

#define TS_MAGIC 0x01020811

void dump_timers_katcp(struct katcp_dispatch *d)
{
  int i;
  struct katcp_shared *s;
  struct katcp_time *ts;

  s = d->d_shared;
  if(s == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "null shared state while checking timer logic");
    return;
  }

  for(i = 0; i < s->s_length; i++){
    ts = s->s_queue[i];
    if(ts == NULL){
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "ts entry %d is null", i);
    } else if(ts->t_magic != TS_MAGIC){
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "ts %d has bad magic 0x%x", i, ts->t_magic);
    } else {
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "%s ts %d runs %p on %p @ %lu.%06lu every %lu.%06lu", (ts->t_armed) ? "armed" : "done", i, ts->t_call, ts->t_data, ts->t_when.tv_sec, ts->t_when.tv_usec, ts->t_interval.tv_sec, ts->t_interval.tv_usec);
    }
  }
}

static struct katcp_time *create_ts_katcp(int (*call)(struct katcp_dispatch *d, void *data), void *data)
{
  struct katcp_time *ts;

  ts = malloc(sizeof(struct katcp_time));
  if(ts == NULL){
    return NULL;
  }

  ts->t_magic = TS_MAGIC;

  ts->t_interval.tv_sec = 0;
  ts->t_interval.tv_usec = 0;

  ts->t_data = data;
  ts->t_call = call;

  return ts;
}

static void destroy_ts_katcp(struct katcp_dispatch *d, struct katcp_time *ts)
{
  if(ts->t_magic != TS_MAGIC){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "bad magic while destroying timer instance");
    return;
  }

  if(ts->t_armed > 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "destruction of armed timer should not happen");
  }

  ts->t_armed = 0;
  ts->t_magic = 0;

  free(ts);
}

static struct katcp_time *find_ts_katcp(struct katcp_dispatch *d, void *data)
{
  int i;
  struct katcp_shared *s;

  s = d->d_shared;
#ifdef DEBUG
  if(s == NULL){
    fprintf(stderr, "find: no shared state\n");
    return NULL;
  }
#endif

  for(i = 0; i < s->s_length; i++){
    if(s->s_queue[i]->t_data == data){
      return s->s_queue[i];
    }
  }

  return NULL;
}

static int append_ts_katcp(struct katcp_dispatch *d, struct katcp_time *ts)
{
  struct katcp_time **tptr;
  struct katcp_shared *s;

  s = d->d_shared;
#ifdef DEBUG
  if(s == NULL){
    fprintf(stderr, "prepend: no shared state\n");
    return -1;
  }
#endif

  tptr = realloc(s->s_queue, sizeof(struct katcp_time *) * (s->s_length + 1));
  if(tptr == NULL){
    return -1;
  }

  s->s_queue = tptr;

  s->s_queue[s->s_length] = ts;
  s->s_length++;

  return 0;
}

/* functions to schedule things at particular times *******************************/

int register_every_ms_katcp(struct katcp_dispatch *d, unsigned int milli, int (*call)(struct katcp_dispatch *d, void *data), void *data)
{
  struct timeval tv;

  tv.tv_sec = milli / 1000;
  tv.tv_usec = (milli % 1000) * 1000;

  return register_every_tv_katcp(d, &tv, call, data);
}

int register_every_tv_katcp(struct katcp_dispatch *d, struct timeval *tv, int (*call)(struct katcp_dispatch *d, void *data), void *data)
{
  struct katcp_shared *s;
  struct katcp_time *ts;
  struct timeval now;

  s = d->d_shared;

#ifdef DEBUG
  if(tv->tv_usec >= 1000000){
    fprintf(stderr, "every tv: major logic problem: usec too large at %luus\n", tv->tv_usec);
    abort();
  }
#endif

  ts = find_ts_katcp(d, data);
  if(ts == NULL){
    ts = create_ts_katcp(call, data);
    if(ts == NULL){
      return -1;
    }

    if(append_ts_katcp(d, ts) < 0){
      destroy_ts_katcp(d, ts);
      return -1;
    }
  }

  gettimeofday(&now, NULL);

  ts->t_interval.tv_sec = tv->tv_sec;
  ts->t_interval.tv_usec = tv->tv_usec;

  add_time_katcp(&(ts->t_when), &now, tv);

  ts->t_armed = 1;

  return 0;
}

int register_at_tv_katcp(struct katcp_dispatch *d, struct timeval *tv, int (*call)(struct katcp_dispatch *d, void *data), void *data)
{
  struct katcp_shared *s;
  struct katcp_time *ts;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

#ifdef DEBUG
  if(tv->tv_usec >= 1000000){
    fprintf(stderr, "at tv: major logic problem: usec too large at %luus\n", tv->tv_usec);
    abort();
  }
#endif

  ts = find_ts_katcp(d, data);
  if(ts == NULL){
    ts = create_ts_katcp(call, data);
    if(ts == NULL){
      return -1;
    }

    if(append_ts_katcp(d, ts) < 0){
      destroy_ts_katcp(d, ts);
      return -1;
    }
  }

  ts->t_interval.tv_sec = 0;
  ts->t_interval.tv_usec = 0;

  ts->t_when.tv_sec = tv->tv_sec; 
  ts->t_when.tv_usec = tv->tv_usec; 

  ts->t_armed = 1;

  return 0;
}

/* deal with time warp ***************************************************************/

int unwarp_timers_katcp(struct katcp_dispatch *d)
{
  unsigned int i;
  struct katcp_shared *s;
  struct katcp_time *ts;
  struct timeval now, sum;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  if(s->s_length == 0){
    return 0;
  }

  gettimeofday(&now, NULL);

  for(i = 0; i < s->s_length; i++){
    ts = s->s_queue[i];

    if((ts->t_interval.tv_sec > 0) || (ts->t_interval.tv_usec > 0)){
      add_time_katcp(&sum, &now, &(ts->t_interval));
      if(cmp_time_katcp(&(ts->t_when), &sum) < 0){
        ts->t_when.tv_sec  = sum.tv_sec;
        ts->t_when.tv_usec = sum.tv_usec;
      }
    }
  }

  return 0;
}

/* release timers ********************************************************************/

int discharge_timer_katcp(struct katcp_dispatch *d, void *data)
{
  struct katcp_shared *s;
  struct katcp_time *ts;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  ts = find_ts_katcp(d, data);
  if(ts == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "nonexistent timer for %p not descheduled", data);
    return -1;
  }

  ts->t_armed = (-1);

  return 0;
}

int empty_timers_katcp(struct katcp_dispatch *d)
{
  unsigned int i;
  struct katcp_shared *s;

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  if(s->s_length == 0){
    return 0;
  }

  for(i = 0; i < s->s_length; i++){
    s->s_queue[i]->t_armed = 0;
    destroy_ts_katcp(d, s->s_queue[i]);
  }

  free(s->s_queue);
  s->s_queue = NULL;
  s->s_length = 0;

  return 0;
}

/* do the hard work of actually running the timers ************************************/

int run_timers_katcp(struct katcp_dispatch *d, struct timespec *interval)
{
  /* this used to be a snazzy priority queue */

  struct katcp_shared *s;
  struct katcp_time *ts;
  struct timeval now, delta, min;
  unsigned int i, j;
  int result;

  s = d->d_shared;
  if(s == NULL){
#ifdef DEBUG
    fprintf(stderr, "schedule: no shared state\n");
#endif
    return -1;
  }

  if(s->s_length <= 0){
#ifdef DEBUG
    fprintf(stderr, "schedule: nothing sheduled, no timeout\n");
#endif
    return 1;
  }

  gettimeofday(&now, NULL);

#ifdef DEBUG
  dump_timers_katcp(d);
#endif

  /* run all timers */
  for(i = 0; i < s->s_length; i++){
    ts = s->s_queue[i];
    if(ts->t_armed > 0){
      result = cmp_time_katcp(&(ts->t_when), &(now));
      if(result <= 0){
        if(result < 0){
          log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "missed deadline at %lu.%06lus now %lu.%06lus for %p", ts->t_when.tv_sec, ts->t_when.tv_usec, now.tv_sec, now.tv_usec, ts->t_data);
        }
        ts->t_armed = 0; /* assume that we won't run again */
        if((*(ts->t_call))(d, ts->t_data) >= 0){
          /* only automatically re-arm if periodic and not failed */
          if((ts->t_interval.tv_sec != 0) || (ts->t_interval.tv_usec != 0)){
            ts->t_armed++; /* a discharge will result in this still being zero */
            add_time_katcp(&(ts->t_when), &(ts->t_when), &(ts->t_interval));
            if(cmp_time_katcp(&(ts->t_when), &now) < 0){
              add_time_katcp(&(ts->t_when), &now, &(ts->t_interval));
              log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "detected time warp or stall, rescheduling periodic task for %lu.%06lus", ts->t_when.tv_sec, ts->t_when.tv_usec);
            }
          }
        }
      }
    }
  }

  /* trim out empty elements */
  j = 0;
  for(i = 0; i < s->s_length; i++){
    ts = s->s_queue[i];
    if(ts->t_armed > 0){
      s->s_queue[j] = ts;
      j++;
    } else {
      destroy_ts_katcp(d, ts);
    }
  }
  s->s_length = j;

  /* only destroy queue if everthing has been done */
  if(s->s_length == 0){

    free(s->s_queue);
    s->s_queue = NULL;

#ifdef DEBUG
    fprintf(stderr, "schedule: everything sheduled done, no timeout\n");
#endif
    return 1;
  }

  ts = s->s_queue[0];
  min.tv_sec  = ts->t_when.tv_sec;
  min.tv_usec = ts->t_when.tv_usec;

  for(i = 1; i < s->s_length; i++){
    ts = s->s_queue[i];

    if(cmp_time_katcp(&(ts->t_when), &min) < 0){
      min.tv_sec  = ts->t_when.tv_sec;
      min.tv_usec = ts->t_when.tv_usec;
    }
  }

  sub_time_katcp(&delta, &min, &now);

#ifdef DEBUG
  fprintf(stderr, "schedule: %d scheduled callbacks left\n", s->s_length);
#endif

  interval->tv_sec = delta.tv_sec;
  interval->tv_nsec = delta.tv_usec * 1000;

  return 0;
}
