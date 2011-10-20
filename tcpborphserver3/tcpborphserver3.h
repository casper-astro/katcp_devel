#ifndef TBS_H_
#define TBS_H_

#include <katcp.h>
#include <avltree.h>

#define TBS_MAX_CLIENTS    32

#define TBS_MODE_RAW        0 
#define TBS_MODE_RAW_NAME  "raw"

#ifdef DEBUG
#define TBS_LOGFILE        "tcpborphserver3.log"
#define TBS_FPGA_CONFIG    "dev-fpga-config"
#else
#define TBS_FPGA_CONFIG    "/dev/fpga/config"
#define TBS_LOGFILE        "/var/log/tcpborphserver3.log"
#endif

int setup_raw_tbs(struct katcp_dispatch *d);

struct tbs_raw
{
  struct avl_tree *r_registers;
  int r_fpga_up;
};

struct tbs_entry
{
  unsigned int e_pos_base;
  unsigned char e_pos_offset;

  unsigned char e_len_offset;
  unsigned int e_len_base;
};

#endif
