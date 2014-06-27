/* TODO Add more information
 *
 *
 *
 */

#ifndef _KATCP_GMON_H_
#define _KATCP_GMON_H_

#include "katcp.h"
#include "katcl.h"

#ifdef __cplusplus
extern "C" {
#endif

int gmon_init(struct katcl_line *l);
int gmon_task(struct katcl_line *l);

#ifdef __cplusplus
}
#endif

#endif /* _KATCP_GMON_H_ */
