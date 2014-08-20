/* Module to collect and report system statistics */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sysexits.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include <katcp.h>
#include <katpriv.h>

#define SMON_SENSORS	  	  12
#define SMON_MODULE_NAME       "smon"
#define SMON_POLL_INTERVAL 	1000
#define SMON_POLL_MIN          	  50 


/**********System Monitoring Sensors Template***************************************************/

struct smon_sensor_template{
	char *t_name;
	char *t_description;
	int t_type;
};

/**********System Monitoring Sensors***************************************************/

struct smon_sensor_template sensor_template[SMON_SENSORS] = {
	{"loadavg.1min", 	"system load average for 1min", 	KATCP_SENSOR_FLOAT}, 
	{"loadavg.5min", 	"system load average for 5min", 	KATCP_SENSOR_FLOAT}, 
	{"loadavg.15min", 	"system load average for 15min", 	KATCP_SENSOR_FLOAT}, 
	{"disksize", 		"disksize", 				KATCP_SENSOR_FLOAT}, 
	{"freesize", 		"freediskpace", 			KATCP_SENSOR_FLOAT}, 
	{"memremain", 		"remainmemory", 			KATCP_SENSOR_FLOAT}, 
	{"temp1.input", 	"temp1.input",		 		KATCP_SENSOR_INTEGER}, 
	{"temp2.input", 	"temp2.input",		 		KATCP_SENSOR_INTEGER}, 
	{"temp3.input", 	"temp3.input",		 		KATCP_SENSOR_INTEGER}, 
	{"temp4.input", 	"temp4.input",		 		KATCP_SENSOR_INTEGER}, 
	{"temp5.input", 	"temp5.input",		 		KATCP_SENSOR_INTEGER}, 
	{"temp6.input", 	"temp6.input",		 		KATCP_SENSOR_INTEGER}, 
};


/**********Common Sensor Attributes**************************************************/

struct smon_sensor{
	int s_type;
	char *s_name;
	char *s_description;
	int s_value;
	double s_fvalue;
	int s_status;
};

/**************Memory Usage***************************************************************/

typedef struct mem_struct_t
{
	long int MemTotal;
	long int MemFree;
	long int Cached;
	long int Buffers;

}MemStruct;

/************************************************************/

struct smon_state
{
	char *s_symbolic;
	struct smon_sensor smon_sensors[SMON_SENSORS];

	/****Temperature sensors monitoring variables******/
	int *temp_sensor_fd;
	int no_sensors;
	int *temp_limit;
	int *temp_val;
};
/************************************************************/
volatile int run;
/************************************************************/

/*************Parsing /proc/meminfo*****************************************/

MemStruct parse_meminfo(struct katcp_dispatch *d)
{
	/* read /proc/meminfo */
	FILE* meminfo;
	MemStruct m;
	char buffer[100] = {0};
	char *end;
	int found = 0;

	if(!(meminfo = fopen("/proc/meminfo", "r"))){
		log_message_katcp(d, KATCP_LEVEL_ERROR, SMON_MODULE_NAME, "Could not open /proc/meminfo [%s]\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	while(fgets(buffer, sizeof(buffer), meminfo)){
		/* MemTotal is the total memory */
		if(strstr(buffer, "MemTotal:") == buffer){
			m.MemTotal = strtol(buffer + 9, &end, 10 );
			found++;
		}
		/* MemFree is the current free memory */
		else if(strstr(buffer, "MemFree:") == buffer){
			m.MemFree = strtol(buffer + 8, &end, 10 );
			found++;
		}
		/* Buffers */
		else if(strstr(buffer, "Buffers:") == buffer){
			m.Buffers = strtol(buffer + 8, &end, 10 );
			found++;
		}
		/* Cached */
		else if(strstr( buffer, "Cached:") == buffer){
			m.Cached = strtol(buffer + 8, &end, 10 );
			found++;
			break;
		}
	}
	fclose(meminfo);
	/* Loosly check if we got all we need */
	if(found != 4){
		log_message_katcp(d, KATCP_LEVEL_ERROR, SMON_MODULE_NAME, "Could not parse /proc/meminfo\n");
		exit( EXIT_FAILURE );
	}
	return m;

}

int make_temp_labels(struct katcp_dispatch *d, struct smon_state *s)
{
	int i = 0;
	int k = 0;
	char *tmp_char;
	int fd;
	char buf[32];
	int *tmp_int;

	k = (strlen("/sys/class/hwmon/hwmon0/temp_input") + 8);

	tmp_char = realloc(s->s_symbolic, k); 
	if(tmp_char == NULL){
		return -1;
	}

	s->s_symbolic = tmp_char;

	do{
		snprintf(s->s_symbolic, k - 1, "/sys/class/hwmon/hwmon0/temp%d_input", i + 1);
		s->s_symbolic[k - 1] = '\0';
		fd = open(s->s_symbolic, O_RDONLY);
		if(fd < 0 && errno == ENOENT){
			log_message_katcp(d, KATCP_LEVEL_TRACE, SMON_MODULE_NAME, "Nothing more to open, %d sensors \n", i);
			break;
		}else{
			tmp_int = realloc(s->temp_sensor_fd, (i + 1) * sizeof(int)); 
			if(tmp_int == NULL){
				log_message_katcp(d, KATCP_LEVEL_TRACE, SMON_MODULE_NAME, "s->temp_sensor_fd realloc failed\n");
				return -1;
			}
			s->temp_sensor_fd = tmp_int;
			s->temp_sensor_fd[i] = fd;
			snprintf(s->s_symbolic, k - 1, "/sys/class/hwmon/hwmon0/temp%d_max", i + 1);
			fd = open(s->s_symbolic, O_RDONLY);
			if(fd < 0 && errno == ENOENT){
#if 0
				fprintf( stderr, "No max file exists\n");
#endif
			}else{
				if(read(fd, &buf, sizeof(buf)) > 0){
					tmp_int = realloc(s->temp_limit, (i + 1) * sizeof(int)); 
					if(tmp_int == NULL){
						log_message_katcp(d, KATCP_LEVEL_ERROR, SMON_MODULE_NAME, "s->temp_limit realloc failed\n");
						return -1;
					}
					s->temp_limit = tmp_int;

					s->temp_limit[i] = atoi(buf);
					close(fd);
				}else{
					log_message_katcp(d, KATCP_LEVEL_ERROR, SMON_MODULE_NAME, "Failed to read %s\n", s->s_symbolic);
				}
			}
		}
		i++;
	}while(1);

	s->no_sensors = i;

	tmp_char = NULL;
	tmp_int = NULL;


	return 0;
}

/***************Reading Temperature Sensor Values******************************************/

int read_temp_sensors(struct katcp_dispatch *d, struct smon_state *s)
{
	char buf[32];
	int i = 0;
	int found = 0;


	while(i < s->no_sensors){
		/* pread was used so that it seeks back to origin offset*/
		if(pread(s->temp_sensor_fd[i], &buf, sizeof(buf), 0) > 0){
			s->temp_val[i] = atoi(buf);
		}else{
			log_message_katcp(d, KATCP_LEVEL_ERROR, SMON_MODULE_NAME, "Failed to read\n");
			break;
		}

		i++;
		found ++;
	};
	/* Loosly check if we got all we need */
	if(found != s->no_sensors){
		log_message_katcp(d, KATCP_LEVEL_ERROR, SMON_MODULE_NAME, "Found[%d] Could not parse temp paths properly\n" , found); 
		exit( EXIT_FAILURE );
	}

	return 0;
}

void destroy_smon(struct smon_state *s)
{
	int i;
	struct smon_sensor *ss;

	if(s == NULL){
		return;
	}

	if(s->s_symbolic){
		free(s->s_symbolic);
		s->s_symbolic = NULL;
	}

	for(i = 0; i < SMON_SENSORS; i++){
		ss = &(s->smon_sensors[i]);

		ss->s_type = (-1);

		if(ss->s_name){
			free(ss->s_name);
			ss->s_name = NULL;
		}

		if(ss->s_description){
			free(ss->s_description);
			ss->s_description = NULL;
		}

	}

	s->no_sensors = 0;

	for(i = 0; i < s->no_sensors; i++){
		close(s->temp_sensor_fd[i]);
	}

	if(s->temp_sensor_fd){
		free(s->temp_sensor_fd);
		s->temp_sensor_fd = NULL;
	}

	if(s->temp_limit){
		free(s->temp_limit);
		s->temp_limit = NULL;
	}
	if(s->temp_val){
		free(s->temp_val);
		s->temp_val = NULL;
	}

	free(s);

}

int populate_sensor_smon(struct smon_sensor *s, struct smon_sensor_template *t)
{

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

	s->s_name = strdup(t->t_name);
	if(s->s_name == NULL){
		return -1;
	}

	s->s_description = strdup(t->t_description);
	if(s->s_description == NULL){
		return -1;
	}

	s->s_type = t->t_type;

	return 0;
}
/*****************************************************************************/
struct smon_state *create_smon()
{
	int i;
	struct smon_state *s;
	struct smon_sensor *ss;

	s = malloc(sizeof(struct smon_state));
	if(s == NULL){
		return NULL;
	}

	s->s_symbolic = NULL;

	for(i = 0; i < SMON_SENSORS; i++){
		ss = &(s->smon_sensors[i]);
		ss->s_type = (-1);
		ss->s_name = NULL;
		ss->s_description = NULL;
		ss->s_value = 0;
		ss->s_fvalue = 0.0;
		ss->s_status = KATCP_STATUS_UNKNOWN;
	} 

	for(i = 0; i < SMON_SENSORS; i++){
		ss = &(s->smon_sensors[i]);
		if(populate_sensor_smon(ss, &(sensor_template[i])) < 0){
			destroy_smon(s);
		};
	}
	s->temp_sensor_fd = NULL;
	s->no_sensors = 0;
	s->temp_limit = NULL;
	s->temp_val = NULL;

	return s;
}

/****************************************************************************/

int print_sensor_list_smon(struct katcp_dispatch *d, struct smon_state *s, struct smon_sensor *ss)
{
	append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#sensor-list");
	append_string_katcp(d,                    KATCP_FLAG_STRING, ss->s_name);
	append_string_katcp(d,                    KATCP_FLAG_STRING, ss->s_description);
	append_string_katcp(d,                    KATCP_FLAG_STRING, "none");

	switch(ss->s_type){
		case KATCP_SENSOR_FLOAT : 
			append_string_katcp(d,  KATCP_FLAG_LAST | KATCP_FLAG_STRING, "float");
			break;
		case KATCP_SENSOR_INTEGER : 
			append_string_katcp(d,  KATCP_FLAG_LAST |  KATCP_FLAG_STRING, "integer");
			break;
		case KATCP_SENSOR_BOOLEAN : 
			append_string_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_STRING, "boolean");
			break;
	}

	return 0;
}

int print_sensor_status_smon(struct katcp_dispatch *d,struct smon_state *s, struct smon_sensor *ss)
{
	struct timeval now;
	unsigned int milli;

	gettimeofday(&now, NULL);
	milli = now.tv_usec / 1000;

	append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#sensor-status");
	append_args_katcp(d, KATCP_FLAG_STRING, "%lu%03d", now.tv_sec, milli);
	append_string_katcp(d, KATCP_FLAG_STRING, "1");
	append_string_katcp(d, KATCP_FLAG_STRING, ss->s_name);
	append_string_katcp(d, KATCP_FLAG_STRING, (ss->s_status == 1)? "nominal" : "warning");


	switch(ss->s_type){
		case KATCP_SENSOR_INTEGER : 
		case KATCP_SENSOR_BOOLEAN : 
			append_unsigned_long_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, ss->s_value);
			break;
		case KATCP_SENSOR_FLOAT : 
			append_double_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_DOUBLE, ss->s_fvalue);
			break;
	}

	return 0;
}

/****************************************************************************/
static void handle_signal(int s)
{
  switch(s){
    case SIGHUP :
      run = (-1);
      break;
    case SIGINT :
    case SIGTERM :
      run = 0;
      break;
  }
}
/****************************************************************************/

int main(int argc, char **argv)
{
#define BUFFER 128
	struct smon_state *s;
	struct smon_sensor *ss;
	struct katcp_dispatch *d;
	struct timeval tv , now, when ,delta;
	struct sigaction sag;
	MemStruct m;
	unsigned long remain;
	fd_set fsr;
	int sfd;
	unsigned int period;
	char buffer[BUFFER];
	int rr;
	double s_load[3];
	struct statfs s_sf;
	int i;
	unsigned long long  disk_size, total_free_bytes;
	char *mount_point;
	int max = 0;
	int retval;

	d = setup_katcp(STDOUT_FILENO);
	if(d == NULL){
		return EX_OSERR;
	}

	s = create_smon();
	if(s == NULL){
		log_message_katcp(d, KATCP_LEVEL_ERROR, SMON_MODULE_NAME, "unable to allocate  smon state");
		write_katcp(d);
	}

	period = SMON_POLL_INTERVAL;

	if(argc > 1){
		period = atoi(argv[1]);
		if(period <= 0){
			period = SMON_POLL_INTERVAL;
			log_message_katcp(d, KATCP_LEVEL_WARN, SMON_MODULE_NAME, "invalid update time %s given, using %dms", argv[1], period);
		} 
	}
	if(argc > 2){
		mount_point = strdup(argv[2]);	
	}else{
		/* If no mount point provided display the root file system stats */
		mount_point = strdup("/");	
	}

	log_message_katcp(d, KATCP_LEVEL_INFO, SMON_MODULE_NAME, "about to start system monitor");
	log_message_katcp(d, KATCP_LEVEL_INFO, SMON_MODULE_NAME, "polling every %dms", period);


	if(period < SMON_POLL_MIN){
		period = SMON_POLL_MIN;
	}

	tv.tv_sec = period / 1000;
	tv.tv_usec = (period % 1000) * 1000;

	delta.tv_sec = 0;
	delta.tv_usec = SMON_POLL_MIN;

	for(i = 0; i < SMON_SENSORS; i++){
		ss = &(s->smon_sensors[i]);
		ss->s_status = KATCP_STATUS_NOMINAL;

		print_sensor_list_smon(d, s, ss);

	}

#if 0
	log_message_katcp(d, KATCP_LEVEL_DEBUG, "Before -> MemTotal: %ldk MemFree: %ldk Cached: %ldk Buffers: %ldk Total: %ldk\n", m.MemTotal, m.MemFree, m.Cached, m.Buffers, remain);
#endif
	retval = make_temp_labels(d, s);
	if(retval){
		fprintf(stderr, "The memory allocation failed for the temperature sensors\n");

	}

	log_message_katcp(d, KATCP_LEVEL_TRACE, SMON_MODULE_NAME,"Found [%d] sensors\n" , s->no_sensors);  

	s->temp_val = (int *) malloc(s->no_sensors * sizeof(int));
	if(s->temp_val == NULL){
		return -1;
	}

	sag.sa_handler = handle_signal;
	sigemptyset(&(sag.sa_mask));
	sag.sa_flags = SA_RESTART;

	sigaction(SIGINT, &sag, NULL);
	sigaction(SIGHUP, &sag, NULL);
	sigaction(SIGTERM, &sag, NULL);


	gettimeofday(&now, NULL);
	gettimeofday(&when, NULL);

	for(run = 1; run > 0; ){

		if(cmp_time_katcp(&now, &when) >= 0){
#ifdef DEBUG
			fprintf(stderr, "smon: next send time, when is %ld.%06lu\n", when.tv_sec, when.tv_usec);
#endif
			add_time_katcp(&when, &when, &delta);
		}

		FD_ZERO(&fsr);

		FD_SET(STDIN_FILENO, &fsr);
		sfd = STDIN_FILENO + 1;

		delta.tv_sec = tv.tv_sec;
		delta.tv_usec = tv.tv_usec;

		retval = select(sfd, &fsr, NULL, NULL, &delta);

		gettimeofday(&now, NULL);

		/*To capture CTRL+D input event */
		if(FD_ISSET(STDIN_FILENO, &fsr)){
			rr = read(STDIN_FILENO, buffer, BUFFER);
			if(rr == 0){
				return EX_OK;
			}
			if(rr < 0){
				switch(errno){
					case EAGAIN :
					case EINTR  :
						break;
					default : 
						return EX_OSERR;
				}
			}

		}

		if(getloadavg(s_load, 3) != -1){


			ss = &(s->smon_sensors[0]);
			ss->s_fvalue = s_load[0];
			print_sensor_status_smon(d, s, ss);

			ss = &(s->smon_sensors[1]);
			ss->s_fvalue = s_load[1];
			print_sensor_status_smon(d, s, ss);

			ss = &(s->smon_sensors[2]);
			ss->s_fvalue = s_load[2];
			print_sensor_status_smon(d, s, ss);

		}

		if(statfs(mount_point, &(s_sf)) == 0){

			/*Info send in MB*/
			ss = &(s->smon_sensors[3]);
			disk_size = ((unsigned long)s_sf.f_bsize * (unsigned long)s_sf.f_blocks) / 1048576;
			ss->s_fvalue = disk_size;
			print_sensor_status_smon(d, s, ss);


			ss = &(s->smon_sensors[4]);
			total_free_bytes = ((unsigned long)s_sf.f_bavail * (unsigned long)s_sf.f_bsize ) / 1048576;
			ss->s_fvalue = total_free_bytes;
			print_sensor_status_smon(d, s, ss);

		}

		m = parse_meminfo(d);
		remain = (m.MemTotal - (m.MemFree + m.Cached + m.Buffers));

		ss = &(s->smon_sensors[5]);
		ss->s_fvalue = remain;
		print_sensor_status_smon(d, s, ss);

		if(read_temp_sensors(d, s)){
			log_message_katcp(d, KATCP_LEVEL_ERROR, SMON_MODULE_NAME, "Error reading temperature sensors\n");
			return -1;
		}

		for(i = 0; i < s->no_sensors; i++){
			ss = &(s->smon_sensors[6 + i]);
			ss->s_value = s->temp_val[i];

			if(s->temp_limit == NULL){
				max = ss->s_value;
			}else{
				max = s->temp_limit[i];
			}
			log_message_katcp(d, KATCP_LEVEL_TRACE, SMON_MODULE_NAME,"[%d]:input[%d] max[%d]\n", i, ss->s_value, max);
			if(ss->s_value > max){
				ss->s_status = KATCP_STATUS_WARN;
			}
			print_sensor_status_smon(d, s, ss);
			ss->s_status = KATCP_STATUS_NOMINAL;

		}

		if(flushing_katcp(d)){
			write_katcp(d);
		}


	}

	if(mount_point){
		free(mount_point);

	}

	destroy_smon(s);
	shutdown_katcp(d);

	return EX_OK;
#undef BUFFER
}
