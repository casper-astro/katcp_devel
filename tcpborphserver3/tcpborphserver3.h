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

#define TBS_FPGA_STATUS    "#fpga"

#define TBS_ROACH_CHASSIS  "roach2chassis"

int setup_raw_tbs(struct katcp_dispatch *d, char *bofdir, int argc, char **argv);

#include "loadbof.h"

int start_fpga_tbs(struct katcp_dispatch *d, struct bof_state *bs);
int stop_fpga_tbs(struct katcp_dispatch *d);

#define GETAP_IP_BUFFER         16
#define GETAP_MAC_BUFFER        18
#define GETAP_MAC_SIZE           6
#define GETAP_ARP_FRAME         64
#define GETAP_MAX_FRAME       4096

#define GETAP_ARP_CACHE        256

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

  unsigned int s_self;

  uint8_t s_mac_binary[GETAP_MAC_SIZE];
  uint32_t s_address_binary;
  uint32_t s_mask_binary;
  uint32_t s_network_binary;

  unsigned int s_instance;
  uint16_t s_iteration;
  unsigned int s_burst;
  unsigned int s_deferrals;

  struct tbs_entry *s_register;

  struct katcp_arb *s_tap_io;
  int s_tap_fd;
  int s_mcast_fd;

#if 0
  struct timeval s_timeout;
#endif

  unsigned int s_timer;

  unsigned int s_rx_len;
  unsigned int s_tx_len;
  unsigned int s_arp_len;

  unsigned char s_rxb[GETAP_MAX_FRAME];
  unsigned char s_txb[GETAP_MAX_FRAME];
  unsigned char s_arp_buffer[GETAP_ARP_FRAME];

  uint8_t s_arp_table[GETAP_ARP_CACHE][GETAP_MAC_SIZE];
  uint16_t s_arp_fresh[GETAP_ARP_CACHE];
};

#define TBS_FPGA_DOWN        0
#define TBS_FPGA_PROGRAMMED  1
#define TBS_FPGA_MAPPED      2
#define TBS_STATES_FPGA      3

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

  struct katcp_arb *r_chassis;

  struct getap_state **r_taps;
  unsigned int r_instances;
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

struct tbs_port_data {
  int t_port;
  unsigned int t_timeout;
  int t_program;
  unsigned int t_expected;
  int t_fd;
#if 0
  struct katcp_notice *t_notice;
  int t_rsize;
  void *t_data;
#endif
};

struct katcp_job * run_child_process_tbs(struct katcp_dispatch *d, struct katcp_url *url, int (*call)(struct katcl_line *, void *), void *data, struct katcp_notice *n); 

int upload_cmd(struct katcp_dispatch *d, int argc);
int uploadbof_cmd(struct katcp_dispatch *d, int argc);

int start_chassis_cmd(struct katcp_dispatch *d, int argc);
int led_chassis_cmd(struct katcp_dispatch *d, int argc);

int pre_hook_led_cmd(struct katcp_dispatch *d, int argc);
int post_hook_led_cmd(struct katcp_dispatch *d, int argc);

/* chassis */
struct katcp_arb *chassis_init_tbs(struct katcp_dispatch *d, char *name);


#endif
