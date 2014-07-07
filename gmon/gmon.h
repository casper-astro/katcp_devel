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

struct gmon_lib {
    struct katcl_line *server;  ///< server
    struct katcl_line *log;     ///< logging
    enum fpga_status f_status;  ///< FPGA status
};

int gmon_task(struct gmon_lib *g);

#ifdef __cplusplus
}
#endif

#endif /* _KATCP_GMON_H_ */
