/* TODO Add more information
 *
 *
 *
 */

#ifndef _KATCP_GMON_H_
#define _KATCP_GMON_H_

#include "fpga.h"

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

#define GMON_PROG       ("kcpgmon")

#define GMON_VER_MAJOR  (0)
#define GMON_VER_MINOR  (0)
#define GMON_VER_BUGFIX (0)

#ifdef __cplusplus
extern "C" {
#endif

enum gmon_status {
    GMON_UNKNOWN,
    GMON_IDLE,
    GMON_FPGA_DOWN,
    GMON_FPGA_READY,
    GMON_REQ_META,
    GMON_POLL
};

struct gmon_lib {
    struct katcl_line *server;              ///< server
    struct katcl_line *log;                 ///< logging
    volatile enum gmon_status g_status;     ///< gateware monitor status
};

int gmon_task(struct gmon_lib *g);

#ifdef __cplusplus
}
#endif

#endif /* _KATCP_GMON_H_ */
