#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <stdlib.h>

#include "katcp.h"

#define SPOINTER_MAGIC 0x1432fa42

struct katcp_owner
{
  int (*o_exise)(struct katcp_dispatch *d, void *owner, void *pointee);
  void *o_owner;
};

struct katcp_spointer
{
  unsigned int s_magic;
  unsigned int s_count;
  struct katcp_owner *s_vector;
#if 0
  void **s_vector;
  int (*s_exise)(struct katcp_dispatch *d, void *owner, void *pointee);
#endif
};

/*************************************************************************************/

#ifdef KATCP_CONSISTENCY_CHECKS
static void sane_spointer_katcp(struct katcp_spointer *sp)
{
  if(sp == NULL){
    fprintf(stderr, "spointer: null structure\n");
    abort();
  }

  if(sp->s_magic != SPOINTER_MAGIC){
    fprintf(stderr, "spointer %p: bad magic 0x%0x, expected 0x%0x\n", sp, sp->s_magic, SPOINTER_MAGIC);
    abort();
  }

}
#else
#define sane_spointer_katcp(sp)
#endif

/* simple api ************************************************************************/

/* owner points to pointee with its own pointer
 * pointee contains a spointer structure (setup using create_spointer)
 * exise function removes pointee from its owner (triggered when pointee calls release_spointer on its own destruction) 
 * owner can tell pointee to forget about it by calling forget_spointer, won't trigger exise 
 */

struct katcp_spointer *create_spointer_katcp(struct katcp_dispatch *d)
{
  struct katcp_spointer *sp;

  sp = malloc(sizeof(struct katcp_spointer));
  if(sp == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate pointer handler");
    return NULL;
  }

  sp->s_magic = SPOINTER_MAGIC;
  sp->s_count = 0;
  sp->s_vector = NULL;

  return sp;
}

int pointat_spointer_katcp(struct katcp_dispatch *d, struct katcp_spointer *sp, void *owner, int (*exise)(struct katcp_dispatch *d, void *owner, void *pointee))
{
  struct katcp_owner *ko, *tmp;

  sane_spointer_katcp(sp);

  tmp = realloc(sp->s_vector, sizeof(struct katcp_owner) * (sp->s_count + 1));
  if(tmp == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "%p can not hold pointer", owner);
    return -1;
  }

  sp->s_vector = tmp;

  ko = &(sp->s_vector[sp->s_count]);

  ko->o_owner = owner;
  ko->o_exise = exise;

  sp->s_count++;

  return 0;
}

int forget_spointer_katcp(struct katcp_dispatch *d, struct katcp_spointer *sp, void *owner)
{
  unsigned int i;

  sane_spointer_katcp(sp);

#ifdef KATCP_CONSISTENCY_CHECKS
  if(owner == NULL){
    fprintf(stderr, "spointer %p: need a non-null owner", sp);
    abort();
  }
#endif

  /* TODO: if this is used in large applications, use bsearch, etc */
  for(i = 0; (i < sp->s_count) && (sp->s_vector[i].o_owner != owner); i++);

  if(i >= sp->s_count){
    fprintf(stderr, "spointer %p: did not find owner %p in vector of %u elements", sp, owner, sp->s_count);
    abort();
    return -1;
  }

  sp->s_count--;

  /* WARNING: note that the exise function is *NOT* run */

  if(sp->s_count > 0){
    while(i < sp->s_count){
      sp->s_vector[i].o_owner = sp->s_vector[i + 1].o_owner;
      sp->s_vector[i].o_exise = sp->s_vector[i + 1].o_exise;
      i++;
    }
  } else {
    free(sp->s_vector);
    sp->s_vector = NULL;
  }

  return sp->s_count;
}

void release_spointer_katcp(struct katcp_dispatch *d, struct katcp_spointer *sp, void *pointee)
{
  unsigned int i;
  struct katcp_owner *ko;

  sane_spointer_katcp(sp);

  for(i = 0; i < sp->s_count; i++){
    ko = &(sp->s_vector[i]);
    (*(ko->o_exise))(d, ko->o_owner, pointee);
    /* TODO: maybe do something with the return code ? */
  }

  if(sp->s_vector){
    free(sp->s_vector);
    sp->s_vector = NULL;
  }

  sp->s_count = 0;
  sp->s_magic = ~SPOINTER_MAGIC;

  free(sp);
}

/*************************************************************************************/

#endif
