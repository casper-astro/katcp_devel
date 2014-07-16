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

struct reg {
    char *name;       ///< register name
    uint32_t val;     ///< register value
};

int reg_readword(struct katcl_line *l, struct reg *r);

int reg_writeword(struct katcl_line *l, struct reg *r);

int reg_readtest(const struct katcl_line *l);

#ifdef __cplusplus
}
#endif

#endif /* _GMON_REGISTER_H_ */
