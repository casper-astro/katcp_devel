#ifndef KCS_H_
#define KCS_H_

#include <katcp.h>

#define KCS_MAX_CLIENTS          32

#define KCS_MODE_BASIC            0 
#define KCS_MODE_BASIC_NAME  "basic"

#define KCS_NOTICE_PYTHON   "python"

#ifdef DEBUG

#define KCS_FOREGROUND 1
#define KCS_LOGFILE "kcslog"

#else

#define KCS_FOREGROUND 0
#define KCS_LOGFILE "/var/log/kcslog"

#endif

char *create_str(char *s);

int setup_basic_kcs(struct katcp_dispatch *d, char *scripts);

struct kcs_basic
{
  char *b_scripts;
  struct p_parser *b_parser;
  struct kcs_obj *b_pool_head;
  struct kcs_statemachines *b_sms;
};


struct p_parser {
  int state;
  struct p_label **labels;
  int lcount;
  char *filename;
  off_t fsize;
  time_t open_time;
  struct p_comment **comments;
  int comcount;
};

struct p_comment {
  char *str;
  int flag;
};

struct p_label {
  struct p_setting **settings;
  int scount;
  char *str;
  struct p_comment **comments;
  int comcount;
};

struct p_setting {
  struct p_value **values;
  int vcount;
  char *str;
  struct p_comment **comments;
  int comcount;
};

struct p_value {
  char *str;
  //int vtype;
  //int vsize;
};


int parser_load(struct katcp_dispatch *d,char *filename);
int parser_destroy(struct katcp_dispatch *d);
int parser_list(struct katcp_dispatch *d);
int parser_save(struct katcp_dispatch *d, char *filename, int force);
struct p_value * parser_get(struct katcp_dispatch *d, char *srcl, char *srcs, unsigned long vidx);
int parser_set(struct katcp_dispatch *d, char *srcl, char *srcs, unsigned long vidx, char *newval);
struct p_value **parser_get_values(struct p_parser *p, char *s, int *count);

struct e_state {
  int fd;
  fd_set insocks;
  fd_set outsocks;
  int highsock;
  
  pid_t pid;
  
  struct katcl_line *kl;
  
  char *cdb;
  int cdbsize;

  char *filename;
};

void execpy_do(char *filename, char **argv);

struct kcs_url {
  char *str;
  char *scheme;
  char *host;
  int port;
  char **path;
  int pcount;
};

char *kurl_string(struct kcs_url *ku, char *path);
char *kurl_add_path(struct kcs_url *ku, char *npath);
void kurl_print(struct kcs_url *ku);
struct kcs_url *kurl_create_url_from_string(char *url);
struct kcs_url *kurl_create_url(char *scheme, char *host, int port, char *path);
void kurl_destroy(struct kcs_url *ku);

#define KCS_ID_ROACH        2 
#define KCS_ID_NODE         1
#define KCS_ID_GENERIC      0

#define KCS_OK    0
#define KCS_FAIL  1

struct kcs_obj {
  int tid;
  struct kcs_obj *parent;
  char *name;
  void *payload;
};

struct kcs_node {
  struct kcs_obj **children;
  int childcount;
};

struct kcs_roach {
  /*char *hostname;*/
  char *ip;
  char *mac;
  char *jl;
  struct kcs_url *kurl;
  struct kcs_statemachine *ksm;
};

/*
struct kcs_tree_operations {
  struct kcs_obj* (*t_init)(void);
  struct kcs_obj* (*t_create_node)(char *pool);
  struct kcs_obj* (*t_create_leaf)(void *payload);
  int (*t_add)(struct kcs_obj *parent, struct kcs_obj *child);
  int (*t_del)(struct kcs_obj *obj);
  struct kcs_obj* (*t_find)(char *sstr);
  int (*t_destroy)(struct kcs_obj *root);
};
*/

int roachpool_greeting(struct katcp_dispatch *d);
int roachpool_add(struct katcp_dispatch *d);
int roachpool_mod(struct katcp_dispatch *d);
int roachpool_del(struct katcp_dispatch *d);
int roachpool_list(struct katcp_dispatch *d);
int roachpool_destroy(struct katcp_dispatch *d);
int roachpool_getconf(struct katcp_dispatch *d);
int roachpool_connect_pool(struct katcp_dispatch *d);
int roachpool_test_timer(struct katcp_dispatch *d);
struct kcs_obj *search_tree(struct kcs_obj *o, char *str);

#define KCS_SM_PING      0
#define KCS_SM_PING_S1   0
#define KCS_SM_PING_S2   1
#define KCS_SM_PING_STOP 2


struct kcs_statemachines {
  struct kcs_statemachine **machines; 
  int mcount;
};

struct kcs_statemachine {
  int (**sm)(struct katcp_dispatch *,struct katcp_notice *, void *); 
  int state;
};

int statemachine_greeting(struct katcp_dispatch *d);
int statemachine_ping(struct katcp_dispatch *d);
//void statemachine_destroy(struct katcp_dispatch *d);
void ksm_destroy(struct kcs_statemachine *ksm);


#endif
