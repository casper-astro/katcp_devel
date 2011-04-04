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

#define KCS_SCHEDULER_NOTICE "<kcs_scheduler>"
#define KCS_SCHEDULER_STOP  0
#define KCS_SCHEDULER_TICK  1


char *create_str(char *s);

int setup_basic_kcs(struct katcp_dispatch *d, char *scripts, char **argv, int argc);

struct kcs_basic
{
  char *b_scripts;
  char **b_argv;
  int b_argc;
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

  struct kcs_statemachine **ksm;
  int ksmcount;
  int ksmactive;
  struct timeval lastnow;

  struct katcp_acquire *r_acquire;
};

int roach_cmd(struct katcp_dispatch *d, int argc);
int roachpool_greeting(struct katcp_dispatch *d);
int roachpool_add(struct katcp_dispatch *d);
int roachpool_mod(struct katcp_dispatch *d);
int roachpool_del(struct katcp_dispatch *d);
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

#define KCS_SM_PING_STOP 0 
#define KCS_SM_PING_S1   1
#define KCS_SM_PING_S2   2

#define KCS_SM_CONNECT_STOP             0 
#define KCS_SM_CONNECT_DISCONNECTED     1 
#define KCS_SM_CONNECT_CONNECTED        2

#define KCS_SM_PROGDEV_TRY      0
#define KCS_SM_PROGDEV_OKAY     1
#define KCS_SM_PROGDEV_STOP     2

struct kcs_statemachine {
  int (**sm)(struct katcp_dispatch *,struct katcp_notice *, void *); 
  int state;
  struct katcp_notice *n;
  void *data; /*used to pass config data around no GC make sure to free when you use*/
};

int statemachine_greeting(struct katcp_dispatch *d);
int statemachine_cmd(struct katcp_dispatch *d, int argc);
int statemachine_ping(struct katcp_dispatch *d);
int statemachine_stop(struct katcp_dispatch *d);
int statemachine_connect(struct katcp_dispatch *d);
int statemachine_disconnect(struct katcp_dispatch *d);
int statemachine_progdev(struct katcp_dispatch *d);
int statemachine_poweron(struct katcp_dispatch *d);
int statemachine_poweroff(struct katcp_dispatch *d);
int statemachine_powersoft(struct katcp_dispatch *d);
//void statemachine_destroy(struct katcp_dispatch *d);
void destroy_last_roach_ksm_kcs(struct kcs_roach *kr);
void destroy_ksm_kcs(struct kcs_statemachine *ksm);


struct katcp_job * run_child_process_kcs(struct katcp_dispatch *d, struct katcp_url *url, int (*call)(struct katcl_line *, void *), void *data, struct katcp_notice *n);
int xport_sync_connect_and_start_subprocess_kcs(struct katcl_line *l, void *data);
int xport_sync_connect_and_stop_subprocess_kcs(struct katcl_line *l, void *data);
int xport_sync_connect_and_soft_restart_subprocess_kcs(struct katcl_line *l, void *data);

int udpear_cmd(struct katcp_dispatch *d, int argc);
#endif
