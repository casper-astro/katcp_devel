/* TODO Add more information
 *
 *
 *
 */

#ifndef _GMON_SENSOR_H_
#define _GMON_SENSOR_H_

#include <stdint.h>

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sensor {
    char *name;     ///< sensor name in dotted notation
    char *desc;     ///< description of the information provided by the sensor
    char *units;    ///< short form of the units for the sensor value, may be blank
    char *type;     ///< [integer, float, boolean, timestamp, discrete, address, string]
    char *status;   ///< [unknown, nominal, warn, error, failure]
    uint32_t val;   ///< value
};

struct sensor *sensor_create(char *name, char *desc, char *units, char *type, char *status);

int sensor_destroy(struct sensor *s);

int sensor_katcp_add(struct katcl_line *l, struct sensor *s);

int sensor_katcp_update(struct katcl_line *l, struct sensor *s);

#ifdef __cplusplus
}
#endif

#endif /* _GMON_SENSOR_H_ */
