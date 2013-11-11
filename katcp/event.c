#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "katcp.h"
#include "katpriv.h"

struct katcp_event;

struct katcp_interest{
  struct katcp_flat *i_flat;
  void *i_data;
  int (*i_call)(struct katcp_dispatch *d, struct katcp_event *e, void *data);
};

struct katcp_event{
  char *e_name;

  struct katcp_interest *e_vector;
  unsigned int e_count;

  int e_use;
  int e_trigger;

  struct katcl_queue *e_queue;

  /* TODO: something to note if we trigger event within this event */
};

void destroy_event_katcp(struct katcp_dispatch *d, struct katcp_event *e)
{
  unsigned int i;
  struct katcp_interest *ki;

  if(e == NULL){
    return;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(e->e_use > 0){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "internal problem: attempting to destroy an event which is held by some parties");
    abort();
  }
#endif

  for(i = 0; i < e->e_count; i++){
    ki = &(e->e_vector[i]);
    /* TODO */
  }
  e->e_count = 0;

  if(e->e_name){
    free(e->e_name);
    e->e_name = NULL;
  }

  if(e->e_vector){
    free(e->e_vector);
    e->e_vector = NULL;
  }

  if(e->e_queue){
    destroy_queue_katcl(e->e_queue);
    e->e_queue = NULL;
  }

  e->e_count = 0;

  free(e);
}

struct katcp_event *create_event_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_event *e;

  e = malloc(sizeof(struct katcp_event));
  if(e == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create event");
    return NULL;
  }

  e->e_name = NULL;

  e->e_vector = NULL;
  e->e_count = 0;

  e->e_use = 0;
  e->e_trigger = 0;

  e->e_queue = NULL;

  return e;
}

#endif
