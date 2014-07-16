#include <stdio.h>
#include <stdlib.h>

#include "reg.h"

#define REG_READ    (0)
#define REG_WRITE   (1)

#define REG_KATCP_READ_CMD    "?readword"     ///< katcp read command
#define REG_KATCP_WRITE_CMD   "?writeword"    ///< katcp write command

static int reg_generic(int readwrite, struct katcl_line *l, struct reg *r)
{
    int retval = 0;

    if (l == NULL) {
        fprintf(stderr, "NULL katcl_line pointer");
        return -1;
    }

    if (r == NULL) {
        fprintf(stderr, "NULL register pointer");
        return -1;
    }

    /* read/write the register */
    if (readwrite == REG_WRITE) {
        retval += append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, REG_KATCP_WRITE_CMD);
    } else {
        retval += append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, REG_KATCP_READ_CMD);
    }
    retval += append_string_katcl(l, KATCP_FLAG_LAST | KATCP_FLAG_STRING, r->name);

    return retval;
}

int reg_readword(struct katcl_line *l, struct reg *r)
{
    return reg_generic(REG_READ, l, r);
}

int reg_writeword(struct katcl_line *l, struct reg *r)
{
    return reg_generic(REG_WRITE, l, r);
}

int reg_readtest(const struct katcl_line *l)
{
    int retval = 0;

    if (l == NULL) {
        fprintf(stderr, "NULL katcl_line pointer");
        return -1;
    }

    /* make use of katcl rpc call */
    

    return retval;
}


