/* module to perform monitoring on fengine gateware */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#include <arpa/inet.h>

#include <sys/time.h>
#include <sys/select.h>

#include <katcp.h>
#include <katcl.h>
#include <katpriv.h>

#define FMON_DEFAULT_INTERVAL 5000
#define FMON_DEFAULT_TIMEOUT  5000

#define FMON_SENSOR_LRU     0
#define FMON_SENSOR_CLOCK   1

#define FMON_BOARD_SENSORS  2

#define FMON_GOOD_DSP_CLOCK 200000000

#define FMON_INPUTS         2

#define FMON_MAX_ENGINES                  128

#define FMON_CONTROL_CLEAR_STATUS      0x0008

#define FMON_FSTATUS_QUANT_OVERRANGE   0x0001
#define FMON_FSTATUS_FFT_OVERRANGE     0x0002
#define FMON_FSTATUS_ADC_OVERRANGE     0x0004
#define FMON_FSTATUS_ADC_DISABLED      0x0010

#define FMON_INPUT_SENSORS  3

#define FMON_SENSOR_ADC_OVERRANGE           0
#define FMON_SENSOR_ADC_DISABLED            1
#define FMON_SENSOR_FFT_OVERRANGE           2

static char inputs_fmon[FMON_INPUTS] = { 'x', 'y' };

static char *board_sensors_fmon[FMON_BOARD_SENSORS] = { "lru.available", "fpga.clock" };
static char *input_sensors_fmon[FMON_INPUT_SENSORS] = { "%s.adc.overrange", "%s.adc.grounded", "%s.fft.overrange" };

struct fmon_sensor{
  char *s_name;
  int s_value;
  int s_status;
};

struct fmon_input{
  struct fmon_sensor n_sensors[FMON_INPUT_SENSORS];
  char *n_label;
};

struct fmon_state
{
  int f_verbose;
  char *f_server;

  struct timeval f_start;
  struct timeval f_when;
  struct timeval f_done;
  int f_maintaining;

  struct katcl_line *f_line;
  struct katcl_line *f_report;

  int f_engine;
  char *f_symbolic;

  struct fmon_input f_inputs[FMON_INPUTS];
  int f_dirty;

  struct fmon_sensor f_sensors[FMON_BOARD_SENSORS];
};

/*************************************************************************/

int update_sensor_fmon(struct fmon_state *f, struct fmon_sensor *s, int value, unsigned int status);

/*************************************************************************/

void usage(char *app)
{
  printf("usage: %s [-t timeout] [-s server] [-h] [-r] [-l] [-v] [-q] commands\n", app);
  printf("\n");

  printf("-h                this help\n");
  printf("-v                increase verbosity\n");
  printf("-q                operate quietly\n");
  printf("-r                restart on transient failures\n");
  printf("-i                poll interval\n");
  printf("-e                engine number\n");

  printf("-s server:port    select the server to contact\n");
  printf("-t milliseconds   set a command timeout in ms\n");

  printf("\n");
  printf("return codes:\n");
  printf("\n");
  printf("0                 success\n");
  printf("1                 logic failure\n");
  printf("2                 communications failure\n");
  printf("3                 other permanent failures\n");
}

/**************************************************************************/

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

void pause_fmon(struct fmon_state *f, unsigned int interval)
{
  struct timeval delta, target;

  delta.tv_sec = interval / 1000;
  delta.tv_usec = (interval % 1000) * 1000;

  gettimeofday(&(f->f_done), NULL);

  add_time_katcp(&target, &(f->f_start), &delta);
  
  if(cmp_time_katcp(&(f->f_done), &target) <= 0){
    sub_time_katcp(&delta, &target, &(f->f_done));

    select(0, NULL, NULL, NULL, &delta);
  }
}

void set_lru_fmon(struct fmon_state *f, int value, unsigned int status)
{
  struct fmon_sensor *sensor_lru;

  sensor_lru = &(f->f_sensors[FMON_SENSOR_LRU]);

  update_sensor_fmon(f, sensor_lru, value, status);

  while(flushing_katcl(f->f_report)){
    write_katcl(f->f_report);
  }
}

int maintain_fmon(struct fmon_state *f)
{
  struct timeval now, delta;

  if(f->f_maintaining){
    sync_message_katcl(f->f_report, KATCP_LEVEL_ERROR, f->f_symbolic ? f->f_symbolic : f->f_server, "calling fmon maintain logic within maintain");
    return -1;
  }

  f->f_maintaining = 1;

  while(f->f_line == NULL){

#ifdef DEBUG
    fprintf(stderr, "maintain: attemping reconnect to %s\n", f->f_server);
#endif
  
    f->f_line = create_name_rpc_katcl(f->f_server);

    if(f->f_line == NULL){

      gettimeofday(&now, NULL);

      if(cmp_time_katcp(&(f->f_when), &now) <= 0){
        set_lru_fmon(f, 0, KATCP_STATUS_ERROR);
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
  }

  set_lru_fmon(f, 1, KATCP_STATUS_NOMINAL);
  f->f_maintaining = 0;

  return 0;
}

int read_word_fmon(struct fmon_state *f, char *name, uint32_t *value)
{
  int result[4], status, i;
  int expect[4] = { 6, 0, 2, 2 };
  char *ptr;
  uint32_t tmp;

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
      return -1;
    }
  }

  while((status = complete_rpc_katcl(f->f_line, 0, &(f->f_when))) == 0);
#ifdef DEBUG
  fprintf(stderr, "read: status is %d\n", status);
#endif
  if(status < 0){
    if(f->f_line){
      destroy_rpc_katcl(f->f_line);
      f->f_line = NULL;
      set_lru_fmon(f, 0, KATCP_STATUS_WARN);
    }
    return -1;
  }

  ptr = arg_string_katcl(f->f_line, 1);
  if(ptr == NULL){
    return -1;
  }

  if(strcmp(ptr, KATCP_OK)){
    return 1;
  }

  status = arg_buffer_katcl(f->f_line, 2, &tmp, 4);
  if(status != 4){
    return -1;
  }

  *value = ntohl(tmp);

  return 0;
}

int write_word_fmon(struct fmon_state *f, char *name, uint32_t value)
{
  int result[4], status, i;
  int expect[4] = { 7, 0, 2, 5 };
  char *ptr;
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
      return -1;
    }
  }

  while((status = complete_rpc_katcl(f->f_line, 0, &(f->f_when))) == 0);
  if(status < 0){
    if(f->f_line){
      destroy_rpc_katcl(f->f_line);
      f->f_line = NULL;
      set_lru_fmon(f, 0, KATCP_STATUS_WARN);
    }
#ifdef DEBUG
    fprintf(stderr, "write: complete call failed\n");
#endif
    return -1;
  }

  ptr = arg_string_katcl(f->f_line, 1);
  if(ptr == NULL){
#ifdef DEBUG
    fprintf(stderr, "write: unable to acquire first parameter\n");
#endif
    return -1;
  }

  if(strcmp(ptr, KATCP_OK)){
#ifdef DEBUG
    fprintf(stderr, "write: problematic return code %s\n", ptr);
#endif
    return 1;
  }

  return 0;
}

/****************************************************************************/

int print_intbool_list_fmon(struct fmon_state *f, char *name, char *description, char *units)
{
  append_string_katcl(f->f_report, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#sensor-list");
  append_string_katcl(f->f_report,                    KATCP_FLAG_STRING, name);
  append_string_katcl(f->f_report,                    KATCP_FLAG_STRING, description);
  append_string_katcl(f->f_report,                    KATCP_FLAG_STRING, units);
  append_string_katcl(f->f_report,  KATCP_FLAG_LAST | KATCP_FLAG_STRING, "boolean");

  return 0;
}

int list_board_sensors_fmon(struct fmon_state *f)
{
  struct fmon_sensor *s;

  s = &(f->f_sensors[FMON_SENSOR_LRU]);
  print_intbool_list_fmon(f, s->s_name, "line replacement unit", "none");

  s = &(f->f_sensors[FMON_SENSOR_CLOCK]);
  print_intbool_list_fmon(f, s->s_name, "signal processing clock", "none");

  return 0;
}

int list_input_sensors_fmon(struct fmon_state *f)
{
  int i; 
  struct fmon_input *n;
  struct fmon_sensor *s;

  for(i = 0; i < FMON_INPUTS; i++){

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

/****************************************************************************/

int print_intbool_status_fmon(struct fmon_state *f, struct fmon_sensor *s)
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
  append_unsigned_long_katcl(f->f_report, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, s->s_value);

  return 0;
}

/****************************************************************************/

int clear_labels_fmon(struct fmon_state *f)
{
  int i, j;
  struct fmon_sensor *s;
  struct fmon_input *n;

  for(i = 0; i < FMON_INPUTS; i++){
    n = &(f->f_inputs[i]);

    for(j = 0; j < FMON_INPUT_SENSORS; j++){
      s = &(n->n_sensors[j]);

      if(s->s_name == NULL){
        free(s->s_name);
        s->s_name = NULL;
      }
    }

    if(n->n_label){
      free(n->n_label);
      n->n_label = NULL;
    }
  }

  return 0;
}

int make_labels_fmon(struct fmon_state *f, unsigned int engine)
{
#define BUFFER 128
  struct fmon_input *n;
  struct fmon_sensor *s;
  int i, j;
  char buffer[BUFFER];

  if(engine < 0){
    return -1;
  }

  if(engine == f->f_engine){
    return 0;
  }

  if(f->f_engine >= 0){
    log_message_katcl(f->f_report, KATCP_LEVEL_WARN, f->f_symbolic ? f->f_symbolic : f->f_server, "roach %s is changing personality from fengine%d to fengine%d", f->f_server, f->f_engine, engine);
    /* TODO: maybe set sensors to unknown */
    clear_labels_fmon(f);
  }

  f->f_engine = (-1);

  i = strlen("fengine") + 8;
  f->f_symbolic = malloc(i);
  if(f->f_symbolic == NULL){
    return -1;
  }
  snprintf(f->f_symbolic, i - 1, "fengine%d", engine);
  f->f_symbolic[i - 1] = '\0';

  for(i = 0; i < FMON_INPUTS; i++){
    n = &(f->f_inputs[i]);

    snprintf(buffer, BUFFER - 1, "%d%c", engine, inputs_fmon[i]);
    buffer[BUFFER - 1] = '\0';

    n->n_label = strdup(buffer);
    if(n->n_label == NULL){
      return -1;
    }

    for(j = 0; j < FMON_INPUT_SENSORS; j++){
      s = &(n->n_sensors[j]);

      snprintf(buffer, BUFFER - 1, input_sensors_fmon[j], n->n_label);
      buffer[BUFFER - 1] = '\0';

      s->s_name = strdup(buffer);
      if(s->s_name == NULL){
        return -1;
      }
    }
  }

  f->f_engine = engine;

  list_input_sensors_fmon(f);

  return 0;

#undef BUFFER
}

void query_rcs_fmon(struct fmon_state *f, char *label, char *reg)
{
  uint32_t value;
  int dirty;

  if(read_word_fmon(f, reg, &value) < 0){
    return;
  }

  append_string_katcl(f->f_report, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#version");
  append_string_katcl(f->f_report,                    KATCP_FLAG_STRING, label);

  if(value & (1 << 31)){
    append_args_katcl(f->f_report, KATCP_FLAG_LAST | KATCP_FLAG_STRING, "t-%u", value);
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
  query_rcs_fmon(f, "gateware-fengine", "rcs_app");
  query_rcs_fmon(f, "gateware-infrastructure", "rcs_lib");
}

int discover_engine_fmon(struct fmon_state *f)
{
  uint32_t word;

  if(read_word_fmon(f, "board_id", &word) < 0){
    return -1;
  }
  
  if(word > FMON_MAX_ENGINES){
    log_message_katcl(f->f_report, KATCP_LEVEL_WARN, f->f_symbolic ? f->f_symbolic : f->f_server, "rather large fengine id %d reported by roach %s", word, f->f_server);
  } else {
    log_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_symbolic ? f->f_symbolic : f->f_server, "roach %s claims fengine%d", f->f_server, word);
  }

  make_labels_fmon(f, word);

  return 0;
}

/******************************************************************************/

void destroy_fmon(struct fmon_state *f)
{
  int i;
  struct fmon_sensor *s;

  if(f == NULL){
    return;
  }

  for(i = 0; i < FMON_BOARD_SENSORS; i++){
    s = &(f->f_sensors[i]);

    if(s->s_name == NULL){
      free(s->s_name);
      s->s_name = NULL;
    }
  }

  if(f->f_line){
    destroy_rpc_katcl(f->f_line);
    f->f_line = NULL;
  }

  if(f->f_report){
    destroy_rpc_katcl(f->f_report);
    f->f_report = NULL;
  }

  if(f->f_server){
    free(f->f_server);
    f->f_server = NULL;
  }

  clear_labels_fmon(f);

  f->f_engine = (-1);

  free(f);
}

struct fmon_state *create_fmon(char *server, int verbose, unsigned int timeout, int engine)
{
  struct fmon_state *f;
  struct fmon_sensor *s;
  struct fmon_input *n;
  int i, j;

  f = malloc(sizeof(struct fmon_state));
  if(f == NULL){
    return NULL;
  }

  f->f_verbose = verbose;
  f->f_server = NULL;
  set_timeout_fmon(f, timeout);

  f->f_line = NULL;
  f->f_report = NULL;
  f->f_engine = (-1);
  f->f_symbolic = NULL;

  f->f_maintaining = 0;

  for(i = 0; i < FMON_BOARD_SENSORS; i++){
    s = &(f->f_sensors[i]);
    s->s_name = NULL;
    s->s_value = 0;
    s->s_status = KATCP_STATUS_UNKNOWN;
  }

  for(i = 0; i < FMON_INPUTS; i++){
    n = &(f->f_inputs[i]);

    for(j = 0; j < FMON_INPUT_SENSORS; j++){
      s = &(n->n_sensors[j]);

      s->s_name = NULL;
      s->s_value = 0;
      s->s_status = KATCP_STATUS_UNKNOWN;
    }

    n->n_label = NULL;
  }

  f->f_server = strdup(server);
  if(f->f_server == NULL){
    destroy_fmon(f);
    return NULL;
  }

  f->f_line = create_name_rpc_katcl(server);

  f->f_report = create_katcl(STDOUT_FILENO);
  if(f->f_report == NULL){
    destroy_fmon(f);
    return NULL;
  }

  for(i = 0; i < FMON_BOARD_SENSORS; i++){
    s = &(f->f_sensors[i]);

    s->s_name = strdup(board_sensors_fmon[i]);
    if(s->s_name == NULL){
      destroy_fmon(f);
      return NULL;
    }
  }

  list_board_sensors_fmon(f);

  if(engine < 0){
    discover_engine_fmon(f);
  } else {
    make_labels_fmon(f, engine);
  }

  query_versions_fmon(f);

#if 0
  i = strlen("fengine") + 8;
  f->f_symbolic = malloc(i);
  if(f->f_symbolic == NULL){
    return NULL;
  }
  snprintf(f->f_symbolic, i - 1, "fengine%d", engine);
  f->f_symbolic[i - 1] = '\0';

  for(i = 0; i < FMON_INPUTS; i++){
    n = &(f->f_inputs[i]);

    snprintf(buffer, BUFFER - 1, "%d%c", engine, inputs_fmon[i]);
    buffer[BUFFER - 1] = '\0';

    n->n_label = strdup(buffer);
    if(n->n_label == NULL){
      destroy_fmon(f);
      return NULL;
    }

    for(j = 0; j < FMON_INPUT_SENSORS; j++){
      s = &(n->n_sensors[j]);

      snprintf(buffer, BUFFER - 1, input_sensors_fmon[j], n->n_label);
      buffer[BUFFER - 1] = '\0';

      s->s_name = strdup(buffer);
      if(s->s_name == NULL){
        destroy_fmon(f);
        return NULL;
      }
    }
  }
#endif

  return f;
}

/****************************************************************************/

int update_sensor_fmon(struct fmon_state *f, struct fmon_sensor *s, int value, unsigned int status)
{
  int change;

  change = 0;

  if(value != s->s_value){
    s->s_value = value;
    change++;
  }

  if(status != s->s_status){
    s->s_status = status;
    change++;
  }

  if(change){
    print_intbool_status_fmon(f, s);
  }

  return 0;
}

int clear_control_fmon(struct fmon_state *f)
{
  uint32_t word;

  if(read_word_fmon(f, "control", &word) < 0){
    return -1;
  }

  word &= ~FMON_CONTROL_CLEAR_STATUS;
  if(write_word_fmon(f, "control", word) < 0){
    return -1;
  }

  if(read_word_fmon(f, "control", &word) < 0){
    return -1;
  }

  word |= FMON_CONTROL_CLEAR_STATUS;
  if(write_word_fmon(f, "control", word) < 0){
    return -1;
  }

  return 0;
}

int check_status_fmon(struct fmon_state *f, struct fmon_input *n, char *name)
{
  uint32_t word;
  struct fmon_sensor *sensor_adc, *sensor_disabled, *sensor_fft;
  int value_adc, value_disabled, value_fft, status_adc, status_disabled, status_fft;

  sensor_adc   = &(n->n_sensors[FMON_SENSOR_ADC_OVERRANGE]);
  sensor_disabled = &(n->n_sensors[FMON_SENSOR_ADC_DISABLED]);
  sensor_fft   = &(n->n_sensors[FMON_SENSOR_FFT_OVERRANGE]);

  if(read_word_fmon(f, name, &word) < 0){
    value_adc       = 1;
    status_adc      = KATCP_STATUS_UNKNOWN;

    value_disabled  = 1;
    status_disabled = KATCP_STATUS_UNKNOWN;

    value_fft       = 1;
    status_fft      = KATCP_STATUS_UNKNOWN;
  } else {
#ifdef DEBUG
    fprintf(stderr, "got status 0x%08x from %s\n", word, n->n_label);
#endif
    value_adc       = (word & FMON_FSTATUS_ADC_OVERRANGE) ? 1 : 0;
    status_adc      = value_adc ? KATCP_STATUS_ERROR : KATCP_STATUS_NOMINAL;

    value_disabled  = (word & FMON_FSTATUS_ADC_DISABLED) ? 1 : 0;
    status_disabled = value_disabled ? KATCP_STATUS_ERROR : KATCP_STATUS_NOMINAL;

    value_fft       = (word & FMON_FSTATUS_FFT_OVERRANGE) ? 1 : 0;
    status_fft      = value_fft ? KATCP_STATUS_ERROR : KATCP_STATUS_NOMINAL;

    if(value_adc || value_disabled || value_fft){
      f->f_dirty = 1;
    }
  }

  update_sensor_fmon(f, sensor_adc,      value_adc,      status_adc);
  update_sensor_fmon(f, sensor_disabled, value_disabled, status_disabled);
  update_sensor_fmon(f, sensor_fft,      value_fft,      status_fft);

  return 0;  
}

int check_all_status_fmon(struct fmon_state *f)
{
#define BUFFER 32
  int i; 
  char buffer[BUFFER];
  int result;

  if(f->f_engine < 0){
    return 0;
  }

  f->f_dirty = 0;

#ifdef DEBUG
  fprintf(stderr, "checking all\n");
#endif

  for(i = 0; i < FMON_INPUTS; i++){
    snprintf(buffer, BUFFER - 1, "fstatus%d", i);
    buffer[BUFFER - 1] = '\0';

#ifdef DEBUG
    fprintf(stderr, "checking status %s\n", buffer);
#endif

    result += check_status_fmon(f, &(f->f_inputs[i]), buffer);
  }

  if(f->f_dirty){
    log_message_katcl(f->f_report, KATCP_LEVEL_DEBUG, f->f_symbolic ? f->f_symbolic : f->f_server, "clearing status bits");
    clear_control_fmon(f);
  }

  return result;
#undef BUFFER
}

int check_clock_fmon(struct fmon_state *f)
{
  uint32_t word;
  struct fmon_sensor *sensor_clock;
  int value_clock, status_clock;

  sensor_clock   = &(f->f_sensors[FMON_SENSOR_CLOCK]);

  if(read_word_fmon(f, "clk_frequency", &word) < 0){
    status_clock = KATCP_STATUS_UNKNOWN;
    value_clock = 0;
  } else if(word != FMON_GOOD_DSP_CLOCK){
    status_clock = KATCP_STATUS_ERROR;
    value_clock = 0;
  } else {
    status_clock = KATCP_STATUS_NOMINAL;
    value_clock = 1;
  }

  update_sensor_fmon(f, sensor_clock, value_clock, status_clock);

  return 0;
}

/***************************************************/

int main(int argc, char **argv)
{
  int i, j, c, status;
  char *app, *server;
  int verbose, retry, run, engine, interval;
  struct fmon_state *f;
  unsigned int timeout;

  verbose = 1;
  i = j = 1;
  app = "fmon";
  timeout = 0;
  retry = 0;
  interval = 0;
  engine = (-1);

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

        case 'r' : 
          retry = 1;
          j++;
          break;

        case 'q' : 
          verbose = 0;
          j++;
          break;

        case 't' :
        case 's' :
        case 'i' :
        case 'e' :

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
            case 'e' :
              engine = atoi(argv[i] + j);
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
      fprintf(stderr, "%s: bad parameter %s\n", app, argv[i]);
      return 2;
    }
  }

  if(interval <= 0){
    interval = FMON_DEFAULT_INTERVAL;
  }

  if(timeout <= 0){
    timeout = FMON_DEFAULT_TIMEOUT;
  }

  status = 0;

  f = create_fmon(server, verbose, timeout, engine);
  if(f == NULL){
    fprintf(stderr, "%s: unable to allocate word operation state\n", app);
    return 2;
  }

  /* we rely on the side effect to flush out the sensor list detail too */
  sync_message_katcl(f->f_report, KATCP_LEVEL_INFO, f->f_symbolic ? f->f_symbolic : f->f_server, "starting monitoring routines");

  for(run = 1; run; ){

    set_timeout_fmon(f, timeout);

    check_clock_fmon(f);
    check_all_status_fmon(f);

    while(flushing_katcl(f->f_report)){
      write_katcl(f->f_report);
    }

    pause_fmon(f, interval);
  }

  if(f){
    destroy_fmon(f);
  }

  return status;
}
