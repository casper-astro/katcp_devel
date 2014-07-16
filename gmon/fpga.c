#include <stdlib.h>
#include <stdio.h>

#include "fpga.h"
#include "katpriv.h"
#include "katcl.h"

/* IMPORTANT: This shall map to the fpga_status enum */
// static char *FPGA_STATUS_STR[] = {"unknown", "down", "ready"}; 

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
/* 
int fpga_requeststatus(struct katcl_line *l)
{
    int retval = 0;

    if (l == NULL) {
        return -1;
    }

    retval += append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING
                                    | KATCP_FLAG_LAST, "?fpgastatus");

    return retval;
}

int fpga_requestmeta(struct katcl_line *l)
{
    int retval = 0;

    if (l == NULL) {
        return -1;
    }

    retval += append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING
                                    | KATCP_FLAG_LAST, "?meta");

    return retval;
}
*/

/*
char *fpga_status_string(enum fpga_status status)
{
    return FPGA_STATUS_STR[status];
}
*/

