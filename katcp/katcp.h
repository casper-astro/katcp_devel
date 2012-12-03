#ifndef _KATCP_H_
#define _KATCP_H_

struct katcl_line;
struct katcl_msg;
struct katcl_parse;
struct katcl_byte_bit;

struct katcp_sensor;
struct katcp_nonsense;
struct katcp_acquire;
struct katcp_dispatch;
struct katcp_cmd;
struct katcp_job;

struct katcp_notice;

struct katcp_url;

#include <sys/types.h>
#include <stdarg.h>

#define KATCP_CODEBASE_NAME     "libkatcp" 

#define KATCP_LIBRARY_LABEL     "katcp-library"
#define KATCP_PROTOCOL_LABEL    "katcp-protocol"

#define KATCP_REQUEST '?' 
#define KATCP_REPLY   '!' 
#define KATCP_INFORM  '#' 

#define KATCP_OK      "ok"
#define KATCP_FAIL    "fail"
#define KATCP_INVALID "invalid"
#define KATCP_PARTIAL "partial"

#define KATCP_RESULT_PAUSE     3    /* stop, do not parse more until resumed */
#define KATCP_RESULT_YIELD     2    /* allow others to run, then run again */
#define KATCP_RESULT_OWN       1
/* return codes actually seen on the wire have to have a value <= 0 */
#define KATCP_RESULT_OK        0
#define KATCP_RESULT_FAIL    (-1)
#define KATCP_RESULT_INVALID (-2)
#define KATCP_RESULT_PARTIAL (-3)

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
#if 0
#define KATCP_FLAG_MORE   0x04
#endif

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

#define KATCP_DICT_REQUEST    "?dict"
#define KATCP_SET_REQUEST     "?set"
#define KATCP_GET_REQUEST     "?get"
#define KATCP_SEARCH_REQUEST  "?search"

#define KATCP_LOG_INFORM               "#log"
#define KATCP_DEVICE_CHANGED_INFORM    "#device-changed"
#define KATCP_HELP_INFORM              "#help"

#define KATCP_SENSOR_LIST_INFORM       "#sensor-list"
#define KATCP_SENSOR_STATUS_INFORM     "#sensor-status"
#define KATCP_SENSOR_VALUE_INFORM      "#sensor-value"

#define KATCP_DISCONNECT_INFORM        "#disconnect"

#define KATCP_RETURN_JOB      "#return"
#define KATCP_WAKE_TIMEOUT    "#timeout"

#define KATCP_VERSION_LIST_INFORM      "#version-list"
#define KATCP_VERSION_CONNECT_INFORM   "#version-connect"

#define KATCP_BUILD_STATE_INFORM       "#build-state"
#define KATCP_VERSION_INFORM           "#version"

#define KATCP_CLIENT_CONNECT           "#client-connected"
#define KATCP_CLIENT_DISCONNECT        "#client-disconnected"

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
int build_katcp(struct katcp_dispatch *d, char *build);

/* make life easier for users, let them store their state here */
void *get_code_katcp(struct katcp_dispatch *d, unsigned int index);
int set_code_katcp(struct katcp_dispatch *d, unsigned int index, void *state);
int set_clear_code_katcp(struct katcp_dispatch *d, unsigned int index, void *p, void (*clear)(struct katcp_dispatch *d));

void *get_state_katcp(struct katcp_dispatch *d);
void set_state_katcp(struct katcp_dispatch *d, void *p);
void set_clear_state_katcp(struct katcp_dispatch *d, void *p, void (*clear)(struct katcp_dispatch *d, unsigned int mode));

/* prints version information, to be called on incomming connection */
void on_connect_katcp(struct katcp_dispatch *d);
/* print explanation on ending connection, if null decodes exit code */
void on_disconnect_katcp(struct katcp_dispatch *d, char *fmt, ...);

/* add a callback to handler (match includes type), help the help message */
int register_katcp(struct katcp_dispatch *d, char *match, char *help, int (*call)(struct katcp_dispatch *d, int argc));
/* if mode > 1, this command is only available if in that mode, 0 implies always */
int register_mode_katcp(struct katcp_dispatch *d, char *match, char *help, int (*call)(struct katcp_dispatch *d, int argc), int mode);
int register_flag_mode_katcp(struct katcp_dispatch *d, char *match, char *help, int (*call)(struct katcp_dispatch *d, int argc), int flags, int mode);
int deregister_command_katcp(struct katcp_dispatch *d, char *match);
int update_command_katcp(struct katcp_dispatch *d, char *match, int flags);

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

void resume_katcp(struct katcp_dispatch *d);

/* run the dispatch handler until error or shutdown */
int run_katcp(struct katcp_dispatch *d, int server, char *host, int port);
int run_client_katcp(struct katcp_dispatch *d, char *host, int port);
int run_server_katcp(struct katcp_dispatch *d, char *host, int port);
int run_multi_server_katcp(struct katcp_dispatch *d, int count, char *host, int port);

int run_config_server_katcp(struct katcp_dispatch *dl, char *file, int count, char *host, int port);
int run_pipe_server_katcp(struct katcp_dispatch *dl, char *file, int pfd);

/******************* io functions ****************/

int fileno_katcp(struct katcp_dispatch *d);
struct katcl_line *line_katcp(struct katcp_dispatch *d);
int read_katcp(struct katcp_dispatch *d);
int have_katcp(struct katcp_dispatch *d);
int flushing_katcp(struct katcp_dispatch *d);
int flush_katcp(struct katcp_dispatch *d);
int write_katcp(struct katcp_dispatch *d);
void reset_katcp(struct katcp_dispatch *d, int fd);

int load_from_file_katcp(struct katcp_dispatch *d, char *file);

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
int arg_byte_bit_katcp(struct katcp_dispatch *d, unsigned int index, struct katcl_byte_bit *b);
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
int append_parameter_katcp(struct katcp_dispatch *d, int flags, struct katcl_parse *p, unsigned int index);
int append_parse_katcp(struct katcp_dispatch *d, struct katcl_parse *p);

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

int name_log_level_katcp(struct katcp_dispatch *d, char *name);
int log_level_katcp(struct katcp_dispatch *d, unsigned int level);
int log_message_katcp(struct katcp_dispatch *d, unsigned int priority, char *name, char *fmt, ...);
int log_relay_katcp(struct katcp_dispatch *d, struct katcl_parse *p);

int extra_response_katcp(struct katcp_dispatch *d, int code, char *fmt, ...);
int basic_inform_katcp(struct katcp_dispatch *d, char *name, char *arg);
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
#if KATCP_PROTOCOL_MAJOR_VERSION >= 5   
#define   KATCP_STATUS_UNREACHABLE 5
#define   KATCP_STATUS_INACTIVE    6
#define   KATCP_STATA_COUNT        7
#else
#define   KATCP_STATA_COUNT        5
#endif

#if 0 /* unclear where this was used */
#define KATCP_PHASE_PREPARE    0
#define KATCP_PHASE_ACQUIRE    1
#define KATCP_PHASES_COUNT     2
#endif

void destroy_sensors_katcp(struct katcp_dispatch *d);
void destroy_nonsensors_katcp(struct katcp_dispatch *d);

void destroy_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a);

struct katcp_sensor *find_sensor_katcp(struct katcp_dispatch *d, char *name);

int set_status_sensor_katcp(struct katcp_sensor *sn, int status);
int set_status_group_sensor_katcp(struct katcp_dispatch *d, char *prefix, int status);

void *get_local_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a);
void generic_release_local_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a);

struct katcp_acquire *acquire_from_sensor_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn);

int is_up_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a);

void adjust_acquire_katcp(struct katcp_acquire *a, struct timeval *defpoll, struct timeval *maxrate);
int propagate_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a);

/****************************************************************************/

int declare_integer_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a), int nom_min, int nom_max, int warn_min, int warn_max, int (*flush)(struct katcp_dispatch *d, struct katcp_sensor *sn));
int register_integer_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a), int min, int max, int (*flush)(struct katcp_dispatch *d, struct katcp_sensor *sn));

int declare_multi_integer_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int nom_min, int nom_max, int warn_min, int warn_max, struct katcp_acquire *a, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn), int (*flush)(struct katcp_dispatch *d, struct katcp_sensor *sn));
int register_multi_integer_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int min, int max, struct katcp_acquire *a, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn), int (*flush)(struct katcp_dispatch *d, struct katcp_sensor *sn));

struct katcp_acquire *setup_integer_acquire_katcp(struct katcp_dispatch *d, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a));
int set_integer_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, int value);

/****************************************************************************/

int register_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a));

int register_direct_multi_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, struct katcp_acquire *a);
int register_invert_multi_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, struct katcp_acquire *a);
int register_multi_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, struct katcp_acquire *a, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn));

struct katcp_acquire *setup_boolean_acquire_katcp(struct katcp_dispatch *d, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a));

int set_boolean_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, int value);

/****************************************************************************/

#ifdef KATCP_USE_FLOATS

int declare_double_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, double (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a), double nom_min, double nom_max, double warn_min, double warn_max, int (*flush)(struct katcp_dispatch *d, struct katcp_sensor *sn));
int register_double_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, double (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a), double min, double max, int (*flush)(struct katcp_dispatch *d, struct katcp_sensor *sn));

int declare_multi_double_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, double nom_min, double nom_max, double warn_min, double warn_max, struct katcp_acquire *a, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn), int (*flush)(struct katcp_dispatch *d, struct katcp_sensor *sn));
int register_multi_double_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, double min, double max, struct katcp_acquire *a, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn), int (*flush)(struct katcp_dispatch *d, struct katcp_sensor *sn));

struct katcp_acquire *setup_double_acquire_katcp(struct katcp_dispatch *d, double (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a));
int set_double_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, double value);

#endif

/***************************************************************************/

int register_discrete_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a), char **vector, unsigned int size);

int expand_sensor_discrete_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn, unsigned int position, char *name);

struct katcp_acquire *setup_discrete_acquire_katcp(struct katcp_dispatch *d, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a));

int set_discrete_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, unsigned value);

/***************************************************************************/



#if 0
int register_discrete_sensor_katcp(struct katcp_dispatch *d, char *name, char *description, char *units, int preferred, int (*get)(struct katcp_sensor *s, void *local), ...);
int register_lru_sensor_katcp(struct katcp_dispatch *d, char *name, char *description, char *units, int preferred, int (*get)(struct katcp_sensor *s, void *local));
#ifdef KATCP_USE_FLOATS
int register_double_sensor_katcp(struct katcp_dispatch *d, char *name, char *description, char *units, int preferred, double (*get)(struct katcp_sensor *s, void *local), double min, double max);
#endif

#endif

#if 0
int job_match_sensor_katcp(struct katcp_dispatch *d, struct katcp_job *j);
#endif

int job_enable_sensor_katcp(struct katcp_dispatch *d, struct katcp_job *j);
int job_suspend_sensor_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data);

/***************************************************************************/

struct katcp_dispatch *template_shared_katcp(struct katcp_dispatch *d);

int store_sensor_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *name, struct katcp_notice *(*prepare)(struct katcp_dispatch *d, char *flags, unsigned int from, unsigned int to), int (*enter)(struct katcp_dispatch *d, struct katcp_notice *n, char *flags, unsigned int to), void (*leave)(struct katcp_dispatch *d, unsigned int to), void *state, void (*clear)(struct katcp_dispatch *d, unsigned int mode), unsigned int status);
int store_prepared_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *name, struct katcp_notice *(*prepare)(struct katcp_dispatch *d, char *flags, unsigned int from, unsigned int to), int (*enter)(struct katcp_dispatch *d, struct katcp_notice *n, char *flags, unsigned int to), void (*leave)(struct katcp_dispatch *d, unsigned int to), void *state, void (*clear)(struct katcp_dispatch *d, unsigned int mode));
int store_full_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *name, int (*enter)(struct katcp_dispatch *d, struct katcp_notice *n, char *flags, unsigned int to), void (*leave)(struct katcp_dispatch *d, unsigned int to), void *state, void (*clear)(struct katcp_dispatch *d, unsigned int mode));
int store_mode_katcp(struct katcp_dispatch *d, unsigned int mode, void *state);
int store_clear_mode_katcp(struct katcp_dispatch *d, unsigned int mode, void *state, void (*clear)(struct katcp_dispatch *d, unsigned int mode));

int is_mode_katcp(struct katcp_dispatch *d, unsigned int mode);
void *get_mode_katcp(struct katcp_dispatch *d, unsigned int mode);
void *get_current_mode_katcp(struct katcp_dispatch *d);
int enter_mode_katcp(struct katcp_dispatch *d, unsigned int mode, char *flags);
int enter_name_mode_katcp(struct katcp_dispatch *d, char *name, char *flags);
int query_mode_katcp(struct katcp_dispatch *d);
char *query_mode_name_katcp(struct katcp_dispatch *d);
int query_mode_code_katcp(struct katcp_dispatch *d, char *name);
void *need_current_mode_katcp(struct katcp_dispatch *d, unsigned int mode);

/* intercept any command, needed in tcpborphserver for trivial reasons */

#define KATCP_HOOK_PRE  0
#define KATCP_HOOK_POST 1

int hook_commands_katcp(struct katcp_dispatch *d, unsigned int type, int (*hook)(struct katcp_dispatch *d, int argc));

/* timing callbacks */

int discharge_timer_katcp(struct katcp_dispatch *d, void *data);
int unwarp_timers_katcp(struct katcp_dispatch *d);
int register_every_ms_katcp(struct katcp_dispatch *d, unsigned int milli, int (*call)(struct katcp_dispatch *d, void *data), void *data);
int register_every_tv_katcp(struct katcp_dispatch *d, struct timeval *tv, int (*call)(struct katcp_dispatch *d, void *data), void *data);
int register_at_tv_katcp(struct katcp_dispatch *d, struct timeval *tv, int (*call)(struct katcp_dispatch *d, void *data), void *data);
int register_in_tv_katcp(struct katcp_dispatch *d, struct timeval *tv, int (*call)(struct katcp_dispatch *d, void *data), void *data);

int wake_notice_at_tv_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct timeval *tv);
int wake_notice_in_tv_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct timeval *tv);
int wake_notice_every_tv_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct timeval *tv);

int watch_shared_katcp(struct katcp_dispatch *d, char *name, pid_t pid, void (*call)(struct katcp_dispatch *d, int status));
int watch_type_shared_katcp(struct katcp_dispatch *d, char *name, pid_t pid, int type, void (*call)(struct katcp_dispatch *d, int status));

int end_name_shared_katcp(struct katcp_dispatch *d, char *name, int force);
int end_pid_shared_katcp(struct katcp_dispatch *d, pid_t pid, int force);
int end_type_shared_katcp(struct katcp_dispatch *d, int type, int force);

/* notice logic */

struct katcp_notice *create_notice_katcp(struct katcp_dispatch *d, char *name, unsigned int tag);
struct katcp_notice *create_parse_notice_katcp(struct katcp_dispatch *d, char *name, unsigned int tag, struct katcl_parse *p);
#if 0
static void destroy_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n);
#endif

int add_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data);
int remove_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data);
unsigned int fetch_data_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void **vector, unsigned int size);

struct katcp_notice *register_notice_katcp(struct katcp_dispatch *d, char *name, unsigned int tag, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data);
struct katcp_notice *register_parse_notice_katcp(struct katcp_dispatch *d, char *name, struct katcl_parse *p, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data);

#if 0
int code_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n);
char *code_name_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n);
void forget_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n);
#endif

struct katcp_notice *find_notice_katcp(struct katcp_dispatch *d, char *name);
int find_prefix_notices_katcp(struct katcp_dispatch *d, char *prefix, struct katcp_notice **n_set, int n_count);
struct katcp_notice *find_used_notice_katcp(struct katcp_dispatch *d, char *name);
int has_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data);

char *path_from_notice_katcp(struct katcp_notice *n, char *suffix, int flags);

void release_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n);
void hold_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n);


#define KATCP_NOTICE_TRIGGER_OFF     0
#define KATCP_NOTICE_TRIGGER_ALL     1
#define KATCP_NOTICE_TRIGGER_SINGLE  2

#if 0
void update_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p, int wake, int forget, void *data);
#endif
int trigger_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n);
int trigger_single_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data);

void wake_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p);
int wake_name_notice_katcp(struct katcp_dispatch *d, char *name, struct katcl_parse *p);

void wake_single_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p, void *data);
int wake_single_name_notice_katcp(struct katcp_dispatch *d, char *name, struct katcl_parse *p, void *data);

int change_name_notice_katcp(struct katcp_dispatch *d, char *name, char *newname);
int rename_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, char *newname);

struct katcl_parse *get_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n);
int set_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p);
int add_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n, struct katcl_parse *p);
struct katcl_parse *remove_parse_notice_katcp(struct katcp_dispatch *d, struct katcp_notice *n);

/* job logic */

struct katcp_job *create_job_katcp(struct katcp_dispatch *d, struct katcp_url *name, pid_t pid, int fd, int async, struct katcp_notice *halt);
struct katcp_job *via_notice_job_katcp(struct katcp_dispatch *d, struct katcp_notice *n);

#if 0
struct katcp_job *process_create_job_katcp(struct katcp_dispatch *d, struct katcp_url *file, char **argv, struct katcp_notice *halt);
#endif

struct katcp_job *process_relay_create_job_katcp(struct katcp_dispatch *d, struct katcp_url *file, char **argv, struct katcp_notice *halt, struct katcp_notice *relay);
struct katcp_job *process_name_create_job_katcp(struct katcp_dispatch *d, char *cmd, char **argv, struct katcp_notice *halt, struct katcp_notice *relay);

struct katcp_job *network_connect_job_katcp(struct katcp_dispatch *d, struct katcp_url *url, struct katcp_notice *halt);
struct katcp_job *network_name_connect_job_katcp(struct katcp_dispatch *d, char *host, int port, struct katcp_notice *halt);

struct katcp_job *find_job_katcp(struct katcp_dispatch *d, char *name);
struct katcp_job *find_containing_job_katcp(struct katcp_dispatch *d, char *name);
int zap_job_katcp(struct katcp_dispatch *d, struct katcp_job *j);

int match_notice_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, char *match, struct katcp_notice *n);
int match_inform_job_katcp(struct katcp_dispatch *d, struct katcp_job *j, char *match, int (*call)(struct katcp_dispatch *d, struct katcp_notice *n, void *data), void *data);

#if 0
int stop_job_katcp(struct katcp_dispatch *d, struct katcp_job *j);
#endif

void destroy_kurl_katcp(struct katcp_url *ku);
char *copy_kurl_string_katcp(struct katcp_url *ku, char *path);
char *add_kurl_path_copy_string_katcp(struct katcp_url *ku, char *npath);
struct katcp_url *create_kurl_from_string_katcp(char *url);
struct katcp_url *create_kurl_katcp(char *scheme, char *host, int port, char *path);
struct katcp_url *create_exec_kurl_katcp(char *cmd);
int containing_kurl_katcp(struct katcp_url *ku, char *string);

/* version support logic */
void destroy_versions_katcp(struct katcp_dispatch *d);
int remove_version_katcp(struct katcp_dispatch *d, char *label);
int add_version_katcp(struct katcp_dispatch *d, char *label, unsigned int mode, char *value, char *build);

int add_kernel_version_katcp(struct katcp_dispatch *d);
int add_code_version_katcp(struct katcp_dispatch *d);

#ifdef VERSION
#define check_code_version_katcp(d) has_code_version_katcp(d, KATCP_LIBRARY_LABEL, VERSION)
#endif
int has_code_version_katcp(struct katcp_dispatch *d, char *label, char *value);

int print_versions_katcp(struct katcp_dispatch *d, int initial);
int version_cmd_katcp(struct katcp_dispatch *d, int argc);
int version_list_cmd_katcp(struct katcp_dispatch *d, int argc);

/* arbitrary function callbacks */

#define KATCP_ARB_READ  0x1
#define KATCP_ARB_WRITE 0x2
#define KATCP_ARB_BOTH  (KATCP_ARB_READ | KATCP_ARB_WRITE)

struct katcp_arb *create_arb_katcp(struct katcp_dispatch *d, char *name, int fd, unsigned int mode, int (*run)(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode), void *data);
int unlink_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a);

struct katcp_arb *find_arb_katcp(struct katcp_dispatch *d, char *name);

void mode_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode);
void *data_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a);
char *name_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a);
int fileno_arb_katcp(struct katcp_dispatch *d, struct katcp_arb *a);


/*katcp_type functions*/

#define KATCP_DEP_BASE          0

#define KATCP_TYPE_DBASE        "db"
#define KATCP_TYPE_DICT         "dict"
#define KATCP_TYPE_SCHEMA       "schema"
#define KATCP_TYPE_STRING       "string"
#define KATCP_TYPE_TAG          "tag"


struct katcp_type;

int store_data_at_type_katcp(struct katcp_dispatch *d, struct katcp_type *t, int dep, char *d_name, void *d_data, void (*fn_print)(struct katcp_dispatch *, char *key, void *), void (*fn_free)(void *), int (*fn_copy)(void *, void *, int), int (*fn_compare)(const void *, const void *), void *(*fn_parse)(struct katcp_dispatch *d, char **), char *(*fn_getkey)(void *));
int store_data_type_katcp(struct katcp_dispatch *d, char *t_name, int dep, char *d_name, void *d_data, void (*fn_print)(struct katcp_dispatch *, char *key, void *), void (*fn_free)(void *), int (*fn_copy)(void *, void *, int), int (*fn_compare)(const void *, const void *), void *(*fn_parse)(struct katcp_dispatch *d, char **), char *(*fn_getkey)(void *));
int register_name_type_katcp(struct katcp_dispatch *d, char *name, int dep, void (*fn_print)(struct katcp_dispatch *, char *key, void *), void (*fn_free)(void *), int (*fn_copy)(void *, void *, int), int (*fn_compare)(const void *, const void *), void *(*fn_parse)(struct katcp_dispatch *d, char **), char *(*fn_getkey)(void *));
int deregister_type_katcp(struct katcp_dispatch *d, char *name);
int find_name_id_type_katcp(struct katcp_dispatch *d, char *type);
struct katcp_type *find_name_type_katcp(struct katcp_dispatch *d, char *str);
struct katcp_type *get_id_type_katcp(struct katcp_dispatch *d, int id);
void *get_key_data_type_katcp(struct katcp_dispatch *d, char *type, char *key);
void *search_type_katcp(struct katcp_dispatch *d, struct katcp_type *t, char *key, void *data);
void *search_named_type_katcp(struct katcp_dispatch *d, char *type, char *key, void *data);
int del_data_type_katcp(struct katcp_dispatch *d, char *type, char *key);
void destroy_type_list_katcp(struct katcp_dispatch *d);
void flush_type_katcp(struct katcp_type *t);
void print_types_katcp(struct katcp_dispatch *d);
void print_type_katcp(struct katcp_dispatch *d, struct katcp_type *t, int flags);

#ifdef DEBUG
void sane_shared_katcp(struct katcp_dispatch *d);
#else 
#define sane_shared_katcp(d)
#endif

/*katcp_stack functions*/
struct katcp_stack *create_stack_katcp();
struct katcp_tobject *create_tobject_katcp(void *data, struct katcp_type *type, int flagman);
struct katcp_tobject *create_named_tobject_katcp(struct katcp_dispatch *d, void *data, char *type, int flagman);
struct katcp_tobject *copy_tobject_katcp(struct katcp_tobject *o);
int compare_tobject_katcp(const void *m1, const void *m2);
void destroy_tobject_katcp(void *data);
#if 0
void inc_ref_tobject_katcp(struct katcp_tobject *o);
int push_stack_ref_obj_katcp(struct katcp_stack *s, struct katcp_tobject *o);
#endif
int push_stack_katcp(struct katcp_stack *s, void *data, struct katcp_type *type);
int push_tobject_katcp(struct katcp_stack *s, struct katcp_tobject *o);
int push_named_stack_katcp(struct katcp_dispatch *d, struct katcp_stack *s, void *data, char *type);
struct katcp_tobject *pop_stack_katcp(struct katcp_stack *s);
struct katcp_tobject *peek_stack_katcp(struct katcp_stack *s);
struct katcp_tobject *index_stack_katcp(struct katcp_stack *s, int indx);
void *index_data_stack_katcp(struct katcp_stack *s, int indx);
void print_tobject_katcp(struct katcp_dispatch *d, struct katcp_tobject *o);
void print_stack_katcp(struct katcp_dispatch *d, struct katcp_stack *s);
void destroy_stack_katcp(struct katcp_stack *s);
int sizeof_stack_katcp(struct katcp_stack *s);
int is_empty_stack_katcp(struct katcp_stack *s);
int empty_stack_katcp(struct katcp_stack *s);
void *pop_data_stack_katcp(struct katcp_stack *s);
void *pop_data_type_stack_katcp(struct katcp_stack *s, struct katcp_type *t);
void *pop_data_expecting_stack_katcp(struct katcp_dispatch *d, struct katcp_stack *s, char *type);

/*katcp_dbase*/
struct katcp_dbase;

#if 0
int dbase_cmd_katcp(struct katcp_dispatch *d, int argc);
#endif
int get_dbase_cmd_katcp(struct katcp_dispatch *d, int argc);
int set_dbase_cmd_katcp(struct katcp_dispatch *d, int argc);
int store_kv_dbase_katcp(struct katcp_dispatch *d, char *key, char *schema, struct katcp_stack *values, struct katcp_stack *tags);
int set_dbase_katcp(struct katcp_dispatch *d, struct katcl_parse *p);
struct katcl_parse *get_dbase_katcp(struct katcp_dispatch *d, struct katcl_parse *p);
int get_value_count_dbase_katcp(struct katcp_dbase *db);
struct katcp_stack *get_value_stack_dbase_katcp(struct katcp_dbase *db);

int dict_katcp(struct katcp_dispatch *d, struct katcl_parse *p);
int dict_cmd_katcp(struct katcp_dispatch *d, int argc);


/*katcp_tag*/
struct katcp_tag;

int tag_data_katcp(struct katcp_dispatch *d, struct katcp_tag *t, void *data, struct katcp_type *type);

int search_cmd_katcp(struct katcp_dispatch *d, int argc);

/* duplex related logic */
int listen_duplex_cmd_katcp(struct katcp_dispatch *d, int argc);
int list_duplex_cmd_katcp(struct katcp_dispatch *d, int argc);

#endif

