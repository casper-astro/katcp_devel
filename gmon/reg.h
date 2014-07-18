/* TODO Add more information
 *
 *
 *
 */

#ifndef _GMON_REGISTER_H_
#define _GMON_REGISTER_H_

#include <stdint.h>

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

#ifdef __cplusplus
extern "C" {
#endif

int reg_req_wordread(struct katcl_line *l, char *reg);

#ifdef __cplusplus
}
#endif

#endif /* _GMON_REGISTER_H_ */
