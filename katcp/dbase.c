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

#if 0
struct katcp_dict *create_dict_katcp(char *key)
{
  struct katcp_dict *dt;

  dt = malloc(sizeof(struct katcp_dict));
  if (dt == NULL)
    return NULL;  

  dt->d_key = strdup(key);
  if (dt->d_key == NULL)
    return NULL;

  dt->d_avl = create_avltree();
  if (dt->d_avl == NULL)
    return NULL;

  return dt;
}

void print_dict_type_katcp(struct katcp_dispatch *d, void *data)
{
  struct katcp_dict *dt;
  
  dt = data;
  if (dt == NULL)
    return;
  
  

  
}

void destroy_dict_type_katcp(void *data);
{
  struct katcp_dict *dt;
  
  dt = data;
  if (dt != NULL){
    if (dt->d_key != NULL) free(dt->d_key);
    destroy_avltree(dt->d_avl, NULL);
  }
}

void *parse_dict_type_katcp(struct katcp_dispatch *d, char **str);
{

}

int store_type_dict_katcp(struct katcp_disct *dt, struct katcp_type *to, char *key, void *data)
{

}

int store_named_type_dict_katcp(struct katcp_dict *dt, char *type, char *key, void *data)
{
  
} 
#endif

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
  fprintf(stderr, "dbase: <%s> with stamp: %lu%03ld\n", db->d_key, db->d_stamped.tv_sec, (long) db->d_stamped.tv_usec / 1000);
#endif
  
  append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#db:");
  append_args_katcp(d, KATCP_FLAG_STRING, "<%s>", db->d_key);
  append_string_katcp(d, KATCP_FLAG_STRING, "schema:");
  append_args_katcp(d, KATCP_FLAG_STRING, "<%s>", db->d_schema);
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
    if (db->d_schema)
      free(db->d_schema);
    free(db);
  }
}

void stamp_dbase_type_katcp(struct katcp_dbase *db)
{
  if (db == NULL)
    return;

  gettimeofday(&(db->d_stamped), NULL);
}

struct katcp_dbase *create_dbase_type_katcp(char *key, char *schema_key, struct katcp_stack *values)
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
  
  if (schema_key != NULL)
    db->d_schema = strdup(schema_key);
  else
    db->d_schema = NULL;

  stamp_dbase_type_katcp(db);
  
  return db;
}

void *parse_dbase_type_katcp(struct katcp_dispatch *d, char **str)
{
  struct katcp_dbase *db;
  struct katcl_parse *p; 
  int i, err;

  if (str == NULL || str[0] == NULL || str[1] == NULL)
    return NULL;
  
  p = create_parse_katcl();
  if (p == NULL)
    return NULL;

  err =  add_string_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "?set");
  
  for (i=0; str[i] != NULL; i++){
    err += add_string_parse_katcl(p, KATCP_FLAG_STRING | ((str[i+1] == NULL) ? KATCP_FLAG_LAST : 0x0), str[i]);
  }
  
  if (err < 0){
    destroy_parse_katcl(p);
    return NULL;
  } 

  if (set_dbase_katcp(d, p) < 0){
    destroy_parse_katcl(p);
    return NULL;
  }

  destroy_parse_katcl(p);

  return search_named_type_katcp(d, KATCP_TYPE_DBASE, str[0], NULL);
}

int get_value_count_dbase_katcp(struct katcp_dbase *db)
{
  return (db != NULL) ? sizeof_stack_katcp(db->d_values) : 0;
}

struct katcp_stack *get_value_stack_dbase_katcp(struct katcp_dbase *db)
{
  return (db != NULL) ? db->d_values : NULL;
}

#if 0
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
#endif

int store_kv_dbase_katcp(struct katcp_dispatch *d, char *key, char *schema, struct katcp_stack *values)
{
  struct katcp_dbase *db;
  
  if (key == NULL || values == NULL)
    return -1;

  db = create_dbase_type_katcp(key, schema, values);
  if (db == NULL)
    return -1;

  return store_data_type_katcp(d, KATCP_TYPE_DBASE, KATCP_DEP_BASE, key, db, &print_dbase_type_katcp, &destroy_dbase_type_katcp, NULL, NULL, &parse_dbase_type_katcp, &getkey_dbase_type_katcp);
}

int replace_dbase_values_katcp(struct katcp_dispatch *d, struct katcp_dbase *db, char *schema, struct katcp_stack *values)
{
  if (db == NULL || values == NULL)
    return -1;
  
  destroy_stack_katcp(db->d_values);
  if (db->d_schema)
    free(db->d_schema);

  if (schema != NULL)
    db->d_schema = strdup(schema);
  else
    db->d_schema = NULL;
  
  db->d_values = values;

  stamp_dbase_type_katcp(db);
  
  return 0;
}

int set_dbase_katcp(struct katcp_dispatch *d, struct katcl_parse *p)
{
#define STATE_PRE     0
#define STATE_SCHEMA  1
#define STATE_POST    2
#define SCHEMA_IDENT  "schema"
  struct katcp_dbase *db;
  struct katcp_stack *stack;
  struct katcp_type *stringtype;
  int i, count, state;
  char *key, *schema, *temp;
  void *data;
  
  key = get_string_parse_katcl(p, 1);
  if (key == NULL)
    return -1;

  stringtype = find_name_type_katcp(d, KATCP_TYPE_STRING);
  if (stringtype == NULL)
    return -1;

  stack = create_stack_katcp(); 
  count = get_count_parse_katcl(p);
  state = STATE_PRE;
  schema = NULL;

  for (i=2; i<count; i++){
    temp = get_string_parse_katcl(p, i);
  
    switch (state){
      case STATE_PRE:
        
        if (strcmp(temp, SCHEMA_IDENT) == 0){
          state = STATE_SCHEMA;
          break;
        }
    
      case STATE_POST:

        data = search_type_katcp(d, stringtype, temp, strdup(temp));
        if (data != NULL && push_stack_katcp(stack, data, stringtype) < 0){
          destroy_stack_katcp(stack);
          return -1;
        }
        break;

      case STATE_SCHEMA:
          
          schema = temp;
          state = STATE_POST;

        break;
    }
    
  }
  
  db = search_named_type_katcp(d, KATCP_TYPE_DBASE, key, NULL);
  if (db != NULL) {
    
    if (replace_dbase_values_katcp(d, db, schema, stack) < 0){
      destroy_stack_katcp(stack);
      return -1;
    }

  } else {

    if (store_kv_dbase_katcp(d, key, schema, stack) < 0){
      destroy_stack_katcp(stack);
      return -1;
    }

  }

  return 0;
#undef STATE_PRE
#undef STATE_SCHEMA
#undef STATE_POST
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
  err += add_string_parse_katcl(prx, KATCP_FLAG_STRING, KATCP_OK);
  
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

