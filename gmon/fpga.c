#include <stdlib.h>
#include <stdio.h>

#include "fpga.h"
#include "katpriv.h"
#include "katcl.h"

/* IMPORTANT: This shall map to the fpga_status enum */
static char *FPGA_STATUS_STR[] = {"unknown", "down", "ready"};  

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

char *fpga_status_string(enum fpga_status status)
{
    return FPGA_STATUS_STR[status];
}

