/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sysexits.h>

#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <arpa/inet.h>

#include <katcp.h>
#include <katpriv.h>

#define TMON_POLL_INTERVAL        1000    /* default poll interval */
#define TMON_POLL_MIN               50    /* minimum (in ms) between a recv followed by another send */

#define TMON_MODULE_NAME         "ntp"
#define TMON_SENSOR_NAME         ".ntp.synchronised"
#define TMON_SENSOR_DESCRIPTION  "clock good"

/************************************************************/

#define NTP_MAGIC                   0x1f113123

#define SET_BITS(v, s, m)  (((v) & (m)) << (s))
#define GET_BITS(v, s, m)  (((v) >> (s)) & (m))

#define NTP_VERSION_SHIFT                  11
#define NTP_VERSION_MASK               0x0007
#define  NTP_VERSION_THREE                  3

#define NTP_MODE_SHIFT                      8 
#define NTP_MODE_MASK                  0x0007
#define  NTP_MODE_CONTROL                   6

#define NTP_RESPONSE                   0x0080
#define NTP_ERROR                      0x0040
#define NTP_MORE                       0x0020 

#define NTP_OPCODE_SHIFT                    0
#define NTP_OPCODE_MASK                0x001f
#define  NTP_OPCODE_STATUS                  1

#define NTP_LEAPI_SHIFT                    14
#define NTP_LEAPI_MASK                 0x0003
#define  NTP_LEAPI_NOWARN              0x0000
#define  NTP_LEAPI_SIXTYONE            0x0001
#define  NTP_LEAPI_FIFTYNINE           0x0002
#define  NTP_LEAPI_ALARM               0x0003

#define NTP_CLOCKSRC_SHIFT                  8
#define NTP_CLOCKSRC_MASK              0x003f
#define  NTP_CLOCKSRC_NTPUDP                6

#define NTP_SYSEVTCNT_SHIFT                 4
#define NTP_SYSEVTCNT_MASK             0x000f

#define NTP_SYSEVTCDE_SHIFT                 0
#define NTP_SYSEVTCDE_MASK             0x000f

#define NTP_PEER_STATUS_SHIFT              11
#define NTP_PEER_STATUS_MASK           0x001f

#define  NTP_PEER_STATUS_CONFIGURED    0x0010
#define  NTP_PEER_STATUS_AUTHENABLE    0x0008
#define  NTP_PEER_STATUS_AUTHENTIC     0x0004
#define  NTP_PEER_STATUS_REACHABLE     0x0002
#define  NTP_PEER_STATUS_RESERVED      0x0001

#define NTP_PEER_SELECT_SHIFT               8
#define NTP_PEER_SELECT_MASK           0x0007

#define  NTP_PEER_SELECT_REJECT             0
#define  NTP_PEER_SELECT_SANE               1
#define  NTP_PEER_SELECT_CORRECT            2
#define  NTP_PEER_SELECT_CANDIDATE          3
#define  NTP_PEER_SELECT_OUTLIER            4
#define  NTP_PEER_SELECT_FAR                5
#define  NTP_PEER_SELECT_CLOSE              6
#define  NTP_PEER_SELECT_RESERVED           7

#define NTP_PEER_EVTCNT_SHIFT               4
#define NTP_PEER_EVTCNT_MASK           0x000f

#define NTP_PEER_EVTCDE_SHIFT               0
#define NTP_PEER_EVTCDE_MASK           0x000f

#define NTP_MAX_DATA                      468
#define NTP_HEADER                         12

#define NTP_MAX_PACKET    (NTP_HEADER + NTP_MAX_DATA)

struct ntp_state
{
  unsigned int n_magic;
  int n_fd;
  unsigned int n_sequence; /* careful, field in packet is only 16 bits */
  int n_sync;
  int n_level;
};

struct ntp_peer{
  uint16_t p_id;
  uint16_t p_status;
} __attribute__ ((packed));

struct ntp_message{
  uint16_t n_bits;
  uint16_t n_sequence;
  uint16_t n_status;
  uint16_t n_id;
  uint16_t n_offset;
  uint16_t n_count;
  uint8_t n_data[NTP_MAX_DATA];
} __attribute__ ((packed));

/*****************************************************************************/

void destroy_ntp(struct katcp_dispatch *d, struct ntp_state *nt)
{
  if(nt == NULL){
    return;
  }

  if(nt->n_magic != NTP_MAGIC){
    log_message_katcp(d, KATCP_LEVEL_FATAL, TMON_MODULE_NAME, "bad magic on ntp state");
  }

  if(nt->n_fd >= 0){
    close(nt->n_fd);
    nt->n_fd = (-1);
  }

  free(nt);
}

struct ntp_state *create_ntp(struct katcp_dispatch *d)
{
  struct ntp_state *nt;

  nt = malloc(sizeof(struct ntp_state));
  if(nt == NULL){
    return NULL;
  }

  nt->n_magic = NTP_MAGIC;
  nt->n_sync = 0;
  nt->n_fd = (-1);
  nt->n_sequence = 1;

  return nt;
}

/*****************************************************************************/

int connect_ntp(struct katcp_dispatch *d, struct ntp_state *nt)
{
  int fd;
  struct sockaddr_in sa;

  if(nt->n_fd >= 0){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, TMON_MODULE_NAME, "closing previous ntp file descriptor");
    close(nt->n_fd);
    nt->n_fd = (-1);
  }

  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sa.sin_port = htons(123);

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, TMON_MODULE_NAME, "unable to create ntp socket: %s", strerror(errno));
    return -1;
  }

  if(connect(fd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, TMON_MODULE_NAME, "unable to connect to ntp: %s", strerror(errno));
    close(fd);
    fd = (-1);
    return -1;
  }

  nt->n_fd = fd;
  nt->n_sequence = 42;

#ifdef DEBUG
  fprintf(stderr, "tmon: (re)connected to localhost\n");
#endif

  return 0;
}

/*****************************************************************************/

int send_ntp(struct katcp_dispatch *d, struct ntp_state *nt)
{
  struct ntp_message buffer, *nm;
  int sw, wr;

  if(nt->n_fd < 0){
    if(connect_ntp(d, nt) < 0){
      return -1;
    }
  }

  nm = &buffer;

  nm->n_bits = htons(
    SET_BITS(NTP_VERSION_THREE, NTP_VERSION_SHIFT, NTP_VERSION_MASK) | 
    SET_BITS(NTP_MODE_CONTROL,  NTP_MODE_SHIFT,    NTP_MODE_MASK) | 
    SET_BITS(NTP_OPCODE_STATUS, NTP_OPCODE_SHIFT,  NTP_OPCODE_MASK));

  nt->n_sequence = 0xffff & (nt->n_sequence + 1);

  nm->n_sequence = htons(nt->n_sequence);
  nm->n_status   = htons(0);
  nm->n_id       = htons(0);
  nm->n_offset   = htons(0);
  nm->n_count    = htons(0);

  sw = NTP_HEADER;

  wr = send(nt->n_fd, nm, sw, MSG_NOSIGNAL);

  if(wr < sw){
    if(wr < 0){
      switch(errno){
        case EAGAIN :
        case EINTR  :
          return 0;
        default :
          log_message_katcp(d, KATCP_LEVEL_ERROR, TMON_MODULE_NAME, "unable to send request: %s", strerror(errno));
          break;
      }
    }

    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "tmon: sent message with sequence number %d\n", nt->n_sequence);
#endif

  return 1;
}

int recv_ntp(struct katcp_dispatch *d, struct ntp_state *nt)
{
  struct ntp_message buffer, *nm;
  struct ntp_peer *np;
  int rr, i, result;
  uint16_t word, field;

  nm = &buffer;

#if 0
  rr = recv(nt->n_fd, nm, NTP_MAX_PACKET, 0);
#endif
  rr = recv(nt->n_fd, nm, NTP_MAX_PACKET, MSG_DONTWAIT);
  if(rr < 0){
    switch(errno){
      case EAGAIN :
      case EINTR  :
        return 0;
      default :
        log_message_katcp(d, KATCP_LEVEL_ERROR, TMON_MODULE_NAME, "ntp receive failed with %s", strerror(errno));
        close(nt->n_fd);
        nt->n_fd = (-1);
        return 0;
    }
  }

  if(rr < NTP_HEADER){
    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp reply of %d bytes too short", rr);
    close(nt->n_fd);
    nt->n_fd = (-1);
    return -1;
  }

  nm->n_bits     = ntohs(nm->n_bits);
  nm->n_sequence = ntohs(nm->n_sequence);
  nm->n_status   = ntohs(nm->n_status);
  nm->n_id       = ntohs(nm->n_id);
  nm->n_offset   = ntohs(nm->n_offset);
  nm->n_count    = ntohs(nm->n_count);

  log_message_katcp(d, KATCP_LEVEL_TRACE, TMON_MODULE_NAME, "ntp reply with sequence number %d", nm->n_sequence);

  field = GET_BITS(nm->n_bits, NTP_VERSION_SHIFT, NTP_VERSION_MASK);
  if(field != NTP_VERSION_THREE){
    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp reply %d not version 3", field);
    return -1;
  }

  field = GET_BITS(nm->n_bits, NTP_MODE_SHIFT, NTP_MODE_MASK);
  if(field != NTP_MODE_CONTROL){
    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp message not control but %d", field);
    return -1;
  }

  field = GET_BITS(nm->n_bits, NTP_OPCODE_SHIFT, NTP_OPCODE_MASK);
  if(field != NTP_OPCODE_STATUS){
    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp opcode not status but %d", field);
    return -1;
  }

  if(!(nm->n_bits & NTP_RESPONSE)){
    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp reply is not a response");
    return -1;
  }

  if(nm->n_bits & NTP_ERROR){
    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp sent an error response");
    return -1;
  }

  if(nm->n_sequence != nt->n_sequence){
    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp received sequence number %d not %d", nm->n_sequence, nt->n_sequence);
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "status is 0x%04x\n", nm->n_status);
#endif

  log_message_katcp(d, KATCP_LEVEL_TRACE, TMON_MODULE_NAME, "ntp status word is 0x%04x", nm->n_status);

  field = GET_BITS(nm->n_status, NTP_LEAPI_SHIFT, NTP_LEAPI_MASK);

  switch(field){
    case NTP_LEAPI_NOWARN :
    case NTP_LEAPI_SIXTYONE :
    case NTP_LEAPI_FIFTYNINE :
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp reports error status with leap indicator 0x%x and full word 0x%x", field, nm->n_status);
      return -1;
  }

  field = GET_BITS(nm->n_status, NTP_CLOCKSRC_SHIFT, NTP_CLOCKSRC_MASK);
  if(field != NTP_CLOCKSRC_NTPUDP){
    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp synchronised by unusual means %d in status word 0x%x", field, nm->n_status);
  }

  if(nm->n_count % 4) {
    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp packet has odd length of %d", nm->n_count);
    return -1;
  }

  if((nm->n_offset + nm->n_count) > NTP_MAX_DATA){
    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "ntp packet of %d and %d exceeds size limits", nm->n_offset, nm->n_count);
    return -1;
  }

  result = (-1);

  for(i = 0; i < nm->n_count; i += 4){
    np = (struct ntp_peer *)(nm->n_data + i);

    word = ntohs(np->p_status);

    log_message_katcp(d, KATCP_LEVEL_TRACE, TMON_MODULE_NAME, "ntp peer %d reports status 0x%x", ntohs(np->p_id), word);

    field = GET_BITS(word, NTP_PEER_STATUS_SHIFT, NTP_PEER_STATUS_MASK);
    if((field & (NTP_PEER_STATUS_CONFIGURED | NTP_PEER_STATUS_REACHABLE)) == (NTP_PEER_STATUS_CONFIGURED | NTP_PEER_STATUS_REACHABLE)){

      field = GET_BITS(word, NTP_PEER_SELECT_SHIFT, NTP_PEER_SELECT_MASK);
      switch(field){
        case NTP_PEER_SELECT_FAR : 
        case NTP_PEER_SELECT_CLOSE : 
          result = 1;
          break;
        default :
          log_message_katcp(d, KATCP_LEVEL_DEBUG, TMON_MODULE_NAME, "ntp peer %d not yet ready with code %d", ntohs(np->p_id), field);
          break;
      }

    } else {
      log_message_katcp(d, KATCP_LEVEL_DEBUG, TMON_MODULE_NAME, "ntp peer %d either unreachable or unconfigured", ntohs(np->p_id));
    }
  }

#if 0
  if(good > nt->n_sync){
#if 0
    if(check_time_poco(d, 0) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, TMON_MODULE_NAME, "detected time warp or other timing problem");
#if 0 /* too dangerous to do at arbitrary times */
      sync_gateware_poco(d);
#endif
    }
#endif
    /* WARNING: only attempt resync once */
  }
#endif

  nt->n_sync = (result <= 0) ? 0 : 1;

  return result;
}

/*****************************************************************************/

#if 0
int recv_ntp_poco(struct katcp_dispatch *d, struct ntp_sensor_poco *nt)
{
  struct ntp_message_poco buffer, *nm;
  struct ntp_peer_poco *np;
  int rr, i;
  unsigned int bytes, offset;
  uint16_t word, field;

  nm = &buffer;

  rr = recv(nt->n_fd, nm, NTP_MAX_PACKET, MSG_DONTWAIT);
  if(rr < 0){
    switch(errno){
      case EAGAIN :
      case EINTR  :
        return 0;
      default :
        return -1;
    }
  }

  if(rr < NTP_HEADER){
    return -1;
  }

  nm->n_bits     = ntohs(nm->n_bits);
  nm->n_sequence = ntohs(nm->n_sequence);
  nm->n_status   = ntohs(nm->n_status);
  nm->n_id       = ntohs(nm->n_id);
  nm->n_offset   = ntohs(nm->n_offset);
  nm->n_count    = ntohs(nm->n_count);

  field = GET_BITS(nm->n_bits, NTP_VERSION_SHIFT, NTP_VERSION_MASK);
  if(field != NTP_VERSION_THREE){
    fprintf(stderr, "not a version three message but %d\n", field);
    return -1;
  }

  field = GET_BITS(nm->n_bits, NTP_MODE_SHIFT, NTP_MODE_MASK);
  if(field != NTP_MODE_CONTROL){
    fprintf(stderr, "message not control but %d\n", field);
    return -1;
  }

  field = GET_BITS(nm->n_bits, NTP_OPCODE_SHIFT, NTP_OPCODE_MASK);
  if(field != NTP_OPCODE_STATUS){
    fprintf(stderr, "opcode not status but but %d\n", field);
    return -1;
  }

  if(!(nm->n_bits & NTP_RESPONSE)){
#if 0
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "received ntp message which is not a response");
#endif
    fprintf(stderr, "received a message which isn't a response\n");
    return -1;
  }

  if(nm->n_bits & NTP_ERROR){
    fprintf(stderr, "got an error response\n");
    /* TODO ? */
  }

  if(nm->n_sequence != nt->n_sequence){
    fprintf(stderr, "bad sequence number, expected %d, got %d\n", nt->n_sequence, nm->n_sequence);
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "status is 0x%04x\n", nm->n_status);
#endif

  field = GET_BITS(nm->n_status, NTP_LEAPI_SHIFT, NTP_LEAPI_MASK);
  fprintf(stderr, "leap indicator is 0x%x\n", field);

  field = GET_BITS(nm->n_status, NTP_CLOCKSRC_SHIFT, NTP_CLOCKSRC_MASK);
  fprintf(stderr, "clock source is 0x%x\n", field);

  field = GET_BITS(nm->n_status, NTP_SYSEVTCNT_SHIFT, NTP_SYSEVTCNT_MASK);
  fprintf(stderr, "got %d new system events\n", field);

  field = GET_BITS(nm->n_status, NTP_SYSEVTCDE_SHIFT, NTP_SYSEVTCDE_MASK);
  fprintf(stderr, "most recent code 0x%x ", field);

#ifdef DEBUG
  fprintf(stderr, "got a further %u data bytes starting at %u\n", nm->n_count, nm->n_offset);
#endif

  if(nm->n_count % 4) {
    fprintf(stderr, "logic problem - packet data size not a multiple of 4\n");
    return -1;
  }

  if((nm->n_offset + nm->n_count) > NTP_MAX_DATA){
    fprintf(stderr, "logic problem - data field %u+%u too large\n", nm->n_offset, nm->n_count);
    return -1;
  }

  for(i = 0; i < nm->n_count; i += 4){
    np = (struct ntp_peer_poco *)(nm->n_data + i);

#ifdef DEBUG
    fprintf(stderr, "peer id 0x%04x/%u\n", ntohs(np->p_id), ntohs(np->p_id));
#endif

    word = ntohs(np->p_status);

    field = GET_BITS(word, NTP_PEER_STATUS_SHIFT, NTP_PEER_STATUS_MASK);

#ifdef DEBUG
    fprintf(stderr, " field=0x%04x", word);
#endif

#ifdef DEBUG
    if(field & NTP_PEER_STATUS_CONFIGURED){
      fprintf(stderr, " configured");
    }
#endif

#ifdef DEBUG
    if(field & NTP_PEER_STATUS_REACHABLE){
      fprintf(stderr, " reachable");
    }
#endif

    field = GET_BITS(word, NTP_PEER_SELECT_SHIFT, NTP_PEER_SELECT_MASK);
    switch(field){
      case NTP_PEER_SELECT_REJECT     :
#ifdef DEBUG
        fprintf(stderr, " rejected");
#endif
        break;
      case NTP_PEER_SELECT_SANE : 
#ifdef DEBUG
        fprintf(stderr, " sane");
#endif
        break;
      case NTP_PEER_SELECT_CORRECT : 
#ifdef DEBUG
        fprintf(stderr, " correct");
#endif
        break;
      case NTP_PEER_SELECT_CANDIDATE : 
#ifdef DEBUG
        fprintf(stderr, " candidate");
#endif
        break;
      case NTP_PEER_SELECT_OUTLIER : 
#ifdef DEBUG
        fprintf(stderr, " outlier");
#endif
        break;
      case NTP_PEER_SELECT_FAR : 
#ifdef DEBUG
        fprintf(stderr, " far");
#endif
        break;
      case NTP_PEER_SELECT_CLOSE : 
#ifdef DEBUG
        fprintf(stderr, " close");
#endif
        break;
    }
#ifdef DEBUG
    fprintf(stderr, "\n");
#endif

    field = GET_BITS(word, NTP_PEER_EVTCNT_SHIFT, NTP_PEER_EVTCNT_MASK);
#ifdef DEBUG
    fprintf(stderr, "%d events, ", field);
#endif

    field = GET_BITS(word, NTP_PEER_EVTCDE_SHIFT, NTP_PEER_EVTCDE_MASK);
#ifdef DEBUG
    fprintf(stderr, "last %d\n", field);
#endif
  }

  return 1;
}
#endif

/*****************************************************************************/

int main(int argc, char **argv)
{
#define BUFFER 128
  struct ntp_state *nt;
  struct katcp_dispatch *d;
  int result, previous, current, mfd, rr;
  char *level;
  unsigned int period;
  struct timeval delta, now, when, template;
  fd_set fsr;
  char buffer[BUFFER];

  period = TMON_POLL_INTERVAL;
  previous = (-1);
  current = 0;

  d = setup_katcp(STDOUT_FILENO);
  if(d == NULL){
    return EX_OSERR;
  }

  nt = create_ntp(d);
  if(nt == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, TMON_MODULE_NAME, "unable to allocate local ntp state");
    write_katcp(d);
    return EX_OSERR;
  }

  if(argc > 1){
    period = atoi(argv[1]);
    if(period <= 0){
      period = TMON_POLL_INTERVAL;
      log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "invalid update time %s given, using %dms", argv[1], period);
    } 
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, TMON_MODULE_NAME, "about to start time monitor");

  if(argc > 2){
    level = argv[2];
    if(name_log_level_katcp(d, level) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "invalid log %s given", level);
    }
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, TMON_MODULE_NAME, "polling local ntp server every %dms", period);

  append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#sensor-list");
  append_string_katcp(d,                    KATCP_FLAG_STRING, TMON_SENSOR_NAME);
  append_string_katcp(d,                    KATCP_FLAG_STRING, TMON_SENSOR_DESCRIPTION);
  append_string_katcp(d,                    KATCP_FLAG_STRING, "none");
  append_string_katcp(d, KATCP_FLAG_LAST  | KATCP_FLAG_STRING, "boolean");


  if(period < TMON_POLL_MIN){
    period = TMON_POLL_MIN;
  }

  template.tv_sec = period / 1000;
  template.tv_usec = (period % 1000) * 1000;

  delta.tv_sec = 0;
  delta.tv_usec = TMON_POLL_MIN;

#if 0
  delta.tv_sec = 0;
  delta.tv_usec = (2 * TMON_POLL_MIN) * 1000;

  result = cmp_time_katcp(&template, &delta);
  
  margin.tv_sec = 0;
  margin.tv_usec = TMON_POLL_MIN * 1000;

  if(result < 0){

    log_message_katcp(d, KATCP_LEVEL_WARN, TMON_MODULE_NAME, "requested poll interval too small, limiting interval to %dms", 2 * TMON_POLL_MIN);

    template.tv_sec = 0;
    template.tv_usec = TMON_POLL_MIN * 1000;
  } else {
    sub_time_katcp(&template, &template, &margin);
  }
#endif

  gettimeofday(&when, NULL);

  for(;;){
    
    if(cmp_time_katcp(&now, &when) >= 0){
#ifdef DEBUG
      fprintf(stderr, "tmon: next send time, when is %ld.%06lu\n", when.tv_sec, when.tv_usec);
#endif
      send_ntp(d, nt);
      add_time_katcp(&when, &when, &delta);
    } 

    FD_ZERO(&fsr);

    FD_SET(STDIN_FILENO, &fsr);
    mfd = STDIN_FILENO + 1;

    if(nt->n_fd >= 0){
      FD_SET(nt->n_fd, &fsr);
      if(nt->n_fd >= mfd){
        mfd = nt->n_fd + 1;
      }
    }

    delta.tv_sec  = template.tv_sec;
    delta.tv_usec = template.tv_usec;

    result = select(mfd, &fsr, NULL, NULL, &delta);

    gettimeofday(&now, NULL);

    if(nt->n_fd >= 0){
      if(FD_ISSET(nt->n_fd, &fsr)){
        current = 0;
        while((result = recv_ntp(d, nt)) != 0){
          current = (result > 0) ? 1 : 0;
        }
      }
    }

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

    if(flushing_katcp(d)){
      write_katcp(d);
    }

    if(current != previous){
      append_string_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#sensor-status");
      append_args_katcp  (d,                    KATCP_FLAG_STRING, "%ld%03u", now.tv_sec, now.tv_usec / 1000);
      append_string_katcp(d,                    KATCP_FLAG_STRING, "1");
      append_string_katcp(d,                    KATCP_FLAG_STRING, TMON_SENSOR_NAME);
      append_string_katcp(d,                    KATCP_FLAG_STRING, current ? "nominal" : "error");
      append_unsigned_long_katcp(d, KATCP_FLAG_LAST  | KATCP_FLAG_ULONG, current);
      previous = current;
    }

  }

  destroy_ntp(d, nt);
  shutdown_katcp(d);

  return EX_OK;
#undef BUFFER
}

