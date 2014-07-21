#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "sensor.h"

struct sensor *sensor_create(char *name, char *desc, char *units, char *type, char *status)
{
    struct sensor *s = NULL;;
    char *s_name = NULL;
    char *s_desc = NULL;
    char *s_units = NULL;
    char *s_type = NULL;
    char *s_status = NULL;

    s_name = strdup(name);
    if (s_name == NULL) {
        return NULL;
    }

    s_desc = strdup(desc);
    if (s_desc == NULL) {
        return NULL;
    }

    /* units may be NULL */
    if (units) {
        s_units = strdup(units);
        if (s_units == NULL) {
            return NULL;
        }
    }

    s_type = strdup(type);
    if (s_type == NULL) {
        return NULL;
    }

    s_status = strdup(status);
    if (s_status == NULL) {
        return NULL;
    }

    s = malloc(sizeof(struct sensor));

    if (s) {
        s->name = s_name;
        s->desc = s_desc;
        s->units = s_units;
        s->type = s_type;
        s->status = s_status;
        s->val = 0;
    }
    
    return s;
}

int sensor_destroy(struct sensor *s)
{
    if (s) {
        if (s->name) {
            free(s->name);
        }
        if (s->desc) {
            free(s->desc);
        }
        if (s->units) {
            free(s->units);
        }
        if (s->type) {
            free(s->type);
        }
        if (s->status) {
            free(s->status);
        }
        free(s);
        s = NULL;
        return 0;
    } else {
        return -1;
    }
}

int sensor_katcp_add(struct katcl_line *l, struct sensor *s)
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

int sensor_katcp_update(struct katcl_line *l, struct sensor *s)
{
    struct timeval now;
    int retval = 0;
    char *valstr = NULL;
    
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
    if (s->val != 0) {
        sprintf(valstr, "%d", s->val);
        retval += append_string_katcl(l, KATCP_FLAG_LAST | KATCP_FLAG_STRING, 
                    valstr);
    } else {
        retval += append_string_katcl(l, KATCP_FLAG_LAST | KATCP_FLAG_STRING, "0");
    }
    
    return retval;
}
