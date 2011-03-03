#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "katcp.h"
#include "katpriv.h"

/* WARNING: still need to update the use count, otherwise notices are likely to disappear */

static struct katcp_trap *create_trap_katcp(char *name)
{
  struct katcp_trap *kt;

  kt = malloc(sizeof(struct katcp_trap));
  if(kt == NULL){
    return NULL;
  }

  kt->t_name = NULL;
  kt->t_notice = NULL;

  kt->t_name = strdup(name);
  if(kt->t_name == NULL){
    free(kt);
    return NULL;
  }

  return kt;
}

static void destroy_trap_katcp(struct katcp_dispatch *d, struct katcp_trap *kt)
{
  if(kt == NULL){
    return;
  }

  if(kt->t_name){
    free(kt->t_name);
    kt->t_name = NULL;
  }

  if(kt->t_notice){
    update_notice_katcp(d, kt->t_notice, NULL, 1, 1);
    kt->t_notice = NULL;
  }

  free(kt);
}

struct katcp_map *create_map_katcp()
{
  struct katcp_map *km;

  km = malloc(sizeof(struct katcp_map));
  if(km == NULL){
    return NULL;
  }

  km->m_size = 0;
  km->m_traps = NULL;

  return km;
}

int destroy_map_katcp(struct katcp_dispatch *d, struct katcp_map *km)
{
  unsigned int i;

  if(km == NULL){
    return -1;
  }

  for(i = 0; i < km->m_size; i++){
    destroy_trap_katcp(d, km->m_traps[i]);
  }

  if(km->m_traps){
    free(km->m_traps);
    km->m_traps = NULL;
  }

  free(km);

  return 0;
}

static int locate_map_katcp(struct katcp_map *km, char *name, int find)
{
  int b, t, m;
  struct katcp_trap *kt;
  int result;

#ifdef DEBUG
  fprintf(stderr, "map: looking for %s in map of %d elements\n", name, km->m_size);
#endif

  t = km->m_size;
  b = 0;

  while(b < t){
    m = (b + t) / 2;
    kt = km->m_traps[m];
    result = strcmp(name, kt->t_name);
#ifdef DEBUG
    fprintf(stderr, "locate map: %d in [%d,%d] returns %d\n", m, b, t, result);
#endif
    if(result == 0){
      return (find > 0) ? m : (-1);
    }
    if(result > 0){
      b = m + 1;
    } else {
      t = m;
    }
  }

  return (find > 0) ? (-1) : b;
}


struct katcp_trap *find_map_katcp(struct katcp_map *km, char *name)
{
  int result;

  result = locate_map_katcp(km, name, 1);
  if(result < 0){
    return NULL;
  }

  return km->m_traps[result];
}


int remove_map_katcp(struct katcp_dispatch *d, struct katcp_map *km, char *name)
{
  int result, i, m;

  result = locate_map_katcp(km, name, 1);

  if(result < 0){
    return -1;
  }

  destroy_trap_katcp(d, km->m_traps[result]);

  m = km->m_size - 1 ;
  i = result;
  while(i < m){
    km->m_traps[i] = km->m_traps[i + 1];
    i++;
  }

  km->m_traps[m] = NULL;
  km->m_size = m;

  return 0;
}

int add_map_katcp(struct katcp_dispatch *d, struct katcp_map *km, char *name, struct katcp_notice *n)
{
  struct katcp_trap *kt;
  struct katcp_trap **tmp;
  int index, j;

  index = locate_map_katcp(km, name, 0);
  if(index < 0){
#ifdef DEBUG
    fprintf(stderr, "item already exists\n");
#endif
    return 1;
  }

  tmp = realloc(km->m_traps, sizeof(struct katcp_trap *) * (km->m_size + 1));
  if(tmp == NULL){
    return -1;
  }

  kt = create_trap_katcp(name);
  if(kt == NULL){
    return -1;
  }

  kt->t_notice = n;
  if(n){
    hold_notice_katcp(d, n);
  }

  km->m_traps = tmp;

  for(j = km->m_size; j > index; j--){
    km->m_traps[j] = km->m_traps[j - 1];
  }

  km->m_traps[index] = kt;
  km->m_size++;

  return 0;
}

int log_map_katcp(struct katcp_dispatch *d, char *prefix, struct katcp_map *km)
{
  int i;
  struct katcp_trap *kt;

  for(i = 0; i < km->m_size; i++){
    kt = km->m_traps[i];
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "filter %s %s", prefix, kt->t_name);
  }

  return 0;
}

int dump_map_katcp(struct katcp_map *km, FILE *fp)
{
  int i;
  struct katcp_trap *kt;

  for(i = 0; i < km->m_size; i++){
    kt = km->m_traps[i];
    fprintf(fp, "[%d]@%p: <%s>\n", i, kt, kt->t_name);
  }

  return 0;
}

#ifdef UNIT_TEST_MAP

#define BUFFER 128

int main()
{
  struct katcp_map *km;
  struct katcp_trap *kt;
  char cmd[BUFFER];

  km = create_map_katcp();
  if(km == NULL){
    fprintf(stderr, "unable to create map\n");
    return 1;
  }

  while(fgets(cmd, BUFFER - 1, stdin)){
    cmd[BUFFER - 1] = '\0';
    switch(cmd[0]){
      case 'r' :
        if(remove_map_katcp(NULL, km, cmd + 1) < 0){
          fprintf(stderr, "unable to remove %s\n", cmd + 1);
        }
        break;
      case 'd' :
        dump_map_katcp(km, stderr);
        break;
      case 'a' : 
        if(add_map_katcp(NULL, km, cmd + 1, NULL) < 0){
          fprintf(stderr, "unable to add %s\n", cmd + 1);
        }
        break;
      case 'f' : 
        kt = find_map_katcp(km, cmd + 1);
        if(kt == NULL){
          fprintf(stderr, "unable to find %s\n", cmd + 1);
        } else {
          fprintf(stderr, "got %s at %p\n", cmd + 1, kt);
        }
        break;
    }
  }

  destroy_map_katcp(NULL, km);

  return 0;
}

#endif
