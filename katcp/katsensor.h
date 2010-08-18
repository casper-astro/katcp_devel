#ifndef KATSENSOR_H_
#define KATSENSOR_H_

/* output functions defined in katcp and katcl */

/* lower level sensor functions */

#include <stdarg.h>

#include <katpriv.h>

void destroy_sensors_katcp(struct katcp_dispatch *d);
void destroy_nonsensors_katcp(struct katcp_dispatch *d);

/* functions used to register sensors */

#if 0
struct katcp_sensor *create_integer_sensor_katcp(char *name, char *description, char *units, int preferred, int (*get)(struct katcp_sensor *s, void *local), int min, int max);

struct katcp_sensor *create_boolean_sensor_katcp(char *name, char *description, char *units, int preferred, int (*get)(struct katcp_sensor *s, void *local));

struct katcp_sensor *create_discrete_sensor_katcp(char *name, char *description, char *units, int preferred, int (*get)(struct katcp_sensor *s, void *local), ...);
struct katcp_sensor *vcreate_discrete_sensor_katcp(char *name, char *description, char *units, int preferred, int (*get)(struct katcp_sensor *s, void *local), va_list args);
struct katcp_sensor *acreate_discrete_sensor_katcp(char *name, char *description, char *units, int preferred, int (*get)(struct katcp_sensor *s, void *local), char **vector, int size);

struct katcp_sensor *create_lru_sensor_katcp(char *name, char *description, char *units, int preferred, int (*get)(struct katcp_sensor *s, void *local));

#ifdef KATCP_USE_FLOATS
struct katcp_sensor *create_float_sensor_katcp(char *name, char *description, char *units, int preferred, double (*get)(struct katcp_sensor *s, void *local), double min, double max);
#endif
#endif

/* functions used by sensor writers *{***************************/

/* set the status (unknown, nominal, etc) */
#if 0
int set_status_sensor_katcp(struct katcp_sensor *sn, int status);
#endif

/* internal functions, probably no need to call them externall **/

#if 0
int has_diff_katcp(struct katcp_sensor *sn);
char *type_name_sensor_katcp(struct katcp_sensor *sn);
char *status_name_sensor_katcp(struct katcp_sensor *sn);
char *strategy_name_sensor_katcp(struct katcp_nonsense *ns);
#endif

#endif
