#ifndef KCS_H_
#define KCS_H_

#define KCS_MAX_CLIENTS          32

#define KCS_MODE_BASIC            0 
#define KCS_MODE_BASIC_NAME  "basic"

#define KCS_NOTICE_PYTHON   "python"


struct kcs_basic
{
  char *b_scripts;
  struct p_parser *b_parser;
  struct kcs_ojb *b_pool_head;
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

int setup_basic_kcs(struct katcp_dispatch *d, char *scripts);

int parser_load(struct katcp_dispatch *d,char *filename);
int parser_destroy(struct katcp_dispatch *d);
int parser_list(struct katcp_dispatch *d);
int parser_save(struct katcp_dispatch *d, char *filename, int force);
struct p_value * parser_get(struct katcp_dispatch *d, char *srcl, char *srcs, unsigned long vidx);
int parser_set(struct katcp_dispatch *d, char *srcl, char *srcs, unsigned long vidx, char *newval);


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

#define KCS_ID_ROACH        2 
#define KCS_ID_NODE         1
#define KCS_ID_GENERIC      0

struct kcs_obj {
  int tid;
  struct kcs_ojb *parent;
  char *pool;
  void *payload;
};

struct kcs_node {
  struct kcs_obj **children;
  int childcount;
};

struct kcs_roach {
  char *hostname;
  char *ip;
  char *mac;
};

struct kcs_tree_operations {
  struct kcs_obj* (*t_init)(void);
  struct kcs_obj* (*t_create_node)(char *pool);
  struct kcs_obj* (*t_create_leaf)(void *payload);
  int (*t_add)(struct kcs_obj *parent, struct kcs_obj *child);
  int (*t_del)(struct kcs_obj *obj);
  struct kcs_obj* (*t_find)(char *sstr);
  int (*t_destroy)(struct kcs_obj *root);
};

int roachpool_greeting(struct katcp_dispatch *d);
int roachpool_add(struct katcp_dispatch *d);
int roachpool_mod(struct katcp_dispatch *d);
int roachpool_del(struct katcp_dispatch *d);
int roachpool_list(struct katcp_dispatch *d);
int roachpool_destroy(struct katcp_dispatch *d);
#endif
