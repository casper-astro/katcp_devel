#include <stdio.h>
#include <stdlib.h>

#include "reg.h"

#define REG_KATCP_WORDREAD  "?wordread"

int reg_req_wordread(struct katcl_line *l, char *reg)
{
    int ret = 0;

    if (l == NULL) {
        fprintf(stderr, "NULL katcl_line pointer");
        return -1;
    }

    ret += append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, REG_KATCP_WORDREAD);
    ret += append_string_katcl(l, KATCP_FLAG_LAST | KATCP_FLAG_STRING, reg);

    return ret;
}


