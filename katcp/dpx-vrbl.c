#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sysexits.h>

#include <sys/stat.h>

#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>
#include <avltree.h>

/* string logic *******************************************************************/

int init_string_vrbl_katcp(struct katcp_dispatch *d, struct katcp_vrbl *vx)
{
  if(vx == NULL){
    return -1;
  }

  vx->v_type = KATCP_VRT_STRING;
  vx->v_union.u_string = NULL;

  return 0;
}

void clear_string_vrbl_katcp(struct katcp_dispatch *d, struct katcp_vrbl *vx)
{
#ifdef KATCP_CONSISTENCY_CHECKS
  if(vx == NULL){
    fprintf(stderr, "append string: no variable given\n");
    abort();
  }
  if(vx->v_type != KATCP_VRT_STRING){
    fprintf(stderr, "append string: bad type %u\n", vx->v_type);
    abort();
  }
#endif

  if(vx->v_union.u_string){
    free(vx->v_union.u_string);
    vx->v_union.u_string = NULL;
  }
}

int scan_string_vrbl_katcp(struct katcp_dispatch *d, struct katcp_vrbl *vx, char *text)
{
  char *ptr;
  int len;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(vx->v_type != KATCP_VRT_STRING){
    fprintf(stderr, "scan string: bad type %u\n", vx->v_type);
    abort();
  }
#endif

  if(text == NULL){
    if(vx->v_union.u_string){
      free(vx->v_union.u_string);
      vx->v_union.u_string = NULL;
    }
    return 0;
  } else {
    len = strlen(text) + 1;
    ptr = realloc(vx->v_union.u_string, len);
    if(ptr == NULL){
      return -1;
    }

    memcpy(ptr, text, len);
    vx->v_union.u_string = ptr;
    return 0;
  }
}

int append_string_vrbl_katcp(struct katcp_dispatch *d, struct katcl_parse *px, int flags, struct katcp_vrbl *vx)
{
  char *ptr;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(vx == NULL){
    fprintf(stderr, "append string: no variable given\n");
    abort();
  }
  if(vx->v_type != KATCP_VRT_STRING){
    fprintf(stderr, "append string: bad type %u\n", vx->v_type);
    abort();
  }
#endif

  ptr = vx->v_union.u_string;

  return add_string_parse_katcl(px, flags, ptr);
}

/* variable region functions ******************************************************/

struct katcp_vrbl_type_ops{
  char *t_name;

  int (*t_init)(struct katcp_dispatch *d, struct katcp_vrbl *vx);
  void (*t_clear)(struct katcp_dispatch *d, struct katcp_vrbl *vx);
  int (*t_scan)(struct katcp_dispatch *d, struct katcp_vrbl *vx, char *text);
  int (*t_append)(struct katcp_dispatch *d, struct katcl_parse *px, int flags, struct katcp_vrbl *vx);
};

struct katcp_vrbl_type_ops ops_type_vrbl[] = {
  [KATCP_VRT_STRING] = 
  { "string",
    &init_string_vrbl_katcp,
    &clear_string_vrbl_katcp,
    &scan_string_vrbl_katcp,
    &append_string_vrbl_katcp
  }
};

/* generic functions using type ops ***********************************************/

struct katcp_vrbl *create_vrbl_katcp(struct katcp_dispatch *d, unsigned int type, unsigned int flags, int (*refresh)(struct katcp_dispatch *d, char *name, struct katcp_vrbl *vx), int (*change)(struct katcp_dispatch *d, char *name, struct katcp_vrbl *vx), void (*release)(struct katcp_dispatch *d, char *name, struct katcp_vrbl *vx))
{
  struct katcp_vrbl *vx;

  if(type >= KATCP_MAX_VRT){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "logic problem while creating a variable: unknown type %u\n", type);
    abort();
#else
    return NULL;
#endif
  }

  vx = malloc(sizeof(struct katcp_vrbl));
  if(vx == NULL){
    return NULL;
  }

  vx->v_status = 0; /* TODO - nominal ? unknown ? */
  vx->v_flags = flags;
  vx->v_type = KATCP_VRT_GONE; /* WARNING: type functions set this, only */

  vx->v_refresh = refresh;
  vx->v_change = change;
  vx->v_release = release;

  if((*(ops_type_vrbl[type].t_init))(d, vx) < 0){
    /* WARNING: failure here doesn't trigger any callbacks */
    free(vx);
    return NULL;
  }

  return vx;
}

void destroy_vrbl_katcp(struct katcp_dispatch *d, char *name, struct katcp_vrbl *vx)
{
  if(vx == NULL){
    return;
  }

  if(vx->v_type == KATCP_VRT_GONE){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "logic failure in variable destruction: variable %p already gone\n", vx);
    abort();
#else
    return;
#endif
  }

  vx->v_change = NULL;
  vx->v_refresh = NULL;

  if(vx->v_release){
    (*(vx->v_release))(d, name, vx);
    vx->v_release = NULL;
  }

  if(vx->v_type < KATCP_MAX_VRT){
    (*(ops_type_vrbl[vx->v_type].t_clear))(d, vx);
  }

  vx->v_type = KATCP_VRT_GONE;
  vx->v_status = 0;
  vx->v_flags = 0;

  free(vx);
}

int scan_vrbl_katcp(struct katcp_dispatch *d, struct katcp_vrbl *vx, char *text)
{
  int result;

  if(vx == NULL){
    return -1;
  }
  
  if((vx->v_type >= KATCP_MAX_VRT) || (vx->v_type < 0)){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "logic failure: invalid type %u while scanning variable %p\n", vx->v_type, vx);
    abort();
#else
    return -1;
#endif
  }

  if(vx->v_type < KATCP_MAX_VRT){
    result = (*(ops_type_vrbl[vx->v_type].t_scan))(d, vx, text);
  }

  if(vx->v_change){
    /* WARNING: unclear if this is supposed to happen this far down the API ? */
    (*(vx->v_change))(d, NULL, vx);
  }
  
  return result;
}

int append_vrbl_katcp(struct katcp_dispatch *d, struct katcl_parse *px, int flags, struct katcp_vrbl *vx)
{
  if(vx == NULL){
    return -1;
  }
  
  if((vx->v_type >= KATCP_MAX_VRT) || (vx->v_type < 0)){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "logic failure: invalid type %u while outputting variable %p\n", vx->v_type, vx);
    abort();
#else
    return -1;
#endif
  }

  return (*(ops_type_vrbl[vx->v_type].t_append))(d, px, flags, vx);
}

/* variable region functions ******************************************************/

struct katcp_vrbl *find_region_katcp(struct katcp_dispatch *d, struct katcp_region *rx, char *key)
{
  if(rx == NULL){
    return NULL;
  }

  return find_data_avltree(rx->r_tree, key);
}

int insert_region_katcp(struct katcp_dispatch *d, struct katcp_region *rx, struct katcp_vrbl *vx, char *key)
{
  if(rx == NULL){
    return -1;
  }

  if(store_named_node_avltree(rx->r_tree, key, vx) < 0){
    return -1;
  }

  return 0;
}

/* traverse the hierachy of variables regions ****************/

/* 
 * exact absolute
 * > var@root*                    (1)
 * > group*var@group*             (2)
 * > group*flat*var@flat*         (3)
 *
 * exact relative
 * > *var@curgrouponly*           (2)
 * > **var@curflatonly*           (3)
 *
 * search
 * > varinflatthengroupthenroot   (0)
 * > *varinflatthengroup          (1)
 */

struct katcp_vrbl *update_vrbl_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, char *name, struct katcp_vrbl *vo)
{
#define MAX_STAR 3
  struct katcp_shared *s;
  struct katcp_group *gx;
  struct katcp_flat *fy;
  struct katcp_vrbl *vx;
  struct katcp_region *vxs[MAX_STAR];
  int i, count, last;
  char *copy, *var;
  unsigned int v[MAX_STAR];

  copy = strdup(name);
  if(copy == NULL){
    return NULL;
  }

  s = d->d_shared;

  count = 0;
  for(i = 0; copy[i] != '\0'; i++){
    if(copy[i] == '*'){
      if(count >= MAX_STAR){
        free(copy);
        return NULL;
      }
      v[count] = i;
      copy[i] = '\0';
      count++;
    }
  }
  if(i <= 0){
    free(copy);
    return NULL;
  }

  last = i - 1;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(fx){
    if(fx->f_group){
      fprintf(stderr, "major logic problem: duplex instance %p (%s) not a member of any group\n", fx, fx->f_name);
      abort();
    }
  }
  if(s == NULL){
    fprintf(stderr, "major logic problem: no global state available\n");
    abort();
  }
#endif

  vxs[0] = NULL;
  vxs[1] = NULL;
  vxs[2] = NULL;

  var = NULL;

  switch(count){
    case 0 : /* search in current flat, then current group, then root */
      if(fx == NULL){
        free(copy);
        return NULL;
      }
      var = copy;
      vxs[0] = fx->f_region;
      vxs[1] = fx->f_group->g_region;
      vxs[2] = s->s_region;
      break;
    case 1 :
      if(v[0] == 0){ /* search in current flat, then current group */
        if(fx == NULL){
          free(copy);
          return NULL;
        }
        var = copy + 1;
        vxs[0] = fx->f_region;
        vxs[1] = fx->f_group->g_region;
      } else if(v[0] == last){ /* search in root */
        var = copy;
        vxs[0] = s->s_region;
      } else {
        free(copy);
        return NULL;
      }
      break;
    case 2 : 
      if(v[1] != last){
        free(copy);
        return NULL;
      }
      if(v[0] == 0){ /* search in current group */
        if(fx == NULL){
          free(copy);
          return NULL;
        }
        var = copy + 1;
        vxs[0] = fx->f_group->g_region;
      } else { /* search in named group */
        gx = find_group_katcp(d, copy);
        if(gx == NULL){
          free(copy);
          return NULL;
        }
        vxs[0] = gx->g_region;
        var = copy + v[1] + 1;
      }
      break;
    case 3 : 
      if(v[0] == 0){ /* search in current flat */
        if(v[1] != 1){
          free(copy);
          return NULL;
        }
        vxs[0] = fx->f_region;
        var = copy + 2;
      } else { /* search in named flat */

        fy = find_name_flat_katcp(d, copy + 1, copy + v[1] + 1);
        if(fy == NULL){
          free(copy);
          return NULL;
        }

        vxs[0] = fy->f_region;
        var = copy + v[2] + 1;
      }
      break;
    default :
      free(copy);
      return NULL;
  }

  if((var == NULL) || (var[0] == '\0')){
    free(copy);
    return NULL;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(vxs[0] == NULL){
    fprintf(stderr, "major logic problem: attempting to update variable without any target location\n");
    abort();
  }
#endif

  vx = NULL;
  i = 0;

  do{
    vx = find_region_katcp(d, vxs[i], var);
    i++;
  } while((i < MAX_STAR) && (vxs[i] != NULL) && (vx == NULL));

  if(vx == NULL){
    if(vo == NULL){
      free(copy);
      return NULL;
    }

    if(insert_region_katcp(d, vxs[0], vo, var) < 0){
      free(copy);
      return NULL;
    }

    free(copy);
    return vo;

  } else {

    if(vo){
      /* TODO: replace content of variable */
#if 1
      fprintf(stderr, "usage problem: no insitu update implemented yet\n");
      abort();
#endif
    }

    return vx;
  }

#undef MAX_STAR
}

/* setup logic ***********************************************/

void destroy_vrbl_for_region_katcp(void *global, char *key, void *payload)
{
  struct katcp_dispatch *d;
  struct katcp_vrbl *vx;

  d = global;
  vx = payload;

  destroy_vrbl_katcp(d, key, vx);
}

void destroy_region_katcp(struct katcp_dispatch *d, struct katcp_region *rx)
{
  if(rx == NULL){
    return;
  }

  if(rx->r_tree){
    destroy_complex_avltree(rx->r_tree, d, &destroy_vrbl_for_region_katcp);
    rx->r_tree = NULL;
  }

  free(rx);
}

struct katcp_region *create_region_katcp(struct katcp_dispatch *d)
{
  struct katcp_region *rx;

  rx = malloc(sizeof(struct katcp_region));
  if(rx == NULL){
    return NULL;
  }

  rx->r_tree = create_avltree();

  if(rx->r_tree == NULL){
    destroy_region_katcp(d, rx);
    return NULL;
  }

  return rx;
}

/******************************************************************************/

#if 0

int insert_string_region_katcp(struct katcp_dispatch *d, struct katcp_region *rx, char *key, unsigned int flags, char *value)
{
  struct katcp_vrbl *vx;

  vx = create_string_vrbl_katcp(d, flags, value);
  if(vx == NULL){
    return -1;
  }

  if(insert_region_katcp(d, rx, vx, key) < 0){
    destroy_vrbl_katcp(d, key, vx);
    return -1;
  }

  return 0;
}
#endif

#if 0
struct katcp_vrbl *create_string_vrbl_katcp(struct katcp_dispatch *d, unsigned int flags, char *value)
{
  struct katcp_vrbl *vx;
  char *ptr;

  if(value){
    ptr = strdup(value);
    if(ptr == NULL){
      return NULL;
    }
  } else {
    ptr = NULL;
  }

  vx = create_vrbl_katcp(d, flags, NULL, NULL, &release_string_vrbl_katcp);
  if(vx == NULL){
    if(ptr){
      free(ptr);
    }
    return NULL;
  }

  vx->v_union.u_string = ptr;
  vx->v_type = KATCP_VRT_STRING;

  return vx;
}
#endif

#endif
