#include <stdlib.h>
#include <stdio.h>

#include "fpga.h"
#include "katpriv.h"
#include "katcl.h"

/* IMPORTANT: This shall map to 'enum fpga_req_cmd' */
static char *FPGA_REQ_CMD_STR[] = {"?fpgastatus", "?listdev", "?meta"};

int fpga_req_cmd(struct katcl_line *l, enum fpga_req_cmd cmd)
{
    int ret = 0;

    if (l == NULL) {
        fprintf(stderr, "NULL katcl_line ptr");
        return -1;
    }

    ret += append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING
                                  | KATCP_FLAG_LAST, FPGA_REQ_CMD_STR[cmd]);

    return ret;
}

