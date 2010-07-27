#ifndef _KATCL_H_
#define _KATCL_H_

/******************* lower level functions *******/

#include <stdarg.h>

struct katcl_larg;
struct katcl_line;

struct katcl_line *create_katcl(int fd);
void destroy_katcl(struct katcl_line *l, int mode);
int error_katcl(struct katcl_line *l);

int read_katcl(struct katcl_line *l);
int have_katcl(struct katcl_line *l);

int arg_request_katcl(struct katcl_line *l);
int arg_reply_katcl(struct katcl_line *l);
int arg_inform_katcl(struct katcl_line *l);

unsigned int arg_count_katcl(struct katcl_line *l);
int arg_null_katcl(struct katcl_line *l, unsigned int index);

char *arg_string_katcl(struct katcl_line *l, unsigned int index);
char *arg_copy_string_katcl(struct katcl_line *l, unsigned int index);
unsigned long arg_unsigned_long_katcl(struct katcl_line *l, unsigned int index);
unsigned int arg_buffer_katcl(struct katcl_line *l, unsigned int index, void *buffer, unsigned int size);
#ifdef KATCP_USE_FLOATS
double arg_double_katcl(struct katcl_line *l, unsigned int index);
#endif

int append_string_katcl(struct katcl_line *l, int flags, void *buffer);
int append_unsigned_long_katcl(struct katcl_line *l, int flags, unsigned long v);
int append_signed_long_katcl(struct katcl_line *l, int flags, unsigned long v);
int append_hex_long_katcl(struct katcl_line *l, int flags, unsigned long v);
int append_buffer_katcl(struct katcl_line *l, int flags, void *buffer, int len);
int append_vargs_katcl(struct katcl_line *l, int flags, char *fmt, va_list args);
int append_args_katcl(struct katcl_line *l, int flags, char *fmt, ...);
#ifdef KATCP_USE_FLOATS
int append_double_katcl(struct katcl_line *l, int flags, double v);
#endif

int vsend_katcl(struct katcl_line *l, va_list ap);
int send_katcl(struct katcl_line *l, ...);

int vprint_katcl(struct katcl_line *l, int full, char *fmt, va_list args);
int print_katcl(struct katcl_line *l, int full, char *fmt, ...);

int flushing_katcl(struct katcl_line *l);
int write_katcl(struct katcl_line *l);

int fileno_katcl(struct katcl_line *l);
int problem_katcl(struct katcl_line *l);

void exchange_katcl(struct katcl_line *l, int fd);

int   log_to_code(char *name);
char *log_to_string(int code);
int   log_message_katcl(struct katcl_line *cl, int level, char *name, char *fmt, ...);
int  vlog_message_katcl(struct katcl_line *cl, int level, char *name, char *fmt, va_list args);

int extra_response_katcl(struct katcl_line *cl, int code, char *fmt, ...);
int vextra_response_katcl(struct katcl_line *cl, int code, char *fmt, va_list args);

int basic_inform_katcl(struct katcl_line *cl, char *name, char *arg);

#endif
