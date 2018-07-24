#define _XOPEN_SOURCE 500
#include <ftw.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>

#include <katcp.h>
#include <katpriv.h>
#include <kcs.h>

#define INOTIFY_ADD_WATCH_DIR         "add_watch_dir"
#define KATCP_OP_IMPORT_DIR           "import_dir"

#define DELIMS        "/ []()-_.&#@!%^*+={}'\""
#define NCOUNT        1000

static struct katcp_stack *__tempstack;

static int see_file(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
  char *buffer, *token, *fnkey;
  int i, err, j;
  struct katcl_parse *p;
  
  err = 0;

  if (tflag == FTW_F){
    
    fnkey = fpath + ftwbuf->base;//crypt(fpath + ftwbuf->base, "$5$") + 4;
    
#if 0 
    def DEBUG
    fprintf(stderr, "see_file: %s\nkey: %s\n", fpath, fnkey);
#endif

    buffer = strdup(fpath);
    if (buffer == NULL)
      return -1;

    p = create_parse_katcl();
    if (p == NULL)
      return -1;

    err += add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_SET_REQUEST);
    err += add_string_parse_katcl(p, KATCP_FLAG_STRING, fnkey);
    err += add_string_parse_katcl(p, KATCP_FLAG_STRING, "schema");
    err += add_string_parse_katcl(p, KATCP_FLAG_STRING, "location");
    //err += add_string_parse_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_LAST, fpath);
    //err += add_string_parse_katcl(p, KATCP_FLAG_STRING, fnkey);
    err += add_string_parse_katcl(p, KATCP_FLAG_STRING, fpath);
    err += add_string_parse_katcl(p, KATCP_FLAG_STRING, "tags");

    for (i=0; (token = strtok((i>0) ? NULL : buffer, DELIMS)) != NULL; i++){

      for (j=0; token[j] != '\0'; j++)
        token[j] = tolower(token[j]);
      
      err += add_string_parse_katcl(p, KATCP_FLAG_STRING, token);

#if 0
    def DEBUG
      fprintf(stderr, "<%s> ", token);
#endif
      
    }
    
    err += finalize_parse_katcl(p);

    free(buffer);
  
    if (err < 0){
      destroy_parse_katcl(p);
      return -1;
    }
    
    if (push_stack_katcp(__tempstack, p, NULL) < 0){
      destroy_parse_katcl(p);
      return -1;
    }

#if 0
  def DEBUG
    fprintf(stderr, "\n");
#endif

  }

  return 0;
}


int import_dir_mod(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *to)
{ 
  struct katcl_parse *p;
  char *dir;
 
  __tempstack = create_stack_katcp();

  dir = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_STRING);
  if (dir == NULL)
    return -1;
  
  if (nftw(dir, &see_file, NCOUNT, FTW_PHYS) == -1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "ftw encountered error");
    return -1;
  }

  while ((p = pop_data_stack_katcp(__tempstack)) != NULL){
    
    if (set_dbase_katcp(d, p) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot ?set");
    }

    /*TODO: this could be included into the parse logic*/
    destroy_parse_katcl(p);
  
  }
  
  if (__tempstack != NULL)
    destroy_stack_katcp(__tempstack);
  
  __tempstack = NULL;

  return 0;
}

struct kcs_sm_op *import_dir_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  return create_sm_op_kcs(&import_dir_mod, NULL);
}

int add_watch_dir_mod(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *to)
{
  char *dir;
  
  dir = pop_data_expecting_stack_katcp(d, stack, KATCP_TYPE_STRING);
  if (dir == NULL)
    return -1;
  
#ifdef DEBUG
  fprintf(stderr, "mod_inotify: add watch for dir <%s>\n", dir);
#endif

   

  return 0;
}

struct kcs_sm_op *add_watch_dir_setup_mod(struct katcp_dispatch *d, struct kcs_sm_state *s)
{
  return create_sm_op_kcs(&add_watch_dir_mod, NULL);
}

int init_mod(struct katcp_dispatch *d)
{
  int rtn;
  
  rtn = 0;

  if (check_code_version_katcp(d) != 0){
#ifdef DEBUG
    fprintf(stderr, "mod: ERROR was build against an incompatible katcp lib\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot load module katcp version mismatch");
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "successfully loaded mod_dsorcer");
  
  
  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, KATCP_OP_IMPORT_DIR, import_dir_setup_mod, NULL, NULL, NULL, NULL, NULL, NULL);

  rtn += store_data_type_katcp(d, KATCP_TYPE_OPERATION, KATCP_DEP_BASE, INOTIFY_ADD_WATCH_DIR, &add_watch_dir_setup_mod, NULL, NULL, NULL, NULL, NULL, NULL);

  return rtn;
}
