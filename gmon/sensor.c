#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "sensor.h"

int sensor_add(struct katcl_line *l, struct sensor *s)
{
    int retval = 0;

    if (l == NULL) {
        fprintf(stderr, "NULL katcl_line pointer");
        return -1;
    }

    if (s == NULL) {
        fprintf(stderr, "NULL sensor pointer");
        return -1;
    }

    /* register the sensor */
    retval += append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING,
                    KATCP_SENSOR_LIST_INFORM);
    retval += append_string_katcl(l, KATCP_FLAG_STRING, s->name); 
    retval += append_string_katcl(l, KATCP_FLAG_STRING, s->desc); 
    retval += append_string_katcl(l, KATCP_FLAG_STRING, s->units); 
    retval += append_string_katcl(l, KATCP_FLAG_LAST | KATCP_FLAG_STRING, 
                    s->type); 

    return retval;
}

int sensor_update(struct katcl_line *l, struct sensor *s)
{
    struct timeval now;
    int retval = 0;
    
    if (l == NULL) {
        fprintf(stderr, "NULL katcl_line pointer");
        return -1;
    }

    if (s == NULL) {
        fprintf(stderr, "NULL sensor pointer");
        return -1;
    }

    gettimeofday(&now, NULL);

    /* update the sensor */
    retval += append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_STRING,
                    KATCP_SENSOR_STATUS_INFORM);
#if KATCP_PROTOCOL_MAJOR_VERSION >= 5
    retval += append_args_katcl(l, 0, "%lu.%03d", now.tv_sec,
                (unsigned int)(now.tv_usec / 1000));
#else
    retval += append_args_katcl(l, 0, "%lu%03d", now.tv_sec,
                (unsigned int)(now.tv_usec / 1000));
#endif
    /* only update one sensor */
    retval += append_string_katcl(l, KATCP_FLAG_STRING, "1");
    retval += append_string_katcl(l, KATCP_FLAG_STRING, s->name);
    retval += append_string_katcl(l, KATCP_FLAG_STRING, s->status);
    retval += append_string_katcl(l, KATCP_FLAG_LAST | KATCP_FLAG_STRING, 
                    s->val);
    
    return retval;
}
