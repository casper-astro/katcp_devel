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
#define TBS_FPGA_CONFIG    "/dev/rconfig"
#define TBS_FPGA_MEM       "/dev/rmem"
#else
#define TBS_FPGA_CONFIG    "dev-roach-config"
#define TBS_FPGA_MEM       "dev-roach-mem"
#endif

int setup_raw_tbs(struct katcp_dispatch *d);

#define TBS_FRGA_DOWN        0
#define TBS_FPGA_PROGRAMED   1
#define TBS_FPGA_MAPPED      2

struct tbs_raw
{
  struct avl_tree *r_registers;
  int r_fpga;

  void *r_map;
  unsigned int r_map_size;

  unsigned int r_top_register;
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

#endif
