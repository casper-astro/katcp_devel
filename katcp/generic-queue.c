/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>
#include <errno.h>
#include <sysexits.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <unistd.h>

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"
#include "netc.h"

/* Pathetic attempt at a generic queue */

struct katcl_gueue
{
  void       **g_queue; /* elements */
  unsigned int g_size;  /* size of queue */
  unsigned int g_head;  /* current position */
  unsigned int g_count; /* No of entries */

  void (*g_release)(void *datum);
};

struct katcl_gueue *create_gueue_katcl(void (*release)(void *datum))
{
  struct katcl_gueue *g;

  g = malloc(sizeof(struct katcl_gueue));
  if(g == NULL){
    fprintf(stderr, "generic queue: unable to allocate state\n");
    return NULL;
  }
  g->g_queue = NULL;

  g->g_head = 0;
  g->g_count = 0;
  g->g_size = 0;

  g->g_release = release;

  return g;
}

void destroy_gueue_katcl(struct katcl_gueue *g)
{
  unsigned int i, j;

  for(j = 0; j < g->g_count; j++){
    i = (g->g_head + j) % g->g_size;
    if(g->g_release){
      (*(g->g_release))(g->g_queue[i]);
    }
    g->g_queue[i] = NULL;
  }

  if(g->g_queue){
    free(g->g_queue);
    g->g_queue = NULL;
  }

  g->g_count = 0;
  g->g_size = 0;
  g->g_head = 0;

  g->g_release = NULL;

  free(g);
}

void clear_gueue_katcl(struct katcl_gueue *g)
{
  unsigned int i, j;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(g == NULL){
    fprintf(stderr, "generic queue: given null queue to clear\n");
    abort();
  }
#endif

  for(j = 0; j < g->g_count; j++){
    i = (g->g_head + j) % g->g_size;
    if(g->g_release){
      (*(g->g_release))(g->g_queue[i]);
    }
    g->g_queue[i] = NULL;
  }

  g->g_count = 0;
}

/* logic to manage the queue ****************************************************/

int add_tail_gueue_katcl(struct katcl_gueue *g, void *datum)
{
  void **tmp;
  unsigned int index;

  if(datum == NULL){
    return -1;
  }

#if DEBUG > 1
  fprintf(stderr, "generic queue: adding %p with queue %p of size %d\n", datum, g, g->g_count);
#endif

  if(g->g_count >= g->g_size){

#if DEBUG > 1
    fprintf(stderr, "generic queue: size=%d, count=%d - increasing queue\n", g->g_size, g->g_count);
#endif

#ifdef KATCP_CONSISTENCY_CHECKS
    if(g->g_size < g->g_count){
      fprintf(stderr, "generic queue: warning: detected rapid size increase of queue, probably a corruption\n");
      abort();
    }
#endif
    tmp = realloc(g->g_queue, sizeof(void *) * (g->g_size + 1));

    if(tmp == NULL){
      return -1;
    }
    g->g_queue = tmp;

    if(g->g_head > 0){
      g->g_queue[g->g_size] = g->g_queue[0];
      if(g->g_head > 1){
        memmove(&(g->g_queue[0]), &(g->g_queue[1]), sizeof(void *) * (g->g_head - 1));
      }
      g->g_queue[g->g_head - 1] = NULL;
    }

    g->g_size = g->g_size + 1;
  } 
  index = (g->g_head + g->g_count) % g->g_size;

#if DEBUG > 1
  fprintf(stderr, "generic queue: %p add[%d]=%p\n", g, index, datum);
#endif

  g->g_queue[index] = datum;
  g->g_count++;

  return 0;
}

/*************************************************************************/

void *get_index_gueue_katcl(struct katcl_gueue *g, unsigned int index)
{
  unsigned int wrap;

  if((g->g_count == 0) || (g->g_size == 0) || (g->g_count >= g->g_size)){
    return NULL;
  }

  if((g->g_count > index) && (index < g->g_size)){
    wrap = ((g->g_head + index) % g->g_size);
    return g->g_queue[wrap];
  } else {
    return NULL;
  }
}

void *get_head_gueue_katcl(struct katcl_gueue *g)
{
  return (g->g_count > 0) ? g->g_queue[g->g_head] : NULL;
}

/*************************************************************************/

void *remove_index_gueue_katcl(struct katcl_gueue *g, unsigned int index)
{
  unsigned int end;
  void *datum;

  /* WARNING: removing from queue does not decrease reference count */

  if(g->g_count <= 0){
#ifdef DEBUG
    fprintf(stderr, "generic queue: nothing to remove\n");
#endif
    return NULL;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(index >= g->g_size){
    fprintf(stderr, "generic queue: index %u out of range %u\n", index, g->g_size);
    abort();
  }
  if(g->g_queue[index] == NULL){
    fprintf(stderr, "generic queue: index %u (head=%u,count=%u) already null\n", index, g->g_head, g->g_count);
    abort();
  }
#endif

#if DEBUG > 1
  fprintf(stderr, "generic queue: del[%d]=%p\n", index, g->g_queue[index]);
#endif

  datum = g->g_queue[index];
  g->g_queue[index] = NULL;

  if(index == g->g_head){
    /* hopefully the common, simple case: only one interested party */
    g->g_head = (g->g_head + 1) % g->g_size;
    g->g_count--;

#if DEBUG > 1
    fprintf(stderr, "generic gueue: removed %p from %p\n", datum, g);
#endif

    return datum;
  }

  if((g->g_head + g->g_count) > g->g_size){ /* wrapping case */
    if(index >= g->g_head){ /* position before wrap around, move up head */
      if(index > g->g_head){
        memcpy(&(g->g_queue[g->g_head + 1]), &(g->g_queue[g->g_head]), (index - g->g_head) * sizeof(void *));
      }
      g->g_queue[g->g_head] = NULL;
      g->g_head = (g->g_head + 1) % g->g_size;
      g->g_count--;
#if DEBUG > 1
      fprintf(stderr, "generic queue: removed %p from %p (wrap)\n", datum, g);
#endif
      return datum; /* WARNING: done here */
    }
  } else { /* if no wrapping, we can not be before head */
    if(index < g->g_head){
      return NULL;
    }
  }

  /* now move back end by one, to overwrite position at index */
  end = g->g_head + g->g_count - 1; /* WARNING: relies on count+head never being zero, hence earlier test */
  if(index > end){
    return NULL;
  }

  if(index < end){
    memcpy(&(g->g_queue[index]), &(g->g_queue[index + 1]), (end - index) * sizeof(void *));
  } /* else index is end, no copy needed  */

  fprintf(stderr, "generic queue: remove[%d]=%p\n", index, g->g_queue[index]);

  g->g_queue[end] = NULL;
  g->g_count--;

#if DEBUG > 1
  fprintf(stderr, "generic queue: removed %p from %p\n", datum, g);
#endif

  return datum;
}

void *remove_head_gueue_katcl(struct katcl_gueue *g)
{
  return remove_index_gueue_katcl(g, g->g_head);
}

/**************************************************************************************/

unsigned int size_gueue_katcl(struct katcl_gueue *g)
{
  return g ? g->g_count : 0;
}

/**************************************************************************************/

#if defined(DEBUG) || defined(UNIT_TEST_GENERIC_QUEUE)
void dump_gueue(struct katcl_gueue *g, FILE *fp)
{
  unsigned int i, k;

  fprintf(fp, "generic queue %p (%d):", g, g->g_size);
  for(i = 0; i < g->g_size; i++){
    if(g->g_queue[i]){
      fprintf(fp, " <%p>", g->g_queue[i]);
    } else {
      fprintf(fp, " [%d]", i);
    }
  }
  fprintf(fp, "\n");

  for(k = g->g_head, i = 0; i < g->g_count; i++, k = (k + 1) % g->g_size){
    if(g->g_queue[k] == NULL){
      fprintf(stderr, "generic gueue: error: null field at %d\n", k);
    }
  }

  while(i < g->g_size){

    if(g->g_queue[k] != NULL){
      fprintf(stderr, "generic gueue: error: used field at %d\n", k);
    }

    k = (k + 1) % g->g_size;
    i++;
  }
}

#endif

#ifdef UNIT_TEST_GENERIC_QUEUE

#include <unistd.h>

#define FUDGE  100
#define RUNS  2000

int main()
{
  struct katcl_gueue *g;
  unsigned int *a, *b;
  int i, k, r;

  g = create_gueue_katcl(free);
  if(g == NULL){
    fprintf(stderr, "unable to create parse gueue\n");
    return 1;
  }

  for(i = 0; i < 200; i++){
    r = rand() % FUDGE;
    if(r == 0){
      for(k = 0; k < FUDGE; k++){
        b = remove_head_gueue_katcl(g);
        if(b){
          free(b);
        }
      }
    } else if((r % 3) == 0){
      b = remove_head_gueue_katcl(g);
      if(b){
        free(b);
      }
    } else {
      a = malloc(sizeof(unsigned int));
      if(a == NULL){
        return 1;
      }
      *a = i;
      add_tail_gueue_katcl(g, a);
    }
    dump_gueue(g, stderr);
  }

  destroy_gueue_katcl(g);

  return 0;
}
#endif
