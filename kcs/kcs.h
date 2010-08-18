#ifndef KCS_H_
#define KCS_H_

#define KCS_MAX_CLIENTS          32

#define KCS_MODE_BASIC            0 
#define KCS_MODE_BASIC_NAME  "basic"

#define KCS_NOTICE_PYTHON   "python"

struct kcs_basic
{
  char *b_scripts;
};

int setup_basic_kcs(struct katcp_dispatch *d, char *scripts);

#endif
