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

#if 0
static struct katcl_parse *get_head_katcl(struct katcl_queue *q)
{
  return (q->q_count ? q->q_queue[q->q_head] : NULL);
}

static struct katcl_parse *get_tail_katcl(struct katcl_queue *q)
{
  unsigned int tail;

  /* Check for error conditions */
  if((q->q_count == 0) || (q->q_size == 0) || (q->q_count >= q->q_size)){
    return NULL;
  }
  tail = ((q->q_head + q->q_count) % q->q_size);
  /* return ptr to parse structure else NULL */

  /* return matching parse */
  return q->q_queue[tail];
}
#endif


/**************************************************************************/

#if 0
unsigned int is_empty_queue_katcl(struct katcl_queue *q)
{
  if(q->q_count <= 0){
    return 1;
  }

  return 0;
}
#endif

struct katcl_queue *create_queue_katcl(void)
{
  struct katcl_queue *q;

  q = malloc(sizeof(struct katcl_queue));
  if(q == NULL){
    fprintf(stderr, "unable to create parse\n");
    return NULL;
  }
  q->q_queue = NULL;
  q->q_head = 0;
  q->q_count = 0;
  q->q_size = 0;

  return q;
}

void destroy_queue_katcl(struct katcl_queue *q)
{
  unsigned int i, j;

  for(j = 0; j < q->q_count; j++){
    i = (q->q_head + j) % q->q_size;
    destroy_parse_katcl(q->q_queue[i]);
    q->q_queue[i] = NULL;
  }

  if(q->q_queue){
    free(q->q_queue);
    q->q_queue = NULL;
  }

  q->q_count = 0;
  q->q_size = 0;
  q->q_head = 0;

  free(q);
}

void clear_queue_katcl(struct katcl_queue *q)
{
  unsigned int i, j;

  /* WARNING: clear queue *does* remove reference count of parse structures */

#ifdef KATCP_CONSISTENCY_CHECKS
  if(q == NULL){
    fprintf(stderr, "queue: given null queue to clear\n");
    abort();
  }
#endif

  for(j = 0; j < q->q_count; j++){
    i = (q->q_head + j) % q->q_size;
    destroy_parse_katcl(q->q_queue[i]);
    q->q_queue[i] = NULL;
  }

  q->q_count = 0;
}

/* manage the parse queue logic *************************************************/

int add_tail_queue_katcl(struct katcl_queue *q, struct katcl_parse *p)
{
  struct katcl_parse **tmp;
  unsigned int index;

  /* WARNING: adding to the queue adds to parse reference count */

  if(p == NULL){
    return -1;
  }

#if DEBUG > 1
  fprintf(stderr, "add queue: adding %p with ref %u to queue %p of size %d\n", p, p->p_refs, q, q->q_count);
#endif

  if(q->q_count >= q->q_size){

#if DEBUG > 1
    fprintf(stderr, "size=%d, count=%d - increasing parse queue\n", q->q_size, q->q_count);
#endif
#ifdef KATCP_CONSISTENCY_CHECKS
    if(q->q_size < q->q_count){
      fprintf(stderr, "add: warning: detected rapid size increase of parse queue, expect corruption\n");
      abort();
    }
#endif
    tmp = realloc(q->q_queue, sizeof(struct katcl_parse *) * (q->q_size + 1));

    if(tmp == NULL){
      return -1;
    }
    q->q_queue = tmp;

    if(q->q_head > 0){
      q->q_queue[q->q_size] = q->q_queue[0];
      if(q->q_head > 1){
        memmove(&(q->q_queue[0]), &(q->q_queue[1]), sizeof(struct katcl_parse *) * (q->q_head - 1));
      }
      q->q_queue[q->q_head - 1] = NULL;
    }

    q->q_size = q->q_size + 1;
  } 
  index = (q->q_head + q->q_count) % q->q_size;

#if DEBUG > 1
  fprintf(stderr, "queue %p add[%d]=%p\n", q, index, p);
#endif

  q->q_queue[index] = copy_parse_katcl(p);
  q->q_count++;

  return 0;
}

/*************************************************************************/

struct katcl_parse *get_index_queue_katcl(struct katcl_queue *q, unsigned int index)
{
  unsigned int wrap;

  if((q->q_count == 0) || (q->q_size == 0) || (q->q_count >= q->q_size)){
    return NULL;
  }

  if((q->q_count > index) && (index < q->q_size)){
    wrap = ((q->q_head + index) % q->q_size);
    return q->q_queue[wrap];
  } else {
    return NULL;
  }
}

struct katcl_parse *get_head_queue_katcl(struct katcl_queue *q)
{
  return (q->q_count > 0) ? q->q_queue[q->q_head] : NULL;
}

/*************************************************************************/

struct katcl_parse *remove_index_queue_katcl(struct katcl_queue *q, unsigned int index)
{
  unsigned int end;
  struct katcl_parse *p;

  /* WARNING: removing from queue does not decrease reference count */

  if(q->q_count <= 0){
#ifdef DEBUG
    fprintf(stderr, "parse remove: nothing to remove\n");
#endif
    return NULL;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(index >= q->q_size){
    fprintf(stderr, "index %u out of range %u\n", index, q->q_size);
    abort();
  }
  if(q->q_queue[index] == NULL){
    fprintf(stderr, "index %u (head=%u,count=%u) already null\n", index, q->q_head, q->q_count);
    abort();
  }
#endif

#if DEBUG > 1
  fprintf(stderr, "queue del[%d]=%p\n", index, q->q_queue[index]);
#endif

  p = q->q_queue[index];
  q->q_queue[index] = NULL;

  if(index == q->q_head){
    /* hopefully the common, simple case: only one interested party */
    q->q_head = (q->q_head + 1) % q->q_size;
    q->q_count--;

#if DEBUG > 1
    fprintf(stderr, "remove queue: releasing %p with ref %u\n", p, p->p_refs);
#endif

    return p;
  }

  if((q->q_head + q->q_count) > q->q_size){ /* wrapping case */
    if(index >= q->q_head){ /* position before wrap around, move up head */
      if(index > q->q_head){
        memmove(&(q->q_queue[q->q_head + 1]), &(q->q_queue[q->q_head]), (index - q->q_head) * sizeof(struct katcl_parse *));
      }
      q->q_queue[q->q_head] = NULL;
      q->q_head = (q->q_head + 1) % q->q_size;
      q->q_count--;
#if DEBUG > 1
      fprintf(stderr, "remove queue: releasing %p with ref %u\n", p, p->p_refs);
#endif
      return p; /* WARNING: done here */
    }
  } else { /* if no wrapping, we can not be before head */
    if(index < q->q_head){
      return NULL;
    }
  }

  /* now move back end by one, to overwrite position at index */
  end = q->q_head + q->q_count - 1; /* WARNING: relies on count+head never being zero, hence earlier test */
  if(index > end){
    return NULL;
  }
  if(index < end){
    memmove(&(q->q_queue[index]), &(q->q_queue[index + 1]), (end - index) * sizeof(struct katcl_parse *));
  } /* else index is end, no copy needed  */

  fprintf(stderr, "queue remove[%d]=%p\n", index, q->q_queue[index]);

  q->q_queue[end] = NULL;
  q->q_count--;

#if DEBUG > 1
  fprintf(stderr, "remove queue: releasing %p with ref %u\n", p, p->p_refs);
#endif

  return p;
}

struct katcl_parse *remove_head_queue_katcl(struct katcl_queue *q)
{
  return remove_index_queue_katcl(q, q->q_head);
}

/**************************************************************************************/

unsigned int size_queue_katcl(struct katcl_queue *q)
{
  return q ? q->q_count : 0;
}

/**************************************************************************************/

#if defined(DEBUG) || defined(UNIT_TEST_QUEUE)
void dump_queue_parse_katcp(struct katcl_queue *q, FILE *fp)
{
  unsigned int i, k;

  fprintf(fp, "queue %p (%d):", q, q->q_size);
  for(i = 0; i < q->q_size; i++){
    if(q->q_queue[i]){
      fprintf(fp, " <%p>", q->q_queue[i]);
    } else {
      fprintf(fp, " [%d]", i);
    }
  }
  fprintf(fp, "\n");

  for(k = q->q_head, i = 0; i < q->q_count; i++, k = (k + 1) % q->q_size){
    if(q->q_queue[k] == NULL){
      fprintf(stderr, "parse queue: error: null field at %d\n", k);
    }
  }

  while(i < q->q_size){

    if(q->q_queue[k] != NULL){
      fprintf(stderr, "parse queue: error: used field at %d\n", k);
    }

    k = (k + 1) % q->q_size;
    i++;
  }
}

#endif

#ifdef UNIT_TEST_QUEUE

#include <unistd.h>

#define FUDGE 10000

int main()
{
  struct katcl_queue *q;
  struct katcl_parse *p, *px;
  int i, k, r;

  q = create_queue_katcl();
  if(q == NULL){
    fprintf(stderr, "unable to create parse queue\n");
    return 1;
  }
#if 0
  d = startup_katcp();
  if(d == NULL){
    fprintf(stderr, "unable to create dispatch\n");
    return 1;
  }
  q = malloc(sizeof(struct katcl_queue));
  if(q == NULL){
    fprintf(stderr, "unable to create parse\n");
    return 1;
  }
  q->q_queue = NULL;
  q->q_head = 0;
  q->q_count = 0;
  q->q_size = 0;
#endif

  p = malloc(sizeof(struct katcl_parse));
  if(p == NULL){
    return 1;
  }
  p->p_buffer = NULL;
  p->p_size = 0;
  p->p_have = 0;
  p->p_used = 0;
  p->p_kept = 0;

  for(i = 0; i < 20; i++){
    r = rand() % FUDGE;
    if(r == 0){
      for(k = 0; k < FUDGE; k++){
        px = remove_head_queue_katcl(q);
        if(px){
          destroy_parse_katcl(px);
        }
      }
    } else if((r % 3) == 0){
      px = remove_head_queue_katcl(q);
      if(px){
        destroy_parse_katcl(px);
      }
    } else {
      add_tail_queue_katcl(q, p);
    }
    dump_queue_parse_katcp(q, stderr);
  }

  return 0;
}
#endif
