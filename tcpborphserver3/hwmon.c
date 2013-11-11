#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <katcp.h>
#include <katcl.h>
#include <katpriv.h>

#include "tcpborphserver3.h"

void destroy_hwsensor_tbs(void *data)
{
  struct tbs_hwsensor *hs;

  hs = data;

  if (hs != NULL){
    
    if (hs->h_adc_fd > 0)
      close(hs->h_adc_fd);
      
    if (hs->h_name)
      free(hs->h_name);

    if (hs->h_desc)
      free(hs->h_desc);

    if (hs->h_unit)
      free(hs->h_unit);

    if (hs->h_min_path)
      free(hs->h_min_path);

    if (hs->h_max_path)
      free(hs->h_max_path);

    free(hs);
  }
}

struct tbs_hwsensor *alloc_hwsensor_tbs()
{
  struct tbs_hwsensor *hs;

  hs = malloc(sizeof(struct tbs_hwsensor));
  if (hs == NULL){
    return NULL;
  }

  hs->h_adc_fd = -1;
  hs->h_min = 0;
  hs->h_max = INT_MAX;
  hs->h_mult = 1;
  hs->h_div = 1;
  hs->h_min_path = NULL;
  hs->h_max_path = NULL;
  hs->h_name = NULL;
  hs->h_desc = NULL;
  hs->h_unit = NULL;

  return hs;
}

int read_fd_hwsensor_tbs(int fd)
{
  char buf[10];
  int size;

  if (fd < 0){
    return -1;
  }

  if (lseek(fd, 0, SEEK_SET) < 0){
#ifdef KATCP_STDERR_ERRORS
    fprintf(stderr, "hwmon: cannot lseek fd: %d\n", fd);
#endif
    return -1;
  }

  size = read(fd, &buf, sizeof(buf));
  if (size <= 0){
    return -1;
  }

  return atoi(buf);
}

int read_hwsensor_tbs(struct katcp_dispatch *d, struct katcp_acquire *a)
{
  struct tbs_hwsensor *hs;
  int value;

  if (a == NULL){
    return -1;
  }

  hs = a->a_local;
  if (hs == NULL){
    return -1;
  }

  value = read_fd_hwsensor_tbs(hs->h_adc_fd);
  if(value == (-1)){
    return -1;
  }

  return (hs->h_mult * value) / hs->h_div;
}

int extract_hwsensor_tbs(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_integer_acquire *ia;
  struct katcp_integer_sensor *is;
  struct katcp_acquire *a;
  struct tbs_hwsensor *hs;

  a = sn->s_acquire;

  hs = a->a_local;
  if (hs == NULL){
    return -1;
  }
  
  if ((a == NULL) || (a->a_type != KATCP_SENSOR_INTEGER) || (sn->s_type != KATCP_SENSOR_INTEGER)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "type mismatch for putative integer sensor %s", sn->s_name);
    return -1;
  }
  
  is = sn->s_more;
  ia = a->a_more;

  if((ia->ia_current < hs->h_min) || (ia->ia_current > hs->h_max)){
    set_status_sensor_katcp(sn, KATCP_STATUS_ERROR);
  } else {
    set_status_sensor_katcp(sn, KATCP_STATUS_NOMINAL);
  }

  is->is_current = ia->ia_current;

  return 0;
}

int write_path_hwsensor_tbs(struct katcp_dispatch *d, char *path, char *value)
{
  int fd, bytes;

  if (path == NULL || value == NULL){
    return -1;
  }
  
  fd = open(path, O_RDWR | O_TRUNC);
  if (fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "write <%s> %s", path, strerror(errno));
#ifdef DEBUG
    fprintf(stderr, "hwmon: write <%s> %s\n", path, strerror(errno));
#endif
    return -1;
  }
  
  bytes = write(fd, value, strlen(value));

  if (bytes < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "write <%s> %s", path, strerror(errno));
#ifdef DEBUG
    fprintf(stderr, "hwmon: write <%s> %s\n", path, strerror(errno));
#endif
    close(fd);
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "write <%s> %d bytes", path, bytes);
#ifdef DEBUG
  fprintf(stderr, "hwmon: write <%s> %d bytes\n", path, bytes);
#endif

  close(fd);
  
  return 0;
}

int flush_hwsensor_tbs(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_acquire *a;
  struct tbs_hwsensor *hs;
  char *limit, *path, *value;

  limit = arg_string_katcp(d, 2);
  value = arg_string_katcp(d, 3);

  if (limit == NULL || value == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters to sensor adjustment logic");
    return -1;
  }

  a = acquire_from_sensor_katcp(d, sn);
  if (a == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic error cannot get acquire");
    return -1;
  }
  
  hs = a->a_local;
  if (hs == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "no local state associated with sensor %s", sn->s_name);
    return -1;
  }

  if (strncmp(limit, "min", 3) == 0){
    hs->h_min = arg_unsigned_long_katcp(d, 3);
    path = hs->h_min_path;    
  } else if (strncmp(limit, "max", 3) == 0) {
    hs->h_max = arg_unsigned_long_katcp(d, 3);
    path = hs->h_max_path;
  } else if (strncmp(limit, "mult", 4) == 0){
    hs->h_mult = arg_unsigned_long_katcp(d, 3);
    path = NULL;
  } else if (strncmp(limit, "div", 3) == 0){
    hs->h_div = arg_unsigned_long_katcp(d, 3);
    path = NULL;
  } else {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unsupported sensor limit setting %s", limit);
    path = NULL;
  }

  if(path){
    if (write_path_hwsensor_tbs(d, path, value) < 0){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to flush new sensor value to path");
    }
  }

  return 0;
}

struct tbs_hwsensor *create_hwsensor_tbs(struct katcp_dispatch *d, char *name, char *desc, char *unit, char *adc, char *min, char *max, unsigned int mult, unsigned int div)
{
  struct tbs_hwsensor *hs;
  int minfd, maxfd;

  if (adc == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc required to create a hardware sensor");
    return NULL;
  }

  hs = alloc_hwsensor_tbs();
  if (hs == NULL){
#ifdef KATCP_STDERR_ERRORS
    fprintf(stderr, "logic error, couldn't malloc\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "memory allocation failure");
    return NULL;
  }

  hs->h_mult = mult;
  hs->h_div  = div;

  hs->h_min_path = (min) ? strdup(min) : NULL;
  if (min && hs->h_min_path == NULL){
    destroy_hwsensor_tbs(hs);
    return NULL;
  }

  hs->h_max_path = (max) ? strdup(max) : NULL;
  if (max && hs->h_max_path == NULL){
    destroy_hwsensor_tbs(hs);
    return NULL;
  }

  hs->h_name = strdup(name);
  if (hs->h_name == NULL){
    destroy_hwsensor_tbs(hs);
    return NULL;
  }

  hs->h_desc = strdup(desc);
  if (hs->h_desc == NULL){
    destroy_hwsensor_tbs(hs);
    return NULL;
  }

  hs->h_unit = strdup(unit);
  if (hs->h_unit == NULL){
    destroy_hwsensor_tbs(hs);
    return NULL;
  }

  hs->h_adc_fd = open(adc, O_RDONLY);
  if (hs->h_adc_fd < 0){
#if KATCP_STDERR_ERRORS
    fprintf(stderr, "hwmon: create hwsensor could not open %s (%s)\n", adc, strerror(errno));
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not open %s (%s)", adc, strerror(errno));
    destroy_hwsensor_tbs(hs);
    return NULL;
  }
  fcntl(hs->h_adc_fd, F_SETFD, FD_CLOEXEC);

  if (min){
    minfd = open(min, O_RDONLY);
    if (minfd > 0){
      hs->h_min = read_fd_hwsensor_tbs(minfd);
      close(minfd);
    } else {
#if KATCP_STDERR_ERRORS
      fprintf(stderr, "hwmon: create hwsensor could not open %s (%s)\n", min, strerror(errno));
#endif
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not open %s (%s)", min, strerror(errno));
    }
  }

  if (max) {
    maxfd = open(max, O_RDONLY);
    if (maxfd > 0){
      hs->h_max = read_fd_hwsensor_tbs(maxfd);
      close(maxfd);
    } else {
#if KATCP_STDERR_ERRORS
      fprintf(stderr, "hwmon: create hwsensor could not open %s (%s)\n", max, strerror(errno));
#endif
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not open %s (%s)", max, strerror(errno));
    }
  }

  return hs;
}

int register_hwmon_tbs(struct katcp_dispatch *d, char *label, char *desc, char *unit, char *file, char *min, char *max, unsigned int mult, unsigned int div)
{
  struct katcp_acquire *a;
  struct tbs_raw *tr;
  struct tbs_hwsensor *hs;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if (tr == NULL){
#ifdef DEBUG
    fprintf(stderr, "hwmon: cannot get raw mode\n");
#endif
    return -1;
  }

  hs = create_hwsensor_tbs(d, label, desc, unit, file, min, max, mult, div);
  if (hs == NULL){
#ifdef DEBUG
    fprintf(stderr, "hwmon: cannot create hw sensor %s\n", label);
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not create hw sensor %s", label);
    return -1;
  }

  /*NULL release since avltree will manage the data*/
  a = setup_integer_acquire_katcp(d, &read_hwsensor_tbs, hs, NULL);
  if (a == NULL){
#ifdef DEBUG
    fprintf(stderr, "hwmon: unable to setup integer acquire for %s\n", label);
#endif
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to setup integer acquire sensor %s", label);
    destroy_hwsensor_tbs(hs);
    return -1; 
  } 
  

#if 0
  if (register_integer_sensor_katcp(d, TBS_MODE_RAW, label, desc, unit, &read_hwsensor_tbs, hs, NULL, hs->h_min, hs->h_max, &flush_hwsensor_tbs) < 0) {
#endif

  if (register_multi_integer_sensor_katcp(d, TBS_MODE_RAW, label, desc, unit, INT_MIN, INT_MAX, a, &extract_hwsensor_tbs, &flush_hwsensor_tbs) < 0){
#if 0
  if (register_integer_sensor_katcp(d, TBS_MODE_RAW, label, desc, unit, &read_hwsensor_tbs, hs, NULL, INT_MIN, INT_MAX, &flush_hwsensor_tbs) < 0) {
#endif
#ifdef DEBUG
    fprintf(stderr, "hwmon: unable to register sensor %s\n", label);
#endif
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register integer sensor %s", label);
    destroy_hwsensor_tbs(hs);
    destroy_acquire_katcp(d, a);
    return -1;
  }
  
  if (store_named_node_avltree(tr->r_hwmon, label, hs) < 0){
#ifdef DEBUG
    fprintf(stderr, "hwmon: unable to store sensor %s\n", label);
#endif
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to store definition of hw sensor %s", label);
    destroy_hwsensor_tbs(hs);
    destroy_acquire_katcp(d, a);
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "hwmon: registered new sensor %s\n", label);
#endif

  return 0;
}


int setup_hwmon_tbs(struct katcp_dispatch *d)
{
  int rtn;
  
  rtn = 0;

#if 0
  rtn += register_hwmon_tbs(d, "dummy", 
                               "dummy debug sensor",
                               "millidegrees",
                               "dummy_input", 
                               "dummy_min",
                               "dummy_max");
#endif

  /* TODO: if input 7 on 50 is nonzero, we have a roach2r2 board, otherwise older */

  rtn += register_hwmon_tbs(d, "raw.temp.ambient", 
                               "Ambient board temperature",
                               "millidegrees",
                               "/sys/bus/i2c/devices/0-0018/temp1_input", 
                               "/sys/bus/i2c/devices/0-0018/temp1_min",
                               "/sys/bus/i2c/devices/0-0018/temp1_max", 
                               1, 1);
  
  rtn += register_hwmon_tbs(d, "raw.temp.ppc", 
                               "PowerPC temperature",
                               "millidegrees",
                               "/sys/bus/i2c/devices/0-0018/temp2_input", 
                               "/sys/bus/i2c/devices/0-0018/temp2_min",
                               "/sys/bus/i2c/devices/0-0018/temp2_max",
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.temp.fpga", 
                               "FPGA temperature",
                               "millidegrees",
                               "/sys/bus/i2c/devices/0-0018/temp3_input", 
                               "/sys/bus/i2c/devices/0-0018/temp3_min",
                               "/sys/bus/i2c/devices/0-0018/temp3_max",
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.fan.chs1", 
                               "Chassis fan speed",
                               "rpm",
                               "/sys/bus/i2c/devices/0-001b/fan1_input",
                               NULL,
                               NULL,
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.fan.chs2",
                               "Chassis fan speed",
                               "rpm",
                               "/sys/bus/i2c/devices/0-001f/fan1_input",
                               NULL,
                               NULL, 
                               1, 1);
  
  rtn += register_hwmon_tbs(d, "raw.fan.fpga", 
                               "FPGA fan speed",
                               "rpm",
                               "/sys/bus/i2c/devices/0-0048/fan1_input",
                               NULL,
                               NULL, 
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.fan.chs0", 
                               "Chassis fan speed",
                               "rpm",
                               "/sys/bus/i2c/devices/0-004b/fan1_input",
                               NULL,
                               NULL,
                               1, 1);
  
  rtn += register_hwmon_tbs(d, "raw.temp.inlet", 
                               "Inlet ambient temperature",
                               "millidegrees",
                               "/sys/bus/i2c/devices/0-004c/temp1_input",
                               "/sys/bus/i2c/devices/0-004c/temp1_min",
                               "/sys/bus/i2c/devices/0-004c/temp1_max",
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.temp.outlet", 
                               "Outlet ambient temperature",
                               "millidegrees",
                               "/sys/bus/i2c/devices/0-004e/temp1_input",
                               "/sys/bus/i2c/devices/0-004e/temp1_min",
                               "/sys/bus/i2c/devices/0-004e/temp1_max",
                               1, 1);

  /* most voltages live on 50. They start here */

  rtn += register_hwmon_tbs(d, "raw.voltage.1v",
                               "1v voltage rail",
                               "millivolts",
                               "/sys/bus/i2c/devices/0-0050/in0_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0050/in0_crit",
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.voltage.1v5",
                               "1.5v voltage rail",
                               "millivolts",
                               "/sys/bus/i2c/devices/0-0050/in1_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0050/in1_crit",
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.voltage.1v8",
                               "1.8v voltage rail",
                               "millivolts",
                               "/sys/bus/i2c/devices/0-0050/in2_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0050/in2_crit",
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.voltage.2v5",
                               "2.5v voltage rail",
                               "millivolts",
                               "/sys/bus/i2c/devices/0-0050/in3_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0050/in3_crit",
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.voltage.3v3",
                               "3.3v voltage rail",
                               "millivolts",
                               "/sys/bus/i2c/devices/0-0050/in4_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0050/in4_crit",
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.voltage.5v",
                               "5v voltage rail",
                               "millivolts",
                               "/sys/bus/i2c/devices/0-0050/in5_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0050/in5_crit",
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.voltage.12v",
                               "12v voltage rail",
                               "millivolts",
                               /* on rev1 this would be not connected */
                               "/sys/bus/i2c/devices/0-0050/in6_input",
                               NULL,
                               NULL,
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.voltage.3v3aux",
                               "auxiliary 3.3v voltage rail",
                               "millivolts",
                               /* on rev1 this would be 12V */
                               "/sys/bus/i2c/devices/0-0050/in7_input",
                               NULL,
                               NULL,
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.voltage.5vaux",
                               "auxiliary 5v voltage rail",
                               "millivolts",
                               /* on rev1 this would be 12V */
                               "/sys/bus/i2c/devices/0-0051/in6_input",
                               NULL,
                               NULL,
                               1, 1);



  rtn += register_hwmon_tbs(d, "raw.current.3v3",
                               "3.3v rail current",
                               "milliamps",
                               "/sys/bus/i2c/devices/0-0051/in0_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0051/in0_crit",
                               5, 2);

  rtn += register_hwmon_tbs(d, "raw.current.2v5",
                               "2.5v rail current",
                               "milliamps",
                               "/sys/bus/i2c/devices/0-0051/in1_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0051/in1_crit", 
                               200, 499);

  rtn += register_hwmon_tbs(d, "raw.current.1v8",
                               "1.8v rail current",
                               "milliamps",
                               "/sys/bus/i2c/devices/0-0051/in2_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0051/in2_crit", 
                               5, 2);

  rtn += register_hwmon_tbs(d, "raw.current.1v5",
                               "1.5v rail current",
                               "milliamps",
                               "/sys/bus/i2c/devices/0-0051/in3_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0051/in3_crit", 
                               10, 1);

  rtn += register_hwmon_tbs(d, "raw.current.1v",
                               "1v rail current",
                               "milliamps",
                               "/sys/bus/i2c/devices/0-0051/in4_input",
                               NULL,
                               "/sys/bus/i2c/devices/0-0051/in4_crit",
                               40, 1);

  rtn += register_hwmon_tbs(d, "raw.current.5v",
                               "5v rail current",
                               "milliamps",
                               "/sys/bus/i2c/devices/0-0051/curr1_input",
                               NULL,
                               NULL, 
                               1, 1);

  rtn += register_hwmon_tbs(d, "raw.current.12v",
                               "12v rail current",
                               "milliamps",
                               "/sys/bus/i2c/devices/0-0050/curr1_input",
                               NULL,
                               NULL, 
                               1, 1);

  return rtn;
}

