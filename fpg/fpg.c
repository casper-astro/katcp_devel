#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>

#include "katcl.h"
#include "katcp.h"
#include "netc.h"
#include <avltree.h>

#define V6_FPGA_DEVICE_ID 0x004288093
#define KCPFPG_LABEL      "kcpfpg"


struct meta_entry{
	char *m_parent;
	char *m_field;
	char *m_strval;
	struct meta_entry *m_next;
};

struct ipr_state{
	int i_verbose;
	int i_fd;
	int i_ufd;
	struct katcl_line *i_line;
	struct katcl_line *i_input;
	struct katcl_line *i_print;

	struct katcl_parse *i_parse;
	struct stat sb;

	char *i_pos;
	char *i_label;

};

static int await_client(struct ipr_state *ipr, int verbose, unsigned int timeout)
{
	fd_set fsr, fsw;
	struct timeval tv;
	int result;
	char *ptr;
	int fd;
	int count;
	int i;

	fd = fileno_katcl(ipr->i_line);

	for(;;){

		FD_ZERO(&fsr);
		FD_ZERO(&fsw);

		FD_SET(fd, &fsr);

		tv.tv_sec  = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;

		result = select(fd + 1, &fsr, &fsw, NULL, &tv);
		switch(result){
			case -1 :
				switch(errno){
					case EAGAIN :
					case EINTR  :
						continue; /* WARNING */
					default  :
						return -1;
				}
				break;
			case  0 :
				if(verbose){
					fprintf(stderr, "dispatch: no io activity within %u ms\n", timeout);
				}
				return -1;
		}

		
    result = read_katcl(ipr->i_line);
    if(result){
	    //fprintf(stderr, "dispatch: read failed: %s\n", (result < 0) ? strerror(error_katcl(l)) : "connection terminated");
	    sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "read result is %d\n", result);
	    return -1;
    }

    while(have_katcl(ipr->i_line) > 0){
	    count = arg_count_katcl(ipr->i_line);

	    if(verbose){
		    sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "parsed a line with %d words\n", count);
	    }

	    for(i = 0; i < count; i++){
		    /* for binary data use the arg_buffer_katcl, string will stop at the first occurrence of a \0 */
		    ptr = arg_string_katcl(ipr->i_line, i);
		    if(verbose){
			    sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "inform[%d] is <%s>\n", i, ptr);
		    }
		    if(ptr && !strcmp(ptr, "#fpga")){                                                                                         
			    ptr = arg_string_katcl(ipr->i_line, i+1);
			    if(ptr && !strcmp(ptr, "ready")){                                                                                         
				    sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "SUCCESS FPGA PROGRAMMED YES\n");
			    }
			    return 0;                                                                                                                
		    }

	    }
    }

	}
	if(verbose){

		sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "#fpga loaded not matched\n");
	}
	return -2;
}

static int dispatch_client(struct ipr_state *ipr, struct katcl_line *l, char *msgname, int verbose, unsigned int timeout)
{
	fd_set fsr, fsw;
	struct timeval tv;
	int result;
	char *ptr, *match;
	int fd;
	int count;
	int i;

	fd = fileno_katcl(l);
#if 0
	if(msgname){
		switch(msgname[0]){
			case '!' :
			case '?' :
				prefix = strlen(msgname + 1);
				match = msgname + 1;
				break;
			default :
				prefix = strlen(msgname);
				match = msgname;
				break;
		}
	} else {
		prefix = 0;
		match = NULL;
	}
#endif
	match = NULL;/*TODO: Temp valgrind soln, Determine whether this is really essential*/

	for(;;){

		FD_ZERO(&fsr);
		FD_ZERO(&fsw);

		if(match){ /* only look for data if we need it */
			FD_SET(fd, &fsr);
		}

		if(flushing_katcl(l)){ /* only write data if we have some */
			FD_SET(fd, &fsw);
		}

		tv.tv_sec  = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;

		result = select(fd + 1, &fsr, &fsw, NULL, &tv);
		switch(result){
			case -1 :
				switch(errno){
					case EAGAIN :
					case EINTR  :
						continue; /* WARNING */
					default  :
						return -1;
				}
				break;
			case  0 :
				if(verbose){
					fprintf(stderr, "dispatch: no io activity within %u ms\n", timeout);
				}
				return -1;
		}

		if(FD_ISSET(fd, &fsw)){
			result = write_katcl(l);
			if(result < 0){
				fprintf(stderr, "dispatch: write failed: %s\n", strerror(error_katcl(l)));
				return -1;
			}
			if((result > 0) && (match == NULL)){ /* if we finished writing and don't expect a match then quit */
				//return 0;
			}
		}

		do{
			result = read_katcl(ipr->i_line);
			if(result){
				//fprintf(stderr, "dispatch: read failed: %s\n", (result < 0) ? strerror(error_katcl(l)) : "connection terminated");
				sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "read result is %d\n", result);
				return -1;
			}

		}while(have_katcl(ipr->i_line) == 0);

		count = arg_count_katcl(ipr->i_line);

		sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "parsed a line with %d words\n", count);

		for(i = 0; i < count; i++){
			/* for binary data use the arg_buffer_katcl, string will stop at the first occurrence of a \0 */
			ptr = arg_string_katcl(ipr->i_line, i);
			sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "reply[%d] is <%s>\n", i, ptr);
		}
		return 0;
    

	}
}

void destroy_ipr(struct ipr_state *i)
{
		sync_message_katcl(i->i_print, KATCP_LEVEL_DEBUG, i->i_label, "Uninitialising intepreter state variables\n");
	if(i == NULL){
		return;
	}

	if(i->i_line){
		destroy_rpc_katcl(i->i_line);
		i->i_line = NULL;
	}

	if(i->i_fd > 0){
		close(i->i_fd);
		i->i_fd = -1;
	}

	if(i->i_input){
		destroy_katcl(i->i_input, 1);
		i->i_input = NULL;
	}

	if(i->i_print){
		destroy_katcl(i->i_print, 1);
		i->i_print = NULL;
	}

	if(i->i_parse){
		i->i_parse = NULL;
	}

	if(i->i_pos){
		munmap(i->i_pos, i->sb.st_size);
		i->i_pos = NULL;
	}

	if(i->i_ufd > 0){
		close(i->i_ufd);
		i->i_ufd = -1;
	}

	i->i_label = NULL;

	free(i);
}

struct ipr_state *create_ipr(char *server, char *file, int verbose)
{
	struct ipr_state *i;

	i = malloc(sizeof(struct ipr_state));
	if(i == NULL){
		return NULL;
	}

	i->i_verbose = verbose;	

	i->i_fd = -1;
	i->i_ufd = -1;

	i->i_line = NULL;
	i->i_input = NULL;
	i->i_parse = NULL;
	i->i_pos = NULL;
	i->i_label = KCPFPG_LABEL;


	i->i_print = create_katcl(STDOUT_FILENO);

	sync_message_katcl(i->i_print, KATCP_LEVEL_DEBUG, i->i_label, "Initialising intepreter state variables\n");


	i->i_line = create_name_rpc_katcl(server);
	if(i->i_line == NULL){
		sync_message_katcl(i->i_print, KATCP_LEVEL_ERROR, i->i_label, "Unable to create client connection to server %s:%s\n", server, strerror(errno));
		destroy_ipr(i);
		return NULL;
	}

	i->i_fd = open(file, O_RDONLY);
	if(i->i_fd < 0){
		sync_message_katcl(i->i_print, KATCP_LEVEL_ERROR, i->i_label, "Error in opening file %s\n", file);
		destroy_ipr(i);
		return NULL;
	}

	i->i_input = create_katcl(i->i_fd);
	if(i->i_input == NULL){
		destroy_ipr(i);
		return NULL;
	}

	/* Memory mapping the file contents and parsing to the start of bin file */
	fstat(i->i_fd, &(i->sb));
	sync_message_katcl(i->i_print, KATCP_LEVEL_DEBUG, i->i_label, "Total size of file is: %lu\n", i->sb.st_size);

	/* offset must be a multiple of page size */
	i->i_pos = mmap(NULL, i->sb.st_size, PROT_READ, MAP_PRIVATE, i->i_fd, 0);
	if(i->i_pos == MAP_FAILED){
		sync_message_katcl(i->i_print, KATCP_LEVEL_ERROR, i->i_label, "Error in memory mapping file %s\n", file);
		i->i_pos = NULL;
		destroy_ipr(i);
		return NULL;
	}

	return i;

}

int finalise_upload(struct ipr_state *ipr, unsigned int timeout)
{

	/* populate a request */
	if(append_string_katcl(ipr->i_line, KATCP_FLAG_FIRST | KATCP_FLAG_LAST, "?finalise")   < 0) {
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "%s:Unable to populate finalise request\n", __func__);
		return -1;
	}


	/* use above function to send upload request */
	if(dispatch_client(ipr, ipr->i_line, "!finalise", 1, timeout)             < 0) {
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "%s: Unable to send finalise request\n", __func__);

		return -1;
	}

	/* clean up request for next call */
	have_katcl(ipr->i_line);

	return 0;
}

int prepare_upload(struct ipr_state *ipr, unsigned int timeout)
{

	/* populate a request */
	if(append_string_katcl(ipr->i_line, KATCP_FLAG_FIRST | KATCP_FLAG_LAST, "?uploadbin")   < 0) {
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "%s:Unable to populate upload request\n", __func__);
		return -1;
	}


	/* use above function to send upload request */
	if(dispatch_client(ipr, ipr->i_line, "!uploadbin", 1, timeout)             < 0) {
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "%s: Unable to send upload request\n", __func__);

		return -1;
	}

	/* clean up request for next call */
	have_katcl(ipr->i_line);

	return 0;
}

int program_bin(struct ipr_state *ipr, char *server, char *data, int port, int bytes_read)
{
#define BUFFER 4096
	int count;
	int bytes_written;

	ipr->i_ufd = net_connect(server, 7146, NETC_VERBOSE_ERRORS | NETC_VERBOSE_STATS);
	if(ipr->i_ufd < 0){
		fprintf(stderr, "unable to connect to %s:upload port:%d\n", server, 7146);
		return -1;
	}

	count = BUFFER;

	while(bytes_read > 0){
		bytes_written = write(ipr->i_ufd, data, count);
		switch(bytes_written){
			case -1 :
				switch(errno){
					case EAGAIN :
					case EINTR  :
						break;
					default :
		        sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "upload to port failed:%s\n");
						close(ipr->i_ufd);
						return -1;
				}
				break;
			case 0 :
				//fprintf(stderr, "write to fpga failed: %s", strerror(errno));
		    sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "write to upload port failed:%s\n");
				close(ipr->i_ufd);
				return -1;
			default :
				bytes_read -= bytes_written;
				if(bytes_read < BUFFER){
					count = bytes_read;
				}
				data += bytes_written;
				break;
		}

	}

	if(close(ipr->i_ufd) < 0){
		fprintf(stderr, "unable to program fpga\n");
		return -1;
	}

	if(await_client(ipr, 0, 15000)             < 0) {
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "%s: await reply failed\n", __func__);

		return -1;
	}

	return 0;
#undef BUFFER
}


int main(int argc, char **argv)
{
	char *server;
	char *app;
	int verbose;
	int count, result;
	char *request;
	char *test_ptr;
	char *str = "?quit";
	char *ptr;
	char *file;
	int i;
        int run;

	int timeout = 0;
	uint32_t fpga_id;
	char *ret;
	int bytes_written, bytes_read;
	int port = 7146;

	struct ipr_state *ipr;

	app = "ipr";
	verbose = 1;
	ptr = NULL;
	fpga_id = 0xdeadbeef;
	test_ptr = NULL;
        run = 1;

	file = argv[1];

	server = getenv("KATCP_SERVER");
	if(server == NULL){
		if(argv[2] != NULL){
			server = argv[2];
		}else{
			server = "localhost";
		}
	}

	/* Initialise the intepreter state */
	ipr = create_ipr(server, file, verbose); 
	if(ipr == NULL){
		//sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "%s: Unable to allocate intepreter state\n", app);
		fprintf(stderr, "%s: Unable to allocate intepreter state\n", app);
		return 2;
	}

	/* Access  memory mapped file  to locate start of the bin file */
	ret = strstr(ipr->i_pos, str); 
	if(ret == NULL){
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "No ?quit substring found\n");
		destroy_ipr(ipr);
		return 2;
	}else{
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "The ?quit substring loc %p\n", ret);
		ptr = (ret + 6);
	}

	/*  Extracting FPGA bin data from file */
	bytes_read = (ipr->sb.st_size - (ptr - (ipr->i_pos)));
	if(!bytes_read){
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "ZERO DATA SIZE ERROR:%d\n", bytes_read);
		destroy_ipr(ipr);
		return 2;

	}else{
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "FPGA DATA SIZE INFO:%d\n", bytes_read);

	}

	/* FPGA device id calculation from bin file */
	for(i = 0; i < 1; i++){
		fpga_id = (0x000000FF & *(ptr + 128 + i)) << 24 | (0x000000FF & *(ptr + 128 + i + 1)) << 16 | (0x000000FF & *(ptr + 128 + i + 2)) <<  8  | (0x000000FF & *(ptr + 128 + i + 3)) <<  0 ;
	}

	/* Virtex6 device:XQ6VSX475T = 0x04288093 */
	if(fpga_id == V6_FPGA_DEVICE_ID){
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "Virtex 6 bin file detected\n");
  }

	/* send UPLOAD command */
	if(prepare_upload(ipr, 15000) < 0){
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "upload prepare failed\n");
		destroy_ipr(ipr);
		return 3;
	}else{
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "upload bin command sent successfully\n");
	}
	sleep(1);


	/* Program FPGA */
	if(program_bin(ipr, server, ptr, port, bytes_read) < 0){
		sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "unable to program fpga\n");
		destroy_ipr(ipr);
		return 4;
	}

	/* Parsing and writing requests */
	do{
		result = read_katcl(ipr->i_input);
		if(result){
			fprintf(stderr, "read result is %d\n", result);
			return 1;
		}

		count = arg_count_katcl(ipr->i_input);

#if 0
		fprintf(stderr, "CHECK COUNT:parsed a line with %d words\n", count);
#endif

		for(i = 0; i < count; i++){
			/* for binary data use the arg_buffer_katcl, string will stop at the first occurrence of a \0 */

			request = arg_string_katcl(ipr->i_input, i);
#if 0
			printf("CHECK reply[%d] is <%s>\n", i, request);
#endif

			if(!strcmp(request, "?uploadbin")){
				printf("?uploadbin keyword matched:DO NOTHING\n");
				break;

			}else if(!strcmp(request, "?quit")){
				printf("?quit keyword matched\n");
				/* send finalise command */
				if(finalise_upload(ipr, 15000) < 0){
					sync_message_katcl(ipr->i_print, KATCP_LEVEL_ERROR, ipr->i_label, "finalise prepare failed\n");
					destroy_ipr(ipr);
					return 3;
				}else{
					sync_message_katcl(ipr->i_print, KATCP_LEVEL_DEBUG, ipr->i_label, "finalise command sent successfully\n");
				}
				while(1 && timeout < 6){
					if(*request == '\r' || *request == '\n' || *request == '\0'){
						printf("Time to get out of loop\n");
						ptr++;
						break;
					}
					request++;
					timeout++;
				}
                                run = 0;
                                break;

			}else{
				ipr->i_parse = ready_katcl(ipr->i_input);
				if(ipr->i_parse){
					append_parse_katcl(ipr->i_line, ipr->i_parse);
				}

				if(await_reply_rpc_katcl(ipr->i_line, 5000) < 0){
					fprintf(stderr, "await reply rpc katcl failure\n");

				}else{
					test_ptr = arg_string_katcl(ipr->i_line, 0);
					if(test_ptr){
#if 0
						fprintf(stderr, "collection: received some inform message %s ...\n", test_ptr);
#endif
					}
					test_ptr = arg_string_katcl(ipr->i_line, 1);
					if(test_ptr){
#if 0
						fprintf(stderr, "collection: received more inform message %s ...\n", test_ptr);
#endif
					}
					if(strcmp(test_ptr, KATCP_OK) != 0){
						printf("sending %s command FAIL: encountered", test_ptr);
						return 2;
					}
					break;


				}

			}
		}

	}while(have_katcl(ipr->i_input) > 0  && run == 1);


	if(ipr){
		destroy_ipr(ipr);
	}

	return 0;
}
