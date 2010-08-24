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
};


struct p_parser {
  struct p_label **labels;
  int lcount;
  char *filename;
};

struct p_label {
  struct p_setting **settings;
  int scount;
  char *str;
};

struct p_setting {
  struct p_value **values;
  int vcount;
  char *str;
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
int parser_save(struct katcp_dispatch *d, char *filename);
struct p_value * parser_get(struct katcp_dispatch *d, char *srcl, char *srcs, unsigned long vidx);
int parser_set(struct katcp_dispatch *d, char *srcl, char *srcs, unsigned long vidx, char *newval);

#endif
