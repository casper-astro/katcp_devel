#ifndef _KATPRIV_H_
#define _KATPRIV_H_

#include <signal.h>

#include <sys/time.h>
#include <sys/types.h>

#define KATCP_NAME_LENGTH  64

struct katcl_larg{
  unsigned int a_begin;
  unsigned int a_end;
};

struct katcl_msg{
  struct katcl_line *m_line;

  char *m_buffer;
  unsigned int m_size;
  unsigned int m_want;
  int m_tag;
  int m_complete;
};

struct katcl_line{
  int l_fd;

  char *l_input;
  unsigned int l_isize;
  unsigned int l_ihave;
  unsigned int l_iused;
  unsigned int l_ikept;
  int l_itag;

  struct katcl_larg *l_args;
  struct katcl_larg *l_current;
  unsigned int l_asize;
  unsigned int l_ahave;

#if 0
  char *l_output;
  unsigned int l_osize;
  unsigned int l_owant;
  unsigned int l_odone;
  int l_otag;
  int l_ocomplete;
#endif

  struct katcl_msg *l_out;
  unsigned int l_odone;

  int l_error;
  int l_problem;
  unsigned int l_state;
};

struct katcp_dispatch;

struct katcp_cmd{
  char *c_name;
  char *c_help;
  int (*c_call)(struct katcp_dispatch *d, int argc);
  struct katcp_cmd *c_next;
  unsigned int c_mode;
  unsigned int c_flags;
};

/**********************************************************************/

#define KATCP_SENSOR_INVALID (-1)
#define KATCP_SENSOR_INTEGER  0 
#define KATCP_SENSOR_BOOLEAN  1
#define KATCP_SENSOR_DISCRETE 2
#define KATCP_SENSOR_LRU      3

#ifdef KATCP_USE_FLOATS
#define KATCP_SENSOR_FLOAT    4
#define KATCP_SENSORS_COUNT   5
#else
#define KATCP_SENSORS_COUNT   4
#endif

struct katcp_sensor;
struct katcp_nonsense;

struct katcp_integer_acquire{
  int ia_current;
  int (*ia_get)(struct katcp_dispatch *d, void *local);
  void *ia_local;
};

struct katcp_acquire{
  struct katcp_sensor **a_sensors;
  unsigned int a_count;

  int a_type;
  int a_up;

  struct timeval a_poll;    /* rate at which we poll this sensor */
  struct timeval a_current; /* current rate */
  struct timeval a_limit;   /* fastest update rate */
  struct timeval a_last;    /* last time value was acquired */

  void *a_more; /* could be a union */
};

struct katcp_sensor{
  int s_magic;
  int s_type;
  char *s_name;
  char *s_description;
  char *s_units;

  int s_preferred;

  int s_status; /* WARNING, etc */
  int s_mode; /* in what mode available */
  struct timeval s_recent;
#if 0
  int s_running; /* have it do stuff */
#endif

  unsigned int s_refs;
  struct katcp_nonsense **s_nonsense;

  struct katcp_acquire *s_acquire;

  int (*s_extract)(struct katcp_dispatch *d, struct katcp_sensor *sn);
  void *s_more;
};

struct katcp_integer_sensor{
  int is_current;
  int is_min;
  int is_max;
};

struct katcp_nonsense{
  int n_magic;
  struct katcp_dispatch *n_client;
  struct katcp_sensor *n_sensor;
  int n_strategy;
  int n_status;
  struct timeval n_period;
  struct timeval n_next;
  int n_manual;

  void *n_more;
};

struct katcp_integer_nonsense{
  int in_previous;
  int in_delta;
};

#if 0
struct katcp_sensor_integer{
  int si_min;
  int si_max;
  int si_current;
  int (*si_get)(struct katcp_sensor *s, void *local);
};

struct katcp_sensor_discrete{
  int sd_current;
  char **sd_vector;
  unsigned int sd_size;
  int (*sd_get)(struct katcp_sensor *s, void *local);
};

struct katcp_nonsense_discrete{
  int nd_previous;
};

#ifdef KATCP_USE_FLOATS
struct katcp_sensor_float{
  double sf_min;
  double sf_max;
  double sf_current;
  double (*sf_get)(struct katcp_sensor *s, void *local);
};

struct katcp_nonsense_float{
  double nf_previous;
  double nf_delta;
};
#endif
#endif

/**********************************************************************/

struct katcp_entry{
  char *e_name;
  int (*e_enter)(struct katcp_dispatch *d, char *flags, unsigned int mode);
  void (*e_leave)(struct katcp_dispatch *d, unsigned int mode);
  void *e_state;
  void (*e_clear)(struct katcp_dispatch *d);
  char *e_version;
  unsigned int e_minor;
  unsigned int e_major;
};

#define KATCP_PS_UP    1
#define KATCP_PS_TERM  2

struct katcp_process{
  void (*p_call)(struct katcp_dispatch *d, int status);
  pid_t p_pid;
  char *p_name;
  int p_type;
  int p_state;
};

struct katcp_notice;

struct katcp_job{
  unsigned int j_magic;
  pid_t j_pid;

  int j_state; /* state machine */
  int j_status; /* exit code */

  struct katcl_line *j_line;

  struct katcp_notice *j_halt;

  struct katcp_notice **j_queue; 
  unsigned int j_size;
  unsigned int j_head; /* points at the current head */
  unsigned int j_count; /* number of entries present */
};

#if 0
#define KATCP_TIME_OTHER   0
#define KATCP_TIME_CURRENT 1
#define KATCP_TIME_REFRESH 2
#define KATCP_TIME_REAP    3
#endif

struct katcp_time{
  int t_magic;

  struct timeval t_when;
  struct timeval t_interval;

  int t_armed;

  void *t_data;
  int (*t_call)(struct katcp_dispatch *d, void *data);
};

struct katcp_invoke{
  struct katcp_dispatch *v_client;
  int (*v_call)(struct katcp_dispatch *d, struct katcp_notice *n);
};

struct katcp_notice{
  struct katcp_invoke *n_vector;
  unsigned int n_count;

  int n_trigger;
  char *n_name;

  int n_tag;
  struct katcl_msg *n_msg;
  void *n_target;
  int (*n_release)(struct katcp_dispatch *d, struct katcp_notice *n, void *target);
};

struct katcp_shared{
  unsigned int s_magic;
  struct katcp_entry *s_vector;
  unsigned int s_size;
  unsigned int s_modal;

  struct katcp_cmd *s_commands;
  unsigned int s_mode;

  struct katcp_dispatch *s_template;
  struct katcp_dispatch **s_clients;

  unsigned int s_count;
  unsigned int s_used;

  int s_lfd;

  struct katcp_job **s_tasks;
  unsigned int s_number;

#if 0
  struct katcp_process *s_table;
  int s_entries;
#endif

  struct katcp_time **s_queue;
  unsigned int s_length;

  struct katcp_notice **s_notices;
  unsigned int s_pending;

#if 0
  int s_version_major;
  int s_version_minor;
  char *s_version_subsystem; /* not ideally named */
#endif

  char **s_build_state;
  int s_build_items;

  struct katcp_sensor **s_sensors;
  unsigned int s_tally;

  sigset_t s_mask_current, s_mask_previous;
  struct sigaction s_action_current, s_action_previous;
  int s_restore_signals;

  fd_set s_read, s_write;
  int s_max;
};

struct katcp_dispatch{
  int d_level; /* log level */
  int d_ready;
  int d_run; /* 1 if up, -1 if shutting down, 0 if shut down */
  int d_exit; /* exit code, reason for shutting down */
  int d_pause; /* waiting for a notice */
  struct katcl_line *d_line;

  int (*d_current)(struct katcp_dispatch *d, int argc);

  struct katcp_shared *d_shared;

  struct katcp_nonsense **d_nonsense;
  unsigned int d_size;

  struct katcp_notice **d_notices;
  unsigned int d_count;

  int d_clone;

  char d_name[KATCP_NAME_LENGTH];
};

#define KATCP_BUFFER_INC 512
#define KATCP_ARGS_INC     8

void exchange_katcl(struct katcl_line *l, int fd);

void component_time_katcp(struct timeval *result, unsigned int ms);
int sub_time_katcp(struct timeval *delta, struct timeval *alpha, struct timeval *beta);
int add_time_katcp(struct timeval *sigma, struct timeval *alpha, struct timeval *beta);
int cmp_time_katcp(struct timeval *alpha, struct timeval *beta);

int startup_shared_katcp(struct katcp_dispatch *d);
void shutdown_shared_katcp(struct katcp_dispatch *d);
int listen_shared_katcp(struct katcp_dispatch *d, int count, char *host, int port);
int link_shared_katcp(struct katcp_dispatch *d, struct katcp_dispatch *cd);

int reap_shared_katcp(struct katcp_dispatch *d, pid_t pid, int status);

void shutdown_cmd_katcp(struct katcp_cmd *c);

int sensor_value_cmd_katcp(struct katcp_dispatch *d, int argc);
int sensor_list_cmd_katcp(struct katcp_dispatch *d, int argc);
int sensor_sampling_cmd_katcp(struct katcp_dispatch *d, int argc);
int sensor_dump_cmd_katcp(struct katcp_dispatch *d, int argc);

char *code_to_name_katcm(int code);

/* timing support */
int empty_timers_katcp(struct katcp_dispatch *d);
int run_timers_katcp(struct katcp_dispatch *d, struct timespec *interval);
void dump_timers_katcp(struct katcp_dispatch *d);

/* nonsense support */
void forget_nonsense_katcp(struct katcp_dispatch *d, unsigned int index);

/* how many times to try waitpid for child to exit */
#define KATCP_WAITPID_CHECKS 5 
/* how long to sleep between checks in nanoseconds */
#define KATCP_WAITPID_POLL   250000000UL

int child_signal_shared_katcp(struct katcp_shared *s);

int reap_children_shared_katcp(struct katcp_dispatch *d, pid_t pid, int force);
int init_signals_shared_katcp(struct katcp_shared *s);
int undo_signals_shared_katcp(struct katcp_shared *s);

/* notice logic */
void unlink_notices_katcp(struct katcp_dispatch *d);
void destroy_notices_katcp(struct katcp_dispatch *d);
int run_notices_katcp(struct katcp_dispatch *d);

int notice_cmd_katcp(struct katcp_dispatch *d, int argc);

/* jobs */
int load_jobs_katcp(struct katcp_dispatch *d);
int wait_jobs_katcp(struct katcp_dispatch *d);
int run_jobs_katcp(struct katcp_dispatch *d);

int job_cmd_katcp(struct katcp_dispatch *d, int argc);

#endif
