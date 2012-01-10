#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/utsname.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <katcp.h>
#include <katcl.h>
#include <avltree.h>

#include "tcpborphserver3.h"
#include "tapper.h"

#define GO_DEFAULT_PORT 7148

#define GO_MAC          0x00
#define GO_GATEWAY      0x0c
#define GO_ADDRESS      0x10
#define GO_BUFFER_SIZES 0x18
#define GO_EN_RST_PORT  0x20

#define MAX_FRAME       4096
#define ARP_FRAME         64
#define MIN_FRAME         64

#define IP_SIZE            4
#define MAC_SIZE           6

#define NAME_BUFFER       64
#define NET_BUFFER     10000
#define IP_BUFFER         16
#define MAC_BUFFER        18
#define CMD_BUFFER      1024

#define GO_TXBUFFER   0x1000
#define GO_RXBUFFER   0x2000

#define GO_ARPTABLE   0x3000
#define ARP_CACHE        256

#define FRAME_TYPE1       12
#define FRAME_TYPE2       13

#define FRAME_DST          0
#define FRAME_SRC          6

#define ARP_OP1            6
#define ARP_OP2            7

#define ARP_SHA_BASE       8
#define ARP_SIP_BASE      14
#define ARP_THA_BASE      18
#define ARP_TIP_BASE      24

#define SIZE_FRAME_HEADER 14

#define IP_DEST1          16
#define IP_DEST2          17
#define IP_DEST3          18
#define IP_DEST4          19

#define POLL_INTERVAL   1000 /* in usecs */
#define ARP_PERIOD       101 /* multiples of POLL_INTERVAL */

#define RUNT_LENGTH       20 /* want a basic ip packet */

static const uint8_t arp_const[] = { 0, 1, 8, 0, 6, 4, 0 }; /* disgusting */
static const uint8_t broadcast_const[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

struct getap_state{
  struct katcp_dispatch *s_dispatch;
  struct tbs_raw *s_raw_mode;

#if 0
  char s_borph_name[NAME_BUFFER];
#endif

  char *s_tap_name;

  /* these items have bounded sizes */
  char s_address_name[IP_BUFFER];
  char s_gateway_name[IP_BUFFER];
  char s_mac_name[MAC_BUFFER];
  unsigned short s_port;

  unsigned int s_index;

  uint8_t s_mac_binary[MAC_SIZE];
  uint32_t s_address_binary;
  uint32_t s_mask_binary;
  uint32_t s_network_binary;

#if 0
  int s_bfd;
#endif

  struct tbs_entry *s_register;
  unsigned int s_instance;

  int s_tap_fd;
#if 0
  fd_set s_fsr;
  fd_set s_fsw;
#endif
  struct timeval s_timeout;

  int s_verbose;
  int s_testing;

  unsigned int s_rx_len;
  unsigned int s_tx_len;

  unsigned char s_rxb[MAX_FRAME];
  unsigned char s_txb[MAX_FRAME];

  unsigned char s_arb[ARP_FRAME]; /* arp buffer. sorry */

  uint8_t s_arp[ARP_CACHE][MAC_SIZE];

  struct katcp_arb *s_tap_io;
};

/* mac related utility functions ****************************************/

void generate_text_mac(char *text, unsigned int index)
{
  struct utsname un;
  char tmp[MAC_SIZE];
  pid_t pid;
  int i, j;

  memset(tmp, 0, MAC_SIZE);

  /* generate the mac address somehow */
  tmp[0] = 0x02; 
  tmp[1] = 0x40 - index;
  if((uname(&un) >= 0) && un.nodename){
    strncpy(tmp + 2, un.nodename, MAC_SIZE - 2);
  } else {
    pid = getpid();
    memcpy(tmp + 2, &pid, MAC_SIZE - 2);
  }

  snprintf(text, MAC_BUFFER, "%02x", tmp[1]);
  j = 2;

  for(i = 1; i < MAC_SIZE; i++){
    snprintf(text + j, MAC_BUFFER - j, ":%02x", tmp[i]);
    j += 3;
  }
}

int text_to_mac(uint8_t *binary, const char *text)
{
  int i;
  unsigned int v;
  char *end;
  const char *ptr;

  ptr = text;
  for(i = 0; i < 6; i++){
    v = strtoul(ptr, &end, 16);
    if(v >= 256){
      return -1;
    }
    binary[i] = v;
    if(i < 5){
      if(*end != ':'){
        return -1;
      }
      ptr = end + 1;
    }
  }

#ifdef DEBUG
  fprintf(stderr, "text_to_mac: in=%s, out=", text);
  for(i = 0; i < 6; i++){
    fprintf(stderr, "%02x ", binary[i]);
  }
  fprintf(stderr, "\n");
#endif

  return 0;
}

/* fpga io routines *****************************************************/

int write_fpga_mac(struct getap_state *gs, unsigned int offset, const uint8_t *mac)
{
  uint32_t value;
  void *base;

  if(gs->s_verbose > 1){
    log_message_katcp(gs->s_dispatch, KATCP_LEVEL_DEBUG, NULL, "writing mac %x:%x:%x:%x:%x:%x to offset 0x%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], offset);
  }

  base = gs->s_raw_mode->r_map + gs->s_register->e_pos_base;

  value = (   0x0         & 0xff000000) | 
          (   0x0         & 0xff0000) |
          ((mac[0] <<  8) & 0xff00) | 
           (mac[1]        & 0xff);
  *((uint32_t *)(base + offset)) = value;


  value = ((mac[2] << 24) & 0xff000000) | 
          ((mac[3] << 16) & 0xff0000) |
          ((mac[4] <<  8) & 0xff00) |
           (mac[5]        & 0xff);
  *((uint32_t *)(base + offset + 4)) = value;

  return 0;
}

static int transmit_frame_fpga(struct getap_state *gs)
{
  uint32_t buffer_sizes;
  struct katcp_dispatch *d;
  void *base;

  base = gs->s_raw_mode->r_map + gs->s_register->e_pos_base;
  d = gs->s_dispatch;

  if(gs->s_tx_len <= MIN_FRAME){
    if(gs->s_tx_len == 0){
      /* nothing to do */
      return 0;
    }
 
    /* pad out short packet */
    memset(gs->s_txb + gs->s_tx_len, 0, MIN_FRAME - gs->s_tx_len);
    gs->s_tx_len = MIN_FRAME;
  }

  if(gs->s_tx_len > MAX_FRAME){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "frame request %u exeeds limit %u", gs->s_tx_len, MAX_FRAME);
    return -1;
  }

  buffer_sizes = *((uint32_t *)(base + GO_BUFFER_SIZES));
#ifdef __PPC__
  if((buffer_sizes & 0xffff0000) > 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "tx still busy (%d)", (buffer_sizes & 0xffff0000) >> 16);
    return -1;
  }
#else
  log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "in test mode we ignore %d words previously queued", (buffer_sizes & 0xffff0000) >> 16);
#endif

  memcpy(base + GO_TXBUFFER, gs->s_txb, gs->s_tx_len);

  buffer_sizes = ((gs->s_tx_len + 7) / 8) << 16;
  *((uint32_t *)(base + GO_BUFFER_SIZES)) = buffer_sizes;

#if 0
  fprintf(stderr, "txb: loaded %u bytes into TX buffer: \n",wr);
  for (wr=0;wr<len;wr++){
        fprintf(stderr, "%02X ",data[wr]);
  }
  fprintf(stderr,"\n");
  if(lseek(gs->s_bfd, GO_TXBUFFER, SEEK_SET) != GO_TXBUFFER){
    fprintf(stderr, "txb: unable to seek to 0x%x\n", GO_TXBUFFER);
    return -1;
  }
  read(gs->s_bfd, gs->s_rxb, len);
  fprintf(stderr, "txb: chk readback %uB frm TX buffer: \n",wr);
  for (wr=0;wr<len;wr++){
    fprintf(stderr, "%02X ",*((gs->s_rxb) + wr));
  }
  fprintf(stderr,"\n");
#endif


#ifdef DEBUG
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "sent %u words to fpga from tap device %s\n", buffer_sizes >> 16, gs->s_tap_name);
#endif

  /* buffer empty, next round */
  gs->s_tx_len = 0;

  return 1;
}

int transmit_ip_fpga(struct getap_state *gs)
{
  uint8_t *mac;

  mac = gs->s_arp[gs->s_txb[SIZE_FRAME_HEADER + IP_DEST4]];
#ifdef DEBUG
  fprintf(stderr, "txb: dst mac %x:%x:%x:%x:%x:%x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
  memcpy(gs->s_txb, mac, MAC_SIZE);

  return transmit_frame_fpga(gs);
}

/* configure tap device *************************************************/

static int configure_tap(struct getap_state *gs)
{
  char cmd_buffer[CMD_BUFFER];
  int len;

  len = snprintf(cmd_buffer, CMD_BUFFER, "ifconfig %s %s netmask 255.255.255.0 up\n", gs->s_tap_name, gs->s_address_name);
  cmd_buffer[CMD_BUFFER - 1] = '\0';

  /* TODO: stalls the system */
  if(system(cmd_buffer)){
    return -1;
  }

  return 0;
}

int receive_io_tap(struct katcp_dispatch *d, struct getap_state *gs)
{
#ifdef DEBUG
  int i;
#endif
  int rr;

#ifdef DEBUG
  fprintf(stderr, "tap: got something to read from tap device\n");
#endif

  if(gs->s_tx_len > 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "transmit buffer on device %s still in use", gs->s_tap_name);
    return 0;
  }

  rr = read(gs->s_tap_fd, gs->s_txb + SIZE_FRAME_HEADER, MAX_FRAME - SIZE_FRAME_HEADER);
  switch(rr){
    case -1 :
      switch(errno){
        case EAGAIN : 
        case EINTR  :
          break;
        default :
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "read from tap device %s failed: %s", gs->s_tap_name, strerror(errno));
          return -1;
      }
    case  0 :
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "got unexpected end of file from tap device %s", gs->s_tap_name);
      return -1;
  }

  if(rr < RUNT_LENGTH){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "read runt packet from tap deivce %s", gs->s_tap_name);
    return 0;
  }

#ifdef DEBUG
  fprintf(stderr, "rxt: tap rx=%d, data=", rr);
  for(i = 0; i < rr; i++){
    fprintf(stderr, " %02x", gs->s_txb[i]); 
  }
  fprintf(stderr, "\n");
#endif

  gs->s_tx_len = rr + SIZE_FRAME_HEADER;

  return 1;
}

int run_io_tap(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode)
{
  struct getap_state *gs;
  int result;

  gs = data_arb_katcp(d, a);
  if(gs == NULL){
#ifdef DEBUG
      fprintf(stderr, "tap: no data associated with io callback\n");
      abort();
#endif
    return -1;
  }

  if(mode & KATCP_ARB_READ){
    result = receive_io_tap(d, gs);
    if(result > 0){
      transmit_ip_fpga(gs);
    }
  }

  if(mode & KATCP_ARB_WRITE){
    /* TODO */
  }

  return 0;
}

/* state allocations ****************************************************/

void destroy_getap(struct katcp_dispatch *d, struct getap_state *gs)
{
  if(gs == NULL){
    return;
  }

  gs->s_raw_mode = NULL;
  gs->s_register = NULL;

  /* TODO: deallocate more */

  if(gs->s_tap_io){
    unlink_arb_katcp(d, gs->s_tap_io);
    gs->s_tap_io = NULL;
  }

  if(gs->s_tap_fd >= 0){
    tap_close(gs->s_tap_fd);
    gs->s_tap_fd = (-1);
  }

  if(gs->s_tap_name){
    free(gs->s_tap_name);
    gs->s_tap_name = NULL;
  }

  free(gs);
}

struct getap_state *create_getap(struct katcp_dispatch *d, unsigned int instance, char *name, char *tap, char *ip, unsigned int port, char *mac)
{
  struct getap_state *gs; 

#ifdef DEBUG
  fprintf(stderr, "attempting to set up tap device %s from register %s (ip=%s, mac=%s)\n", tap, name, ip, mac);
#endif

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "attempting to set up tap device %s", tap);

  gs = malloc(sizeof(struct getap_state));
  if(gs == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate state for %s", name);
    return NULL;
  }

  gs->s_dispatch = d;
  gs->s_raw_mode = NULL;

  gs->s_tap_name = NULL;

  gs->s_address_name[0] = '\0';
  gs->s_gateway_name[0] = '\0';
  gs->s_mac_name[0] = '\0';
  gs->s_port = GO_DEFAULT_PORT;

  gs->s_index = 0;

  /* mac, address, mask, network binary */

  gs->s_register = NULL;
  gs->s_instance = 0;

  gs->s_tap_fd = (-1);

  gs->s_verbose = 1;
  gs->s_testing = 0;

  gs->s_tap_io = NULL;

  /* TODO: initialise the rest of the structure here */

  gs->s_raw_mode = get_mode_katcp(d, TBS_MODE_RAW);
  if(gs->s_raw_mode == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need raw mode for tap operation");
    destroy_getap(d, gs);
    return NULL;
  }

  gs->s_register = find_data_avltree(gs->s_raw_mode->r_registers, name);
  if(gs->s_register == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no register of name %s available", name);
    destroy_getap(d, gs);
    return NULL;
  }

  gs->s_tap_name = strdup(tap);
  if(gs->s_tap_name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to duplicate tap device name %s", tap);
    destroy_getap(d, gs);
    return NULL;
  }

  strncpy(gs->s_address_name, ip, IP_BUFFER);
  gs->s_address_name[IP_BUFFER - 1] = '\0';

  /* TODO: populate gateway */

  if(mac){
    strncpy(gs->s_mac_name, mac, MAC_BUFFER);
  } else {
    generate_text_mac(gs->s_mac_name, gs->s_instance); /* TODO: increment index somehow */
  }

  if(port > 0){
    gs->s_port = port;
  }

  gs->s_tap_fd = tap_open(gs->s_tap_name);
  if(gs->s_tap_fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create tap device %s", gs->s_tap_name);
    destroy_getap(d, gs);
    return NULL;
  }

  gs->s_tap_io = create_arb_katcp(d, gs->s_tap_name, gs->s_tap_fd, KATCP_ARB_READ, &run_io_tap, gs);
  if(gs->s_tap_io == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create io handler for tap device %s", gs->s_tap_name);
    destroy_getap(d, gs);
    return NULL;
  }

  if(configure_tap(gs)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to configure tap device");
    destroy_getap(d, gs);
    return NULL;
  }

  return gs;
}

/* commands registered **************************************************/

int tap_start_cmd(struct katcp_dispatch *d, int argc)
{
  char *name, *tap, *ip, *mac;
  unsigned int port;
  struct getap_state *gs;

  mac = NULL;
  port = 0;

  if(argc < 4){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need at least a tap device name, register name and an ip address");
    return KATCP_RESULT_INVALID;
  }

  tap = arg_string_katcp(d,1);
  name = arg_string_katcp(d, 2);
  ip = arg_string_katcp(d, 3);

  if((tap == NULL) || (ip == NULL) || (name == NULL)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire essential parameters");
    return KATCP_RESULT_INVALID;
  }

  if(argc > 4){
    port = arg_unsigned_long_katcp(d, 4);
    if(port == 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire optional port");
      return KATCP_RESULT_INVALID;
    }

    if(argc > 5){
      mac = arg_string_katcp(d, 5);
      if(mac == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire optional mac");
        return KATCP_RESULT_INVALID;
      }
    }
  }

  gs = create_getap(d, 0, name, tap, ip, port, mac);
  if(gs == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to initialise tap module");
    return KATCP_RESULT_INVALID;
  }

  return KATCP_RESULT_FAIL;
}

int tap_stop_cmd(struct katcp_dispatch *d, int argc)
{
  char *name;

  if(argc <= 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a register name");
    return KATCP_RESULT_INVALID;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal failure while acquireing parameters");
    return KATCP_RESULT_FAIL;
  }

#if 0
  result = end_name_shared_katcp(d, name, 1);
  if(result < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to terminate any prior instance of %s", name);
    return KATCP_RESULT_FAIL;
  }

  if(result == 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "could not find an instance of %s to terminate", name);
  }
#endif

  return KATCP_RESULT_FAIL;
}


