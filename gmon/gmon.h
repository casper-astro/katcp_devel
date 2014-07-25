/* TODO Add more information
 *
 *
 *
 */

#ifndef _KATCP_GMON_H_
#define _KATCP_GMON_H_

#include "fpga.h"
#include "sensor.h"
#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

#define GMON_PROG           ("kcpgmon")

/* version information */
#define GMON_VER_MAJOR      (0)
#define GMON_VER_MINOR      (0)
#define GMON_VER_BUGFIX     (0)

#define GMON_POLL_TIME_S    (5)     ///< default register polling time in seconds
#define GMON_POLL_QUEUE_LEN (5)     ///< number of wordread requests that can be in 'transit'

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
    unsigned int polltime;                  ///< register polling time
    volatile enum gmon_status state;        ///< gateware monitor state
    unsigned int numsensors;                ///< number of sensors in the below list
    struct sensor **sensorlist;             ///< sensor list
    unsigned int readdispatch;              ///< 'transit' wordread counter
    unsigned int readcollect;               ///< 'transit' wordread counter 
};

int gmon_task(struct gmon_lib *g);

void gmon_destroy(struct gmon_lib *g);

#ifdef __cplusplus
}
#endif

#endif /* _KATCP_GMON_H_ */
