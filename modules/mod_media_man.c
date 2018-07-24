#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <alloca.h>

#include <katcp.h>
#include <katpriv.h>
#include <kcs.h>





int init_mod(struct katcp_dispatch *d)
{
  int rtn;

  if (check_code_version_katcp(d) != 0){
#ifdef DEBUG
    fprintf(stderr, "mod: ERROR was build against an incompatible katcp lib\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot load module katcp version mismatch");
    return -1;
  }
  


  return rtn;
}


#if 0

#define MEDIAMAN_OPERATION_SUBPROCESS "subprocess"
#define MEDIAMAN_OPERATION_SEARCH     "search"
#define MEDIAMAN_TYPE_MEDIA_ITEM      "media_item"
#define MEDIAMAN_TYPE_MEDIA_TAG       "media_tag"
#define MEDIAMAN_TYPE_SEARCH_TERMS    "search_terms"

struct media_item {
  char *mi_key;
  char *mi_path;
  struct media_tag **mi_tag;
  int mi_tag_count;
};

struct media_tag {
  char *mt_key;
  struct media_item **mt_item;
  int mt_item_count;
};

//void print_media_item_mm(struct katcp_dispatch *d, void *data);

void print_media_tag_mm(struct katcp_dispatch *d, void *data)
{
  //int i;
  struct media_tag *mt;
  mt = data;

  if(mt == NULL)
    return;

#ifdef DEBUG
  fprintf(stderr,"media tag: %s\n", mt->mt_key);
#endif
  append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#media tag:");
  append_string_katcp(d, KATCP_FLAG_STRING, mt->mt_key);
  append_args_katcp(d, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, "%d", mt->mt_item_count);
  
#if 0
  for (i=0; i<mt->mt_item_count; i++){
    print_media_item_mm(d, mt->mt_item[i]);
  }
#endif
}
void destroy_media_tag_mm(void *data)
{
  struct media_tag *mt;
  mt = data;
  if (mt == NULL)
    return;
  
  if (mt->mt_key != NULL) 
    free(mt->mt_key);
  if (mt->mt_item != NULL)
    free(mt->mt_item);
  free(mt);
}
void *parse_media_tag_mm(struct katcp_dispatch *d, char **str)
{
  struct media_tag *mt;

  if (str == NULL || str[0] == NULL)
    return NULL;

  mt = malloc(sizeof(struct media_tag));
  if (mt == NULL)
    return NULL;

  mt->mt_key = strdup(str[0]);
  if (mt->mt_key == NULL){
    destroy_media_tag_mm(mt);
    return NULL;
  }
  mt->mt_item = NULL;
  mt->mt_item_count = 0;

  return mt;
}

void destroy_search_terms_mm(void *data)
{
  char **tags;
  int i;
  tags = data;
  if (tags == NULL)
    return;
  for (i=0; tags[i] != NULL; i++){
    free(tags[i]);
  }
  free(tags);
}
void print_search_terms_mm(struct katcp_dispatch *d, void *data)
{
  char **tags;
  int i;
  tags = data;
  if (tags == NULL)
    return;

  append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#search terms:");
  for (i=0; tags[i] != NULL; i++){
    append_args_katcp(d, KATCP_FLAG_STRING | ((tags[i+1] == NULL) ? KATCP_FLAG_LAST : 0), "<%s>", tags[i]);
  }
}

void *parse_search_terms_mm(struct katcp_dispatch *d, char **str)
{
  char **tags;
  int i;
  tags = NULL;
  for (i=0; str[i] != NULL; i++){
#ifdef DEBUG
    fprintf(stderr, "MEDIAMAN: parse search terms %s\n", str[i]);
#endif
    tags = realloc(tags, sizeof(char *) * (i+2));
    if (tags == NULL){
      destroy_search_terms_mm(tags);
      return NULL;
    }
    tags[i] = strdup(str[i]);
    if (tags[i] == NULL){
      destroy_search_terms_mm(tags[i]);
      return NULL;
    }
  }
#ifdef DEBUG
  fprintf(stderr, "MEDIAMAN: parse search terms count %d\n", i);
#endif
  tags[i] = NULL;

  return tags;
}

struct media_tag *get_named_media_tag_mm(struct katcp_dispatch *d, char *tagname)
{
  char *temp[1];
  struct media_tag *mt;

  mt = get_key_data_type_katcp(d, MEDIAMAN_TYPE_MEDIA_TAG, tagname);

  if (mt != NULL){
    //mt->mt_item_count++;
    return mt;
  }

  temp[0] = tagname;

  mt = parse_media_tag_mm(d, temp);
  if (mt == NULL)
    return NULL;

  if (store_data_type_katcp(d, MEDIAMAN_TYPE_MEDIA_TAG, KATCP_DEP_BASE, tagname, mt, &print_media_tag_mm, &destroy_media_tag_mm, NULL, NULL, &parse_media_tag_mm, NULL) < 0){
    destroy_media_tag_mm(mt);
    return NULL;
  }
  
  return mt;
}

int add_media_item_media_tag_mm(struct katcp_dispatch *d, struct media_tag *mt, struct media_item *mi)
{
  int i;

  if (mt == NULL || mi == NULL)
    return -1;

/*should replace with a bsearch / qsort*/
  for(i=0; i<mt->mt_item_count; i++){
    if (mt->mt_item[i] == mi){
      return -1;
    }
  }

  mt->mt_item = realloc(mt->mt_item, sizeof(struct media_item *) * (mt->mt_item_count+1));
  mt->mt_item[mt->mt_item_count] = mi;
  mt->mt_item_count++;

  return 0;
}

void print_media_item_mm(struct katcp_dispatch *d, void *data)
{
  struct media_item *mi;
 // int i;

  mi = data;
  if (mi == NULL)
    return;

#ifdef debug
  fprintf(stderr, "media item: %s\n\t%s\n", mi->mi_key, mi->mi_path);
#endif
  
  append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#media item:");
  append_string_katcp(d, KATCP_FLAG_STRING, mi->mi_key);
  append_string_katcp(d, KATCP_FLAG_STRING, mi->mi_path);
  append_args_katcp(d, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, "%d", mi->mi_tag_count);
#if 0
  for (i=0; i<mi->mi_tag_count; i++){
    print_media_tag_mm(d, mi->mi_tag[i]);
  }
#endif
}
void destroy_media_item_mm(void *data)
{
  struct media_item *mi;
  mi = data;
  if(mi != NULL){
    if (mi->mi_key) free(mi->mi_key);
    if (mi->mi_path) free(mi->mi_path);
    if (mi->mi_tag) free(mi->mi_tag);
    free(mi);
  }
}
void *parse_media_item_mm(struct katcp_dispatch *d, char **str)
{
  struct media_item *mi;
  struct media_tag **tags;
  //char *ptr;
  int i, count;

  if (str == NULL || str[0] == NULL || str[1] == NULL)
    return NULL;

  mi = malloc(sizeof(struct media_item));
  if (mi == NULL)
    return NULL;
  
  mi->mi_tag = NULL;
  mi->mi_tag_count = 0;

  tags = NULL;
  count = 0;

  for (i=0; str[i] != NULL; i++){
    switch(i){
      case 0:
        mi->mi_path = strdup(str[i]);
        break;
      case 1:
        mi->mi_key = strdup(str[i]);
        break;
      default:
        tags = realloc(tags, sizeof(struct media_tag *) * (count + 1));
        if (tags == NULL){
          destroy_media_item_mm(mi);
          return NULL;
        }
        tags[count] = get_named_media_tag_mm(d, str[i]);
        if (add_media_item_media_tag_mm(d, tags[count], mi) < 0){
#if 0
          def DEBUG
          fprintf(stderr, "MEDIAMAN: media item <%s> already tagged with <%s>\n", mi->mi_key, tags[count]->mt_key);
#endif
        }
        count++;
        break;
    }
  }

  mi->mi_tag = tags;
  mi->mi_tag_count = count;

  if (mi->mi_path == NULL || mi->mi_key == NULL){
    destroy_media_item_mm(mi);
    return NULL;
  }

  return mi;
}

#define MIS_PATH 0
#define MIS_FILE 1
#define MIS_TAG  2
#define PATHMARKER "PATH"
#define FILEMARKER "NAME"
#define TAGMARKER  "TAGS"
int catch_media_item_mm(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct media_item *mi;

  struct katcl_parse *p;
  int max, i, state, nlen, olen, size, j;
  char *ptr, **temp;

  p = get_parse_notice_katcp(d, n);
  if (p){
    max = get_count_parse_katcl(p);
    if (max > 6){
      ptr = get_string_parse_katcl(p, 0);
      if (strcmp(ptr,"#media_item") != 0){
        return 1;
      }

      size = max - 4 + 1;
      temp = malloc(sizeof(char*) * size);
      if (temp == NULL)
        return 1;
      for(i=0;i<size;i++)
        temp[i] = NULL;
      
      j = 0;
      nlen = 0;
      olen = 0;
      state = MIS_PATH;
      for(i=2; i<max; i++) {
        
        ptr = get_string_parse_katcl(p, i);

        switch (state){
          case MIS_PATH:
            if (strcmp(ptr, FILEMARKER) == 0){
              temp[j][nlen-1] = '\0';
#if 0
              def DEBUG
              fprintf(stderr, "<%s>\n", temp[j]);
#endif
              state = MIS_FILE;
              j++;
              olen = 0;
              nlen = 0;
              break;
            }
            olen = nlen;
            nlen = olen + strlen(ptr) + 1;
            temp[j] = realloc(temp[j], sizeof(char) * nlen);
            strncpy(temp[j] + olen, ptr, strlen(ptr));
            temp[j][nlen-1] = ' ';
            break;
          case MIS_FILE:
            if (strcmp(ptr, TAGMARKER) == 0){
              temp[j][nlen-1] = '\0';
#if 0
              def DEBUG
              fprintf(stderr, "<%s>\n", temp[j]);
#endif
              state = MIS_TAG;
              j++;
              break;
            }
            olen = nlen;
            nlen = olen + strlen(ptr) + 1;
            temp[j] = realloc(temp[j], sizeof(char) * nlen);
            strncpy(temp[j] + olen, ptr, strlen(ptr));
            temp[j][nlen-1] = ' ';
            break;
          case MIS_TAG:
            temp[j] = strdup(ptr);
#if 0
            def DEBUG
            fprintf(stderr, "<%s>\n", temp[j]);
#endif
            j++;
            break;
        } /*switch*/
      } /*for*/
     
      if (get_key_data_type_katcp(d, MEDIAMAN_TYPE_MEDIA_ITEM, temp[1]) == NULL){
        mi = parse_media_item_mm(d, temp);

        if (store_data_type_katcp(d, MEDIAMAN_TYPE_MEDIA_ITEM, KATCP_DEP_BASE, mi->mi_key, mi, &print_media_item_mm, &destroy_media_item_mm, NULL, NULL, &parse_media_item_mm, NULL) < 0){
          destroy_media_item_mm(mi);
          return 1;
        }
      } else {
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "caught media_item already exists");
      }

      if (temp != NULL){
        for (i=0; i<size; i++){
          if (temp[i] != NULL)
            free(temp[i]);
        }
        free(temp);
      }
    }
    
#if 0
    for (i=0; i<max; i++){
      ptr = get_string_parse_katcl(p, i);
#ifdef DEBUG
      fprintf(stderr, "MEDIAMAN: %s\n", ptr);
#endif
    } 
#endif

  }

  return 1;
}
#undef MIS_PATH
#undef MIS_FILE
#undef MIS_TAG
#undef PATHMARKER
#undef FILEMARKER
#undef TAGMARKER

int subprocess_mm(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
  char *cmd, *path[3];
  struct katcp_job *j;
  //struct katcp_url *u;

  cmd = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_STRING);
  path[0] = cmd;
  path[1] = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_STRING);
  path[2] = NULL;

  if (cmd == NULL || path == NULL) {
#ifdef DEBUG
    fprintf(stderr, "MEDIAMAN subprocess: need strings\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "subprocess needs a the cmd and path");
    return -1;
  }

  j = process_name_create_job_katcp(d, cmd, path, NULL, NULL);
  if (j == NULL){
#ifdef DEBUG
    fprintf(stderr, "MEDIAMAN subprocess: wrapper process create job FAIL\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "subprocess cannot create subprocess job");
    return -1;
  }

  if (match_inform_job_katcp(d, j, "#media_item", &catch_media_item_mm, NULL) < 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "subprocess cannot match inform");
    zap_job_katcp(d, j);
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "subprocess running %s %s", cmd, path[0]);

  return 0;
}

struct kcs_sm_op *subprocess_setup_mm(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct kcs_sm_op *op;
  
  op = create_sm_op_kcs(&subprocess_mm, NULL);
  if (op == NULL)
    return NULL;

#ifdef DEBUG
  fprintf(stderr, "mod_media_man: created op %s (%p)\n", MEDIAMAN_OPERATION_SUBPROCESS, op);
#endif

  return op;
}

struct result_item {
  struct media_item *r_mi;
  int r_weight;
};

int compare_result_items_mm(const void *m1, const void *m2)
{
  const struct result_item *a, *b;
  a = m1;
  b = m2;
#ifdef DEBUG
  fprintf(stderr, "MEDIAMAN compare %s & %s\n", a->r_mi->mi_key, b->r_mi->mi_key);
#endif
  return strcmp(a->r_mi->mi_key, b->r_mi->mi_key);
}

int add_item_results_mm(struct katcp_dispatch *d, struct result_item **r, int count, struct media_item *mi)
{
  struct result_item key, *check, *results;

  if (r == NULL || mi == NULL)
    return -1;
  
  results = *r;

  key.r_mi = mi;
  key.r_weight = 1;

#ifdef DEBUG
  fprintf(stderr, "MEDIAMAN: about to bsearch\n");
#endif
  
  check = bsearch(&key, results, count, sizeof(struct result_item), &compare_result_items_mm);
  if (check != NULL){
#ifdef DEBUG
    fprintf(stderr, "MEDIAMAN: found item ++weight\n");
#endif
    check->r_weight++;
    return count;
  }
  
  results = realloc(results, sizeof(struct result_item)*(count+1));
  if (results == NULL)
    return -1;
  
  results[count].r_mi = key.r_mi;
  results[count].r_weight = key.r_weight;

  count++;

#ifdef DEBUG
  fprintf(stderr, "MEDIAMAN: about to qsort\n");
#endif
  qsort(results, count, sizeof(struct result_item), &compare_result_items_mm);

  *r = results;
  return count;
}
void destroy_result_items_mm(void *data)
{
  struct result_item *r;
  r = data;
  if (r != NULL)
    free(r);
}

int search_mm(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o)
{
  struct result_item *results, r;
  struct media_tag *mt;
  char **tags;
  int i, j, count;

  results = NULL;
  count = 0;

#ifdef DEBUG
  fprintf(stderr, "MEDIAMAN: runing SEARCH\n");
#endif
  
  tags = pop_data_expecting_stack_katcp(d, stack, MEDIAMAN_TYPE_SEARCH_TERMS);
  if (tags == NULL)
    return -1;
  
  for (i=0; tags[i] != NULL; i++) {
    mt = get_named_media_tag_mm(d, tags[i]);
    if (mt != NULL){
#ifdef DEBUG
      fprintf(stderr, "MEDIAMAN SEARCH: got <%s>\n", mt->mt_key);
#endif
      for (j=0; j<mt->mt_item_count; j++){
        count = add_item_results_mm(d, &results, count, mt->mt_item[j]);
        if (count < 0){
#ifdef DEBUG
          fprintf(stderr, "MEDIAMAN: fatal error adding to item results\n"); 
#endif
          destroy_result_items_mm(results);
          return -1;
        }
      }
#if 0
      if (push_named_stack_katcp(d, stack, mt, MEDIAMAN_TYPE_MEDIA_TAG) < 0){
#ifdef DEBUG
        fprintf(stderr, "MEDIAMAN: seach failed to push media tag onto stack\n");
#endif
        return -1;
      }
#endif
    }
  }
  
  for (i=0;i<count;i++){
    r = results[i];
#ifdef DEBUG
    fprintf(stderr, "MEDIAMAN: %3d <%s>\n", r.r_weight, r.r_mi->mi_key);
#endif
  }

  destroy_result_items_mm(results);
  return 0;
}

struct kcs_sm_op *search_setup_mm(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct kcs_sm_op *op;

  op = create_sm_op_kcs(&search_mm, NULL);
  if (op == NULL)
    return NULL;

#ifdef DEBUG
  fprintf(stderr, "mod_media_man: created op %s (%p)\n", MEDIAMAN_OPERATION_SEARCH, op);
#endif

  return op;
}

#if 0
int media_item_cmd_mm(struct katcp_dispatch *d, int argc)
{
  int i, max;
  char *ptr;

  max = arg_count_katcp(d);
  for (i=0; i<max; i++){
    
    ptr = arg_string_katcp(d,i);

#ifdef DEBUG
    fprintf(stderr, "MEDIA_ITEM CMD: %s\n", ptr);
#endif

  }
  
  return KATCP_RESULT_OK;
}
#endif

int init_mod(struct katcp_dispatch *d)
{
  int rtn;

  if (check_code_version_katcp(d) != 0){
#ifdef DEBUG
    fprintf(stderr, "mod: ERROR was build against an incompatible katcp lib\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot load module katcp version mismatch");
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "successfully loaded mod_media_man");
  
#if 1
  rtn  = register_name_type_katcp(d, MEDIAMAN_TYPE_MEDIA_ITEM, KATCP_DEP_BASE, &print_media_item_mm, &destroy_media_item_mm, NULL, NULL, &parse_media_item_mm, NULL);
  rtn += register_name_type_katcp(d, MEDIAMAN_TYPE_MEDIA_TAG, KATCP_DEP_BASE, &print_media_tag_mm, &destroy_media_tag_mm, NULL, NULL, &parse_media_tag_mm, NULL);
  rtn += register_name_type_katcp(d, MEDIAMAN_TYPE_SEARCH_TERMS, KATCP_DEP_BASE, &print_search_terms_mm, &destroy_search_terms_mm, NULL, NULL, &parse_search_terms_mm, NULL);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added type:");
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", MEDIAMAN_TYPE_MEDIA_ITEM);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", MEDIAMAN_TYPE_MEDIA_TAG);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", MEDIAMAN_TYPE_SEARCH_TERMS);
#endif  

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added operations:");
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, MEDIAMAN_OPERATION_SUBPROCESS, &subprocess_setup_mm, NULL, NULL, NULL, NULL, NULL, NULL);
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, MEDIAMAN_OPERATION_SEARCH, &search_setup_mm, NULL, NULL, NULL, NULL, NULL, NULL);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", MEDIAMAN_OPERATION_SUBPROCESS);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", MEDIAMAN_OPERATION_SEARCH);
#if 0
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added edges:");
  rtn += store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_DEP_BASE, KATCP_EDGE_CONF_SEARCH, &config_search_setup_mod, NULL, NULL, NULL, NULL, NULL);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_EDGE_CONF_SEARCH);
#endif
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "to see the full operation list: ?sm oplist");
  
  //rtn = register_flag_mode_katcp(d, "?media_item", "access the media_item data store (add path,item,tags,...)", &media_item_cmd_mm, 0, KCS_MODE_BASIC);


  return rtn;
}
#endif


