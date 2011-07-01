#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <katcp.h>
#include <katpriv.h>
#include <kcs.h>

#define KATCP_OPERATION_CONF_PARSE  "confparser"
#define KATCP_EDGE_CONF_SEARCH      "confsearch"
#define KATCP_TYPE_CONFIG_SETTING   "configsetting"

#define OLABEL        '['
#define CLABEL        ']'
#define SETTING       '='
#define VALUE         ',' 
#define COMMENT       '#'
#define CR            '\r'
#define LF            '\n'
#define SPACE         ' '

#define S_WHITESPACE  -1
#define S_COMMENT     0
#define S_LABEL       1
#define S_SETTING     2
#define S_VALUE       3

#if 1
struct config_setting {
  char *s_name;
  char *s_value;
};

void print_config_setting_type_mod(struct katcp_dispatch *d, void *data)
{
  struct config_setting *s;
  
  s = data;
  if (s == NULL)
    return;
  
#ifdef DEBUG
  fprintf(stderr, "mod_config_parser: print_config_setting_type %s = %s\n", s->s_name, s->s_value);
#endif
  //log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "configsetting: %s = %s", s->s_name, s->s_value);
  //prepend_inform_katcp(d);
  append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#configsetting:");
  append_string_katcp(d, KATCP_FLAG_STRING, s->s_name);
  append_string_katcp(d, KATCP_FLAG_STRING, "=");
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, s->s_value);
}
void destroy_config_setting_type_mod(void *data)
{
  struct config_setting *s;
  s = data;
  if (s != NULL){
    if (s->s_value)
      free(s->s_value);
    if (s->s_name)
      free(s->s_name);
    free(s);
  }
}
void *parse_config_setting_type_mod(char **str)
{
  struct config_setting *s;

  if (str == NULL || str[0] == NULL || str[1] == NULL)
    return NULL;
  
  s = malloc(sizeof(struct config_setting));

  s->s_name   = strdup(str[0]);
  if (s->s_name == NULL){
    destroy_config_setting_type_mod(s);
    return NULL;
  }
  
  s->s_value  = strdup(str[1]);
  if (s->s_value == NULL){
    destroy_config_setting_type_mod(s);
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "config_parse_setup_mod: created new configsetting: %s %s\n", s->s_name, s->s_value);
#endif

  return s;
}
#endif

int store_config_setting_mod(struct katcp_dispatch *d, char *setting, char *value)
{
  struct config_setting *s;
  char *params[2];

  params[0] = setting;
  params[1] = value;

  s = parse_config_setting_type_mod(params);
  
  if (s == NULL)
    return -1;

  return store_data_type_katcp(d, KATCP_TYPE_CONFIG_SETTING, KATCP_DEP_BASE, setting, s, &print_config_setting_type_mod, &destroy_config_setting_type_mod, NULL, NULL, &parse_config_setting_type_mod);
}

char *rm_whitespace_mod(char *str)
{
  int i,len,pos;
  char *buf;

  if (str == NULL){
#ifdef DEBUG
    fprintf(stderr,"Cannot remove whitespace from NULL string\n");
#endif
    return NULL;
  }

  len = strlen(str);
  buf = malloc(sizeof(char) * (len+1));

  if (buf == NULL)
    return NULL;

  pos = 0;

  for (i=0;i<len;i++){
    switch(str[i]){
      case 0x09:
      case 0x0a:
      case 0x0b:
      case 0x0c:
      case 0x0d:
      case 0x20:
        break;
      default:
        buf[pos++] = str[i];
        break;
    }
  }
  buf[pos] = '\0';
  free(str);

  return buf;
}

int start_config_parser_mod(struct katcp_dispatch *d, char *file)
{
  char *buffer, c, *setting, *value;
  int fd, i, pos, state, rcount, run;
  struct stat file_stats;
  off_t fsize;
  
  if (file == NULL)
    return -1;

  fd = open(file, O_RDONLY);
  if (fd < 0){
#ifdef DEBUG
    fprintf(stderr, "mod_config_parser: open failed: %s\n", strerror(errno));
#endif
    return -1;
  }
  
  if (fstat(fd, &file_stats) != 0){
#ifdef DEBUG
    fprintf(stderr, "mod_config_parser: fstat failed: %s\n", strerror(errno));
#endif
    close(fd);
    return -1;
  }
    
  fsize = file_stats.st_size;

  buffer = mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);

  if (buffer == MAP_FAILED){
#ifdef DEBUG
    fprintf(stderr, "mod_config_parser: mmap failed: %s\n", strerror(errno));
#endif
    close(fd);
    return -1;
  }
  
  state   = S_WHITESPACE;
  pos     = 0;
  setting = NULL;
  value   = NULL;
  rcount  = 0;
  run     = 1;

#ifdef DEBUG
  fprintf(stderr, "mod_config_parser: about to start parsing file: %s\n", file);
#endif

  for (i=0; i<fsize && run; i++){
    c = buffer[i];

    switch(state){
      
      case S_WHITESPACE:
        switch (c){
          case COMMENT:
          case OLABEL:
          case CLABEL:
            state = S_COMMENT;
            break;
          case CR:
          case LF:
            state = S_WHITESPACE;
            pos = i;
            rcount++;
            break;
          case SETTING:
            state = S_SETTING;
            break;
        }
        break;

      case S_COMMENT:
        switch (c){
          case CR:
          case LF:
            state = S_WHITESPACE;
            pos = i;
            rcount++;
            break;
        }
        break;

      case S_SETTING:
        if (setting != NULL){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "config parser cannot parse malformed config around line[%d,%d]", rcount, i-(i/rcount));
          run = 0;
        }
        setting = strndup(buffer + pos + 1, i - pos - 2);
        setting = rm_whitespace_mod(setting);
#ifdef DEBUG
        fprintf(stderr, "%d: SETTING: {%s} ", rcount, setting);  
#endif
        state = S_VALUE;
        pos = i;
        break;
      
      case S_VALUE:
        switch(c) {
          case CR:
          case LF:
            if (value != NULL){
              log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "config parser cannot parse malformed config around line[%d,%d]", rcount, i-(i/rcount));
              run = 0;
            }
            value = strndup(buffer + pos, i - pos);
            value = rm_whitespace_mod(value);
#ifdef DEBUG
            fprintf(stderr, "\tVALUE: {%s}\n", value);  
#endif
#if 1 
            if (store_config_setting_mod(d, setting, value) < 0){
              log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "config parser cannot store data ending");
#ifdef DEBUG
              fprintf(stderr, "mod_config_parser: cannot store data!!\n");
#endif
              run = 0;
            }
#endif

            free(value);
            value = NULL;
            free(setting);
            setting = NULL;
            state = S_WHITESPACE;
            pos = i;
            rcount++;
            break;
        }
        break;

    }
  
  }
  
  if (setting != NULL)
    free(setting);

  if (value != NULL)
    free(value);

  munmap(buffer, fsize);
  close(fd);

#ifdef DEBUG
  fprintf(stderr, "mod_config_parser: done!\n");
#endif

  return 0;
}

int config_parser_mod(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_stack_obj *o)
{
  char *string;
  int rtn; 
  
  string = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_STRING);
  
  rtn = start_config_parser_mod(d, string);
  
  return rtn;
}

struct kcs_sm_op *config_parser_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct kcs_sm_op *op;
  
  op = create_sm_op_kcs(&config_parser_mod, NULL);
  if (op == NULL)
    return NULL;

#ifdef DEBUG
  fprintf(stderr, "mod_config_parser: created op %s (%p)\n", KATCP_OPERATION_CONF_PARSE, op);
#endif

  return op;
}

struct config_setting *search_config_settings_mod(struct katcp_dispatch *d, void *data)
{
  char *str;
  str = data;
  return get_key_data_type_katcp(d, KATCP_TYPE_CONFIG_SETTING, str);
}

int config_search_mod(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_stack *stack;
  char *string;
  struct config_setting *cs;

  stack = data;
 
  string = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_STRING);
  
  cs = search_config_settings_mod(d, string);
  
  if (cs == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "config search not found!");
    return -1;
  }

  print_config_setting_type_mod(d, cs);
  
  if (push_named_stack_katcp(d, stack, cs, KATCP_TYPE_CONFIG_SETTING) < 0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "config search could not push return to stack!");
    return -1;
  }
  
  wake_notice_katcp(d, n, NULL);

  return 0;
}

struct kcs_sm_edge *config_search_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  struct kcs_sm_edge *e;
  
  e = create_sm_edge_kcs(s, &config_search_mod);
  if (e == NULL)
    return NULL;

#ifdef DEBUG
  fprintf(stderr, "mod_config_parser: created edge %s (%p)\n", KATCP_EDGE_CONF_SEARCH, e);
#endif

  return e;
}


int create_search_statemachine_mod(struct katcp_dispatch *d)
{
  int rtn;
  
  rtn  = create_named_node_kcs(d, "search");

  rtn += create_named_op_kcs(d, "search", KATCP_OPERATION_STACK_PUSH, p);
  rtn += create_named_op_kcs(d, "search", KATCP_OPERATION_CONF_PARSE);

  rtn += create_named_node_kcs(d, "found");
  rtn += create_named_node_kcs(d, "notfound");

  rtn += create_named_edge_kcs(d, "search", "found", KATCP_EDGE_CONF_SEARCH);
  rtn += create_named_edge_kcs(d, "search", "notfound", NULL);

  
  

}


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

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "successfully loaded mod_config_parser");
  
#if 1
  rtn  = register_name_type_katcp(d, KATCP_TYPE_CONFIG_SETTING, KATCP_DEP_BASE, &print_config_setting_type_mod, &destroy_config_setting_type_mod, NULL, NULL, &parse_config_setting_type_mod);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added type:");
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_TYPE_CONFIG_SETTING);
#endif  

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added operations:");
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OPERATION_CONF_PARSE, &config_parser_setup_mod, NULL, NULL, NULL, NULL, NULL);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_OPERATION_CONF_PARSE);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "added edges:");
  rtn += store_data_type_katcp(d, KATCP_TYPE_EDGE, KATCP_DEP_BASE, KATCP_EDGE_CONF_SEARCH, &config_search_setup_mod, NULL, NULL, NULL, NULL, NULL);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%s", KATCP_EDGE_CONF_SEARCH);

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "to see the full operation list: ?sm oplist");
  
  return rtn;
}
