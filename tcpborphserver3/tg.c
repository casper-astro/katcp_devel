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

#define POLL_INTERVAL      3  /* in msecs */
#define ARP_PERIOD       101 /* multiples of POLL_INTERVAL */

#define RUNT_LENGTH       20 /* want a basic ip packet */

#define GS_MAGIC  0x490301fc

#define FRESH_FOUND    15000 /* length of time to cache a valid reply - units iffy */
#define FRESH_REQUEST  10000 /* length of time for next request - units iffy */
#define FRESH_SELF      7000 /* interval when we announce ourselves - good idea to be shorter than others */

#define RECEIVE_BURST     16 /* read at most N frames per polling interval */

static const uint8_t arp_const[] = { 0, 1, 8, 0, 6, 4, 0 }; /* disgusting */
static const uint8_t broadcast_const[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

struct getap_state{
  uint32_t s_magic;

  struct katcp_dispatch *s_dispatch;
  struct tbs_raw *s_raw_mode;

  char *s_tap_name;

  /* these names have bounded sizes */
  char s_address_name[IP_BUFFER];
  char s_gateway_name[IP_BUFFER];
  char s_mac_name[MAC_BUFFER];

  unsigned short s_port;

  unsigned int s_self;

  uint8_t s_mac_binary[MAC_SIZE];
  uint32_t s_address_binary;
  uint32_t s_mask_binary;
  uint32_t s_network_binary;

  unsigned int s_instance;
  uint16_t s_iteration;
  unsigned int s_burst;

  struct tbs_entry *s_register;

  struct katcp_arb *s_tap_io;
  int s_tap_fd;

#if 0
  struct timeval s_timeout;
#endif

  unsigned int s_timer;

  unsigned int s_rx_len;
  unsigned int s_tx_len;
  unsigned int s_arp_len;

  unsigned char s_rxb[MAX_FRAME];
  unsigned char s_txb[MAX_FRAME];
  unsigned char s_arp_buffer[ARP_FRAME];

  uint8_t s_arp_table[ARP_CACHE][MAC_SIZE];
  uint16_t s_arp_fresh[ARP_CACHE];
};

/************************************************************************/

static int write_mac_fpga(struct getap_state *gs, unsigned int offset, const uint8_t *mac);
static int write_frame_fpga(struct getap_state *gs, unsigned char *data, unsigned int len);

/************************************************************************/

#ifdef DEBUG
static void sane_gs(struct getap_state *gs)
{
  if(gs == NULL){
    fprintf(stderr, "tap: invalid handle\n");
    abort();
  }
  if(gs->s_magic != GS_MAGIC){
    fprintf(stderr, "tap: bad magic 0x%x\n", gs->s_magic);
    abort();
  }
}
#else
#define sane_gs(d)
#endif

/* mac parsing and generation *******************************************/

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

  snprintf(text, MAC_BUFFER, "%02x", tmp[0]);
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

/* arp related functions  ***********************************************/

int set_entry_arp(struct getap_state *gs, unsigned int index, const uint8_t *mac, unsigned int fresh)
{

#ifdef DEBUG
  fprintf(stderr, "arp: entering at index %u\n", index);
  if(index > ARP_CACHE){
    fprintf(stderr, "arp: logic failure: index %u out of range\n", index);
  }
#endif

  memcpy(gs->s_arp_table[index], mac, 6);
  gs->s_arp_fresh[index] = gs->s_iteration + fresh;

  return write_mac_fpga(gs, GO_ARPTABLE + (8 * index), mac);
}

void glean_arp(struct getap_state *gs, uint8_t *mac, uint8_t *ip)
{
  uint32_t v;

  v = ((ip[0] << 24) & 0xff000000) | 
      ((ip[1] << 16) & 0xff0000) |
      ((ip[2] <<  8) & 0xff00) |
      ( ip[3]        & 0xff);

  if(v == 0){
    return;
  }

  if(ip[3] == 0xff){
    return;
  }

  if((v & gs->s_mask_binary) != gs->s_network_binary){
#ifdef DEBUG
    fprintf(stderr, "glean: not my network 0x%08x != 0x%08x\n", v & gs->s_mask_binary, gs->s_network_binary);
#endif
    return;
  }

#ifdef DEBUG
  fprintf(stderr, "glean: adding entry %d\n", ip[3]);
#endif

  set_entry_arp(gs, ip[3], mac, FRESH_FOUND);
}

void announce_arp(struct getap_state *gs)
{
  uint32_t subnet;
  int result;

  subnet = (~(gs->s_mask_binary)) | gs->s_address_binary;

  memcpy(gs->s_arp_buffer + FRAME_DST, broadcast_const, 6);
  memcpy(gs->s_arp_buffer + FRAME_SRC, gs->s_mac_binary, 6);

  gs->s_arp_buffer[FRAME_TYPE1] = 0x08;
  gs->s_arp_buffer[FRAME_TYPE2] = 0x06;

  memcpy(gs->s_arp_buffer + SIZE_FRAME_HEADER, arp_const, 7);

  gs->s_arp_buffer[SIZE_FRAME_HEADER + ARP_OP2] = 2;

  /* spam the subnet */
  memcpy(gs->s_arp_buffer + SIZE_FRAME_HEADER + ARP_TIP_BASE, &subnet, 4);
  memcpy(gs->s_arp_buffer + SIZE_FRAME_HEADER + ARP_THA_BASE, broadcast_const, 6);

  /* write in our own sending information */
  memcpy(gs->s_arp_buffer + SIZE_FRAME_HEADER + ARP_SIP_BASE, &(gs->s_address_binary), 4);
  memcpy(gs->s_arp_buffer + SIZE_FRAME_HEADER + ARP_SHA_BASE, gs->s_mac_binary, 6);

#ifdef DEBUG
  fprintf(stderr, "arp: sending arp announce\n");
#endif


  gs->s_arp_fresh[gs->s_self] = gs->s_iteration + FRESH_SELF;
  gs->s_arp_len = 42;

  result = write_frame_fpga(gs, gs->s_arp_buffer, gs->s_arp_len);
  if(result != 0){
    gs->s_arp_len = 0;
  }
}

static void request_arp(struct getap_state *gs, int index)
{
  uint32_t host;
  int result;

  if(gs->s_self == index){
    return;
  }

  host = htonl(index) | (gs->s_mask_binary & gs->s_address_binary);

  memcpy(gs->s_arp_buffer + FRAME_DST, broadcast_const, 6);
  memcpy(gs->s_arp_buffer + FRAME_SRC, gs->s_mac_binary, 6);

  gs->s_arp_buffer[FRAME_TYPE1] = 0x08;
  gs->s_arp_buffer[FRAME_TYPE2] = 0x06;

  memcpy(gs->s_arp_buffer + SIZE_FRAME_HEADER, arp_const, 7);

  gs->s_arp_buffer[SIZE_FRAME_HEADER + ARP_OP2] = 1;

  memcpy(gs->s_arp_buffer + SIZE_FRAME_HEADER + ARP_TIP_BASE, &host, 4);
  memcpy(gs->s_arp_buffer + SIZE_FRAME_HEADER + ARP_THA_BASE, broadcast_const, 6);

  /* write in our own sending information */
  memcpy(gs->s_arp_buffer + SIZE_FRAME_HEADER + ARP_SIP_BASE, &(gs->s_address_binary), 4);
  memcpy(gs->s_arp_buffer + SIZE_FRAME_HEADER + ARP_SHA_BASE, gs->s_mac_binary, 6);

#ifdef DEBUG
  fprintf(stderr, "arp: sending arp request for index %d (host=0x%08x)\n", index, host);
#endif

  gs->s_arp_fresh[index] = gs->s_iteration + FRESH_REQUEST;
  gs->s_arp_len = 42;

  result = write_frame_fpga(gs, gs->s_arp_buffer, gs->s_arp_len);
  if(result != 0){
    gs->s_arp_len = 0;
  }
}

int reply_arp(struct getap_state *gs)
{
  int result;

  /* WARNING: attempt to get away without using the arp buffer, just turn the rx buffer around */
  memcpy(gs->s_rxb + FRAME_DST, gs->s_rxb + FRAME_SRC, 6);
  memcpy(gs->s_rxb + FRAME_SRC, gs->s_mac_binary, 6);

  gs->s_rxb[SIZE_FRAME_HEADER + ARP_OP2] = 2;

  /* make sender of receive the target of transmit*/
  memcpy(gs->s_rxb + SIZE_FRAME_HEADER + ARP_THA_BASE, gs->s_rxb + SIZE_FRAME_HEADER + ARP_SHA_BASE, 10); 

  /* write in our own sending information */
  memcpy(gs->s_rxb + SIZE_FRAME_HEADER + ARP_SIP_BASE, &(gs->s_address_binary), 4);
  memcpy(gs->s_rxb + SIZE_FRAME_HEADER + ARP_SHA_BASE, gs->s_mac_binary, 6);

#ifdef DEBUG
  fprintf(stderr, "arp: sending arp reply\n");
#endif

  result = write_frame_fpga(gs, gs->s_rxb, 42);
  if(result == 0){
    /* WARNING: in case of write failure, clobber arp buffer to save reply. The buffer could contain something else (probably generated by spam_arp), but we assume the current one is more important. In any event, if writes fail on the 10Gbe interface we are already in trouble */
    memcpy(gs->s_arp_buffer, gs->s_rxb, 42);
    gs->s_arp_len = 42;
  }

  return result;
}

int process_arp(struct getap_state *gs)
{
#ifdef DEBUG
  fprintf(stderr, "arp: got arp packet\n");
#endif

  if(memcmp(arp_const, gs->s_rxb + SIZE_FRAME_HEADER, 7)){
    fprintf(stderr, "arp: unknown or malformed arp packet\n");
    return 1;
  }

  switch(gs->s_rxb[SIZE_FRAME_HEADER + ARP_OP2]){
    case 2 : /* reply */
#ifdef DEBUG
      fprintf(stderr, "arp: saw reply\n");
#endif
      glean_arp(gs, gs->s_rxb + SIZE_FRAME_HEADER + ARP_SHA_BASE, gs->s_rxb + SIZE_FRAME_HEADER + ARP_SIP_BASE);
      return 1;

    case 1 : /* request */
#ifdef DEBUG
      fprintf(stderr, "arp: saw request\n");
#endif
      glean_arp(gs, gs->s_rxb + SIZE_FRAME_HEADER + ARP_SHA_BASE, gs->s_rxb + SIZE_FRAME_HEADER + ARP_SIP_BASE);
      if(!memcmp(gs->s_rxb + SIZE_FRAME_HEADER + ARP_TIP_BASE, &(gs->s_address_binary), 4)){
#ifdef DEBUG
        fprintf(stderr, "arp: somebody is looking for me\n");
#endif
        return reply_arp(gs);
      } else {
        return 1;
      }
    default :
      fprintf(stderr, "arp: unhandled arp message %x\n", gs->s_rxb[SIZE_FRAME_HEADER + ARP_OP2]);
      return 1;
  }
}

void spam_arp(struct getap_state *gs)
{
  unsigned int i;
  uint32_t update;

  /* unfortunate, but the gateware needs to know other systems and can't wait, so we have to work things out in advance */

  if(gs->s_arp_len > 0){
    /* already stuff in arp buffer not flushed, wait ... */
    return;
  }

  update = 0;

  for(i = 1; i < 254; i++){
    if(gs->s_arp_fresh[i] == gs->s_iteration){
      if(update){ /* defer */
        gs->s_arp_fresh[i] += update;
      } else {
        if(i == gs->s_self){
          announce_arp(gs);
        } else {
          request_arp(gs, i);
        }
      }
      update++;
    }
  }

  gs->s_iteration++;
}

/* transmit to gateware *************************************************/

static int write_mac_fpga(struct getap_state *gs, unsigned int offset, const uint8_t *mac)
{
  uint32_t value;
  void *base;

#if 0
  log_message_katcp(gs->s_dispatch, KATCP_LEVEL_DEBUG, NULL, "writing mac %x:%x:%x:%x:%x:%x to offset 0x%x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], offset);
#endif

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

#if 0
int write_mac_fpga(struct getap_state *gs, unsigned int offset, const uint8_t *mac)
{
  uint32_t v[2];
  struct katcp_dispatch *d;
  void *base;

  d = gs->s_dispatch;
  base = gs->s_raw_mode->r_map + gs->s_register->e_pos_base + offset;

  v[0] = (   0x0         & 0xff000000) | 
         (   0x0         & 0xff0000) |
         ((mac[0] <<  8) & 0xff00) | 
          (mac[1]        & 0xff);
  v[1] = ((mac[2] << 24) & 0xff000000) | 
         ((mac[3] << 16) & 0xff0000) |
         ((mac[4] <<  8) & 0xff00) |
          (mac[5]        & 0xff);

  memcpy(base, v, 8);

  return 0;
}
#endif

/* transmit to gateware *************************************************/

static int write_frame_fpga(struct getap_state *gs, unsigned char *data, unsigned int len)
{
  uint32_t buffer_sizes;
  unsigned int actual;
  struct katcp_dispatch *d;
  void *base;

  base = gs->s_raw_mode->r_map + gs->s_register->e_pos_base;
#ifdef DEBUG
  fprintf(stderr, "txf: base address is %p + 0x%x\n", gs->s_raw_mode->r_map, gs->s_register->e_pos_base);
#endif
  d = gs->s_dispatch;

  if(len <= MIN_FRAME){
    if(len == 0){
      /* nothing to do */
      return 1;
    }
 
    /* pad out short packet */
    memset(data + len, 0, MIN_FRAME - len);
    actual = MIN_FRAME;
  } else {
    actual = len;
  }

  if(actual > MAX_FRAME){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "frame request %u exeeds limit %u", actual, MAX_FRAME);
    return -1;
  }

  buffer_sizes = *((uint32_t *)(base + GO_BUFFER_SIZES));
#ifdef DEBUG
  fprintf(stderr, "txf: previous value in tx word count is %d", buffer_sizes >> 16);
#endif

  if((buffer_sizes & 0xffff0000) > 0){
#ifdef __PPC__
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "tx queue still busy (%d words to send)", (buffer_sizes & 0xffff0000) >> 16);
    return 0;
#else
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "in test mode we ignore %d words previously queued", (buffer_sizes & 0xffff0000) >> 16);
#endif
  }

  memcpy(base + GO_TXBUFFER, data, actual);

  buffer_sizes = (buffer_sizes & 0xffff) & (0xffff0000 | (((actual + 7) / 8) << 16));
  *((uint32_t *)(base + GO_BUFFER_SIZES)) = buffer_sizes;

#if 0
  fprintf(stderr, "txf: loaded %u bytes into TX buffer: \n",wr);
  for (wr=0;wr<len;wr++){
        fprintf(stderr, "%02X ",data[wr]);
  }
  fprintf(stderr,"\n");
  if(lseek(gs->s_bfd, GO_TXBUFFER, SEEK_SET) != GO_TXBUFFER){
    fprintf(stderr, "txf: unable to seek to 0x%x\n", GO_TXBUFFER);
    return -1;
  }
  read(gs->s_bfd, gs->s_rxb, len);
  fprintf(stderr, "txf: chk readback %uB frm TX buffer: \n",wr);
  for (wr=0;wr<len;wr++){
    fprintf(stderr, "%02X ",*((gs->s_rxb) + wr));
  }
  fprintf(stderr,"\n");
#endif


#ifdef DEBUG
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "sent %u words to fpga from tap device %s\n", buffer_sizes >> 16, gs->s_tap_name);
#endif

  return 1;
}

static int transmit_frame_fpga(struct getap_state *gs)
{
  int result;

  result = write_frame_fpga(gs, gs->s_txb, gs->s_tx_len);
  if(result != 0){
    gs->s_tx_len = 0;
  }

  return result;
}

int transmit_ip_fpga(struct getap_state *gs)
{
  uint8_t *mac;

  mac = gs->s_arp_table[gs->s_txb[SIZE_FRAME_HEADER + IP_DEST4]];
#ifdef DEBUG
  fprintf(stderr, "txf: looked up dst mac: %x:%x:%x:%x:%x:%x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
  memcpy(gs->s_txb, mac, MAC_SIZE);

  return transmit_frame_fpga(gs);
}

/* receive from gateware ************************************************/

int receive_frame_fpga(struct getap_state *gs)
{
  /* 1 - useful data, 0 - false alarm, -1 problem */
  struct katcp_dispatch *d;
  uint32_t buffer_sizes;
  int len;
  void *base;
#ifdef DEBUG
  int i;
#endif

  base = gs->s_raw_mode->r_map + gs->s_register->e_pos_base;
#ifdef DEBUG
  fprintf(stderr, "rxf: base address is %p + 0x%x\n", gs->s_raw_mode->r_map, gs->s_register->e_pos_base);
#endif
  d = gs->s_dispatch;

  if(gs->s_rx_len > 0){
    fprintf(stderr, "rxf: receive buffer (%u) not yet cleared\n", gs->s_rx_len);
    return -1;
  }

  buffer_sizes = *((uint32_t *)(base + GO_BUFFER_SIZES));
  len = (buffer_sizes & 0xffff) * 8;
  if(len <= 0){
#ifdef DEBUG
    fprintf(stderr, "rxf: nothing to read: register 0x%x\n", buffer_sizes);
#endif
    return 0;
  }

#ifdef DEBUG
  fprintf(stderr, "rxf: %d bytes to read\n", len);
#endif

  if((len <= SIZE_FRAME_HEADER) || (len > MAX_FRAME)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "saw runt or oversized frame, len=%u\n bytes", len);
    return -1;
  }

  memcpy(gs->s_rxb, base + GO_RXBUFFER, len);

  gs->s_rx_len = len;

#ifdef DEBUG
  fprintf(stderr, "rxf: data:");
  for(i = 0; i < len; i++){
    fprintf(stderr, " %02x", gs->s_rxb[i]);
  }
  fprintf(stderr, "\n");
#endif

  /* WARNING: still unclear how this register ends up being read and writeable */
  buffer_sizes &= 0xffff0000;
  *((uint32_t *)(base + GO_BUFFER_SIZES)) = buffer_sizes;

  return 1;
}

/* receive from kernel **************************************************/

int receive_ip_kernel(struct katcp_dispatch *d, struct getap_state *gs)
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
          return 0;
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

/* send to kernel *******************************************************/

static void forget_receive(struct getap_state *gs)
{
  gs->s_rx_len = 0;
}

int transmit_ip_kernel(struct getap_state *gs)
{
  int wr;
  struct katcp_dispatch *d;

  d = gs->s_dispatch;

  if(gs->s_rx_len <= SIZE_FRAME_HEADER){
    if(gs->s_rx_len == 0){
      return 1;
    } else {
      forget_receive(gs);
      return -1;
    }
  }

  wr = write(gs->s_tap_fd, gs->s_rxb + SIZE_FRAME_HEADER, gs->s_rx_len - SIZE_FRAME_HEADER);
  if(wr < 0){
    switch(errno){
      case EINTR  :
      case EAGAIN :
        return 0;
      default :
        log_message_katcp(gs->s_dispatch, KATCP_LEVEL_WARN, NULL, "write to tap device %s failed: %s", gs->s_tap_name, strerror(errno));
        /* WARNING: drops packet on floor, better than spamming logs */
        forget_receive(gs);
        return -1;
    }
  }

  if((wr + SIZE_FRAME_HEADER) < gs->s_rx_len){
    log_message_katcp(gs->s_dispatch, KATCP_LEVEL_WARN, NULL, "incomplete packet transmission to %s: %d + %d < %u", gs->s_tap_name, SIZE_FRAME_HEADER, wr, gs->s_rx_len);
    /* WARNING: also ditches packet, otherwise we might have an unending stream of fragments (for some errors) */
    forget_receive(gs);
    return -1;
  }

  forget_receive(gs);

  return 1;
}

/* callback/scheduling parts ********************************************/

int run_timer_tap(struct katcp_dispatch *d, void *data)
{
  struct getap_state *gs;
  struct katcp_arb *a;
  int result, run;
  unsigned int burst;

  gs = data;

  sane_gs(gs);

  a = gs->s_tap_io;

  /* attempt to flush out stuff still stuck in buffers */

  if(gs->s_arp_len > 0){
    result = write_frame_fpga(gs, gs->s_arp_buffer, gs->s_arp_len);
    if(result != 0){
      gs->s_arp_len = 0;
    }
  }

  if(gs->s_tx_len > 0){
    result = write_frame_fpga(gs, gs->s_txb, gs->s_tx_len);
    if(result != 0){
      gs->s_tx_len = 0;
    }
  }

  /* TODO: might want to handle burst of traffic better, instead of waiting poll interval for next frame */
  do{

    if(receive_frame_fpga(gs) > 0){

      if(gs->s_rxb[FRAME_TYPE1] == 0x08){
        switch(gs->s_rxb[FRAME_TYPE2]){
          case 0x00 : /* IP packet */
            if(transmit_ip_kernel(gs) == 0){
              /* attempt another transmit when tfd becomes writable */
              mode_arb_katcp(d, a, KATCP_ARB_READ | KATCP_ARB_WRITE);
              run = 0; /* don't bother getting more if we can't send it on */
            }
            break;

          case 0x06 : /* arp packet */
            if(process_arp(gs) == 0){
              run = 0; /* arp reply stalled, wait ... */
            }
            forget_receive(gs);
            break;

          default :

            log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "discarding frame of unknown type 0x%02x%02x and length %d\n", gs->s_rxb[FRAME_TYPE1], gs->s_rxb[FRAME_TYPE2], gs->s_rx_len);
            forget_receive(gs);

            break;
        }
      } else {
        forget_receive(gs);
      }

      burst++;

      if(burst > gs->s_burst){
        run = 0;
      }

    } else { /* nothing more to receive */
      run = 0;
    }
  } while(run);

  if(burst < 1){ /* only spam the network if we don't have anything better to do */
    spam_arp(gs);
  }

  return 0;
}

int run_io_tap(struct katcp_dispatch *d, struct katcp_arb *a, unsigned int mode)
{
  struct getap_state *gs;
  int result;

  gs = data_arb_katcp(d, a);

  sane_gs(gs);

  if(mode & KATCP_ARB_READ){
    result = receive_ip_kernel(d, gs);
    if(result > 0){
      transmit_ip_fpga(gs); /* if it doesn't work out, hope that next schedule will clear it */
    }
  }

  if(mode & KATCP_ARB_WRITE){
    if(transmit_ip_kernel(gs) != 0){ /* unless we have to defer again disable write select */
      mode_arb_katcp(d, a, KATCP_ARB_READ);
    }
  }

  return 0;
}

/* configure fpga *******************************************************/

int configure_fpga(struct getap_state *gs)
{
  struct in_addr in;
  uint32_t value;
  unsigned int i;
  void *base;
  struct katcp_dispatch *d;

  sane_gs(gs);

  d = gs->s_dispatch;
  base = gs->s_raw_mode->r_map + gs->s_register->e_pos_base;

  if(text_to_mac(gs->s_mac_binary, gs->s_mac_name) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse %s to a mac address", gs->s_mac_name);
    return -1;
  }
  if(write_mac_fpga(gs, GO_MAC, gs->s_mac_binary) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to tell gateware about our own mac %s", gs->s_mac_name);
    return -1;
  }

  memcpy(gs->s_txb + 6, gs->s_mac_binary, 6);
  gs->s_txb[FRAME_TYPE1] = 0x08;
  gs->s_txb[FRAME_TYPE2] = 0x00;

  if(gs->s_gateway_name[0] != '\0'){
    if(inet_aton(gs->s_gateway_name, &in) == 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse gateway %s to ip address", gs->s_gateway_name);
      return -1;
    }
    value = (in.s_addr) & 0xff; /* WARNING: unclear why this has to be masked */

    *((uint32_t *)(base + GO_GATEWAY)) = value;
  }

  if(inet_aton(gs->s_address_name, &in) == 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to parse %s to ip address", gs->s_address_name);
    return -1;
  }
  if(sizeof(in.s_addr) != 4){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "major logic problem: ip address not 4 bytes");
    return -1;
  }
  value = in.s_addr;

  gs->s_address_binary = value; /* in network byte order */
  gs->s_mask_binary = htonl(0xffffff00); /* fixed mask */
  gs->s_network_binary = gs->s_mask_binary & gs->s_address_binary;

  gs->s_self = ntohl(~(gs->s_mask_binary) & gs->s_address_binary);

  *((uint32_t *)(base + GO_ADDRESS)) = value;

  if(gs->s_port){
    /* assumes plain big endian value */
    /* Bitmask: 24   : Reset core */
    /*          16   : Enable fabric interface */
    /*          00-15: Port */
    /* First, reset the core */

    /* WARNING: the below use of the + operator doesn't look right. Jason ?  */

    value = (0xff << 16) + (0xff << 16) + (gs->s_port);
    *((uint32_t *)(base + GO_EN_RST_PORT)) = value;

    /* Next, remove core from reset state: */
    value = (0x00 << 16) + (0xff << 16) + (gs->s_port);
    *((uint32_t *)(base + GO_EN_RST_PORT)) = value;
  }

  for(i = 0; i < ARP_CACHE; i++){
    /* heuristic to make things less bursty ... unclear if it is worth anything */
    set_entry_arp(gs, i, broadcast_const, ((gs->s_self % 32) * 4) + gs->s_iteration + i);
  }

  set_entry_arp(gs, gs->s_self, gs->s_mac_binary, gs->s_self % 32); /* announce ourselves as function of IP % 32 - an attempt to make things less bursty */

  return 0;
}

/* configure tap device *************************************************/

static int configure_tap(struct getap_state *gs)
{
  char cmd_buffer[CMD_BUFFER];
  int len;

  len = snprintf(cmd_buffer, CMD_BUFFER, "ifconfig %s %s netmask 255.255.255.0 up\n", gs->s_tap_name, gs->s_address_name);
  cmd_buffer[CMD_BUFFER - 1] = '\0';

  /* WARNING: stalls the system, could possibly handle it via a job command */
  if(system(cmd_buffer)){
    return -1;
  }

  return 0;
}

/* state allocations ****************************************************/

void destroy_getap(struct katcp_dispatch *d, struct getap_state *gs)
{
  sane_gs(gs);

  /* make all the callbacks go away */

  if(gs->s_tap_io){
    unlink_arb_katcp(d, gs->s_tap_io);
    gs->s_tap_io = NULL;
  }

  if(gs->s_timer){
    discharge_timer_katcp(d, gs);
    gs->s_timer = 0;
  }

  /* empty out the rest of the data structure */

  if(gs->s_tap_fd >= 0){
    tap_close(gs->s_tap_fd);
    gs->s_tap_fd = (-1);
  }

  if(gs->s_tap_name){
    free(gs->s_tap_name);
    gs->s_tap_name = NULL;
  }

  /* now ensure that things are invalidated */

  gs->s_raw_mode = NULL;
  gs->s_register = NULL;
  gs->s_dispatch = NULL;

  gs->s_port = 0;
  gs->s_self = 0;
  gs->s_iteration = 0;

  gs->s_rx_len = 0;
  gs->s_tx_len = 0;
  gs->s_arp_len = 0;

  gs->s_magic = 0;

  free(gs);
}

struct getap_state *create_getap(struct katcp_dispatch *d, unsigned int instance, char *name, char *tap, char *ip, unsigned int port, char *mac, unsigned int period)
{
  struct getap_state *gs; 
  unsigned int i;

#ifdef DEBUG
  fprintf(stderr, "attempting to set up tap device %s from register %s (ip=%s, mac=%s)\n", tap, name, ip, mac);
#endif

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "attempting to set up tap device %s", tap);

  gs = malloc(sizeof(struct getap_state));
  if(gs == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate state for %s", name);
    return NULL;
  }

  gs->s_magic = GS_MAGIC;

  gs->s_dispatch = d;
  gs->s_raw_mode = NULL;

  gs->s_tap_name = NULL;

  gs->s_address_name[0] = '\0';
  gs->s_gateway_name[0] = '\0';
  gs->s_mac_name[0] = '\0';
  gs->s_port = GO_DEFAULT_PORT;

  gs->s_self = 0;

  /* mac, address, mask, network binary */

  gs->s_instance = 0;
  gs->s_iteration = 0;
  gs->s_burst = RECEIVE_BURST;

  gs->s_register = NULL;

  gs->s_tap_io = NULL;
  gs->s_tap_fd = (-1);

  gs->s_timer = 0;

  gs->s_rx_len = 0;
  gs->s_tx_len = 0;
  gs->s_arp_len = 0;

  /* buffers, table */

  for(i = 0; i < ARP_CACHE; i++){
    gs->s_arp_fresh[i] = i;
  }

  /* initialise the rest of the structure here */

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
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "generated mac %s", gs->s_mac_name);
  }

  if(port > 0){
    gs->s_port = port;
  }

  if(configure_fpga(gs) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to initialise gateware on %s", name);
    destroy_getap(d, gs);
    return NULL;
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

  if(register_every_ms_katcp(d, period, &run_timer_tap, gs) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register timer for interval of %dms", gs->s_timer);
    destroy_getap(d, gs);
    return NULL;
  }

  gs->s_timer = period; /* a nonzero value here means the timer is running ... */

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

  gs = create_getap(d, 0, name, tap, ip, port, mac, POLL_INTERVAL);
  if(gs == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to initialise tap module");
    return KATCP_RESULT_INVALID;
  }

  return KATCP_RESULT_OK;
}

int tap_stop_cmd(struct katcp_dispatch *d, int argc)
{
  char *name;
  struct katcp_arb *a;
  struct getap_state *gs;

  if(argc <= 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a register name");
    return KATCP_RESULT_INVALID;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal failure while acquireing parameters");
    return KATCP_RESULT_FAIL;
  }

  a = find_arb_katcp(d, name);
  if(a == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate %s", name);
    return KATCP_RESULT_FAIL;
  }

  gs = data_arb_katcp(d, a);
  if(gs == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no user state found for %s", name);
    return KATCP_RESULT_FAIL;
  }

#if 0
  unlink_arb_katcp(d, a);
  discharge_timer_katcp(d, gs);
#endif

  destroy_getap(d, gs);

  return KATCP_RESULT_OK;
}

int tap_info_cmd(struct katcp_dispatch *d, int argc)
{
  char *name;
  struct katcp_arb *a;
  struct getap_state *gs;
  unsigned int i;

  if(argc <= 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a register name");
    return KATCP_RESULT_INVALID;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal failure while acquireing parameters");
    return KATCP_RESULT_FAIL;
  }

  a = find_arb_katcp(d, name);
  if(a == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate %s", name);
    return KATCP_RESULT_FAIL;
  }

  gs = data_arb_katcp(d, a);
  if(gs == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no user state found for %s", name);
    return KATCP_RESULT_FAIL;
  }

  for(i = 1; i < 254; i++){
    if(memcmp(gs->s_arp_table[i], broadcast_const, 6)){
      log_message_katcp(gs->s_dispatch, KATCP_LEVEL_INFO, NULL, "peer %02x:%02x:%02x:%02x:%02x:%02x at %u", gs->s_arp_table[i][0], gs->s_arp_table[i][1], gs->s_arp_table[i][2], gs->s_arp_table[i][3], gs->s_arp_table[i][4], gs->s_arp_table[i][5], gs->s_arp_fresh[i]);
    }
  }

  log_message_katcp(gs->s_dispatch, KATCP_LEVEL_INFO, NULL, "current iteration %u", gs->s_iteration);
  log_message_katcp(gs->s_dispatch, KATCP_LEVEL_INFO, NULL, "buffers arp=%u/rx=%u/tx=%u", gs->s_arp_len, gs->s_rx_len, gs->s_tx_len);
  log_message_katcp(gs->s_dispatch, KATCP_LEVEL_INFO, NULL, "polling interval %u", gs->s_timer);
  log_message_katcp(gs->s_dispatch, KATCP_LEVEL_INFO, NULL, "address %s", gs->s_address_name);
  log_message_katcp(gs->s_dispatch, KATCP_LEVEL_INFO, NULL, "gateware port is %u", gs->s_port);
  log_message_katcp(gs->s_dispatch, KATCP_LEVEL_INFO, NULL, "gateware name %s", name);
  log_message_katcp(gs->s_dispatch, KATCP_LEVEL_INFO, NULL, "tap device name %s on fd %d", gs->s_tap_name, gs->s_tap_fd);

  return KATCP_RESULT_OK;
}
