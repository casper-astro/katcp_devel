#ifndef _KATCL_H_
#define _KATCL_H_

/******************* lower level functions *******/

#include <stdarg.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct katcl_larg;
struct katcl_line;
struct katcl_parse;

struct katcl_byte_bit{
  unsigned long b_byte;
  unsigned char b_bit;
  unsigned char b_align;
};

struct katcl_line *create_katcl(int fd);
void destroy_katcl(struct katcl_line *l, int end);
int error_katcl(struct katcl_line *l);

int load_katcl(struct katcl_line *l, char *buffer, unsigned int size);
int read_katcl(struct katcl_line *l);
int have_katcl(struct katcl_line *l);
void clear_katcl(struct katcl_line *l);
void discard_katcl(struct katcl_line *l);
int awaiting_katcl(struct katcl_line *l);

int arg_request_katcl(struct katcl_line *l);
int arg_reply_katcl(struct katcl_line *l);
int arg_inform_katcl(struct katcl_line *l);

unsigned int arg_count_katcl(struct katcl_line *l);
int arg_tag_katcl(struct katcl_line *l);
int arg_null_katcl(struct katcl_line *l, unsigned int index);

char *arg_string_katcl(struct katcl_line *l, unsigned int index);
char *arg_copy_string_katcl(struct katcl_line *l, unsigned int index);
unsigned long arg_unsigned_long_katcl(struct katcl_line *l, unsigned int index);
int arg_bb_katcl(struct katcl_line *l, unsigned int index, struct katcl_byte_bit *b);
unsigned int arg_buffer_katcl(struct katcl_line *l, unsigned int index, void *buffer, unsigned int size);
#ifdef KATCP_USE_FLOATS
double arg_double_katcl(struct katcl_line *l, unsigned int index);
#endif

int append_string_katcl(struct katcl_line *l, int flags, char *buffer);
int append_unsigned_long_katcl(struct katcl_line *l, int flags, unsigned long v);
int append_signed_long_katcl(struct katcl_line *l, int flags, unsigned long v);
int append_hex_long_katcl(struct katcl_line *l, int flags, unsigned long v);
int append_vargs_katcl(struct katcl_line *l, int flags, char *fmt, va_list args);
int append_args_katcl(struct katcl_line *l, int flags, char *fmt, ...);
#ifdef KATCP_USE_FLOATS
int append_double_katcl(struct katcl_line *l, int flags, double v);
#endif
int append_buffer_katcl(struct katcl_line *l, int flags, void *buffer, int len);
int append_timestamp_katcl(struct katcl_line *l, int flags, struct timeval *tv);
int append_parameter_katcl(struct katcl_line *l, int flags, struct katcl_parse *px, unsigned int index); /* single field */
int append_trailing_katcl(struct katcl_line *l, int flags, struct katcl_parse *px, unsigned int start); /* all further fields */
int append_parse_katcl(struct katcl_line *l, struct katcl_parse *p); /* the whole line */
int append_end_katcl(struct katcl_line *l);

int vsend_katcl(struct katcl_line *l, va_list ap);
int send_katcl(struct katcl_line *l, ...);

#if 0
int vprint_katcl(struct katcl_line *l, int full, char *fmt, va_list args);
int print_katcl(struct katcl_line *l, int full, char *fmt, ...);
#endif

int relay_katcl(struct katcl_line *lx, struct katcl_line *ly);

int flushing_katcl(struct katcl_line *l);
int write_katcl(struct katcl_line *l);

int fileno_katcl(struct katcl_line *l);
int problem_katcl(struct katcl_line *l);

void exchange_katcl(struct katcl_line *l, int fd);

int   log_to_code_katcl(char *name);
char *log_to_string_katcl(int code);
int   log_message_katcl(struct katcl_line *cl, int level, char *name, char *fmt, ...);
int   sync_message_katcl(struct katcl_line *cl, int level, char *name, char *fmt, ...);
int  vlog_message_katcl(struct katcl_line *cl, int level, char *name, char *fmt, va_list args);
int vlog_parse_katcl(struct katcl_parse *px, int level, char *name, char *fmt, va_list args);

int extra_response_katcl(struct katcl_line *cl, int code, char *fmt, ...);
int vextra_response_katcl(struct katcl_line *cl, int code, char *fmt, va_list args);

#if 0
int basic_inform_katcl(struct katcl_line *cl, char *name, char *arg);
#endif

/* basic sensor naming stuff, maybe add strategies too ? */

char *name_status_sensor_katcl(unsigned int code);
int status_code_sensor_katcl(char *name);

/* client side rpc logic */

#define KATCL_RPC_LOG_TEXT   0x1
#define KATCL_RPC_LOG_KATCP  0x2

struct katcl_line *create_name_rpc_katcl(char *name);
struct katcl_line *create_extended_rpc_katcl(char *name, int flags);

void destroy_rpc_katcl(struct katcl_line *l);
int await_reply_rpc_katcl(struct katcl_line *l, unsigned int timeout);

#include <sys/time.h>

int complete_rpc_katcl(struct katcl_line *l, unsigned int flags, struct timeval *until);
int send_rpc_katcl(struct katcl_line *l, unsigned int timeout, ...);

#if 0
int finished_request_katcl(struct katcl_line *l, struct timeval *until);
#endif

/* more byte bit ops */

int make_bb_katcl(struct katcl_byte_bit *bb, unsigned long byte, unsigned long bit);
int word_normalise_bb_katcl(struct katcl_byte_bit *bb);
int byte_normalise_bb_katcl(struct katcl_byte_bit *bb);

int exceeds_bb_katcl(struct katcl_byte_bit *bb, struct katcl_byte_bit *limit);
int add_bb_katcl(struct katcl_byte_bit *sigma, struct katcl_byte_bit *alpha, struct katcl_byte_bit *beta);

/* generic queue logic */
struct katcl_gueue *create_precedence_gueue_katcl(void (*release)(void *datum), unsigned int (*precedence)(void *datum));
struct katcl_gueue *create_gueue_katcl(void (*release)(void *datum));
void destroy_gueue_katcl(struct katcl_gueue *g);

unsigned int size_gueue_katcl(struct katcl_gueue *g);
int add_tail_gueue_katcl(struct katcl_gueue *g, void *datum);

void *get_from_head_gueue_katcl(struct katcl_gueue *g, unsigned int position);
void *get_head_gueue_katcl(struct katcl_gueue *g);
void *get_precedence_head_gueue_katcl(struct katcl_gueue *g, unsigned int precedence);

void *remove_head_gueue_katcl(struct katcl_gueue *g);
void *remove_from_head_gueue_katcl(struct katcl_gueue *g, unsigned int position);
void *remove_datum_gueue_katcl(struct katcl_gueue *g, void *datum);

/* specific queue implemenetations ... should replace all of queue.c */

struct katcl_gueue *create_parse_gueue_katcl();

#ifdef __cplusplus
}
#endif

#endif
