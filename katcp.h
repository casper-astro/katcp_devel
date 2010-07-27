#ifndef _KATCP_H_
#define _KATCP_H_

struct katcp_sensor;
struct katcp_nonsense;
struct katcp_acquire;
struct katcp_dispatch;
struct katcp_cmd;

#include <sys/types.h>
#include <stdarg.h>

#define KATCP_REQUEST '?' 
#define KATCP_REPLY   '!' 
#define KATCP_INFORM  '#' 

#define KATCP_OK      "ok"
#define KATCP_FAIL    "fail"
#define KATCP_INVALID "invalid"

#define KATCP_RESULT_RESUME    2
#define KATCP_RESULT_OWN       1
#define KATCP_RESULT_OK        0
#define KATCP_RESULT_FAIL    (-1)
#define KATCP_RESULT_INVALID (-2)

#define KATCP_LEVEL_TRACE    0
#define KATCP_LEVEL_DEBUG    1
#define KATCP_LEVEL_INFO     2
#define KATCP_LEVEL_WARN     3
#define KATCP_LEVEL_ERROR    4
#define KATCP_LEVEL_FATAL    5
#define KATCP_LEVEL_OFF      6
#define KATCP_MAX_LEVELS     7

#define KATCP_MASK_LEVELS   0xff
#define KATCP_LEVEL_LOCAL 0x100

#define KATCP_FLAG_FIRST  0x01
#define KATCP_FLAG_LAST   0x02
#define KATCP_FLAG_MORE   0x04

#define KATCP_FLAG_STRING 0x10
#define KATCP_FLAG_ULONG  0x20
#define KATCP_FLAG_SLONG  0x30
#define KATCP_FLAG_XLONG  0x40
#define KATCP_FLAG_BUFFER 0x50
#ifdef KATCP_USE_FLOATS
#define KATCP_FLAG_DOUBLE 0x60
#endif

#define KATCP_TYPE_FLAGS  0xf0
#define KATCP_ORDER_FLAGS 0x0f

#define KATCP_EXIT_NOTYET  0
#define KATCP_EXIT_QUIT    1
#define KATCP_EXIT_HALT    2
#define KATCP_EXIT_RESTART 3
#define KATCP_EXIT_ABORT  10

#define KATCP_CMD_HIDDEN    0x1
#define KATCP_CMD_WILDCARD  0x2

/******************* core api ********************/

/* create a dispatch handler */
struct katcp_dispatch *startup_katcp(void);
/* create a dispatch handler on file descriptor */
struct katcp_dispatch *setup_katcp(int fd); 

#include <stdarg.h>
int name_katcp(struct katcp_dispatch *d, char *fmt, ...);

/* make a copy of of an instance */
struct katcp_dispatch *clone_katcp(struct katcp_dispatch *cd);

/* destroy handler */
void shutdown_katcp(struct katcp_dispatch *d);

/* set build and version fields, needed for welcome banner */
int mode_version_katcp(struct katcp_dispatch *d, int mode, char *subsystem, int major, int minor);
int version_katcp(struct katcp_dispatch *d, char *subsystem, int major, int minor);
int add_build_katcp(struct katcp_dispatch *d, char *build);
int del_build_katcp(struct katcp_dispatch *d, int index);

/* make life easier for users, let them store their state here */
void *get_code_katcp(struct katcp_dispatch *d, unsigned int index);
int set_code_katcp(struct katcp_dispatch *d, unsigned int index, void *state);
int set_clear_code_katcp(struct katcp_dispatch *d, unsigned int index, void *p, void (*clear)(struct katcp_dispatch *d));

void *get_state_katcp(struct katcp_dispatch *d);
void set_state_katcp(struct katcp_dispatch *d, void *p);
void set_clear_state_katcp(struct katcp_dispatch *d, void *p, void (*clear)(struct katcp_dispatch *d));

/* prints version information, to be called on incomming connection */
void on_connect_katcp(struct katcp_dispatch *d);
/* print explanation on ending connection, if null decodes exit code */
void on_disconnect_katcp(struct katcp_dispatch *d, char *fmt, ...);

/* add a callback to handler (match includes type), help the help message */
int register_katcp(struct katcp_dispatch *d, char *match, char *help, int (*call)(struct katcp_dispatch *d, int argc));
/* if mode > 1, this command is only available if in that mode, 0 implies always */
int register_mode_katcp(struct katcp_dispatch *d, char *match, char *help, int (*call)(struct katcp_dispatch *d, int argc), int mode);
int register_flag_mode_katcp(struct katcp_dispatch *d, char *match, char *help, int (*call)(struct katcp_dispatch *d, int argc), int flags, int mode);

/* invoke function run as subprocess, invoke function call in parent on its exit */ 
pid_t spawn_child_katcp(struct katcp_dispatch *d, char *name, int (*run)(void *data), void *data, void (*call)(struct katcp_dispatch *d, int status));

/* change the callback in mid-flight */
int continue_katcp(struct katcp_dispatch *d, int when, int (*call)(struct katcp_dispatch *d, int argc));

/* see if there is a command, then parse it */
int lookup_katcp(struct katcp_dispatch *d);
/* invoke command for a quantum */
int call_katcp(struct katcp_dispatch *d);
/* combines lookup and call while there is stuff to do */
int dispatch_katcp(struct katcp_dispatch *d);

/* run the dispatch handler until error or shutdown */
int run_katcp(struct katcp_dispatch *d, int server, char *host, int port);
int run_client_katcp(struct katcp_dispatch *d, char *host, int port);
int run_server_katcp(struct katcp_dispatch *d, char *host, int port);

int run_multi_server_katcp(struct katcp_dispatch *dl, int count, char *host, int port);

/******************* io functions ****************/

int fileno_katcp(struct katcp_dispatch *d);
struct katcl_line *line_katcp(struct katcp_dispatch *d);
int read_katcp(struct katcp_dispatch *d);
int have_katcp(struct katcp_dispatch *d);
int flushing_katcp(struct katcp_dispatch *d);
int flush_katcp(struct katcp_dispatch *d);
int write_katcp(struct katcp_dispatch *d);
void reset_katcp(struct katcp_dispatch *d, int fd);

/******************* read arguments **************/

int arg_request_katcp(struct katcp_dispatch *d);
int arg_reply_katcp(struct katcp_dispatch *d);
int arg_inform_katcp(struct katcp_dispatch *d);

unsigned int arg_count_katcp(struct katcp_dispatch *d);
int arg_null_katcp(struct katcp_dispatch *d, unsigned int index);

char *arg_string_katcp(struct katcp_dispatch *d, unsigned int index);
char *arg_copy_string_katcp(struct katcp_dispatch *d, unsigned int index);
unsigned long arg_unsigned_long_katcp(struct katcp_dispatch *d, unsigned int index);
unsigned int arg_buffer_katcp(struct katcp_dispatch *d, unsigned int index, void *buffer, unsigned int size);
#ifdef KATCP_USE_FLOATS
double arg_double_katcp(struct katcp_dispatch *d, unsigned int index);
#endif

/******************* write arguments *************/

int prepend_reply_katcp(struct katcp_dispatch *d);
int prepend_inform_katcp(struct katcp_dispatch *d);

int append_string_katcp(struct katcp_dispatch *d, int flags, char *buffer);
int append_unsigned_long_katcp(struct katcp_dispatch *d, int flags, unsigned long v);
int append_signed_long_katcp(struct katcp_dispatch *d, int flags, unsigned long v);
int append_hex_long_katcp(struct katcp_dispatch *d, int flags, unsigned long v);
int append_buffer_katcp(struct katcp_dispatch *d, int flags, void *buffer, int len);
int append_vargs_katcp(struct katcp_dispatch *d, int flags, char *fmt, va_list args);
int append_args_katcp(struct katcp_dispatch *d, int flags, char *fmt, ...);
#ifdef KATCP_USE_FLOATS
int append_double_katcp(struct katcp_dispatch *d, int flags, double v);
#endif

/* sensor writes */
#if 0
int append_sensor_type_katcp(struct katcp_dispatch *d, int flags, struct katcp_sensor *s);
int append_sensor_value_katcp(struct katcp_dispatch *d, int flags, struct katcp_sensor *s);
int append_sensor_delta_katcp(struct katcp_dispatch *d, int flags, struct katcp_sensor *s);
#endif

/******************* write list of arguments *****/

int vsend_katcp(struct katcp_dispatch *d, va_list ap);
int send_katcp(struct katcp_dispatch *d, ...);

/******************* utility functions ***********/

int log_message_katcp(struct katcp_dispatch *d, unsigned int priority, char *name, char *fmt, ...);
int extra_response_katcp(struct katcp_dispatch *d, int code, char *fmt, ...);
int broadcast_inform_katcp(struct katcp_dispatch *d, char *name, char *arg);

int error_katcp(struct katcp_dispatch *d);

int terminate_katcp(struct katcp_dispatch *d, int code); /* request a quit, after this exited will be true, and exiting will be true once */
int exited_katcp(struct katcp_dispatch *d); /* has this client requested a quit */
int exiting_katcp(struct katcp_dispatch *d); /* run cleanup functions once */

/******************* sensor work *****************/

#define KATCP_STRATEGY_OFF     0
#define KATCP_STRATEGY_PERIOD  1
#define KATCP_STRATEGY_EVENT   2
#define KATCP_STRATEGY_DIFF    3
#define KATCP_STRATEGY_FORCED  4
#define KATCP_STRATEGIES_COUNT 5

#define KATCP_STATUS_UNKNOWN   0
#define KATCP_STATUS_NOMINAL   1
#define KATCP_STATUS_WARN      2
#define KATCP_STATUS_ERROR     3
#define KATCP_STATUS_FAILURE   4
#define KATCP_STATA_COUNT      5

#define KATCP_PHASE_PREPARE    0
#define KATCP_PHASE_ACQUIRE    1
#define KATCP_PHASES_COUNT     2


void adjust_acquire_katcp(struct katcp_acquire *a, struct timeval *defpoll, struct timeval *maxrate);

int register_integer_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int (*get)(struct katcp_dispatch *d, void *local), void *local, int min, int max);
int register_multi_integer_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int min, int max, struct katcp_acquire *a, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn));
struct katcp_acquire *setup_integer_acquire_katcp(struct katcp_dispatch *d, int (*get)(struct katcp_dispatch *d, void *local), void *local);

void destroy_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a);


int register_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int (*get)(struct katcp_dispatch *d, void *local), void *local);
int register_direct_multi_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, struct katcp_acquire *a);
int register_invert_multi_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, struct katcp_acquire *a);
int register_multi_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, struct katcp_acquire *a, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn));
struct katcp_acquire *setup_boolean_acquire_katcp(struct katcp_dispatch *d, int (*get)(struct katcp_dispatch *d, void *local), void *local);

int set_status_sensor_katcp(struct katcp_sensor *sn, int status);

#if 0
int register_discrete_sensor_katcp(struct katcp_dispatch *d, char *name, char *description, char *units, int preferred, int (*get)(struct katcp_sensor *s, void *local), ...);
int register_lru_sensor_katcp(struct katcp_dispatch *d, char *name, char *description, char *units, int preferred, int (*get)(struct katcp_sensor *s, void *local));
#ifdef KATCP_USE_FLOATS
int register_double_sensor_katcp(struct katcp_dispatch *d, char *name, char *description, char *units, int preferred, double (*get)(struct katcp_sensor *s, void *local), double min, double max);
#endif

#endif

int store_full_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *name, int (*enter)(struct katcp_dispatch *d, char *flags, unsigned int from), void (*leave)(struct katcp_dispatch *d, unsigned int to), void *state, void (*clear)(struct katcp_dispatch *d));
int store_mode_katcp(struct katcp_dispatch *d, unsigned int mode, void *state);
int store_clear_mode_katcp(struct katcp_dispatch *d, unsigned int mode, void *state, void (*clear)(struct katcp_dispatch *d));

int is_mode_katcp(struct katcp_dispatch *d, unsigned int mode);
void *get_mode_katcp(struct katcp_dispatch *d, unsigned int mode);
void *get_current_mode_katcp(struct katcp_dispatch *d);
int enter_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *flags);
int enter_name_mode_katcp(struct katcp_dispatch *d, char *name, char *flags);
int query_mode_katcp(struct katcp_dispatch *d);
char *query_mode_name_katcp(struct katcp_dispatch *d);
void *need_current_mode_katcp(struct katcp_dispatch *d, unsigned int mode);

/* timing callbacks */

int discharge_timer_katcp(struct katcp_dispatch *d, void *data);
int unwarp_timers_katcp(struct katcp_dispatch *d);
int register_every_ms_katcp(struct katcp_dispatch *d, unsigned int milli, int (*call)(struct katcp_dispatch *d, void *data), void *data);
int register_every_tv_katcp(struct katcp_dispatch *d, struct timeval *tv, int (*call)(struct katcp_dispatch *d, void *data), void *data);
int register_at_tv_katcp(struct katcp_dispatch *d, struct timeval *tv, int (*call)(struct katcp_dispatch *d, void *data), void *data);

int watch_shared_katcp(struct katcp_dispatch *d, char *name, pid_t pid, void (*call)(struct katcp_dispatch *d, int status));
int watch_type_shared_katcp(struct katcp_dispatch *d, char *name, pid_t pid, int type, void (*call)(struct katcp_dispatch *d, int status));

int end_name_shared_katcp(struct katcp_dispatch *d, char *name, int force);
int end_pid_shared_katcp(struct katcp_dispatch *d, pid_t pid, int force);
int end_type_shared_katcp(struct katcp_dispatch *d, int type, int force);


#endif
