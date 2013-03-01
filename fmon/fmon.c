/* module to perform monitoring on fengine gateware */

/* WARNING: this is poorly written interrim code, to be redone in zmon */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>

#include <arpa/inet.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>

#include <katcp.h>
#include <katcl.h>
#include <katpriv.h>

/* largest board id */

#define FMON_MAX_BOARDS       15

/* largest number of engines */

#define FMON_MAX_CROSSES        2
#define FMON_MAX_INPUTS         2

#define FMON_XENG_THRESHOLD    15 /* number of times we see an error */

/* sensors of which a board only has one, if at all */

#define FMON_SENSOR_LRU     0
#define FMON_SENSOR_CLOCK   1

#define FMON_BOARD_SENSORS  2

/* per input, adc related sensors */

#define FMON_SENSOR_ADC_OVERRANGE           0
#define FMON_SENSOR_ADC_DISABLED            1
#define FMON_SENSOR_FFT_OVERRANGE           2
#define FMON_SENSOR_SRAM                    3
#define FMON_SENSOR_LINK                    4
#define FMON_SENSOR_ADC_RAW_POWER           5
#define FMON_SENSOR_ADC_DBM_POWER           6

#define FMON_INPUT_SENSORS                  7

/* register fields */

#define FMON_QDRCTRL_RESET         0x00000001

#define FMON_FCONTROL_CLEAR_TERMINATE  0x0010
#define FMON_FCONTROL_CLEAR_STATUS     0x0008
#define FMON_FCONTROL_FLASHER_EN       0x1000

#define FMON_XCONTROL_FLASHER_EN       0x1000

#define FMON_FSTATUS_WBC_QUANT_OVERRANGE   0x0001  /* bit  0, complex eq  registers 10.1 */
#define FMON_FSTATUS_WBC_FFT_OVERRANGE     0x0002  /* bit  1, fft         registers 8.1 */
#define FMON_FSTATUS_WBC_ADC_OVERRANGE     0x0004  /* bit  2, adc         registers 7.1 */
#define FMON_FSTATUS_WBC_SDRAM_BAD         0x0008  /* bit  3, misc        registers 6.2 */
#define FMON_FSTATUS_WBC_ADC_DISABLED      0x0010  /* bit  4, adc         registers 7.1 */
#define FMON_FSTATUS_WBC_CLOCK_BAD         0x0020  /* bit  5, timing      registers 5.1 */
#define FMON_FSTATUS_WBC_XAUI_LINKBAD  0x00020000  /* bit 17, guessed */

#define FMON_FSTATUS_NBC_QUANT_OVERRANGE   0x0001 
#define FMON_FSTATUS_NBC_COARSE_OVERGANGE  0x0002 
#define FMON_FSTATUS_NBC_FFT_OVERRANGE     0x0004 
#define FMON_FSTATUS_NBC_ADC_OVERRANGE     0x0008 
#define FMON_FSTATUS_NBC_SDRAM_BAD         0x0010
#define FMON_FSTATUS_NBC_ADC_DISABLED      0x0020 
#define FMON_FSTATUS_NBC_CLOCK_BAD         0x0040 
#define FMON_FSTATUS_NBC_XAUI_LINKBAD  0x00020000 

#define FMON_FSTATUS_QUANT_OVERRANGE(f)  (map_mode_bits[(f)->f_mode][0])
#define FMON_FSTATUS_FFT_OVERRANGE(f)    (map_mode_bits[(f)->f_mode][1]) 
#define FMON_FSTATUS_ADC_OVERRANGE(f)    (map_mode_bits[(f)->f_mode][2])
#define FMON_FSTATUS_SDRAM_BAD(f)        (map_mode_bits[(f)->f_mode][3])
#define FMON_FSTATUS_ADC_DISABLED(f)     (map_mode_bits[(f)->f_mode][4])
#define FMON_FSTATUS_CLOCK_BAD(f)        (map_mode_bits[(f)->f_mode][5])
#define FMON_FSTATUS_XAUI_LINKBAD(f)     (map_mode_bits[(f)->f_mode][6])

/* time related */

#define FMON_DEFAULT_INTERVAL 1000
#define FMON_DEFAULT_TIMEOUT  5000

#define FMON_INIT_PERIOD    100000

/* misc */

#define FMON_GOOD_DSP_CLOCK 200000000

#define FMON_KATADC_SCALE   1.0/184.3
#define FMON_IADC_SCALE     1.0/368.0

#define FMON_KATADC_ERR_HIGH       0.0
#define FMON_KATADC_ERR_LOW      -32.0

#define FMON_KATADC_FUDGE_LOW    -12.0

#define FMON_KATADC_WARN_HIGH    -15.0
#define FMON_KATADC_WARN_LOW     -28.0

#define FMON_MODE_WBC             0
#define FMON_MODE_NBC             1

volatile int run;

static char inputs_fmon[FMON_MAX_INPUTS] = { 'x', 'y' };

static unsigned int map_mode_bits[2][7] = { { 
  FMON_FSTATUS_WBC_QUANT_OVERRANGE,
  FMON_FSTATUS_WBC_FFT_OVERRANGE,
  FMON_FSTATUS_WBC_ADC_OVERRANGE,
  FMON_FSTATUS_WBC_SDRAM_BAD,
  FMON_FSTATUS_WBC_ADC_DISABLED,
  FMON_FSTATUS_WBC_CLOCK_BAD,
  FMON_FSTATUS_WBC_XAUI_LINKBAD }, 

{ FMON_FSTATUS_NBC_QUANT_OVERRANGE,
  FMON_FSTATUS_NBC_FFT_OVERRANGE,
  FMON_FSTATUS_NBC_ADC_OVERRANGE,
  FMON_FSTATUS_NBC_SDRAM_BAD,
  FMON_FSTATUS_NBC_ADC_DISABLED,
  FMON_FSTATUS_NBC_CLOCK_BAD,
  FMON_FSTATUS_NBC_XAUI_LINKBAD }
};


#if 0
static char *board_sensor_labels_fmon[FMON_BOARD_SENSORS]       = { "lru.available", "fpga.synchronised" };
static char *board_sensor_descriptions_fmon[FMON_BOARD_SENSORS] = { "line replacement unit operational", "signal processing clock stable" };

static char *input_sensor_labels_fmon[FMON_INPUT_SENSORS]       = { "%s.adc.overrange", "%s.adc.terminated", "%s.fft.overrange", "%s.adc.power.raw" };

static char *input_sensor_descriptions_fmon[FMON_INPUT_SENSORS] = { "adc overrange indicator", "adc disabled", "fft overrange indicator", "raw power" };
#endif

struct fmon_sensor_template{
  char *t_name;
  char *t_description;
  int t_type;
  int t_min;
  int t_max;
  double t_fmin;
  double t_fmax;
  int t_logging;
};

struct fmon_sensor_template board_template[FMON_BOARD_SENSORS] = {
  { "lru.available", "line replacement unit operational",  KATCP_SENSOR_BOOLEAN, 0, 1, 0.0, 0.0, 1 },
  { "fpga.synchronised", "signal processing clock stable", KATCP_SENSOR_BOOLEAN, 0, 1, 0.0, 0.0, 0 }
};

struct fmon_sensor_template input_template[FMON_INPUT_SENSORS] = {
  { "%s.adc.overrange",  "adc overrange indicator",     KATCP_SENSOR_BOOLEAN, 0, 1, 0.0, 0.0, 0 },
  { "%s.adc.terminated", "adc disabled",                KATCP_SENSOR_BOOLEAN, 0, 1, 0.0, 0.0, 1 },
  { "%s.fft.overrange",  "fft overrange indicator",     KATCP_SENSOR_BOOLEAN, 0, 1, 0.0, 0.0, 0 },
  { "%s.sram.available", "sram calibrated and ready",   KATCP_SENSOR_BOOLEAN, 0, 1, 0.0, 0.0, 0 },
  { "%s.xaui.link",      "data link up",                KATCP_SENSOR_BOOLEAN, 0, 1, 0.0, 0.0, 1 },
#if 0
  { "%s.adc.raw",        "untranslated average of squared inputs",  KATCP_SENSOR_FLOAT,   0, 0, 0.0, 65000.0, 0},
  { "%s.adc.power",      "approximate input signal strength",  KATCP_SENSOR_FLOAT,   0, 0, -81.0, 16.0, 0}
#else
  { "%s.adc.raw",        "untranslated average of squared inputs",  KATCP_SENSOR_FLOAT,   0, 0, 0.0, 65000.0, 0},
  { "%s.adc.power",      "approximate input signal strength",  KATCP_SENSOR_FLOAT,   0, 0, FMON_KATADC_ERR_LOW + FMON_KATADC_FUDGE_LOW, FMON_KATADC_ERR_HIGH, 0}
#endif
};

struct fmon_sensor{
  int s_type;
  char *s_name;
  char *s_description;
  int s_value;
  double s_fvalue;
  int s_status;
  int s_new;
  int s_min;
  int s_max;
  double s_fmin;
  double s_fmax;

  int s_logging;
};

struct fmon_input{
  struct fmon_sensor n_sensors[FMON_INPUT_SENSORS];
  char *n_label;
  double n_rf_gain;
  int n_rf_enabled;
};

struct fmon_state
{
  int f_verbose;
  char *f_server;

  struct timeval f_start;
  struct timeval f_when;
  struct timeval f_done;
  struct timeval f_io;
  int f_maintaining;

  struct katcl_line *f_line;
  struct katcl_line *f_report;

  char *f_symbolic;

  int f_board;
  int f_fixed;
  int f_prior;

  int f_reprobe;
  int f_cycle;
  int f_something;
  int f_grace; /* grace period (ms) within which initial terminated not reported */

  int f_mode;

  int f_fs;
  int f_xs;

  unsigned int f_clock_err;

  struct fmon_input f_inputs[FMON_MAX_INPUTS];
  int f_dirty;

  struct fmon_sensor f_sensors[FMON_BOARD_SENSORS];

  unsigned int f_amplitude_acc_len;
  double f_adc_scale_factor;

  unsigned long f_xe_errors[FMON_MAX_CROSSES];

  int f_xp_count;
  unsigned long f_xp_errors[FMON_MAX_CROSSES];

  int f_x_threshold;
};

/*************************************************************************/

int update_sensor_fmon(struct fmon_state *f, struct fmon_sensor *s, int value, unsigned int status);
int update_sensor_status_fmon(struct fmon_state *f, struct fmon_sensor *s, unsigned int status);

void set_lru_fmon(struct fmon_state *f, int value, unsigned int status);

#if 0
int list_all_sensors_fmon(struct fmon_state *f);
int list_board_sensors_fmon(struct fmon_state *f);
#endif

int make_labels_fmon(struct fmon_state *f);

void set_timeout_fmon(struct fmon_state *f, unsigned int timeout);
int detect_fmon(struct fmon_state *f);

void query_versions_fmon(struct fmon_state *f);

/*************************************************************************/

static void handle_signal(int s)
{
  switch(s){
    case SIGHUP :
      run = (-1);
      break;
    case SIGTERM :
      run = 0;
      break;
  }
}

/* setup / destroy ************************************************************/

void destroy_fmon(struct fmon_state *f)
{
  int i, j;
  struct fmon_sensor *s;
  struct fmon_input *n;

  if(f == NULL){
    return;
  }

  f->f_board = (-1);
  f->f_fixed = (-1);
  f->f_prior = (-1);
  f->f_reprobe = 0;
  f->f_fs = 0;
  f->f_xs = 0;

  if(f->f_line){
    destroy_rpc_katcl(f->f_line);
    f->f_line = NULL;
  }

  for(i = 0; i < FMON_MAX_INPUTS; i++){
    n = &(f->f_inputs[i]);

    for(j = 0; j < FMON_INPUT_SENSORS; j++){
      s = &(n->n_sensors[j]);

      s->s_type = (-1);

      if(s->s_name == NULL){
        free(s->s_name);
        s->s_name = NULL;
      }

      if(s->s_description == NULL){
        free(s->s_description);
        s->s_description = NULL;
      }

      s->s_new = 1;
    }

    if(n->n_label){
      free(n->n_label);
      n->n_label = NULL;
    }
  }

  /* above clears, below breaks structure */

  for(i = 0; i < FMON_BOARD_SENSORS; i++){
    s = &(f->f_sensors[i]);

    s->s_type = (-1);

    if(s->s_name == NULL){
      free(s->s_name);
      s->s_name = NULL;
    }

    if(s->s_description == NULL){
      free(s->s_description);
      s->s_description = NULL;
    }

    s->s_new = 1;
  }

  if(f->f_report){
    destroy_katcl(f->f_report, 0);
    f->f_report = NULL;
  }

  if(f->f_server){
    free(f->f_server);
    f->f_server = NULL;
  }

  free(f);
}

int populate_sensor_fmon(struct fmon_sensor *s, struct fmon_sensor_template *t, char *instance)
{
  int len;

  if((s == NULL) || (t == NULL)){
    return -1;
  }

  if(s->s_name){
    free(s->s_name);
    s->s_name = NULL;
  }
  if(s->s_description){
    free(s->s_description);
    s->s_description = NULL;
  }

  s->s_type = t->t_type;

  if(instance){
    len = strlen(instance) + strlen(t->t_name) + 1;
    s->s_name = malloc(len);
    if(s->s_name){
      snprintf(s->s_name, len - 1, t->t_name, instance);
      s->s_name[len - 1] = '\0';
    } else {
      return -1;
    }
  } else {
    s->s_name = strdup(t->t_name);
    if(s->s_name == NULL){
      return -1;
    }
  }

  s->s_description = strdup(t->t_description);
  if(s->s_description == NULL){
    return -1;
  }

  s->s_min = t->t_min;
  s->s_max = t->t_max;

  s->s_fmin = t->t_fmin;
  s->s_fmax = t->t_fmax;

  s->s_logging = t->t_logging;

  return 0;
}

struct fmon_state *create_fmon(char *server, int verbose, unsigned int timeout, int reprobe, int fixed)
{
  struct fmon_state *f;
  struct fmon_sensor *s;
  struct fmon_input *n;
  int i, j;
  int flags;

  f = malloc(sizeof(struct fmon_state));
  if(f == NULL){
    return NULL;
  }

  f->f_verbose = verbose;
  f->f_server = NULL;

  set_timeout_fmon(f, timeout);
  f->f_io.tv_sec = 0;
  f->f_io.tv_usec = 0;

  f->f_maintaining = 0;

  f->f_line = NULL;
  f->f_report = NULL;

  f->f_symbolic = NULL;

  f->f_board = (-1);
  f->f_prior = (-1);
  f->f_fixed = fixed;

  f->f_reprobe = reprobe;
  f->f_cycle = 0;
  f->f_grace = 0;

  f->f_mode = FMON_MODE_WBC; /* assume a mode */

  f->f_amplitude_acc_len = 0x10000;
  f->f_adc_scale_factor = FMON_KATADC_SCALE;

  for(i = 0; i < FMON_MAX_CROSSES; i++){
    f->f_xe_errors[i] = 0;
    f->f_xp_errors[i] = 0;
  }

  f->f_xp_count = 0;

  f->f_fs = 0;
  f->f_xs = 0;

  for(i = 0; i < FMON_BOARD_SENSORS; i++){
    s = &(f->f_sensors[i]);
    s->s_type = (-1);
    s->s_name = NULL;
    s->s_value = 0;
    s->s_fvalue = 0.0;
    s->s_status = KATCP_STATUS_UNKNOWN;
    s->s_new = 1;
  }

  for(i = 0; i < FMON_MAX_INPUTS; i++){
    n = &(f->f_inputs[i]);

    for(j = 0; j < FMON_INPUT_SENSORS; j++){
      s = &(n->n_sensors[j]);

      s->s_type = (-1);
      s->s_name = NULL;
      s->s_value = 0;
      s->s_fvalue = 0.0;
      s->s_status = KATCP_STATUS_UNKNOWN;
      s->s_new = 1;
    }

    n->n_label = NULL;
    n->n_rf_gain = 0.0;
  }

  f->f_server = strdup(server);
  if(f->f_server == NULL){
    destroy_fmon(f);
    return NULL;
  }

  for(i = 0; i < FMON_BOARD_SENSORS; i++){
    s = &(f->f_sensors[i]);

    if(populate_sensor_fmon(s, &(board_template[i]), NULL) < 0){
      destroy_fmon(f);
      return NULL;
    }
  }

  flags = fcntl(STDOUT_FILENO, F_GETFL, NULL);
  if(flags >= 0){
    flags = fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);
  }

  f->f_report = create_katcl(STDOUT_FILENO);
  if(f->f_report == NULL){
    destroy_fmon(f);
    return NULL;
  }

  return f;
}

/* io management **********************************************************/

#if 0
void set_checkpoint_fmon(struct fmon_state *f)
{
  f->f_answer = 0;
}
#endif

void set_timeout_fmon(struct fmon_state *f, unsigned int timeout)
{
  struct timeval delta;

  gettimeofday(&(f->f_start), NULL);

  delta.tv_sec = timeout / 1000;
  delta.tv_usec = (timeout % 1000) * 1000;

#ifdef DEBUG
  fprintf(stderr, "timeout: +%ums\n", timeout);
#endif

  add_time_katcp(&(f->f_when), &(f->f_start), &delta);
}

struct fmon_sensor *find_sensor_fmon(struct fmon_state *f, char *name)
{
  int i, j;
  struct fmon_sensor *s;
  struct fmon_input *n;

  if(name == NULL){
    return NULL;
  }

  for(i = 0; i < FMON_BOARD_SENSORS; i++){
    s = &(f->f_sensors[i]);
    if(s->s_name){
      if(!strcmp(s->s_name, name)){
        return s;
      }
    }
  }

  for(i = 0; FMON_MAX_INPUTS; i++){
    n = &(f->f_inputs[i]);
    for(j = 0; j < FMON_INPUT_SENSORS; j++){
      s = &(n->n_sensors[j]);
      if(s->s_name){
        if(!strcmp(s->s_name, name)){
          return s;
        }
      }
    }
  }

  return NULL;
}

int check_parent(struct fmon_state *f)
{
  if(getppid() <= 1){
    return -1;
  }

  return 0;
}

int catchup_fmon(struct fmon_state *f, unsigned int interval)
{
  struct timeval delta, target;
  fd_set fsr, fsw;
  int fd, result;
  char *request, *label, *strategy;

  delta.tv_sec = interval / 1000;
  delta.tv_usec = (interval % 1000) * 1000;

  add_time_katcp(&target, &(f->f_start), &delta);
  fd = fileno_katcl(f->f_report);
  if(fd < 0){
    return -1;
  }
  
  gettimeofday(&(f->f_done), NULL);

  while(cmp_time_katcp(&(f->f_done), &target) <= 0){

    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    FD_SET(fd, &fsr);
    if(flushing_katcl(f->f_report)){
      FD_SET(fd, &fsw);
    }

    sub_time_katcp(&delta, &target, &(f->f_done));

    result = select(fd + 1, &fsr, &fsw, NULL, &delta);

    if(result < 0){
      switch(errno){
        case EAGAIN : 
        case EINTR  :
          break;
        default : 
          return -1;
      }
    }

    if(result > 0){
      if(FD_ISSET(fd, &fsr)){
        result = read_katcl(f->f_report);
        if(result){
          return -1;
        }

        while(have_katcl(f->f_report) > 0){
          if(arg_request_katcl(f->f_report)){
            request = arg_string_katcl(f->f_report, 0);
            if(request){
              log_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_server, "got %s request", request);
              if(!strcmp(request, "?sensor-sampling")){
                result = 0;
                label = arg_string_katcl(f->f_report, 1);
                strategy = arg_string_katcl(f->f_report, 2);
                if(label && strategy){
                  if(!strcmp(strategy, "event") && (find_sensor_fmon(f, label) != NULL)){
                    result = 1;
                  }
                }
                append_string_katcl(f->f_report, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!sensor-sampling");
                append_string_katcl(f->f_report, KATCP_FLAG_LAST  | KATCP_FLAG_STRING, result ? KATCP_OK : KATCP_FAIL);
              }
            }
          }
        }

      }

      if(FD_ISSET(fd, &fsw)){
        if(write_katcl(f->f_report) < 0){
          return -1;
        }
      }
    }

    gettimeofday(&(f->f_done), NULL);
  }

  return 0;
}

void drop_connection_fmon(struct fmon_state *f)
{
  int i, j;
  struct fmon_input *n;
  struct fmon_sensor *s;

  if(f->f_line == NULL){
    return;
  }

  for(i = 0; i < FMON_BOARD_SENSORS; i++){
    s = &(f->f_sensors[i]);
    if(i == FMON_SENSOR_LRU){
      if(s->s_status == KATCP_STATUS_NOMINAL){ /* grr - too much special case stuff here */
        update_sensor_fmon(f, s, 0, KATCP_STATUS_WARN);
      }
    } else {
      update_sensor_status_fmon(f, s, KATCP_STATUS_UNKNOWN);
    }
  }

  for(i = 0; i < f->f_fs; i++){
    n = &(f->f_inputs[i]);
    for(j = 0; j < FMON_INPUT_SENSORS; j++){
      s = &(n->n_sensors[j]);
      update_sensor_status_fmon(f, s, KATCP_STATUS_UNKNOWN);
    }
  }

#if 0
  while(flushing_katcl(f->f_report)){
    if(write_katcl(f->f_report) != 0){
      break;
    }
  }
#endif
  while(write_katcl(f->f_report) == 0);


  /* previous part did notification, below invalidates */

  f->f_cycle = 0;
  f->f_fs = 0;
  f->f_xs = 0;
  f->f_board = (-1);

  if(f->f_symbolic){
    free(f->f_symbolic);
    f->f_symbolic = NULL;
  }

  destroy_rpc_katcl(f->f_line);
  f->f_line = NULL;
}

int probe_fmon(struct fmon_state *f)
{
  int result;

  result = 0;

  if(((f->f_board >= 0) && (f->f_fs > 0)) || (f->f_xs > 0)){
    return 0;
  }

  result = detect_fmon(f);
  if(result == 0){
    result = make_labels_fmon(f);
  }

  query_versions_fmon(f);

#if 0
  list_all_sensors_fmon(f);
#endif

  return result;
}

#if 0
int resume_connection_fmon(struct fmon_state *f)
{
  if(f->f_line){
    return 0;
  }

  f->f_line = create_name_rpc_katcl(f->f_server);
  if(f->f_line){

#if 0
    if(((f->f_board >= 0) && ((f->f_fs > 0) || (f->f_xs > 0))) ||
       (detect_fmon(f) == 0)){

      if(make_labels_fmon(f) == 0){
        list_all_sensors_fmon(f);
        query_versions_fmon(f);

        return 0;
      }
    } else {
      f->f_board = (-1);
      f->f_fs = 0;
      f->f_xs = 0;
    }
#endif

    if(probe_fmon(f) < 0){
      destroy_rpc_katcl(f->f_line);
      f->f_line = NULL;
    }
  }

  return -1;
}
#endif

int maintain_fmon(struct fmon_state *f)
{
  struct timeval now, delta;
  int state;

#define STATE_CONNECT   0
#define STATE_PROBE     1
#define STATE_DONE      2

#ifdef DEBUG
  fprintf(stderr, "maintain[%d], line=%p\n", f->f_maintaining, f->f_line);

#endif

  if(f->f_maintaining){
    return f->f_line ? 0 : (-1);
  }

  f->f_maintaining = 1;

  if(f->f_line == NULL){
    state = STATE_CONNECT;
  } else {
    state = STATE_DONE;
    if((f->f_board < 0) && (f->f_reprobe)){
      f->f_cycle++;
#ifdef DEBUG
      fprintf(stderr, "maintain: considering probing again (cycle=%d, reprobe=%d)\n", f->f_cycle, f->f_reprobe);
#endif
      if(f->f_cycle >= f->f_reprobe){
        state = STATE_PROBE;
        f->f_cycle = 0;
      }
    }
  }

  for(;;){
    switch(state){
      case STATE_CONNECT : 
        f->f_line = create_name_rpc_katcl(f->f_server);
        if(f->f_line == NULL){
          log_message_katcl(f->f_report, KATCP_LEVEL_TRACE, f->f_server, "connect to %s failed: %s", f->f_server, strerror(errno));
          /* state = STATE_CONNECT */
          break;
        } /* fall */
        state = STATE_PROBE;
      case STATE_PROBE : 
        if(probe_fmon(f) < 0){
          destroy_rpc_katcl(f->f_line);
          f->f_line = NULL;
          state = STATE_CONNECT; /* try again */
          break;
        } /* fall */
        state = STATE_DONE; /* superfluous, but symmetrical */
        f->f_grace = 0; /* start counter on done transition */
      case STATE_DONE :
        set_lru_fmon(f, 1, KATCP_STATUS_NOMINAL);
        f->f_maintaining = 0;
        return 0;
    }

    gettimeofday(&now, NULL);

    if(cmp_time_katcp(&(f->f_when), &now) <= 0){

      set_lru_fmon(f, 0, KATCP_STATUS_ERROR);
      f->f_grace = 0; /* unclear if needed ... */
      f->f_maintaining = 0;
      return -1;
    }

    sub_time_katcp(&delta, &(f->f_when), &now);
    if(delta.tv_sec > 0){
      delta.tv_sec = 1;
      delta.tv_usec = 0;
    }

    select(0, NULL, NULL, NULL, &delta);
  }

  /* return 0; */
}

/* basic io routines ***************************************************************/

int relay_build_state_fmon(struct fmon_state *f)
{
  char *parm;
  char *version;
  int delta;

#ifdef DEBUG
  fprintf(stderr, "relay: attempting to relay build information\n");
#endif

  parm = arg_string_katcl(f->f_line, 1);
  if(parm == NULL){
    return -1;
  }

  version = strchr(parm, '-');
  if(version == NULL){
    return -1;
  }

  delta = version - parm;
  if((delta < 0) || (delta > 256)){
    return -1;
  }

  version++;
  if(version[0] == '\0'){
    return -1;
  }

  append_string_katcl(f->f_report, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_VERSION_CONNECT_INFORM);
  append_buffer_katcl(f->f_report,                    KATCP_FLAG_STRING, parm, delta);
  append_string_katcl(f->f_report,  KATCP_FLAG_LAST | KATCP_FLAG_STRING, version);

  return 0;
}

int collect_io_fmon(struct fmon_state *f)
{
  int status;
  char *ptr;

  while((status = complete_rpc_katcl(f->f_line, 0, &(f->f_when))) == 0){
    ptr = arg_string_katcl(f->f_line, 0);
    if(ptr){
#ifdef DEBUG
      fprintf(stderr, "collection: received some inform message %s ...\n", ptr);
#endif
      if(!strcmp("#build-state", ptr)){
        relay_build_state_fmon(f);
      }
    }
  }

#ifdef DEBUG
  fprintf(stderr, "collect: status is %d\n", status);
#endif
  if(status < 0){
    drop_connection_fmon(f);
    return -1;
  }

  gettimeofday(&(f->f_io), NULL);

  ptr = arg_string_katcl(f->f_line, 1);
  if(ptr == NULL){
    return -1;
  }

  if(strcmp(ptr, KATCP_OK)){
    return 1;
  }

  return 0;
}

int read_word_fmon(struct fmon_state *f, char *name, uint32_t *value)
{
  int result[4], r, status, i;
  int expect[4] = { 6, 0, 2, 2 };
  uint32_t tmp;
  char *code;

  if(maintain_fmon(f) < 0){
    return -1;
  }

  result[0] = append_string_katcl(f->f_line,                           KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?read");
  result[1] = append_string_katcl(f->f_line,                           KATCP_FLAG_STRING, name);
  result[2] = append_unsigned_long_katcl(f->f_line,                    KATCP_FLAG_ULONG,  0);
  result[3] = append_unsigned_long_katcl(f->f_line,  KATCP_FLAG_LAST | KATCP_FLAG_ULONG,  4);

  expect[1] = strlen(name) + 1;
  for(i = 0; i < 4; i++){
    if(result[i] != expect[i]){
#ifdef DEBUG
      fprintf(stderr, "read: append[%d]=%d != %d\n", i, result[i], expect[i]);
#endif
      drop_connection_fmon(f);
      return -1;
    }
  }

  r = collect_io_fmon(f);
  if(r != 0){
    return r;
  }

  f->f_something++;

  /* TODO: check ok */
  code = arg_string_katcl(f->f_line, 1);
  if(code == NULL){
    return -1;
  }

#if 1
  if(strcmp(code, KATCP_OK)){
    return 1;
  }
#endif

  status = arg_buffer_katcl(f->f_line, 2, &tmp, 4);
  if(status != 4){
    return -1;
  }

  *value = ntohl(tmp);

  return 0;
}

int write_word_fmon(struct fmon_state *f, char *name, uint32_t value)
{
  int result[4], r, i;
  int expect[4] = { 7, 0, 2, 5 };
  uint32_t tmp;

  if(maintain_fmon(f) < 0){
    return -1;
  }

  tmp = htonl(value);

  result[0] = append_string_katcl(f->f_line,       KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?write");
  result[1] = append_string_katcl(f->f_line,                          KATCP_FLAG_STRING, name);
  result[2] = append_unsigned_long_katcl(f->f_line,                   KATCP_FLAG_ULONG,  0);
  result[3] = append_buffer_katcl(f->f_line,        KATCP_FLAG_LAST | KATCP_FLAG_BUFFER, &tmp, 4);

  expect[1] = strlen(name) + 1;
  for(i = 0; i < 4; i++){
    if(result[i] != expect[i]){
#ifdef DEBUG
      fprintf(stderr, "write: result[%d]=%d != %d\n", i, result[i], expect[i]);
#endif
      drop_connection_fmon(f);
      return -1;
    }
  }

  r = collect_io_fmon(f);
  if(r != 0){
    return r;
  }

  f->f_something++;

  gettimeofday(&(f->f_io), NULL);

  return 0;
}

/* routines to display sensors **********************************************/

#if 0
int list_all_sensors_fmon(struct fmon_state *f)
{
  int i;
  struct fmon_sensor *s;
  struct fmon_input *n;

  list_board_sensors_fmon(f);

  for(i = 0; i < f->f_fs; i++){

    n = &(f->f_inputs[i]);

    s = &(n->n_sensors[FMON_SENSOR_ADC_OVERRANGE]);
    print_intbool_list_fmon(f, s->s_name, "adc overrange indicator", "none");

    s = &(n->n_sensors[FMON_SENSOR_ADC_DISABLED]);
    print_intbool_list_fmon(f, s->s_name, "adc disabled", "none");

    s = &(n->n_sensors[FMON_SENSOR_FFT_OVERRANGE]);
    print_intbool_list_fmon(f, s->s_name, "fft overrange indicator", "none");
  }

  return 0;
}

int list_board_sensors_fmon(struct fmon_state *f)
{
  struct fmon_sensor *s;

  s = &(f->f_sensors[FMON_SENSOR_LRU]);
  print_intbool_list_fmon(f, s->s_name, "line replacement unit operational", "none");

  if(f->f_fs > 0){
    s = &(f->f_sensors[FMON_SENSOR_CLOCK]);
    print_intbool_list_fmon(f, s->s_name, "signal processing clock stable", "none");
  }

  return 0;
}
#endif

#if 0
int list_input_sensors_fmon(struct fmon_state *f)
{
  int i; 
  struct fmon_input *n;
  struct fmon_sensor *s;

  for(i = 0; i < f->f_fs; i++){

    n = &(f->f_inputs[i]);

    s = &(n->n_sensors[FMON_SENSOR_ADC_OVERRANGE]);
    print_intbool_list_fmon(f, s->s_name, "adc overrange indicator", "none");

    s = &(n->n_sensors[FMON_SENSOR_ADC_DISABLED]);
    print_intbool_list_fmon(f, s->s_name, "adc switched to ground", "none");

    s = &(n->n_sensors[FMON_SENSOR_FFT_OVERRANGE]);
    print_intbool_list_fmon(f, s->s_name, "fft overrange indicator", "none");
  }

  return 0;
}
#endif

/****************************************************************************/

int print_sensor_list_fmon(struct fmon_state *f, struct fmon_sensor *s)
{
  append_string_katcl(f->f_report, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#sensor-list");
  append_string_katcl(f->f_report,                    KATCP_FLAG_STRING, s->s_name);
  append_string_katcl(f->f_report,                    KATCP_FLAG_STRING, s->s_description);
  append_string_katcl(f->f_report,                    KATCP_FLAG_STRING, "none");

  switch(s->s_type){
    case KATCP_SENSOR_FLOAT : 
      append_string_katcl(f->f_report,         KATCP_FLAG_STRING, "float");

      append_double_katcl(f->f_report,         KATCP_FLAG_DOUBLE, s->s_fmin);
      append_double_katcl(f->f_report,    KATCP_FLAG_DOUBLE | KATCP_FLAG_LAST, s->s_fmax);
      break;
    case KATCP_SENSOR_INTEGER : 
      append_string_katcl(f->f_report,         KATCP_FLAG_STRING, "integer");

      append_unsigned_long_katcl(f->f_report,  KATCP_FLAG_ULONG, s->s_max);
      append_unsigned_long_katcl(f->f_report,  KATCP_FLAG_ULONG | KATCP_FLAG_LAST, s->s_max);
      break;
    case KATCP_SENSOR_BOOLEAN : 
      append_string_katcl(f->f_report,  KATCP_FLAG_LAST | KATCP_FLAG_STRING, "boolean");
      break;
  }

  return 0;
}

int print_sensor_status_fmon(struct fmon_state *f, struct fmon_sensor *s)
{
  struct timeval now;
  unsigned int milli;

  gettimeofday(&now, NULL);
  milli = now.tv_usec / 1000;

  append_string_katcl(f->f_report, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#sensor-status");
  append_args_katcl(f->f_report, KATCP_FLAG_STRING, "%lu%03d", now.tv_sec, milli);
  append_string_katcl(f->f_report, KATCP_FLAG_STRING, "1");
  append_string_katcl(f->f_report, KATCP_FLAG_STRING, s->s_name);
  append_string_katcl(f->f_report, KATCP_FLAG_STRING, name_status_sensor_katcl(s->s_status));

  switch(s->s_type){
    case KATCP_SENSOR_INTEGER : 
    case KATCP_SENSOR_BOOLEAN : 
      append_unsigned_long_katcl(f->f_report, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, s->s_value);
      break;
    case KATCP_SENSOR_FLOAT : 
      append_double_katcl(f->f_report, KATCP_FLAG_LAST | KATCP_FLAG_DOUBLE, s->s_fvalue);
      break;
  }

  return 0;
}

/****************************************************************************/

int make_labels_fmon(struct fmon_state *f)
{
#define BUFFER 128
  struct fmon_input *n;
  struct fmon_sensor *s;
  int i, j;
  char buffer[BUFFER];
  char *tmp;

  if(f->f_board < 0){
    return 1;
  }

  i = strlen("board") + 8;
  tmp = realloc(f->f_symbolic, i);
  if(tmp == NULL){
    return -1;
  }
  f->f_symbolic = tmp;

  snprintf(f->f_symbolic, i - 1, "board%d", f->f_board);
  f->f_symbolic[i - 1] = '\0';

  log_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_server, "roach %s is now board%d", f->f_server, f->f_board);

  for(i = 0; i < f->f_fs; i++){
    n = &(f->f_inputs[i]);

    snprintf(buffer, BUFFER - 1, "%d%c", f->f_board, inputs_fmon[i]);
    buffer[BUFFER - 1] = '\0';

    if(n->n_label){
      free(n->n_label);
      n->n_label = NULL;
    }

    n->n_label = strdup(buffer);
    if(n->n_label == NULL){
      return -1;
    }

    for(j = 0; j < FMON_INPUT_SENSORS; j++){
      s = &(n->n_sensors[j]);

      if(populate_sensor_fmon(s, &(input_template[j]), n->n_label) < 0){
        return -1;
      }

#if 0
      if(s->s_name){
        free(s->s_name);
        s->s_name = NULL;
      }

      snprintf(buffer, BUFFER - 1, input_sensor_labels_fmon[j], n->n_label);
      buffer[BUFFER - 1] = '\0';

      s->s_name = strdup(buffer);
      if(s->s_name == NULL){
        return -1;
      }

      s->s_description = strdup(input_sensor_descriptions_fmon[j]);
      if(s->s_description == NULL){
        return -1;
      }
#endif
    }
  }

  return 0;
#undef BUFFER
}

void query_user_tag_fmon(struct fmon_state *f, char *label, char *reg)
{
  uint32_t value;
  char buffer[5];
  int i;

  if(f->f_board < 0){
    return;
  }

  if(read_word_fmon(f, reg, &value)){
    return;
  }

  append_string_katcl(f->f_report, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_VERSION_CONNECT_INFORM);
  append_string_katcl(f->f_report,                    KATCP_FLAG_STRING, label);

  if(value < 0xffff){
    append_unsigned_long_katcl(f->f_report, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, (unsigned long)value);
  } else {
    buffer[0] = 0xff & (value >> 24);
    buffer[1] = 0xff & (value >> 16);
    buffer[2] = 0xff & (value >>  8);
    buffer[3] = 0xff & (value);
    buffer[4] = '\0';

    for(i = 0; (i < 4) && (isalnum(buffer[i])); i++);

    if(i < 4){
      append_hex_long_katcl(f->f_report, KATCP_FLAG_LAST | KATCP_FLAG_XLONG, value);
    } else {
      append_string_katcl(f->f_report, KATCP_FLAG_LAST | KATCP_FLAG_STRING, buffer);
    }
  }

}

void query_rcs_fmon(struct fmon_state *f, char *label, char *reg)
{
  uint32_t value;
  int dirty;

  if(f->f_board < 0){
    return;
  }

  if(read_word_fmon(f, reg, &value)){
    return;
  }

  append_string_katcl(f->f_report, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, KATCP_VERSION_CONNECT_INFORM);
  append_string_katcl(f->f_report,                    KATCP_FLAG_STRING, label);

  if(value & (1 << 31)){
    append_args_katcl(f->f_report, KATCP_FLAG_LAST | KATCP_FLAG_STRING, "t-%u", value & 0x7fffffff);
  } else {

    dirty = (value >> 29) & 1;

    switch((value >> 30) & 1){
      case 0 : /* git */
        append_args_katcl(f->f_report, KATCP_FLAG_LAST | KATCP_FLAG_STRING, dirty ? "git-%07x-dirty" : "git-%07x", value & 0x0fffffff);
        break;

      default : 
        /* case 1 : */ /* svn */
        append_args_katcl(f->f_report, KATCP_FLAG_LAST | KATCP_FLAG_STRING, dirty ? "svn-%d-dirty" : "svn-%d", value & 0x0fffffff);
        break;
    }
  }

  /* require that we have appended a FLAG_LAST */
  return;
}

void query_versions_fmon(struct fmon_state *f)
{
  if(f->f_fs && f->f_xs){
    query_rcs_fmon(f, "gateware.combined", "rcs_app");
  } else if(f->f_fs){
    query_rcs_fmon(f, "gateware.fengine", "rcs_app");
  } else if(f->f_xs){
    query_rcs_fmon(f, "gateware.xengine", "rcs_app");
  } 

  query_user_tag_fmon(f, "gateware.tag", "rcs_user");
  query_rcs_fmon(f, "gateware.infrastructure", "rcs_lib");
}

int detect_fmon(struct fmon_state *f)
{
#define BUFFER 16
  uint32_t word;
  char buffer[BUFFER];
  int limit, result;
#if 0
  struct fmon_input *n;
#endif

  /* assume all things to have gone wrong */
  f->f_board = (-1);
  f->f_fs = 0;
  f->f_xs = 0;

  result = read_word_fmon(f, "board_id", &word);
  if(result != 0){
    if(f->f_fixed >= 0){
      log_message_katcl(f->f_report, KATCP_LEVEL_DEBUG, f->f_server, "using fixed board %u rather than %d", f->f_fixed, word);
    }
    return result;
  }

  if(f->f_fixed >= 0){
    f->f_board = f->f_fixed;
    log_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_server, "using fixed board %u rather than %d", f->f_fixed, word);
  } else {
    if(word > FMON_MAX_BOARDS){
      log_message_katcl(f->f_report, KATCP_LEVEL_WARN, f->f_server, "rather large board id %d reported by roach %s", word, f->f_server);
    } else {
      log_message_katcl(f->f_report, KATCP_LEVEL_DEBUG, f->f_server, "roach %s claims board%d", f->f_server, word);
    }
    f->f_board = word;
  }

  if(f->f_reprobe == 0){
    f->f_reprobe = 1;
  }

  if(read_word_fmon(f, "fine_ctrl", &word) == 0){
    log_message_katcl(f->f_report, KATCP_LEVEL_DEBUG, f->f_server, "roach %s seems to be in narrowband mode", f->f_server);
    f->f_mode = FMON_MODE_NBC;
  } else {
    log_message_katcl(f->f_report, KATCP_LEVEL_DEBUG, f->f_server, "roach %s presumed to be in wideband mode", f->f_server);
    f->f_mode = FMON_MODE_WBC;
  }

  limit = FMON_MAX_INPUTS;
  while(f->f_fs < limit){
    snprintf(buffer, BUFFER - 1, "fstatus%d", f->f_fs);
    buffer[BUFFER - 1] = '\0';
    if(read_word_fmon(f, buffer, &word)){
      limit = 0;
    } else {

#if 0 /* this code now happens periodically */
      snprintf(buffer, BUFFER - 1, "adc_ctrl%d", f->f_fs);
      buffer[BUFFER - 1] = '\0';

      if(read_word_fmon(f, buffer, &word)){
        limit = 0;
      } else {
        n = &(f->f_inputs[f->f_fs]);
        n->n_rf_enabled = (word & 0x80000000) ? 1 : 0;
        n->n_rf_gain = 20.0 - (word & 0x3f) * 0.5;
#if 0
        /* wrong alternative: applies only if values are inverted */
        n->n_rf_gain = -11.5 + (word & 0x3f) * 0.5;
#endif 
#endif

        f->f_fs++;
#if 0
      }
#endif
    }
  }

  limit = FMON_MAX_CROSSES;
  while(f->f_xs < limit){
    snprintf(buffer, BUFFER - 1, "xstatus%d", f->f_xs);
    buffer[BUFFER - 1] = '\0';
    if(read_word_fmon(f, buffer, &word)){
      limit = 0;
    } else {
      f->f_xe_errors[f->f_xs] = 0;
      f->f_xs++;
    }
  }

  limit = f->f_xs;
  f->f_xp_count = 0;
  while(f->f_xp_count < limit){
    snprintf(buffer, BUFFER - 1, "gbe_tx_cnt%d", f->f_xp_count);
    buffer[BUFFER - 1] = '\0';
    if(read_word_fmon(f, buffer, &word)){
      limit = 0;
    } else {
      f->f_xe_errors[f->f_xp_count] = 0;
      f->f_xp_count++;
    }
  }

  log_message_katcl(f->f_report, KATCP_LEVEL_DEBUG, f->f_server, "board contains %d fengines and %d xengines (%d ports)", f->f_fs, f->f_xs, f->f_xp_count);

  if((f->f_board > 0) || (f->f_fixed >= 0)){
    f->f_prior = f->f_board;
    return 0;
  } 
  
  /* else if board_id == 0, then do some more checking that we are actually set up */

  if(f->f_fs > 0){
    result = read_word_fmon(f, "control", &word);
    if(result != 0){
      return result;
    }
    if(word & FMON_FCONTROL_FLASHER_EN){
      f->f_prior = 0;
      return 0;
    }
  }

  if(f->f_xs > 0){
    result = read_word_fmon(f, "ctrl", &word);
    if(result != 0){
      return result;
    }
    if(word & FMON_XCONTROL_FLASHER_EN){
      f->f_prior = 0;
      return 0;
    }
  }

  log_message_katcl(f->f_report, KATCP_LEVEL_DEBUG, f->f_server, "ignoring board id 0 as roach not initalised");

  f->f_board = (-1); /* board id is just 0 because it is unset */

  return -1;
#undef BUFFER
}

/****************************************************************************/

int update_sensor_status_fmon(struct fmon_state *f, struct fmon_sensor *s, unsigned int status)
{
  char *str;

  if(status != s->s_status){

    if(s->s_logging){
      str = name_status_sensor_katcl(status);
      if(str == NULL){
        str = "broken";
      }
      log_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_server, "sensor %s transitioned to %s status", s->s_name, str);
    }

    s->s_status = status;

    if(s->s_new){
      s->s_new = 0;
      print_sensor_list_fmon(f, s);
    }
    print_sensor_status_fmon(f, s);
  }

  return 0;
}

int update_sensor_fmon(struct fmon_state *f, struct fmon_sensor *s, int value, unsigned int status)
{
  int change;
  char *str;

  change = 0;

  switch(s->s_type){
    case KATCP_SENSOR_BOOLEAN :
    case KATCP_SENSOR_INTEGER :
      break;
    default :
      log_message_katcl(f->f_report, KATCP_LEVEL_WARN, f->f_server, "logic problem, updating integer for sensor type %d", s->s_type);
      return -1;
  }

  if(value != s->s_value){
    s->s_value = value;
    change++;
  }

  if(status != s->s_status){

    if(s->s_logging){
      str = name_status_sensor_katcl(status);
      if(str == NULL){
        str = "broken";
      }
      log_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_server, "sensor %s transitioned to %s status", s->s_name, str);
    }

    s->s_status = status;

    change++;
  }

  if(change){
    if(s->s_new){
      s->s_new = 0;
      print_sensor_list_fmon(f, s);
    }
    print_sensor_status_fmon(f, s);
  }

  return 0;
}

int update_sensor_double_fmon(struct fmon_state *f, struct fmon_sensor *s, double value, unsigned int status)
{
  int change;

  change = 0;

  if(s->s_type != KATCP_SENSOR_FLOAT){
    log_message_katcl(f->f_report, KATCP_LEVEL_WARN, f->f_server, "logic problem, updating float for sensor type %d", s->s_type);
    return -1;
  }

  if(value != s->s_fvalue){
    s->s_fvalue = value;
    change++;
  }

  if(status != s->s_status){
    s->s_status = status;
    change++;
  }

  if(change){
    if(s->s_new){
      s->s_new = 0;
      print_sensor_list_fmon(f, s);
    }
    print_sensor_status_fmon(f, s);
  }

  return 0;
}

void set_lru_fmon(struct fmon_state *f, int value, unsigned int status)
{
  struct fmon_sensor *sensor_lru;

  sensor_lru = &(f->f_sensors[FMON_SENSOR_LRU]);

  update_sensor_fmon(f, sensor_lru, value, status);

  while(write_katcl(f->f_report) == 0);
#if 0
  while(flushing_katcl(f->f_report)){
    if(write_katcl(f->f_report) == 0){
      break;
    }
  }
#endif
}

/* f-engine monitoring stuff ***************************************/

int clear_control_fmon(struct fmon_state *f)
{
  uint32_t word;

  if(read_word_fmon(f, "control", &word)){
    return -1;
  }

  word &= ~FMON_FCONTROL_CLEAR_STATUS;
  if(write_word_fmon(f, "control", word)){
    return -1;
  }

  if(read_word_fmon(f, "control", &word)){
    return -1;
  }

  word |= FMON_FCONTROL_CLEAR_STATUS;
  if(write_word_fmon(f, "control", word)){
    return -1;
  }

  return 0;
}

int check_status_fengine_fmon(struct fmon_state *f, struct fmon_input *n, unsigned int number)
{
#define BUFFER 32
  int result;
  uint32_t word;
  struct fmon_sensor *sensor_adc, *sensor_disabled, *sensor_fft, *sensor_sram, *sensor_xaui;
  int value_adc, value_disabled, value_fft, value_sram, value_xaui;
  int status_adc, status_disabled, status_fft, status_sram, status_xaui;
  char buffer[BUFFER];

  sensor_adc      = &(n->n_sensors[FMON_SENSOR_ADC_OVERRANGE]);
  sensor_disabled = &(n->n_sensors[FMON_SENSOR_ADC_DISABLED]);
  sensor_fft      = &(n->n_sensors[FMON_SENSOR_FFT_OVERRANGE]);
  sensor_sram     = &(n->n_sensors[FMON_SENSOR_SRAM]);
  sensor_xaui     = &(n->n_sensors[FMON_SENSOR_LINK]);

  snprintf(buffer, BUFFER - 1, "fstatus%d", number);
  buffer[BUFFER - 1] = '\0';

#ifdef DEBUG
  fprintf(stderr, "checking status %s\n", buffer);
#endif

  result = 0;

  if(read_word_fmon(f, buffer, &word)){
    value_adc       = 1;
    status_adc      = KATCP_STATUS_UNKNOWN;

    value_disabled  = 0;
    status_disabled = KATCP_STATUS_UNKNOWN;

    value_fft       = 1;
    status_fft      = KATCP_STATUS_UNKNOWN;

    value_sram      = 0;
    status_sram     = KATCP_STATUS_UNKNOWN;

    value_xaui      = 0;
    status_xaui     = KATCP_STATUS_UNKNOWN;

#ifdef DEBUG
    fprintf(stderr, "check: unable to check fstatus, dropping connection\n");
#endif

    drop_connection_fmon(f);

    f->f_grace      = 0;
    result          = (-1);

  } else {
#ifdef DEBUG
    fprintf(stderr, "got status 0x%08x from %s\n", word, n->n_label);
#endif
    value_adc       = (word & FMON_FSTATUS_ADC_OVERRANGE(f)) ? 1 : 0;
    status_adc      = value_adc ? KATCP_STATUS_ERROR : KATCP_STATUS_NOMINAL;

    value_disabled  = (word & FMON_FSTATUS_ADC_DISABLED(f)) ? 1 : 0;
    if(value_disabled){
      if(f->f_grace < FMON_INIT_PERIOD){
        status_disabled = KATCP_STATUS_UNKNOWN;
      } else {
        status_disabled = KATCP_STATUS_ERROR;
      }
    } else {
      status_disabled = KATCP_STATUS_NOMINAL;
    }

#ifdef DEBUG
    fprintf(stderr, "mode=%d, adc-overrange=%d, adc-disabled=%d, adc-disabled-status=%d, grace=%d\n", f->f_mode, value_adc, value_disabled, status_disabled, f->f_grace);
#endif

    value_fft       = (word & FMON_FSTATUS_FFT_OVERRANGE(f)) ? 1 : 0;
    status_fft      = value_fft ? KATCP_STATUS_ERROR : KATCP_STATUS_NOMINAL;

    value_sram      = (word & FMON_FSTATUS_SDRAM_BAD(f)) ? 0 : 1;
    status_sram     = value_sram ? KATCP_STATUS_NOMINAL : KATCP_STATUS_ERROR;

    value_xaui      = (word & FMON_FSTATUS_XAUI_LINKBAD(f)) ? 0 : 1;
    status_xaui     = value_xaui ? KATCP_STATUS_NOMINAL : KATCP_STATUS_ERROR;

    if(value_adc || value_disabled || value_fft || (value_sram == 0) || (value_xaui == 0)){
      if(f->f_grace >= FMON_INIT_PERIOD){ /* only clear status outside initial grace period */
        f->f_dirty = 1;
      } else {
        log_message_katcl(f->f_report, KATCP_LEVEL_DEBUG, f->f_server, "not clearing status yet, we are  are %ds post reconnect", f->f_grace);
      }
    } else { /* but shorten grace period if everything worked out */
#if 0
      log_message_katcl(f->f_report, KATCP_LEVEL_DEBUG, f->f_server, "all ok after %ds post startup, assuming good from now on", f->f_grace);
#endif
      f->f_grace    = FMON_INIT_PERIOD;
    }

    if(status_sram == KATCP_STATUS_ERROR){

      if(f->f_grace < FMON_INIT_PERIOD){
#if 0
        log_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_server, "qdr not synchronised yet ... giving it time");
#endif
      } else {
        log_message_katcl(f->f_report, KATCP_LEVEL_WARN, f->f_server, "qdr not synchronised after %d, attempting reset", f->f_grace);

        word = FMON_QDRCTRL_RESET;
        if(write_word_fmon(f, "qdr0_ctrl", word)){
          log_message_katcl(f->f_report, KATCP_LEVEL_ERROR, f->f_server, "unable to reset qdr");
        }
      }
    }

  }

  update_sensor_fmon(f, sensor_adc,      value_adc,      status_adc);
  update_sensor_fmon(f, sensor_disabled, value_disabled, status_disabled);
  update_sensor_fmon(f, sensor_fft,      value_fft,      status_fft);
  update_sensor_fmon(f, sensor_sram,     value_sram,     status_sram);
  update_sensor_fmon(f, sensor_xaui,     value_xaui,     status_xaui);

  return result;  
#undef BUFFER
}

int check_power_fengine_fmon(struct fmon_state *f, struct fmon_input *n, unsigned int number)
{
#define BUFFER 32
  uint32_t word;
  double result, dbm, corrected, plain;
  struct fmon_sensor *raw, *pow;
  unsigned int value;
  int status;
  char buffer[BUFFER];

  raw = &(n->n_sensors[FMON_SENSOR_ADC_RAW_POWER]);
  pow = &(n->n_sensors[FMON_SENSOR_ADC_DBM_POWER]);

  snprintf(buffer, BUFFER - 1, "adc_sum_sq%d", number);
  buffer[BUFFER - 1] = '\0';

#ifdef DEBUG
  fprintf(stderr, "checking sums %s\n", buffer);
#endif

  if(read_word_fmon(f, buffer, &word)){
    update_sensor_status_fmon(f, raw, KATCP_STATUS_UNKNOWN);
    return 0;
  }

  value = word;

  plain = sqrt((double)value / ((double)f->f_amplitude_acc_len));

  result = plain * f->f_adc_scale_factor;

  dbm = 10.0 * log10(result * result / 50.0 * 1000.0);

#ifdef DEBUG
  fprintf(stderr, "raw value 0x%x (/%d) -> %f (*%f) -> %f -> %f (-%f)\n", 
  value, f->f_amplitude_acc_len, 
  plain, f->f_adc_scale_factor, 
  result, 
  dbm, n->n_rf_gain);
#endif

  update_sensor_double_fmon(f, raw, plain, KATCP_STATUS_NOMINAL);

  snprintf(buffer, BUFFER - 1, "adc_ctrl%d", number);
  buffer[BUFFER - 1] = '\0';

  if(!read_word_fmon(f, buffer, &word)){
    n->n_rf_enabled = (word & 0x80000000) ? 1 : 0;
    n->n_rf_gain = 20.0 - (word & 0x3f) * 0.5;
  } else {
    n->n_rf_enabled = 0;
  }

  if(n->n_rf_enabled){

    corrected = dbm - n->n_rf_gain;
    status = KATCP_STATUS_NOMINAL;

    if(corrected > FMON_KATADC_WARN_HIGH){
      if(corrected > FMON_KATADC_ERR_HIGH){
        status = KATCP_STATUS_ERROR;
      } else {
        status = KATCP_STATUS_WARN;
      }
    }

    if(corrected < FMON_KATADC_WARN_LOW){
      if(corrected < FMON_KATADC_ERR_LOW){
        status = KATCP_STATUS_ERROR;
      } else {
        status = KATCP_STATUS_WARN;
      }
    }

    update_sensor_double_fmon(f, pow, corrected, status);

  } else {
    update_sensor_double_fmon(f, pow, FMON_KATADC_ERR_HIGH, KATCP_STATUS_UNKNOWN);
  }

  return 0;  
#undef BUFFER
}

int check_watchdog_fmon(struct fmon_state *f)
{
#if 0
  int result;
#endif

  if(f->f_something){
    f->f_something = 0;
    return 0;
  }

  if(maintain_fmon(f) < 0){
    return -1;
  }

  if(append_string_katcl(f->f_line, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, "?watchdog") < 0){
    drop_connection_fmon(f);
    return -1;
  }

  return collect_io_fmon(f);
}

int check_inputs_fengine_fmon(struct fmon_state *f)
{
  int i; 
  int result;

  if((f->f_board < 0) || (f->f_fs <= 0)){
    return 0;
  }

  f->f_dirty = 0;

#ifdef DEBUG
  fprintf(stderr, "checking all\n");
#endif

  result = 0;
  for(i = 0; i < f->f_fs; i++){
    result += check_status_fengine_fmon(f, &(f->f_inputs[i]), i);
    result += check_power_fengine_fmon(f, &(f->f_inputs[i]), i);
  }

  if(f->f_dirty){
    log_message_katcl(f->f_report, KATCP_LEVEL_TRACE, f->f_server, "clearing status bits");
    clear_control_fmon(f);
  }

  return result;
}

int check_clock_fengine_fmon(struct fmon_state *f)
{
  uint32_t word;
  struct fmon_sensor *sensor_clock;
  int value_clock, status_clock;
  int delta;

  if(f->f_fs > 0){
    sensor_clock   = &(f->f_sensors[FMON_SENSOR_CLOCK]);

    if(read_word_fmon(f, "clk_frequency", &word)){
      status_clock = KATCP_STATUS_UNKNOWN;
      value_clock = 0;

      f->f_clock_err = 0;
    } else {

      delta = FMON_GOOD_DSP_CLOCK - word;
      if(abs(delta) > 1){ /* major clock problem */
        status_clock = KATCP_STATUS_ERROR;
        value_clock = 0;
      } else if(abs(delta + f->f_clock_err) > 1){ /* still major clock problem */
        status_clock = KATCP_STATUS_ERROR;
        value_clock = 0;
      } else {
#if 0
        if(delta == 0){ /* clock perfect */
#endif
          status_clock = KATCP_STATUS_NOMINAL;
#if 0
        } else {        /* clock kindof ok */
          status_clock = KATCP_STATUS_WARN;
        }
#endif
        value_clock = 1;
      }

      f->f_clock_err = delta;
    }

    update_sensor_fmon(f, sensor_clock, value_clock, status_clock);
  }

  return 0;
}

int check_basic_xengine_fmon(struct fmon_state *f)
{
#define BUFFER 128
  int i, problems, result, status;
  uint32_t vector_error, reorder_error, rx_error, gbe_rx_error, gbe_tx_error;
  unsigned long total;
  char buffer[BUFFER];
  struct fmon_sensor *s;

  if(f->f_xs <= 0){
    return 0;
  }

  problems = 0;

  for(i = 0; i < f->f_xs; i++){
    total = 0;

    snprintf(buffer, BUFFER - 1, "vacc_err_cnt%d", i);
    buffer[BUFFER - 1] = '\0';
    result = read_word_fmon(f, buffer, &vector_error);
    if(result){
      break;
    }
    total += vector_error;

    snprintf(buffer, BUFFER - 1, "pkt_reord_err%d", i);
    buffer[BUFFER - 1] = '\0';
    result = read_word_fmon(f, buffer, &reorder_error);
    if(result){
      break;
    }
    total += reorder_error;

#if 0
    snprintf(buffer, BUFFER - 1, "pkt_reord_err%d", i);
    buffer[BUFFER - 1] = '\0';
    result = read_word_fmon(f, buffer, &reorder_error);
    if(result){
      break;
    }
    total += reorder_error;
#endif

    if(f->f_xe_errors[i] < total){
      sync_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_server, "encountered xengine%d errors: vector-accumulator=%u reorder=%u", i, vector_error, reorder_error);
      problems++;
    }
    f->f_xe_errors[i] = total;

  }

  if(result >= 0){
    for(i = 0; i < f->f_xp_count; i++){
      total = 0;

      snprintf(buffer, BUFFER - 1, "gbe_tx_err_cnt%d", i);
      buffer[BUFFER - 1] = '\0';
      result = read_word_fmon(f, buffer, &gbe_tx_error);
      if(result){
        break;
      }
      total += gbe_tx_error;

      snprintf(buffer, BUFFER - 1, "gbe_rx_err_cnt%d", i);
      buffer[BUFFER - 1] = '\0';
      result = read_word_fmon(f, buffer, &gbe_rx_error);
      if(result){
        break;
      }
      total += gbe_rx_error;

      snprintf(buffer, BUFFER - 1, "rx_err_cnt%d", i);
      buffer[BUFFER - 1] = '\0';
      result = read_word_fmon(f, buffer, &rx_error);
      if(result){
        break;
      }
      total += rx_error;

      if(f->f_xp_errors[i] < total){
        sync_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_server, "encountered xengine port%d errors: rx=%u, gbe-rx=%u, gbe-tx=%u", i, rx_error, gbe_rx_error, gbe_tx_error);
        problems++;
      }
      f->f_xp_errors[i] = total;
    }
  }

  if(problems){
    f->f_x_threshold++;
    if(f->f_x_threshold >= FMON_XENG_THRESHOLD){
      status = KATCP_STATUS_ERROR;
    } else {
      status = KATCP_STATUS_WARN;
    }
  } else {
    f->f_x_threshold = 0;
    status = KATCP_STATUS_NOMINAL;
  }

  if(result < 0){
    drop_connection_fmon(f);
    return result;
  }

  s = &(f->f_sensors[FMON_SENSOR_LRU]);
  update_sensor_fmon(f, s, problems ? 0 : 1, status);

  return 0;
#undef BUFFER
}

/* main related **********************************************************/

void usage(char *app)
{
  printf("usage: %s [-t timeout] [-s server] [-h] [-r] [-l] [-v] [-q] [-b id] [server [id]]\n", app);
  printf("\n");

  printf("-h                this help\n");
  printf("-v                increase verbosity\n");
  printf("-q                operate quietly\n");
  printf("-b                fix board number rather than autodetect\n");

  printf("-s server:port    select the server to contact\n");
  printf("-t milliseconds   command timeout in ms\n");
  printf("-i milliseconds   interval between polls in ms\n");
  printf("-r count          reprobe count in poll intervals\n");

  printf("\n");
  printf("return codes:\n");
  printf("\n");
  printf("0                 success\n");
  printf("1                 logic failure\n");
  printf("2                 communications failure\n");
  printf("3                 other permanent failures\n");
}

int main(int argc, char **argv)
{
  int i, j, c, g;
  char *app, *server;
  int verbose, interval, reprobe;
  struct fmon_state *f;
  unsigned int timeout;
  struct sigaction sag;
  unsigned int fixed;

  verbose = 1;
  i = j = 1;
  g = 0;
  app = "fmon";
  timeout = 0;
  interval = 0;
  reprobe = (-1);
  fixed = (-1);

  if(strncmp(argv[0], "roach", 5) == 0){
    server = argv[0];
  } else {
    server = getenv("KATCP_SERVER");
    if(server == NULL){
      server = "localhost";
    }
  }

  while (i < argc) {
#ifdef DEBUG
    fprintf(stderr, "main: considering argument [%d]=%s\n", i, argv[i]);
#endif
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(app);
          return 0;

        case 'v' : 
          verbose++;
          j++;
          break;

#if 0
        case 'f' : 
          fs += 2;
          j++;
          break;
        case 'x' : 
          xs += 2;
          j++;
          break;
#endif

        case 'q' : 
          verbose = 0;
          j++;
          break;

        case 't' :
        case 's' :
        case 'i' :
        case 'r' :
#if 0        
        case 'e' :
#endif

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }
          if (i >= argc) {
            fprintf(stderr, "%s: argument needs a parameter\n", app);
            return 2;
          }

          switch(c){
            case 's' :
              server = argv[i] + j;
              break;
            case 't' :
              timeout = atoi(argv[i] + j);
#ifdef DEBUG
              fprintf(stderr, "%s: new timeout is %u\n", app, timeout);
#endif
              break;
            case 'i' :
              interval = atoi(argv[i] + j);
#ifdef DEBUG
              fprintf(stderr, "%s: new interval is %u\n", app, timeout);
#endif
              break;
            case 'r' :
              reprobe = atoi(argv[i] + j);
              break;
            case 'b' :
              fixed = atoi(argv[i] + j);
              break;
          }

          i++;
          j = 1;
          break;

        case '-' :
          j++;
          break;

        case '\0':
          j = 1;
          i++;
          break;
        default:
          fprintf(stderr, "%s: unknown option -%c\n", app, argv[i][j]);
          return 2;
      }
    } else {
      switch(g){
        case 0 :
          server = argv[i];
          break;
        case 1 :
          fixed = atoi(argv[i]);
          break;
        default :
          fprintf(stderr, "%s: execess parameter %s\n", app, argv[i]);
          return 2;
      }
      i++;
      g++;
    }
  }

  if(reprobe < 0){
    if(strncmp(server, "roach", 5)){
      reprobe = 0;
    } else {
      reprobe = 1;
    }
  }

  sag.sa_handler = handle_signal;
  sigemptyset(&(sag.sa_mask));
  sag.sa_flags = SA_RESTART;

  sigaction(SIGHUP, &sag, NULL);
  sigaction(SIGTERM, &sag, NULL);

  if(interval <= 0){
    interval = FMON_DEFAULT_INTERVAL;
  }

  if(timeout <= 0){
    timeout = FMON_DEFAULT_TIMEOUT;
  }

  f = create_fmon(server, verbose, timeout, reprobe, fixed);
  if(f == NULL){
    fprintf(stderr, "%s: unable to allocate monitoring state\n", app);
    return 2;
  }

  /* we rely on the side effect to flush out the sensor list detail too */
  sync_message_katcl(f->f_report, KATCP_LEVEL_INFO, server, "starting monitoring routines");

#ifdef DEBUG
  fprintf(stderr, "server %s, reprobe %d\n", f->f_server, f->f_reprobe);
#endif


  for(run = 1; run > 0; ){
    set_timeout_fmon(f, timeout);

    maintain_fmon(f); /* might have to check return code, but if we do we skip checks which set sensors to unknown on failure ?  */

    check_clock_fengine_fmon(f);
    check_inputs_fengine_fmon(f);

    check_basic_xengine_fmon(f);

    check_watchdog_fmon(f); /* only gets done if nothing else happened */

    if(catchup_fmon(f, interval) < 0){
      run = 0;
    }

    if(check_parent(f) < 0){
      run = 0;
    }

    if(f->f_grace <= FMON_INIT_PERIOD){
      f->f_grace += interval;
    }
  }

  sync_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_server, "%s sensor monitoring logic for %s", (run < 0) ? "restarting" : "stopping", f->f_server);

  if(run < 0){
    execvp(argv[0], argv);

    sync_message_katcl(f->f_report, KATCP_LEVEL_WARN, f->f_server, "unable to restart %s: %s", argv[0], strerror(errno));
    return 2;
  }

  if(f){
    destroy_fmon(f);
  }

  return 0;
}
