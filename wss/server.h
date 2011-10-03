#ifndef _SERVER_H
#define _SERVER_h

#include <openssl/ssl.h>

#define TLS_CERT  "./certs/server.crt"
#define TLS_KEY   "./certs/server.key"

#define C_STATE_NEW       0
#define C_STATE_UPGRADED  1


struct ws_client {
  int c_fd;

  unsigned char *c_rb;
  int c_rb_len;

  SSL *c_ssl;
  int c_state;
#if 0
  uint8_t *c_sb;
  int c_sb_len;
#endif
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


int register_client_handler_server(int (*client_data_fn)(struct ws_client *c), int port);

unsigned char *readline_client_ws(struct ws_client *c);
#endif
