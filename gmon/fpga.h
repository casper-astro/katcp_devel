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

enum fpga_status {
    FPGA_UNKNOWN,
    FPGA_DOWN,
    FPGA_READY
};

int fpga_requeststatus(struct katcl_line *l);

#ifdef __cplusplus
}
#endif

#endif /* _FPGA_H_ */
