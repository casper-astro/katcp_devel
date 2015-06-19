#ifndef TBS_H_
#define TBS_H_

#include <stdint.h>

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

#define TBS_KCPFPG_PATH    "/bin/kcpfpg"
#define TBS_RAMFILE_PATH   "/dev/shm/gateware"

#define TBS_FPGA_STATUS    "#fpga"
#define TBS_KCPFPG_EXE     "kcpfpg"

#define TBS_ROACH_CHASSIS  "roach2chassis"

/* on a 1Gb kernel / 3G user split, this is what we can see */
#define TBS_ROACH_PARTIAL_MAP  (32*1024*1024)
/* on a 2Gb kernel / 2G user split, we can see the full bank EPB of 128M */
#define TBS_ROACH_FULL_MAP     (128*1024*1024)

int setup_raw_tbs(struct katcp_dispatch *d, char *bofdir, int argc, char **argv);

#include "loadbof.h"

int start_fpg_tbs(struct katcp_dispatch *d);
int start_bof_tbs(struct katcp_dispatch *d, struct bof_state *bs);
int stop_fpga_tbs(struct katcp_dispatch *d);

int status_fpga_tbs(struct katcp_dispatch *d, int status);
int map_raw_tbs(struct katcp_dispatch *d);
unsigned int infer_fpga_range(struct katcp_dispatch *d);


#define GETAP_IP_BUFFER         16
#define GETAP_MAC_BUFFER        18
#define GETAP_MAC_SIZE           6
#define GETAP_ARP_FRAME         72 /* needs to be round8(min_frame+4) */
#define GETAP_MAX_FRAME       4096

#define GETAP_ARP_CACHE        256

#define GETAP_PERIOD_CURRENT     0
#define GETAP_PERIOD_INCREMENT   1
#define GETAP_PERIOD_START       2
#define GETAP_PERIOD_STOP        3

#define GETAP_VECTOR_PERIOD      4

struct getap_state{
  uint32_t s_magic;

  struct katcp_dispatch *s_dispatch;
  struct tbs_raw *s_raw_mode;

  char *s_tap_name;

  /* these names have bounded sizes */
  char s_address_name[GETAP_IP_BUFFER];
  char s_gateway_name[GETAP_IP_BUFFER];
  char s_mac_name[GETAP_MAC_BUFFER];

  unsigned short s_port;
  unsigned int s_subnet;

  unsigned int s_self;
  unsigned int s_index;
  unsigned int s_period;

  uint8_t s_mac_binary[GETAP_MAC_SIZE];
  uint32_t s_address_binary;
  uint32_t s_mask_binary;
  uint32_t s_network_binary;
  uint32_t s_gateway_binary;

  unsigned int s_instance;
  uint32_t s_iteration;
  unsigned int s_burst;
  unsigned int s_deferrals;

  unsigned int s_spam_period[GETAP_VECTOR_PERIOD];      /* how often do we query all entries */
  unsigned int s_announce_period[GETAP_VECTOR_PERIOD];  /* interval at which we announce ourselves */
  unsigned int s_valid_period;     /* how long do we cache valid entries */

  struct tbs_entry *s_register;

  struct katcp_arb *s_tap_io;
  int s_tap_fd;
  int s_mcast_fd;
  unsigned int s_mcast_count;

#if 0
  struct timeval s_timeout;
#endif

  unsigned int s_timer;

  unsigned int s_rx_len;
  unsigned int s_tx_len;
  unsigned int s_arp_len;

  unsigned long s_tx_arp;
  unsigned long s_tx_user;
  unsigned long s_tx_error;

  unsigned long s_rx_arp;
  unsigned long s_rx_user;
  unsigned long s_rx_error;

  unsigned char s_rxb[GETAP_MAX_FRAME];
  unsigned char s_txb[GETAP_MAX_FRAME];

  unsigned int s_rx_big;
  unsigned int s_rx_small;

  unsigned int s_tx_big;
  unsigned int s_tx_small;

  unsigned int s_x_glean;

  unsigned int s_table_size;

  unsigned char s_arp_buffer[GETAP_ARP_FRAME];

  uint8_t s_arp_table[GETAP_ARP_CACHE][GETAP_MAC_SIZE];
  uint32_t s_arp_fresh[GETAP_ARP_CACHE];

};

#define TBS_FPGA_DOWN        0
#define TBS_FPGA_PROGRAMMED  1
#define TBS_FPGA_MAPPED      2
#define TBS_FPGA_READY       3    
#define TBS_STATES_FPGA      4

struct tbs_raw
{
  struct avl_tree *r_registers;
  struct avl_tree *r_hwmon; /* only used if INTERNAL_HWMON set */
  int r_fpga;

  void *r_map;
  unsigned int r_map_size;

  char *r_image;
  char *r_bof_dir;
  unsigned int r_top_register;

  int r_argc;
  char **r_argv;

  struct katcp_arb *r_chassis;

  struct getap_state **r_taps;
  unsigned int r_instances;

  struct avl_tree *r_meta;
};

struct meta_entry
{
  char **m;
  int m_size;
  struct meta_entry *m_next;
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
  int h_mult;
  int h_div;
  char *h_min_path;
  char *h_max_path;
  char *h_name;
  char *h_desc;
  char *h_unit;
};

int setup_hwmon_tbs(struct katcp_dispatch *d);
void destroy_hwsensor_tbs(void *data);

#define TBS_FORMAT_BAD (-1)
#define TBS_FORMAT_ANY   0
#define TBS_FORMAT_BOF   1
#define TBS_FORMAT_FPG   2
#define TBS_FORMAT_BIN   3

#define TBS_DEL_NEVER  0
#define TBS_DEL_ERROR  1
#define TBS_DEL_ALWAYS 2

struct tbs_port_data {
  int t_port;
  unsigned int t_timeout;

  unsigned int t_expected;

  int t_fd;
  char *t_name;
  int t_type;
  int t_del;

  int t_program;

#if 0
  struct katcp_notice *t_notice;
  int t_rsize;
  void *t_data;
#endif
};

int upload_generic_resume_tbs(struct katcp_dispatch *d, struct katcp_notice *n, void *data);
int detect_file_tbs(struct katcp_dispatch *d, char *name, int fd);

struct katcp_job *run_child_process_tbs(struct katcp_dispatch *d, struct katcp_url *url, int (*call)(struct katcl_line *, void *), void *data, struct katcp_notice *n); 

int upload_program_cmd(struct katcp_dispatch *d, int argc);
int upload_filesystem_cmd(struct katcp_dispatch *d, int argc);
int upload_bin_cmd(struct katcp_dispatch *d, int argc);


#if 0
int run_fpg_generic(struct katcp_dispatch *d, struct katcp_notice *n, void *data);
int run_resume_generic(struct katcp_dispatch *d, struct katcp_notice *n, void *data);
#endif

int start_chassis_cmd(struct katcp_dispatch *d, int argc);
int led_chassis_cmd(struct katcp_dispatch *d, int argc);

int pre_hook_led_cmd(struct katcp_dispatch *d, int argc);
int post_hook_led_cmd(struct katcp_dispatch *d, int argc);

/* chassis */
struct katcp_arb *chassis_init_tbs(struct katcp_dispatch *d, char *name);


#endif
