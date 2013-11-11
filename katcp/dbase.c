/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include <sys/wait.h>
#include <sys/types.h>

#include <unistd.h>
#include <search.h>
#if 0
#include <ctype.h>
#endif

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"
#include "netc.h"
#include "avltree.h"


void print_string_type_katcp(struct katcp_dispatch *d, char *key, void *data)
{
  char *o;
  o = data;
  if (o == NULL)
    return;
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "#string:");
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

int update_dict_katcp(struct katcp_dict *dt, char *key, struct katcp_tobject *to)
{
  struct avl_node *n;

  n = find_name_node_avltree(dt->d_avl, key);
  if (n == NULL)
    return -1;

  destroy_tobject_katcp(n->n_data);

  n->n_data = to;

  return 0;
}

int add_dict_katcp(struct katcp_dict *dt, char *key, struct katcp_tobject *to)
{  
  if (dt == NULL)
    return -1;

  return store_named_node_avltree(dt->d_avl, key, to);
}

int add_type_dict_katcp(struct katcp_dict *dt, char *key, struct katcp_type *t, void *data)
{
  struct katcp_tobject *to;

  to = create_tobject_katcp(data, t, 0);
  if (to == NULL)
    return -1;
  
  if (add_dict_katcp(dt, key, to) < 0){
    if (update_dict_katcp(dt, key, to) < 0){
      destroy_tobject_katcp(to);
      return -1;
    }
  }

  return 0;
}

void *get_value_data_dict_katcp(struct katcp_dict *dt, char *key)
{
  struct katcp_tobject *to;
  
  if (dt == NULL)
    return NULL;
    
  to = find_data_avltree(dt->d_avl, key);
  if (to == NULL)
    return NULL;

  return to->o_data;
}

int add_named_dict_katcp(struct katcp_dispatch *d, struct katcp_dict *dt, char *key, char *type, void *data)
{
  struct katcp_type *t;

  t = find_name_type_katcp(d, type);
  if (t == NULL)
    return -1;

  return add_type_dict_katcp(dt, key, t, data);
}


void print_dict_type_katcp(struct katcp_dispatch *d, char *key, void *data)
{
  struct katcp_dict *dt;
  struct katcp_tobject *to;
  struct avl_node *n;

  dt = data;
  if (dt == NULL)
    return;
  
  append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#dict:");
  append_args_katcp(d, KATCP_FLAG_STRING, "<%s>", dt->d_key);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "item tree:");
  
  if (dt->d_avl == NULL)
    return;
  
#ifdef DEBUG
  fprintf(stderr, "dict: <%s>\n", dt->d_key);
#endif
  
  while ((n = walk_inorder_avltree(dt->d_avl->t_root)) != NULL){
    
    if (n != NULL){
      
      to = n->n_data;
      if (to != NULL && to->o_type != NULL && to->o_type->t_print != NULL){
        append_string_katcp(d, KATCP_FLAG_FIRST  | KATCP_FLAG_STRING, "#key:");
        append_args_katcp  (d, KATCP_FLAG_STRING | KATCP_FLAG_LAST , "%s", n->n_key);
      
        (*(to->o_type->t_print))(d, n->n_key, to->o_data);

      }
    }
  }

}

void destroy_dict_type_katcp(void *data)
{
  struct katcp_dict *dt;
  
  dt = data;
  if (dt != NULL){
    if (dt->d_key != NULL) 
      free(dt->d_key);
    destroy_avltree(dt->d_avl, &destroy_tobject_katcp);
    free(dt);
  }
}

#define TOKEN_START_DICT        '{'
#define TOKEN_END_DICT          '}'
#define TOKEN_START_VALUE       ':'
#define TOKEN_END_VALUE         ','

#define STATE_START             0
#define STATE_KEY               1
#define STATE_VALUE             2
#define STATE_END               3

void *parse_dict_type_katcp(struct katcp_dispatch *d, char **str)
{
  struct katcp_dict *dt;
  struct katcp_type *stringtype;
  int i, j, tlen, state, ipos, spos, len;
  char c, *key, *value;
  void *data;
  
  if (str == NULL || str[0] == NULL)
    return NULL;

  dt = NULL;

  state = STATE_START;
  c = TOKEN_START_DICT;
  ipos = 0;
  spos = 0;
  len = 0;
  key = NULL;
  value = NULL;

  dt = search_named_type_katcp(d, KATCP_TYPE_DICT, str[0], NULL);
  if (dt == NULL){
    dt = create_dict_katcp(str[0]);
  }

  if (dt == NULL)
    return NULL;

  stringtype = find_name_type_katcp(d, KATCP_TYPE_STRING);

#ifdef DEBUG
  fprintf(stderr, "dict: %s\n", str[0]);
#endif

  for (i=1; str[i] != NULL; i++){

    tlen = strlen(str[i]);

    for (j=0; j<tlen; j++){
      c = str[i][j];  
      switch (state){

        case STATE_START:
          switch (c){
            case TOKEN_START_DICT:
              state = STATE_KEY;
              spos = j+1;
              ipos = i;
              break;
          }
          break;

        case STATE_KEY:
          switch (c){
            case TOKEN_START_VALUE:
              if ((i - ipos) > 0 && j > 0){
                ipos++;
                spos = 0;
                len = j - spos;
              } else if ((i - ipos) == 2){
                ipos = i-1;
                spos = 0;
                len = strlen(str[ipos]);
              } else {
                len = j - spos;
              }
              key = strndup(str[ipos] + spos, len);
              if (key == NULL){
                destroy_dict_type_katcp(dt);
                if (value != NULL)
                  free(value);
                return NULL;
              }
#ifdef DEBUG            
              fprintf(stderr, "dict KEY: (%s)\n", key);
#endif
              
              state = STATE_VALUE;
              spos = j+1;
              ipos = i;
              break;
          }
          break;

        case STATE_VALUE:
          switch (c){
            case TOKEN_END_DICT:
              state = STATE_END;
            case TOKEN_END_VALUE:
              if ((i - ipos) > 0 && j > 0){
                ipos++;
                spos = 0;
                len = j - spos;
              } else if ((i - ipos) == 2){
                ipos = i-1;
                spos = 0;
                len = strlen(str[ipos]);
              } else {
                len = j - spos;
              }
              value = strndup(str[ipos] + spos, len);
              if (value == NULL){
                destroy_dict_type_katcp(dt);
                if (key != NULL)
                  free(key);
                return NULL;
              }
#ifdef DEBUG
              fprintf(stderr, "dict VALUE: (%s)\n", value);
#endif
               
              data = search_type_katcp(d, stringtype, value, value);
              if (data != NULL){
                if (add_type_dict_katcp(dt, key, stringtype, data) < 0){
#ifdef DEBUG
                  fprintf(stderr, "dict: add <%s> to dict <%s> failed\n", key, dt->d_key);
#endif
                }
              } else {
                if (value != NULL) { free(value); value = NULL; }
              }
              
              if (key != NULL) { free(key); key = NULL; }
              value = NULL;

              state = (state != TOKEN_END_DICT) ? STATE_KEY : state;
              spos = j+1;
              ipos = i;
              break;
          }
          break;

        case STATE_END:
          break;
      }
    }
  }

  return dt;
}

char **parse_to_data_vector_katcp(struct katcl_parse *p, int base)
{
  char **data;
  int i, count, len;
  
  if (p == NULL)
    return NULL;

  count = get_count_parse_katcl(p);
  
  len = count - base;
  
  data = malloc(sizeof(char *) * (len+1));
  if (data == NULL)
    return NULL;

  for (i=0; i<len; i++){
    data[i] = get_string_parse_katcl(p, base + i);
  }
  data[len] = NULL;

  return data;
}

int dict_katcp(struct katcp_dispatch *d, struct katcl_parse *p)
{
  struct katcp_dict *dt, *temp;
  char **data;
  
#if 0
  key = get_string_parse_katcl(p, 1);
  if (key == NULL)
    return -1;
#endif

  data = parse_to_data_vector_katcp(p, 1);
  if (data == NULL)
    return -1;

  dt = parse_dict_type_katcp(d, data);
  if (dt == NULL){
    free(data);
    return -1;
  }

  temp = search_named_type_katcp(d, KATCP_TYPE_DICT, data[0], dt);
  if (temp == NULL){
    destroy_dict_type_katcp(dt);
    return -1;
  }

  free(data);
 
  return 0;
} 

char *getkey_dbase_type_katcp(void *data)
{
  struct katcp_dbase *db;
  db = data;
  if (db == NULL)
    return NULL;
  return db->d_key;
}

void print_dbase_type_katcp(struct katcp_dispatch *d, char *key, void *data)
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
  struct katcl_parse *p; 
  int i, err;

  if (str == NULL || str[0] == NULL || str[1] == NULL)
    return NULL;
  
  p = create_parse_katcl();
  if (p == NULL)
    return NULL;

  err =  add_string_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, KATCP_SET_REQUEST);
  
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



int tag_dbase_katcp(struct katcp_dispatch *d, struct katcp_dbase *db, struct katcp_stack *tags)
{
  struct katcp_tag *t;
  struct katcp_type *tagtype, *dbtype;
  
  if (db == NULL )//|| tags == NULL)
    return -1;

  if (tags == NULL){
#ifdef DEBUG
    fprintf(stderr, "dbase: tags are null skipping\n");
#endif
    return 0;
  }

  tagtype = find_name_type_katcp(d, KATCP_TYPE_TAG);
  dbtype  = find_name_type_katcp(d, KATCP_TYPE_DBASE); 
  
  if (tagtype == NULL || dbtype == NULL)
    return -1;

  while ((t = pop_data_type_stack_katcp(tags, tagtype)) != NULL){
    
    if (tag_data_katcp(d, t, db, dbtype) < 0){
#if DEBUG > 1
      fprintf(stderr, "dbase: cannot tag db:<%s> with <%s>\n", db->d_key, t->t_name);
#endif
    }

  }

  return 0;
}

int store_kv_dbase_katcp(struct katcp_dispatch *d, char *key, char *schema, struct katcp_stack *values, struct katcp_stack *tags)
{
  struct katcp_dbase *db;
  
  if (key == NULL || values == NULL)
    return -1;

  db = create_dbase_type_katcp(key, schema, values);
  if (db == NULL)
    return -1;

  if (store_data_type_katcp(d, KATCP_TYPE_DBASE, KATCP_DEP_BASE, key, db, &print_dbase_type_katcp, &destroy_dbase_type_katcp, NULL, NULL, &parse_dbase_type_katcp, &getkey_dbase_type_katcp) < 0)
    return -1;

  return tag_dbase_katcp(d, db, tags);
}

int replace_dbase_values_katcp(struct katcp_dispatch *d, struct katcp_dbase *db, char *schema, struct katcp_stack *values, struct katcp_stack *tags)
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
  
  return tag_dbase_katcp(d, db, tags);
}

int set_dbase_katcp(struct katcp_dispatch *d, struct katcl_parse *p)
{
#define STATE_PRE     0
#define STATE_SCHEMA  1
#define STATE_POST    2
#define STATE_TAGS    3
#define SCHEMA_IDENT  "schema"
#define TAG_IDENT     "tags"
  struct katcp_dbase *db;
  struct katcp_stack *stack, *tags;
  struct katcp_type *stringtype, *tagtype;
  int i, count, state;
  char *key, *schema, *temp;
  void *data;
  
  key = get_string_parse_katcl(p, 1);
  if (key == NULL)
    return -1;

  stringtype = find_name_type_katcp(d, KATCP_TYPE_STRING);
  if (stringtype == NULL)
    return -1;

  tagtype = find_name_type_katcp(d, KATCP_TYPE_TAG);
  if (tagtype == NULL)
    return -1;

  stack  = create_stack_katcp(); 
  tags   = create_stack_katcp();
  count  = get_count_parse_katcl(p);
  state  = STATE_PRE;
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

        if (strcmp(temp, TAG_IDENT) == 0){
          state = STATE_TAGS;
          break;
        }
        
        data = search_type_katcp(d, stringtype, temp, strdup(temp));
        if (data != NULL && push_stack_katcp(stack, data, stringtype) < 0){
          destroy_stack_katcp(stack);
          destroy_stack_katcp(tags);
          return -1;
        }

        break;

      case STATE_SCHEMA:
          
          schema = temp;
          state = STATE_POST;

        break;

      case STATE_TAGS:
        
        data = search_type_katcp(d, tagtype, temp, create_tag_katcp(temp, 0));
        if (data != NULL && push_stack_katcp(tags, data, tagtype) < 0){
          destroy_stack_katcp(stack);
          destroy_stack_katcp(tags);
          return -1;
        }

        break;

    }
    
  }
  
  db = search_named_type_katcp(d, KATCP_TYPE_DBASE, key, NULL);
  if (db != NULL) {
    
    if (replace_dbase_values_katcp(d, db, schema, stack, tags) < 0){
      destroy_stack_katcp(stack);
      destroy_stack_katcp(tags);
      return -1;
    }

  } else {

    if (store_kv_dbase_katcp(d, key, schema, stack, tags) < 0){
      destroy_stack_katcp(stack);
      destroy_stack_katcp(tags);
      return -1;
    }

  }

  destroy_stack_katcp(tags);

  return 0;
#undef STATE_PRE
#undef STATE_SCHEMA
#undef STATE_POST
#undef STATE_TAGS
#undef SCHEMA_IDENT
#undef TAG_IDENT
}

struct katcl_parse *get_dbase_katcp(struct katcp_dispatch *d, struct katcl_parse *p)
{
  struct katcl_parse *prx;
  struct katcp_dbase *db;
  struct katcp_dict  *dt;
  char *key, *rtn, *skey;
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

    skey = get_string_parse_katcl(p, 2);

    if (skey == NULL || strcmp(skey, "0") == 0){
      indx = sizeof_stack_katcp(db->d_values);
      for (i=0; i<indx; i++){
        rtn = index_data_stack_katcp(db->d_values, i);
        err += add_string_parse_katcl(prx, KATCP_FLAG_STRING | ((i+1 == indx)?KATCP_FLAG_LAST:0x0), rtn);
      }
      if (err < 0){
        destroy_parse_katcl(prx);
        return NULL;
      }

    } else {
     
      dt = search_named_type_katcp(d, KATCP_TYPE_DICT, db->d_schema, NULL);
      if (dt == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "connot find schema %s", db->d_schema);
        destroy_parse_katcl(prx);
        return NULL;
      }

      rtn = get_value_data_dict_katcp(dt, skey);
      if (rtn == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "connot find schema %s item %s", db->d_schema, skey);
        destroy_parse_katcl(prx);
        return NULL;
      }
      indx = atol(rtn);
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
  
  ptx = arg_parse_katcp(d);
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
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "usage");
    return KATCP_RESULT_FAIL;
  }
  
  p = arg_parse_katcp(d);
  if (p == NULL)
    return KATCP_RESULT_FAIL;

  if (set_dbase_katcp(d, p) < 0)
    return KATCP_RESULT_FAIL;
  
  return KATCP_RESULT_OK;
}

int dict_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcl_parse *p;

  if (argc < 3){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "usage");
    return KATCP_RESULT_FAIL;
  }

  p = arg_parse_katcp(d);
  if (p == NULL)
    return KATCP_RESULT_FAIL;

  if (dict_katcp(d, p) < 0)
    return KATCP_RESULT_FAIL;

  return KATCP_RESULT_OK;
}


/***************************[tag]*******************************/

struct katcp_tag *create_tag_katcp(char *name, int level)
{
  struct katcp_tag *t;

  t = malloc(sizeof(struct katcp_tag));
  if (t == NULL)
    return NULL;

  t->t_name = strdup(name);
  if (t->t_name == NULL){
    free(t);
    return NULL;
  }

#if 0 
  for (i=0; t->t_name[i] != '\0'; i++){
    t->t_name[i] = tolower(t->t_name[i]);
  }
#endif
  
  t->t_level = level;
  t->t_tobject_root = NULL;
  t->t_tobject_count = 0;

#if 0
  t->t_memb = NULL;
  t->t_memb_count = 0;
#endif

  return t;
}

/*Unsafe*/
void destroy_tag_katcp(void *data)
{
  struct katcp_tag *t;
  t = data;
  if (t == NULL)
    return;
  
  if (t->t_name != NULL) free(t->t_name);
#if 0
  if (t->t_memb != NULL) free(t->t_memb);
#endif
  if (t->t_tobject_root != NULL) 
    tdestroy(t->t_tobject_root, &destroy_tobject_katcp);

  free(t);
}

int get_count_tag_katcp(struct katcp_tag *t)
{
  if (t == NULL)
    return 0;
  return t->t_tobject_count;
}

void print_tag_katcp(struct katcp_dispatch *d, char *key, void *data)
{
  struct katcp_tag *t;
  t = data;
  if (t == NULL)
    return;

  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "#tag:");
  append_string_katcp(d, KATCP_FLAG_STRING, t->t_name);
  append_args_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "%d", t->t_tobject_count);
  
#if 0 
  def DEBUG
  fprintf(stderr, "tag: %s has %d tobjects:\n", t->t_name, t->t_tobject_count);
  if (t->t_tobject_root)
    twalk(t->t_tobject_root, &walk_tag_tobjects);
  fprintf(stderr, "tag: end\n");
#endif
}

void *parse_tag_katcp(struct katcp_dispatch *d, char **str)
{
  struct katcp_tag *t;
  int level;

  level = 0;

  if (str == NULL || str[0] == NULL || str[1] == NULL)
    return NULL;

  level = atoi(str[1]);

  t = create_tag_katcp(str[0], level);
  
  return t;
}

char *getkey_tag_katcp(void *data)
{
  struct katcp_tag *t;
  t = data;
  if (t == NULL)
    return NULL;
  return t->t_name;
}


int compare_tag_katcp(const void *m1, const void *m2)
{
  const struct katcp_tag *a, *b;

  a = m1;
  b = m2;
  if (a == NULL || b == NULL)
    return 2;

  return strcmp(a->t_name, b->t_name);
}


int register_tag_katcp(struct katcp_dispatch *d, char *name, int level)
{
  struct katcp_tag *t;

  t = create_tag_katcp(name, level);
  if (t == NULL)
    return -1;

  if (store_data_type_katcp(d, KATCP_TYPE_TAG, KATCP_DEP_BASE, name, t, &print_tag_katcp, &destroy_tag_katcp, NULL, NULL, &parse_tag_katcp, &getkey_tag_katcp) < 0){
    destroy_tag_katcp(d);
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "tag: register tag <%s>\n", name);
#endif
  return 0;
}

int tag_tobject_katcp(struct katcp_dispatch *d, struct katcp_tag *t, struct katcp_tobject *to)
{
  void *val;

  val = tsearch((void *) to, &(t->t_tobject_root), &compare_tobject_katcp);
  if (val == NULL || (*(struct katcp_tobject **) val != to)){
#if 0
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tag: error with tsearch for tag:<%s>", t->t_name);
#ifdef DEBUG
    fprintf(stderr, "tag: error with tsearch for tag:<%s>\n", t->t_name);
#endif
#endif
    return -1;
  }

  t->t_tobject_count++;
  
  return 0;
}

int tag_named_tobject_katcp(struct katcp_dispatch *d, char *tag, struct katcp_tobject *to)
{
  struct katcp_tag *t;

  t = get_key_data_type_katcp(d, KATCP_TYPE_TAG, tag);
  if (t == NULL)
    return -1;

  return tag_tobject_katcp(d, t, to);
}

int tag_data_katcp(struct katcp_dispatch *d, struct katcp_tag *t, void *data, struct katcp_type *type)
{
  struct katcp_tobject * to;

  to = create_tobject_katcp(data, type, 0);
  if (to == NULL)
    return -1;

  if (tag_tobject_katcp(d, t, to) < 0){
    destroy_tobject_katcp(to);
    return -1;
  }
  
  return 0;
}

static struct katcp_tobject **__tobjs;
static int __tcount;

static void __collect_from_tag(const void *nodep, const VISIT which, const int depth)
{
  struct katcp_tobject *to;

  to = *(struct katcp_tobject **) nodep;
  if (to == NULL)
    return;

  switch (which){
    case leaf:
    case postorder:
      if (__tobjs == NULL)
        return;
      __tobjs[__tcount] = to;
      __tcount++;
      break;
    case preorder:
    case endorder:
      break;
  }
}

int search_katcp(struct katcp_dispatch *d, struct katcl_parse *p)
{
  struct katcp_stack *tags, *ans; 
  struct katcp_type *tagtype;
  char *tag;
  int i, j, count, *dsize, *di, run, seen;
  struct katcp_tobject ***data, *to;
  struct katcp_tag *t;
  struct timeval ts, te, delta;
  void *min;
  
  t = NULL;

  gettimeofday(&ts, NULL);
  
  count = get_count_parse_katcl(p);
  tagtype = find_name_type_katcp(d, KATCP_TYPE_TAG);

  if (tagtype == NULL)
    return -1;
  
  tags = create_stack_katcp();
  if (tags == NULL)
    return -1;

  for (i=1; i<count; i++){
   
    tag = get_string_parse_katcl(p, i);
    if (push_stack_katcp(tags, search_type_katcp(d, tagtype, tag, NULL), tagtype) < 0){
#ifdef DEBUG
      fprintf(stderr, "search: cannot find tag <%s>\n", tag);
#endif
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "search tag %s doesn't exist", tag);
    }

  }

#if 0
  print_stack_katcp(d, tags); 
#endif

  count = sizeof_stack_katcp(tags);

  if (count == 0){
    destroy_stack_katcp(tags);
    return -1;
  }

  ans = create_stack_katcp();

  dsize = malloc(sizeof(int) * count);
  if (dsize == NULL){
    destroy_stack_katcp(tags);
    destroy_stack_katcp(ans);
    return -1;
  }

  di = malloc(sizeof(int) * count);
  if (di == NULL){
    if (dsize)
      free(dsize);
    destroy_stack_katcp(tags);
    destroy_stack_katcp(ans);
    return -1;
  }

  data = malloc(sizeof(struct katcp_tobject **) * count);
  if (data == NULL){
    if (dsize)
      free(dsize);
    if (di)
      free(di);
    destroy_stack_katcp(tags);
    destroy_stack_katcp(ans);
    return -1;
  }

  for (i=0; i<count; i++){
    t = index_data_stack_katcp(tags, i);
    if (t){
      dsize[i] = get_count_tag_katcp(t);
      data[i] = malloc(sizeof(struct katcp_tobject *) * dsize[i]);
      if (data[i]){
        
        __tobjs   = data[i];  
        __tcount  = 0;
        
        if (t->t_tobject_root != NULL)
          twalk(t->t_tobject_root, &__collect_from_tag);
        
#if DEBUG >1
        fprintf(stderr,"i:%d t:(%p) [ ", i, t);
        for (j=0; j<dsize[i]; j++)
          fprintf(stderr,"(%p) ", (data[i][j])->o_data);
        fprintf(stderr,"]\n");
#endif

      }
    }
  }
  

  bzero(di, sizeof(int) * count);

  run  = 1;
  min  = NULL;
  seen = 0;
  to = NULL;

  while (run) {
    
    for(i=0; i<count; i++){
      
#if DEBUG>1
      fprintf(stderr, "i:%d ", i);
#endif

      if (di[i] < dsize[i] && data[i][di[i]] != NULL){
       
        if(min == NULL){
          t = index_data_stack_katcp(tags, i);
          min = (data[i][di[i]])->o_data;
          to = data[i][di[i]];
          seen = 1;
#if DEBUG >1
          fprintf(stderr, "NULL setting t:(%p) min:(%p) ", t, min);
#endif
        } else if (min == (data[i][di[i]])->o_data && t != index_data_stack_katcp(tags, i)) {
          seen++;
#if DEBUG >1
          fprintf(stderr, "seen: %d ", seen);
#endif
        } else if (min < (data[i][di[i]])->o_data) {
          t = index_data_stack_katcp(tags, i);
          min = (data[i][di[i]])->o_data;
          to = data[i][di[i]];
          seen = 1;
#if DEBUG >1
          fprintf(stderr, "NEW MIN setting t:(%p) min:(%p) ", t, min);
#endif
        } else if (min > (data[i][di[i]])->o_data){
          (di[i])++;
#if DEBUG >1
          fprintf(stderr, "di[%d] %d ", i, di[i]);
#endif
        } else if (t == index_data_stack_katcp(tags, i)){
#if DEBUG >1
          fprintf(stderr, "original ");
#endif
          if (seen >= count){
#if DEBUG >1
            fprintf(stderr, "FOUND match\n");
#endif
            if (push_tobject_katcp(ans, copy_tobject_katcp(to)) < 0){
#if DEBUG >1  
              fprintf(stderr, "search: error could not push an answer onto stack\n");
#endif
            }

            to  = NULL;
            min = NULL;
            seen = 0;
            (di[i])++;
#if DEBUG >1
            fprintf(stderr, "reset all and di[%d] %d ", i, di[i]);
#endif
          }

        }


      
      } else {
        run = 0;
#if DEBUG >1
        fprintf(stderr, "the end");
#endif
      }

#if DEBUG >1
      fprintf(stderr, "\n");
#endif

    }
#if 0
    if (seen >= count){
      run = 0;

    }
#endif
  }
  
  gettimeofday(&te, NULL);

  sub_time_katcp(&delta, &te, &ts); 

  print_stack_katcp(d, ans);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "search took %lu%06lu\xC2\xB5s", delta.tv_sec, delta.tv_usec);
  
  if (dsize)
    free(dsize);

  if (di)
    free(di);

  for (i=0; i<count; i++){
    if (data[i])
      free(data[i]);
  }
  if (data)
    free(data);
  
  destroy_stack_katcp(tags);
  destroy_stack_katcp(ans);

  __tobjs  = NULL;
  __tcount = 0;
  
  return 0;
}

int search_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcl_parse *p;

  if (argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "usage");
    return KATCP_RESULT_FAIL;
  }
  
  p = arg_parse_katcp(d);
  if (p == NULL)
    return KATCP_RESULT_FAIL;

  if (search_katcp(d, p) < 0)
    return KATCP_RESULT_FAIL;
  
  return KATCP_RESULT_OK;
}


