#ifndef _KATCL_H_
#define _KATCL_H_

/******************* lower level functions *******/

#include <stdarg.h>

struct katcl_larg;
struct katcl_line;
struct katcl_parse;

struct katcl_byte_bit{
  unsigned long b_byte;
  unsigned char b_bit;
};

struct katcl_line *create_katcl(int fd);
void destroy_katcl(struct katcl_line *l, int end);
int error_katcl(struct katcl_line *l);

int read_katcl(struct katcl_line *l);
int have_katcl(struct katcl_line *l);
void clear_katcl(struct katcl_line *l);


int arg_request_katcl(struct katcl_line *l);
int arg_reply_katcl(struct katcl_line *l);
int arg_inform_katcl(struct katcl_line *l);

unsigned int arg_count_katcl(struct katcl_line *l);
int arg_null_katcl(struct katcl_line *l, unsigned int index);

char *arg_string_katcl(struct katcl_line *l, unsigned int index);
char *arg_copy_string_katcl(struct katcl_line *l, unsigned int index);
unsigned long arg_unsigned_long_katcl(struct katcl_line *l, unsigned int index);
int arg_byte_bit_katcl(struct katcl_line *l, unsigned int index, struct katcl_byte_bit *b);
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
int append_parameter_katcl(struct katcl_line *l, int flags, struct katcl_parse *px, unsigned int index); /* single field */
int append_parse_katcl(struct katcl_line *l, struct katcl_parse *p); /* the whole line */

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

int complete_rpc_katcl(struct katcl_line *l, unsigned int flags, struct timeval *until);
int send_rpc_katcl(struct katcl_line *l, unsigned int timeout, ...);

#if 0
int finished_request_katcl(struct katcl_line *l, struct timeval *until);
#endif

#endif
