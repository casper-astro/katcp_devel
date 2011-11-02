#ifndef _SERVER_H
#define _SERVER_h

#include <stdint.h>
#include <openssl/ssl.h>

#define TLS_CERT  "./certs/server.crt"
#define TLS_KEY   "./certs/server.key"

#define C_STATE_NEW       0
#define C_STATE_UPGRADED  1

#define WSF_FIN         0x80
#define WSF_OPCODE      0x0f
#define WSF_MASK        0x80
#define WSF_PAYLOAD     0x7f
#define WSF_PAYLOAD_16  0x7e
#define WSF_PAYLOAD_64  0x7f

struct ws_frame {
  uint8_t hdr[2];
  uint8_t msk[4];
  uint64_t payload;
};

struct ws_client {
  int c_fd;

#if 0
  unsigned char *c_rb;
#endif
  unsigned char *c_rb_last;
  void *c_rb;
  int c_rb_len;

  SSL *c_ssl;
  int c_state;

  void *c_sb;
  int c_sb_len;

  struct ws_frame *c_frame;
};

struct ws_server {
  int s_fd; 
  
  fd_set s_in;
  fd_set s_out;
  
  int s_hi;
  
  struct ws_client **s_c;
  int s_c_count;

  unsigned long long s_br_count;

  int (*s_cdfn)(struct ws_client *c);
  
  SSL_CTX *s_tlsctx;

#if 0
  int s_up_count;

  uint8_t *s_sb;
  int s_sb_len;
#endif
};


int register_client_handler_server(int (*client_data_fn)(struct ws_client *c), char *port);

unsigned char *readline_client_ws(struct ws_client *c);
int readdata_client_ws(struct ws_client *c, void *dest, unsigned int n);
void dropdata_client_ws(struct ws_client *c);

int write_to_client_ws(struct ws_client *c, void *buf, int n);

int upgrade_client_ws(struct ws_client *c);

#endif
