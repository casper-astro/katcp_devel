/* TODO Add more information
 *
 *
 *
 */

#ifndef _KATCP_GMON_H_
#define _KATCP_GMON_H_

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

#define GMON_PROG    ("kcpgmon")

#ifdef __cplusplus
extern "C" {
#endif

int gmon_init(void);
int gmon_task(struct katcl_line *l, struct katcl_line *k);

#ifdef __cplusplus
}
#endif

#endif /* _KATCP_GMON_H_ */
