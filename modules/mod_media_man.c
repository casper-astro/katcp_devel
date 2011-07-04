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


#define MEDIAMAN_OPERATION_SUBPROCESS "subprocess"
#define MEDIAMAN_TYPE_MEDIA_ITEM      "media_item"
#define MEDIAMAN_TYPE_MEDIA_TAG       "media_tag"

struct media_item {
  char *mi_key;
  char *mi_path;
  struct media_tags **mi_tag;
  int mi_tag_count;
};

struct media_tag {
  char *mt_key;
  struct media_item **mt_item;
  int mt_item_count;
};

void print_media_tag_mm(struct katcp_dispatch *d, void *data)
{

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
void *parse_media_tag_mm(char **str)
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

  return mt;
}

void print_media_item_mm(struct katcp_dispatch *d, void *data)
{

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
void *parse_media_item_mm(char **str)
{
  struct media_item *mi;
  char *ptr;
  int i;

  if (str == NULL || str[0] == NULL || str[1] == NULL)
    return NULL;

  mi = malloc(sizeof(struct media_item));
  if (mi == NULL)
    return NULL;
  
  mi->mi_tag = NULL;
  mi->mi_tag_count = 0;

  for (i=0; str[i] != NULL; i++){
    switch(i){
      case 0:
        mi->mi_path = strdup(str[i]);
        break;
      case 1:
        mi->mi_key = strdup(str[i]);
        break;
      default:
        
        break;
    }
  }

  if (mi->mi_path == NULL || mi->mi_key == NULL){
    destroy_media_item_mm(mi);
    return NULL;
  }

  return mi;
}

#if 1
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
#if 0
    def DEBUG
    fprintf(stderr, "max: %d\n", max);
#endif
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
#if 0
            def DEBUG
            fprintf(stderr, "%3d %s\n", j, ptr);
#endif
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
#if 0 
            def DEBUG
            fprintf(stderr, "%3d %s\n", j, ptr);
#endif
            olen = nlen;
            nlen = olen + strlen(ptr) + 1;
            temp[j] = realloc(temp[j], sizeof(char) * nlen);
            strncpy(temp[j] + olen, ptr, strlen(ptr));
            temp[j][nlen-1] = ' ';
            break;
          case MIS_TAG:
#if 0
            def DEBUG
            fprintf(stderr, "%3d %s\n", j, ptr);
#endif
            temp[j] = strdup(ptr);
#if 0
            def DEBUG
            fprintf(stderr, "<%s>\n", temp[j]);
#endif
            j++;
            break;
        } /*switch*/
      } /*for*/
     
      mi = parse_media_item_mm(temp);
      
     

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
#endif

int subprocess_mm(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_stack_obj *o)
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
#if 0
  u = create_exec_kurl_katcp(cmd);
  if (u == NULL){
#ifdef DEBUG
    fprintf(stderr, "MEDIAMAN subprocess: cannot create exec url\n");
#endif
    return -1; 
  }

  j = wrapper_process_create_job_katcp(d, u, path, NULL);
#endif
  j = process_name_create_job_katcp(d, cmd, path, NULL, NULL);
  if (j == NULL){
#ifdef DEBUG
    fprintf(stderr, "MEDIAMAN subprocess: wrapper process create job FAIL\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "subprocess cannot create subprocess job");
    return -1;
  }

#if 1
  if (match_inform_job_katcp(d, j, "#media_item", &catch_media_item_mm, NULL) < 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "subprocess cannot match inform");
    zap_job_katcp(d, j);
    return -1;
  }
#endif

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
  fprintf(stderr, "mod_config_parser: created op %s (%p)\n", MEDIAMAN_OPERATION_SUBPROCESS, op);
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
  rtn  = register_name_type_katcp(d, MEDIAMAN_TYPE_MEDIA_ITEM, KATCP_DEP_BASE, &print_media_item_mm, &destroy_media_item_mm, NULL, NULL, &parse_media_item_mm);
  rtn  = register_name_type_katcp(d, MEDIAMAN_TYPE_MEDIA_TAG, KATCP_DEP_BASE, &print_media_tag_mm, &destroy_media_tag_mm, NULL, NULL, &parse_media_tag_mm);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added type:");
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", MEDIAMAN_TYPE_MEDIA_ITEM);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", MEDIAMAN_TYPE_MEDIA_TAG);
#endif  

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added operations:");
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, MEDIAMAN_OPERATION_SUBPROCESS, &subprocess_setup_mm, NULL, NULL, NULL, NULL, NULL);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", MEDIAMAN_OPERATION_SUBPROCESS);
#if 0
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added edges:");
  rtn += store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_DEP_BASE, KATCP_EDGE_CONF_SEARCH, &config_search_setup_mod, NULL, NULL, NULL, NULL, NULL);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_EDGE_CONF_SEARCH);
#endif
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "to see the full operation list: ?sm oplist");
  
  //rtn = register_flag_mode_katcp(d, "?media_item", "access the media_item data store (add path,item,tags,...)", &media_item_cmd_mm, 0, KCS_MODE_BASIC);


  return rtn;
}
