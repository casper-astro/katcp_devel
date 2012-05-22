#ifndef TBS_H_
#define TBS_H_

#include <katcp.h>
#include <avltree.h>

#define TBS_MAX_CLIENTS    32

#define TBS_MODE_RAW        0 
#define TBS_MODE_RAW_NAME  "raw"

#ifdef DEBUG
#define TBS_LOGFILE        "tcpborphserver3.log"
#else
#define TBS_LOGFILE        "/var/log/tcpborphserver3.log"
#endif

#ifdef __PPC__
#define TBS_FPGA_CONFIG    "/dev/roach/config"
#define TBS_FPGA_MEM       "/dev/roach/mem"
#else
#define TBS_FPGA_CONFIG    "dev-roach-config"
#define TBS_FPGA_MEM       "dev-roach-mem"
#endif

int setup_raw_tbs(struct katcp_dispatch *d, char *bofdir, int argc, char **argv);
int map_raw_tbs(struct katcp_dispatch *d);
int unmap_raw_tbs(struct katcp_dispatch *d);
void free_entry(void *data);


#define TBS_FPGA_DOWN        0
#define TBS_FPGA_PROGRAMMED  1
#define TBS_FPGA_MAPPED      2

struct tbs_raw
{
  struct avl_tree *r_registers;
  struct avl_tree *r_hwmon;
  int r_fpga;

  void *r_map;
  unsigned int r_map_size;

  char *r_image;
  char *r_bof_dir;
  unsigned int r_top_register;

  int r_argc;
  char **r_argv;
};

#define TBS_READABLE   1
#define TBS_WRITABLE   2
#define TBS_WRABLE     3

struct tbs_entry
{
  unsigned int e_pos_base;
  unsigned int e_len_base;

  unsigned char e_pos_offset;
  unsigned char e_len_offset;
  unsigned char e_mode;
};

struct tbs_hwsensor 
{
  int h_adc_fd;
  int h_min;
  int h_max;
  char *h_min_path;
  char *h_max_path;
  char *h_name;
  char *h_desc;
  char *h_unit;
};

int setup_hwmon_tbs(struct katcp_dispatch *d);
void destroy_hwsensor_tbs(void *data);


struct tbs_port_data {
  int t_port;
  int t_rsize;
  int t_fd;
  void *t_data;
};

struct katcp_job * run_child_process_tbs(struct katcp_dispatch *d, struct katcp_url *url, int (*call)(struct katcl_line *, void *), void *data, struct katcp_notice *n); 

int upload_cmd(struct katcp_dispatch *d, int argc);


#endif
