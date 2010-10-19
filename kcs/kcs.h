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
  struct kcs_node *b_pool_head;
};

struct kcs_node {
  struct kcs_node *parent;
  struct kcs_node **childnodes;
  struct kcs_roach **childroaches;
  int childcount;
  char *desc;
};

struct kcs_roach {
  struct kcs_node *parent;
  char *hostname;
  char *ip;
  char *mac;
  char *type;
};

/*
struct kcs_roach_pool {
  struct kcs_roach **krr;
  int krrcount;
  char *type;
};
*/

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

int roachpool_greeting(struct katcp_dispatch *d);
int roachpool_add(struct katcp_dispatch *d);
int roachpool_destroy(struct katcp_dispatch *d);
#endif
