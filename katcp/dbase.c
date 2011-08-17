/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include <sys/wait.h>
#include <sys/types.h>

#include <unistd.h>

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"
#include "netc.h"


void print_string_type_katcp(struct katcp_dispatch *d, void *data)
{
  char *o;
  o = data;
  if (o == NULL)
    return;
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "#string type:");
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, o);
}

void destroy_string_type_katcp(void *data)
{
  char *o;
  o = data;
  if (o != NULL){
    free(o);
  }
}

void *parse_string_type_katcp(struct katcp_dispatch *d, char **str)
{
  char *o;
  int i, len, start, size;

  len = 0;
  o   = NULL;

  for (i=0; str[i] != NULL; i++) {
    
    start = len;
    size  = strlen(str[i]); 
    len   += size;

#ifdef DEBUG
    fprintf(stderr,"statemachine: parse_string_type_katcp: start: %d size: %d len: %d str: %s\n", start, size, len, str[i]);
#endif

    o = realloc(o, sizeof(char *) * (len + 1));
    if (o == NULL)
      return NULL;
    
    strncpy(o + start, str[i], size); 
    o[len] = ' ';
    len++;
    
  }
  
  o[len-1] = '\0';

#ifdef DEBUG
  fprintf(stderr,"statemachine: parse_string_type_katcp: string: <%s> len:%d strlen:%d\n", o, len, (int)strlen(o));
#endif

  return o;
}

char *getkey_dbase_type_katcp(void *data)
{
  struct katcp_dbase *db;
  db = data;
  if (db == NULL)
    return NULL;
  return db->d_key;
}

void print_dbase_type_katcp(struct katcp_dispatch *d, void *data)
{
  struct katcp_dbase *db;
  
  db = data;
  if (db == NULL)
    return;

#ifdef DEBUG
  fprintf(stderr, "dbase: <%s> with stamp: %lu%03d\n", db->d_key, db->d_stamped.tv_sec, db->d_stamped.tv_usec / 1000);
#endif
  
  append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#db:");
  append_args_katcp(d, KATCP_FLAG_STRING,"<%s>", db->d_key);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "values:");
  print_stack_katcp(d, db->d_values);
}

void destroy_dbase_type_katcp(void *data)
{
  struct katcp_dbase *db;
  db = data;
  if (db != NULL){
    if (db->d_values)
      destroy_stack_katcp(db->d_values);
    if (db->d_key)
      free(db->d_key);
    free(db);
  }
}

void stamp_dbase_type_katcp(struct katcp_dbase *db)
{
  if (db == NULL)
    return;

  gettimeofday(&(db->d_stamped), NULL);
}

struct katcp_dbase *create_dbase_type_katcp(char *key, struct katcp_stack *values)
{
  struct katcp_dbase *db;
  
  if (key == NULL)
    return NULL;
  
  db = malloc(sizeof(struct katcp_dbase));
  
  db->d_key   = strdup(key);
  if (db->d_key == NULL){
    destroy_dbase_type_katcp(db);
    return NULL;
  }
  
  if (values == NULL){
    db->d_values = create_stack_katcp();
    if (db->d_values == NULL){
      destroy_dbase_type_katcp(db);
      return NULL;
    }
  } else {
    db->d_values = values;
  }
  
  stamp_dbase_type_katcp(db);
  
  return db;
}

void *parse_dbase_type_katcp(struct katcp_dispatch *d, char **str)
{
  struct katcp_dbase *db;
  struct katcp_type *stringtype;
  int i;
  void *data;
  
  if (str == NULL || str[0] == NULL || str[1] == NULL)
    return NULL;
  
  db = create_dbase_type_katcp(str[0], NULL);
  if (db == NULL)
    return NULL;

  stringtype = find_name_type_katcp(d, KATCP_TYPE_STRING);
  
  for (i=1; str[i] != NULL; i++){
    data = search_type_katcp(d, stringtype, str[i], strdup(str[i]));
    push_stack_katcp(db->d_values, data, stringtype);
  }
  
  stamp_dbase_type_katcp(db);

  return db;
}

int store_dbase_katcp(struct katcp_dispatch *d, char **params)
{
  struct katcp_dbase *db;
  
  if (params[0] == NULL && params[1] == NULL)
    return -1;

  db = parse_dbase_type_katcp(d, params);
  if (db == NULL)
    return -1;
  
  return store_data_type_katcp(d, KATCP_TYPE_DBASE, KATCP_DEP_BASE, params[0], db, &print_dbase_type_katcp, &destroy_dbase_type_katcp, NULL, NULL, &parse_dbase_type_katcp, &getkey_dbase_type_katcp);
}

int store_kv_dbase_katcp(struct katcp_dispatch *d, char *key, struct katcp_stack *values)
{
  struct katcp_dbase *db;
  
  if (key == NULL || values == NULL)
    return -1;

  db = create_dbase_type_katcp(key, values);
  if (db == NULL)
    return -1;

  return store_data_type_katcp(d, KATCP_TYPE_DBASE, KATCP_DEP_BASE, key, db, &print_dbase_type_katcp, &destroy_dbase_type_katcp, NULL, NULL, &parse_dbase_type_katcp, &getkey_dbase_type_katcp);
}

int replace_dbase_values_katcp(struct katcp_dispatch *d, struct katcp_dbase *db, char **values)
{
  struct katcp_type *stringtype;
  void *data;
  int i;

  if (db == NULL || values == NULL)
    return -1;
  
  if (empty_stack_katcp(db->d_values) < 0)
    return -1;

  stringtype = find_name_type_katcp(d, KATCP_TYPE_STRING);
  
  for (i=0; values[i] != NULL; i++){
    data = search_type_katcp(d, stringtype, values[i], strdup(values[i]));
    push_stack_katcp(db->d_values, data, stringtype);
  }
  
  stamp_dbase_type_katcp(db);
  
  return 0;
}

int set_dbase_katcp(struct katcp_dispatch *d, struct katcl_parse *p)
{
  struct katcp_dbase *db;
  int i, count;
  char *key, **params;
  
  key = get_string_parse_katcl(p, 1);
  if (key == NULL)
    return -1;

  count = get_count_parse_katcl(p);

  params = malloc(sizeof(char *) * (count));
  if (params == NULL)
    return -1;

  for (i=1; i < count; i++){
    params[i-1] = get_string_parse_katcl(p, i);
  }
  params[i-1] = NULL;

  db = search_named_type_katcp(d, KATCP_TYPE_DBASE, key, NULL);
  if (db != NULL) {
    
    if (replace_dbase_values_katcp(d, db, params + 1) < 0){
      if (params != NULL)
        free(params);
      return -1;
    }

  } else {
    
    if (store_dbase_katcp(d, params) < 0){
      if (params != NULL)
        free(params);
      return -1;
    }
    
  }
  
  if (params != NULL)
    free(params);
  
  return 0;
}

int set_dbase_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcl_parse *p;

  if (argc < 3){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "useage");
    return KATCP_RESULT_FAIL;
  }
  
  p = ready_katcp(d);
  if (p == NULL)
    return KATCP_RESULT_FAIL;

  if (set_dbase_katcp(d, p) < 0)
    return KATCP_RESULT_FAIL;
  
  return KATCP_RESULT_OK;
}

struct katcl_parse *get_dbase_katcp(struct katcp_dispatch *d, struct katcl_parse *p)
{
  struct katcl_parse *prx;
  struct katcp_dbase *db;
  char *key, *rtn;
  unsigned long indx;
  int i, err;

  key = get_string_parse_katcl(p, 1);
  if (key == NULL)
    return NULL;
  
  db = search_named_type_katcp(d, KATCP_TYPE_DBASE, key, NULL);
  if (db == NULL) 
    return NULL;
  
  prx = create_parse_katcl();
  
  err =  add_string_parse_katcl(prx, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "!get");
  err += add_string_parse_katcl(prx, KATCP_FLAG_STRING, "ok");
  
  if (err < 0){
    destroy_parse_katcl(prx);
    return NULL;
  } 

  indx = get_unsigned_long_parse_katcl(p, 2);
  if (indx > 0){
    
    if (sizeof_stack_katcp(db->d_values) >= indx){
      rtn = index_data_stack_katcp(db->d_values, indx - 1);
      err += add_string_parse_katcl(prx, KATCP_FLAG_STRING | KATCP_FLAG_LAST, rtn);
      if (err < 0){
        destroy_parse_katcl(prx);
        return NULL;
      } 
      
    } else{
      destroy_parse_katcl(prx);
      return NULL;
    }

  } else {
    
    indx = sizeof_stack_katcp(db->d_values);
    for (i=0; i<indx; i++){
      rtn = index_data_stack_katcp(db->d_values, i);
      err += add_string_parse_katcl(prx, KATCP_FLAG_STRING | ((i+1 == indx)?KATCP_FLAG_LAST:0x0), rtn);
    }
    if (err < 0){
      destroy_parse_katcl(prx);
      return NULL;
    } 
    
  }

  return prx;
}

int get_dbase_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcl_parse *ptx, *prx;

  if (argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "useage");
    return KATCP_RESULT_FAIL;
  }
  
  ptx = ready_katcp(d);
  if (ptx == NULL)
    return KATCP_RESULT_FAIL;

  prx = get_dbase_katcp(d, ptx);
  if (prx == NULL)
    return KATCP_RESULT_FAIL;
  
  if (append_parse_katcp(d, prx) < 0)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_OWN;
}

