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

  void (*g_release)(void *datum);  /* the cleanup function */

  /* this may actually one day be a proper priority queue, here just the 
   * feeble attempts: g_precedence is a user function which computes a metric
   * of the stored item, can be used to retrieve an which is at the head of the
   * queue but also has a precendence of greater or equal the given value */

  unsigned int (*g_precedence)(void *datum); 
};

struct katcl_gueue *create_precedence_gueue_katcl(void (*release)(void *datum), unsigned int (*precedence)(void *datum))
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
  g->g_precedence = precedence;

  return g;
}

struct katcl_gueue *create_gueue_katcl(void (*release)(void *datum))
{
  return create_precedence_gueue_katcl(release, NULL);
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

void *get_from_head_gueue_katcl(struct katcl_gueue *g, unsigned int position)
{
  unsigned int wrap;

  if((g->g_count == 0) || (g->g_size == 0) || (g->g_count > g->g_size)){
    return NULL;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(g->g_size < g->g_count){
    fprintf(stderr, "generic queue: more elements %u than slots %u\n", g->g_count, g->g_size);
    abort();
  }
#endif

  if(position < g->g_count){
    wrap = ((g->g_head + position) % g->g_size);
    return g->g_queue[wrap];
  } else {
    return NULL;
  }
}

void *get_head_gueue_katcl(struct katcl_gueue *g)
{
  return (g->g_count > 0) ? g->g_queue[g->g_head] : NULL;
}

void *get_precedence_head_gueue_katcl(struct katcl_gueue *g, unsigned int precedence)
{
  unsigned int value, i, j;

  if(g->g_precedence == NULL){
#ifdef DEBUG
    fprintf(stderr, "generic queue: get request with no precedence calculation\n");
#endif
    return get_head_gueue_katcl(g);
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if((g->g_count == 0) || (g->g_size == 0) || (g->g_count > g->g_size)){
#ifdef DEBUG
    fprintf(stderr, "generic queue: reasonability test on get failed (count=%u, size=%u)\n", g->g_count, g->g_size);
#endif
    return NULL;
  }
#endif

  for(j = 0; j < g->g_count; j++){
    i = (g->g_head + j) % g->g_size;
    value =  (*(g->g_precedence))(g->g_queue[i]);
#ifdef DEBUG
    fprintf(stderr, "generic queue: %p->%u, looking for at least %u\n", g->g_queue[i], value, precedence);
#endif
    if(value >= precedence){
      return g->g_queue[i];
    }
  }

#ifdef DEBUG
  fprintf(stderr, "generic queue: no match found, need precedence %u, searched %u\n", precedence, g->g_count);
#endif

  return NULL;
}

/*************************************************************************/

static void *remove_index_gueue_katcl(struct katcl_gueue *g, unsigned int index)
{
  unsigned int end;
  void *datum;

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

#if DEBUG
  fprintf(stderr, "generic queue: del[%u]=%p\n", index, g->g_queue[index]);
#endif

  datum = g->g_queue[index];
  g->g_queue[index] = NULL;

  if(index == g->g_head){
    /* hopefully the common, simple case - remove from head */
    g->g_head = (g->g_head + 1) % g->g_size;
    g->g_count--;

#if DEBUG 
    fprintf(stderr, "generic gueue: removed %p from %p\n", datum, g);
#endif

    return datum;
  }

  if(index > g->g_head){ /* index ahead of head, move head toward index */
    memmove(&(g->g_queue[g->g_head + 1]), &(g->g_queue[g->g_head]), (index - g->g_head) * sizeof(void *));
    g->g_queue[g->g_head] = NULL;
    g->g_head = (g->g_head + 1) % g->g_size;
    g->g_count--;
#if DEBUG 
    fprintf(stderr, "generic queue: removed %p from %p, head advanced to %u\n", datum, g, g->g_head);
#endif
    return datum; /* WARNING: done here */
  }

  /* else index < g->g_head, move tail back */

  /* now move back end by one, to overwrite position at index */
  end = (g->g_head + g->g_count - 1) % g->g_size; /* WARNING: relies on count+head never being zero, hence earlier test */
#ifdef KATCP_CONSISTENCY_CHECKS
  if(index > end){
    fprintf(stderr, "generic queue: logic problem: attempting to remove %u, queue only valid from head=%u to head+count=%u+%u-1=%u", index, g->g_head, g->g_head, g->g_count, end);
    abort();
  }
#endif

  if(index < end){
    memmove(&(g->g_queue[index]), &(g->g_queue[index + 1]), (end - index) * sizeof(void *));
  } /* else index is end, no copy needed  */

  fprintf(stderr, "generic queue: removed %p from %p, tail reduced from %u\n", datum, g, end);

  g->g_queue[end] = NULL;
  g->g_count--;

#if DEBUG > 1
  fprintf(stderr, "generic queue: removed %p from %p\n", datum, g);
#endif

  return datum;
}

void *remove_from_head_gueue_katcl(struct katcl_gueue *g, unsigned int position)
{
  unsigned int index;

#ifdef KATCP_CONSISTENCY_CHECKS
  if((g->g_count == 0) || (g->g_size == 0) || (g->g_count > g->g_size)){
    return NULL;
  }
#endif

#ifdef KATCP_CONSISTENCY_CHECKS
  if(g->g_size < g->g_count){
    fprintf(stderr, "generic queue: more elements %u than slots %u\n", g->g_count, g->g_size);
    abort();
  }
#endif

  if(position >= g->g_count){
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "generic queue: removing position %u from head (used=%u/size=%u)\n", position, g->g_count, g->g_size);
#endif

  index = (g->g_head + position) % g->g_size;

  return remove_index_gueue_katcl(g, index);
}

void *remove_datum_gueue_katcl(struct katcl_gueue *g, void *datum)
{
  unsigned int i, j;

#ifdef KATCP_CONSISTENCY_CHECKS
  if((g->g_count == 0) || (g->g_size == 0) || (g->g_count > g->g_size)){
    return NULL;
  }
#endif

  for(j = 0; j < g->g_count; j++){
    i = (g->g_head + j) % g->g_size;
    if(g->g_queue[i] == datum){
      break;
    }
  }

  if(j >= g->g_count){
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "generic queue: removing position %u from head (used=%u/size=%u)\n", i, g->g_count, g->g_size);
#endif

  return remove_index_gueue_katcl(g, i);
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
      abort();
    }
  }

  while(i < g->g_size){

    if(g->g_queue[k] != NULL){
      fprintf(stderr, "generic gueue: error: used field at %d\n", k);
      abort();
    }

    k = (k + 1) % g->g_size;
    i++;
  }
}

#endif

#ifdef UNIT_TEST_GENERIC_QUEUE

#include <unistd.h>

#define RUNS  2000

#define FROM_HEAD_RANGE       4
#define REMOVE_BATCH        100
/* these have to be primes */
#define CHANCE_HEAD_REMOVE    3
#define CHANCE_POS_REMOVE     7

int main(int argc, char **argv)
{
  struct katcl_gueue *g;
  unsigned int *a, *b;
  unsigned int insert, remove, margin, arb, distance, seed;
  int i, k, r;

  g = create_gueue_katcl(free);
  if(g == NULL){
    fprintf(stderr, "unable to create parse gueue\n");
    return 1;
  }

  if(argc > 1){
    seed = atoi(argv[1]);
    fprintf(stderr, "using seed %u\n", seed);
    srand(seed);
  }

  insert = 0;
  remove = 0;

  for(i = 0; i < RUNS; i++){
    r = rand() % REMOVE_BATCH;
    if(r == 0){
      for(k = 0; k < REMOVE_BATCH; k++){
        b = remove_head_gueue_katcl(g);
        if(b){
          if(distance > 0){
            distance--;
          } else {
            margin = 0;
          }
          if((*b > remove) || ((*b + margin) < remove)){
            fprintf(stderr, "test: implementation problem: expected to remove %u-%u, removed %u\n", remove - margin, remove, *b);
            abort();
          }
          remove++;
          free(b);
        } else {
          if(remove != insert){
            fprintf(stderr, "test: implementation problem: nothing to remove, yet remove is %u, not %u\n", remove, insert);
            abort();
          }
        }
      }
    } else if((r % CHANCE_HEAD_REMOVE) == 0){
      b = remove_head_gueue_katcl(g);
      if(b){
        if(distance > 0){
          distance--;
        } else {
          margin = 0;
        }
        if((*b > remove) || ((*b + margin) < remove)){
          fprintf(stderr, "test: implementation problem: expected to remove %u-%u, removed %u\n", remove - margin, remove, *b);
          abort();
        }
        remove++;
        free(b);
      }
    } else if((r % CHANCE_POS_REMOVE) == 0){
      arb = rand() % FROM_HEAD_RANGE;
      
      if(arb > distance){
        distance = arb;
      }

      b = remove_from_head_gueue_katcl(g, arb);
      if(b){
        if((*b > (remove + arb)) || (*b + margin < (remove + arb))){
          fprintf(stderr, "test: implementation problem: expected to remove %u-%u, removed %u\n", remove + arb - margin, remove + arb, *b);
          abort();
        }
        remove++;
        margin++;
        free(b);
      }
    } else {
      a = malloc(sizeof(unsigned int));
      if(a == NULL){
        return 1;
      }
      *a = insert;
      insert++;
      add_tail_gueue_katcl(g, a);
    }
    dump_gueue(g, stderr);
  }

  destroy_gueue_katcl(g);
  
  fprintf(stderr, "test: done\n");

  return 0;
}
#endif
