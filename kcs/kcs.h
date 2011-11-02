#ifndef KCS_H_
#define KCS_H_

#include <katcp.h>
#include <avltree.h>

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

int setup_basic_kcs(struct katcp_dispatch *d, char *scripts, char **argv, int argc);

struct kcs_basic
{
  char *b_scripts;

  char **b_argv;
  int b_argc;
  
  struct p_parser *b_parser;
  struct kcs_obj *b_pool_head;

  struct avl_tree *b_ds;
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
  char *ip;
  char *mac;
  struct katcp_url *kurl;

  struct timeval r_seen;

  struct katcp_acquire *r_acquire;
};

int roach_cmd(struct katcp_dispatch *d, int argc);
int roachpool_greeting(struct katcp_dispatch *d);
int roachpool_add(struct katcp_dispatch *d);
int roachpool_mod(struct katcp_dispatch *d);
int roachpool_flush(struct katcp_dispatch *d);
int roachpool_list(struct katcp_dispatch *d);
int roachpool_destroy(struct katcp_dispatch *d);
int roachpool_getconf(struct katcp_dispatch *d);
int roachpool_connect_pool(struct katcp_dispatch *d);
int add_roach_to_pool_kcs(struct katcp_dispatch *d, char *pool, char *url, char *ip);
struct kcs_obj *roachpool_get_obj_by_name_kcs(struct katcp_dispatch *d, char *name);
int mod_roach_to_new_pool(struct kcs_obj *root, char *pool, char *hostname);
int roachpool_count_kcs(struct katcp_dispatch *d);
int update_sensor_for_roach_kcs(struct katcp_dispatch *d, struct kcs_obj *ko, int val);
int add_sensor_to_roach_kcs(struct katcp_dispatch *d, struct kcs_obj *ko);

#define STATEMACHINE_SCHEDULER_NOTICE   "<kcs_scheduler>"

#if 0
#define KATCP_DEP_BASE                  0
#endif
#define KATCP_DEP_ELAV                  1

#define KATCP_TYPE_MODULES              "modules"
#define KATCP_TYPE_STATEMACHINE         "statemachines"
#define KATCP_TYPE_STATEMACHINE_STATE   "states"
#define KATCP_TYPE_EDGE                 "edges"
#define KATCP_TYPE_OPERATION            "operations"
#define KATCP_TYPE_INTEGER              "int"
#if 0
#define KATCP_TYPE_STRING               "string"
#endif
#define KATCP_TYPE_FLOAT                "float"
#define KATCP_TYPE_DOUBLE               "double"
#define KATCP_TYPE_CHAR                 "char"
#define KATCP_TYPE_ACTOR                "actor"

#define KATCP_OPERATION_STACK_PUSH      "push"
#define KATCP_OPERATION_TAG_ACTOR       "tagactor"
#define KATCP_OPERATION_GET_TAG_SET     "gettagset"
#define KATCP_OPERATION_STORE           "store"
#define KATCP_OPERATION_SPAWN           "spawn"
#define KATCP_OPERATION_PRINT_STACK     "printstack"
#define KATCP_OPERATION_GET_DBASE_VALUES "getvalues"


#define KATCP_EDGE_SLEEP                "msleep"
#define KATCP_EDGE_PEEK_STACK_TYPE      "peekstacktype"
#define KATCP_EDGE_IS_STACK_EMPTY       "is_stackempty"
#define KATCP_EDGE_RELAY_KATCP          "relaykatcp"

#define TASK_STATE_CLEAN_UP             1
#define TASK_STATE_FOLLOW_EDGES         2
#define TASK_STATE_RUN_OPS              3
#define TASK_STATE_EDGE_WAIT            4

#define EDGE_WAIT                       1
#define EDGE_OKAY                       0
#define EDGE_FAIL                       -1

#define PROCESS_MASTER                  0x1
#define PROCESS_SLAVE                   0x2

struct katcp_module {
  char *m_name;
  void *m_handle;
};

struct kcs_sched_task {
  int t_flags;

  int t_state;
  int t_edge_i;
  int t_op_i;

  struct katcp_stack *t_stack;
  struct kcs_sm_state *t_pc;
  
  int t_rtn;
};

struct kcs_sm {
  char *m_name;
};

struct kcs_sm_op {
  int (*o_call)(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o);
  struct katcp_tobject *o_tobject;
};

struct kcs_sm_state {
  char *s_name;
  
  struct kcs_sm_edge **s_edge_list;
  int s_edge_list_count;

  struct kcs_sm_op **s_op_list;
  int s_op_list_count;
};

struct kcs_sm_edge {
  struct kcs_sm_state *e_next;
  int (*e_call)(struct katcp_dispatch *, struct katcp_notice *, void *);
};

int *create_integer_type_kcs(int val);
int init_statemachine_base_kcs(struct katcp_dispatch *d);

int statemachine_init_kcs(struct katcp_dispatch *d);
int statemachine_cmd(struct katcp_dispatch *d, int argc);
void destroy_statemachine_data_kcs(struct katcp_dispatch *d);
//struct avl_tree *get_datastore_tree_kcs(struct katcp_dispatch *d);
struct kcs_sm_op *create_sm_op_kcs(int (*call)(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *o), struct katcp_tobject *o);
struct kcs_sm_edge *create_sm_edge_kcs(struct kcs_sm_state *s_next, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data));

int start_process_kcs(struct katcp_dispatch *d, char *startnode, struct katcp_tobject *to, int flags);
int trigger_edge_process_kcs(struct katcp_dispatch *d, struct katcp_stack *stack, struct katcp_tobject *to);

int init_actor_tag_katcp(struct katcp_dispatch *d);

struct katcp_actor;

int deregister_tag_katcp(struct katcp_dispatch *d, char *name);
int dump_tagsets_katcp(struct katcp_dispatch *d);

struct katcp_actor {
  char *a_key;

  struct katcp_job *a_job;
  struct katcp_notice *a_sm_notice;

  struct katcp_tobject *a_data;

  void *a_tag_root;
  int a_tag_count;
};

struct katcp_actor *create_actor_type_katcp(struct katcp_dispatch *d, char *str, struct katcp_job *j, struct katcp_notice *n, void *data, char *datatype);
void print_actor_type_katcp(struct katcp_dispatch *d, char *key, void *data);
void destroy_actor_type_katcp(void *data);
int copy_actor_type_katcp(void *src, void *dest, int n);
int compare_actor_type_katcp(const void *a, const void *b);
void *parse_actor_type_katcp(struct katcp_dispatch *d, char **str);
char *getkey_actor_katcp(void *data);

int hold_sm_notice_actor_katcp(struct katcp_actor *a, struct katcp_notice *n);
int release_sm_notice_actor_katcp(struct katcp_dispatch *d, struct katcp_actor *a, struct katcl_parse *p);

int tag_actor_katcp(struct katcp_dispatch *d, struct katcp_actor *a, struct katcp_tag *t);
int tag_named_actor_katcp(struct katcp_dispatch *d, struct katcp_actor *a, char *tag);
int untag_actor_katcp(struct katcp_dispatch *d, struct katcp_actor *a, struct katcp_tag *t);
int untag_named_actor_katcp(struct katcp_dispatch *d, struct katcp_actor *a, char *tag);
int unlink_tags_actor_katcp(struct katcp_dispatch *d, struct katcp_actor *a);

struct katcp_job * run_child_process_kcs(struct katcp_dispatch *d, struct katcp_url *url, int (*call)(struct katcl_line *, void *), void *data, struct katcp_notice *n);
int xport_sync_connect_and_start_subprocess_kcs(struct katcl_line *l, void *data);
int xport_sync_connect_and_stop_subprocess_kcs(struct katcl_line *l, void *data);
int xport_sync_connect_and_soft_restart_subprocess_kcs(struct katcl_line *l, void *data);

int watchannounce_cmd(struct katcp_dispatch *d, int argc);

struct katcp_job *wrapper_process_create_job_katcp(struct katcp_dispatch *d, struct katcp_url *file, char **argv, struct katcp_notice *halt);

#endif
