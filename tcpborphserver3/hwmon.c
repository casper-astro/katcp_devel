#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
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
      
    /*close(hs->h_min_fd);
    close(hs->h_max_fd);
*/
    if (hs->h_name)
      free(hs->h_name);

    if (hs->h_desc)
      free(hs->h_desc);

    if (hs->h_unit)
      free(hs->h_unit);

    free(hs);
  }
}

int read_fd_hwsensor_tbs(int fd)
{
  char buf[10];
  int size;

  if (fd < 0)
    return -1;

  if (lseek(fd, 0, SEEK_SET) < 0){
#ifdef DEBUG
    fprintf(stderr, "hwmon: cannot lseek fd: %d\n", fd);
#endif
    return -1;
  }

  size = read(fd, &buf, sizeof(buf));
  if (size <= 0)
    return -1;

  return atoi(buf);
}

int read_hwsensor_tbs(struct katcp_dispatch *d, struct katcp_acquire *a)
{
  struct tbs_hwsensor *hs;

  if (a == NULL)
    return -1;

  hs = a->a_local;
  if (hs == NULL)
    return -1;

  return read_fd_hwsensor_tbs(hs->h_adc_fd);
}

struct tbs_hwsensor *create_hwsensor_tbs(struct katcp_dispatch *d, char *name, char *desc, char *unit, char *adc, char *min, char *max)
{
  struct tbs_hwsensor *hs;
  int minfd, maxfd;

  hs = malloc(sizeof(struct tbs_hwsensor));
  if (hs == NULL)
    return NULL;

  hs->h_adc_fd = -1;
  hs->h_min = 0;
  hs->h_max = 0;

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

  if (adc){
    hs->h_adc_fd = open(adc, O_RDONLY);
    if (hs->h_adc_fd < 0){
#if DEBUG>1
      fprintf(stderr, "hwmon: create hwsensor could not open %s (%s)\n", adc, strerror(errno));
#endif
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not open %s (%s)", adc, strerror(errno));
      destroy_hwsensor_tbs(hs);
      return NULL;
    }
    fcntl(hs->h_adc_fd, F_SETFD, FD_CLOEXEC);
  }

  if (min && max){
    minfd = open(min, O_RDONLY);
    if (minfd < 0){
#if DEBUG>1
      fprintf(stderr, "hwmon: create hwsensor could not open %s (%s)\n", min, strerror(errno));
#endif
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not open %s (%s)", min, strerror(errno));
      destroy_hwsensor_tbs(hs);
      return NULL;
    }

    hs->h_min = read_fd_hwsensor_tbs(minfd);
    if (minfd > 0)
      close(minfd);

    maxfd = open(max, O_RDONLY);
    if (maxfd < 0){
#if DEBUG>1
      fprintf(stderr, "hwmon: create hwsensor could not open %s (%s)\n", max, strerror(errno));
#endif
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not open %s (%s)", max, strerror(errno));
      destroy_hwsensor_tbs(hs);
      return NULL;
    }

    hs->h_max = read_fd_hwsensor_tbs(maxfd);
    if (maxfd > 0)
      close(maxfd);
   
  }

  return hs;
}

int register_hwmon_tbs(struct katcp_dispatch *d, char *label, char *desc, char *unit, char *file, char *min, char *max)
{
  struct tbs_raw *tr;
  //struct katcp_acquire *a;
  struct tbs_hwsensor *hs;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if (tr == NULL){
#ifdef DEBUG
    fprintf(stderr, "hwmon: cannot get raw mode\n");
#endif
    return -1;
  }

  hs = create_hwsensor_tbs(d, label, desc, unit, file, min, max);
  if (hs == NULL){
#ifdef DEBUG
    fprintf(stderr, "hwmon: cannot create hw sensor %s\n", label);
#endif
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not create hw sensor %s", label);
    return -1;
  }

  /*a = setup_integer_acquire_katcp(d, &read_hwsensor_tbs, hs, NULL);
  if (a == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not create acquire for hw sensor %s", label);
    destroy_hwsensor_tbs(hs);
    return -1;
  }  */
  
  if (register_integer_sensor_katcp(d, TBS_MODE_RAW, label, desc, unit, &read_hwsensor_tbs, hs, NULL, hs->h_min, hs->h_max) < 0) {
#ifdef DEBUG
    fprintf(stderr, "hwmon: unable to register sensor %s\n", label);
#endif
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to register integer sensor %s", label);
    destroy_hwsensor_tbs(hs);
    return -1;
  }
  
  if (store_named_node_avltree(tr->r_hwmon, label, hs) < 0){
#ifdef DEBUG
    fprintf(stderr, "hwmon: unable to store sensor %s\n", label);
#endif
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to store definition of hw sensor %s", label);
    destroy_hwsensor_tbs(hs);
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

  rtn += register_hwmon_tbs(d, "raw.temp.ambient", 
                               "Ambient board temperature",
                               "millidegrees",
                               "/sys/bus/i2c/devices/0-0018/temp1_input", 
                               "/sys/bus/i2c/devices/0-0018/temp1_min",
                               "/sys/bus/i2c/devices/0-0018/temp1_max");
  
  rtn += register_hwmon_tbs(d, "raw.temp.ppc", 
                               "PowerPC temperature",
                               "millidegrees",
                               "/sys/bus/i2c/devices/0-0018/temp2_input", 
                               "/sys/bus/i2c/devices/0-0018/temp2_min",
                               "/sys/bus/i2c/devices/0-0018/temp2_max");

  rtn += register_hwmon_tbs(d, "raw.temp.fpga", 
                               "FPGA temperature",
                               "millidegrees",
                               "/sys/bus/i2c/devices/0-0018/temp3_input", 
                               "/sys/bus/i2c/devices/0-0018/temp3_min",
                               "/sys/bus/i2c/devices/0-0018/temp3_max");

  rtn += register_hwmon_tbs(d, "raw.fan.chs1", 
                               "Chassis fan speed",
                               "rpm",
                               "/sys/bus/i2c/devices/0-001b/fan1_input",
                               NULL,
                               NULL);

  rtn += register_hwmon_tbs(d, "raw.fan.chs2",
                               "Chassis fan speed",
                               "rpm",
                               "/sys/bus/i2c/devices/0-001f/fan1_input",
                               NULL,
                               NULL);
  
  rtn += register_hwmon_tbs(d, "raw.fan.fpga", 
                               "FPGA fan speed",
                               "rpm",
                               "/sys/bus/i2c/devices/0-0048/fan1_input",
                               NULL,
                               NULL);

  rtn += register_hwmon_tbs(d, "raw.fan.chs0", 
                               "Chassis fan speed",
                               "rpm",
                               "/sys/bus/i2c/devices/0-004b/fan1_input",
                               NULL,
                               NULL);
  
  rtn += register_hwmon_tbs(d, "raw.temp.inlet", 
                               "Inlet ambient temperature",
                               "millidegrees",
                               "/sys/bus/i2c/devices/0-004c/temp1_input",
                               "/sys/bus/i2c/devices/0-004c/temp1_min",
                               "/sys/bus/i2c/devices/0-004c/temp1_max");

  rtn += register_hwmon_tbs(d, "raw.temp.outlet", 
                               "Outlet ambient temperature",
                               "millidegrees",
                               "/sys/bus/i2c/devices/0-004e/temp1_input",
                               "/sys/bus/i2c/devices/0-004e/temp1_min",
                               "/sys/bus/i2c/devices/0-004e/temp1_max");


  return rtn;
}

