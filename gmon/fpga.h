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

enum fpga_req_cmd {
    FPGA_REQ_STATUS,    ///< ?fpgastatus
    FPGA_REQ_LISTDEV,   ///< ?listdev
    FPGA_REQ_META       ///< ?meta
};

int fpga_req_cmd(struct katcl_line *l, enum fpga_req_cmd cmd);

#ifdef __cplusplus
}
#endif

#endif /* _FPGA_H_ */
