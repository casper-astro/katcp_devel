/* TODO Add more information
 *
 *
 *
 */

#ifndef _FPGA_H_
#define _FPGA_H_

#include "katcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/*enum fpga_status {
    FPGA_UNKNOWN,
    FPGA_DOWN,
    FPGA_READY
};*/

enum fpga_req_cmd {
    FPGA_REQ_CMD_STATUS,
    FPGA_REQ_CMD_LISTDEV,
    FPGA_REQ_CMD_META
};

//int fpga_requeststatus(struct katcl_line *l);

//int fpga_requestmeta(struct katcl_line *l);

int fpga_req_cmd(struct katcl_line *l, enum fpga_req_cmd cmd);

//char *fpga_status_string(enum fpga_status status);

#ifdef __cplusplus
}
#endif

#endif /* _FPGA_H_ */
