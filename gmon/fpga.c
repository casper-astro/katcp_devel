#include <stdlib.h>
#include <stdio.h>

#include "fpga.h"
#include "katpriv.h"
#include "katcl.h"


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
