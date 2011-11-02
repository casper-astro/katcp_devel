#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sysexits.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/conf.h>
#include <openssl/engine.h>

#include "server.h"

#define HTTP_EOL             "\r\n"
#define READBUFFERSIZE        4096

static volatile int run = 1;

void ws_handle(int signum) 
{
  run = 0;
}
#if 0
struct ws_frame *crete_ws_frame(uint8_t hdr[], uint8_t msk[], uint64_t payload)
{
  struct ws_frame *f;

  if (hdr == NULL || 
}
#endif
struct ws_server *create_server_ws(int (*cdfn)(struct ws_client *c))
{
  struct ws_server *s;

  if (cdfn == NULL)
    return NULL;
  
  s = malloc(sizeof(struct ws_server));
  if (s == NULL)
    return NULL;
  
  s->s_fd = 0;

  FD_ZERO(&s->s_in);
  FD_ZERO(&s->s_out);

  s->s_hi      = 0;
  s->s_c       = NULL;
  s->s_c_count = 0;
  s->s_br_count= 0;
  s->s_cdfn    = cdfn;
  s->s_tlsctx  = NULL;
#if 0
  s->s_up_count= 0;
  s->s_sb      = NULL;
  s->s_sb_len  = 0;
#endif
  return s;
}

struct ws_client *create_client_ws(int fd, SSL *ssl)
{
  struct ws_client *c;
  
  c = malloc(sizeof(struct ws_client));
  if (c == NULL)
    return NULL;
  
  c->c_fd       = fd;
  c->c_rb       = NULL;
  c->c_rb_len   = 0;
  c->c_rb_last  = NULL;
  c->c_ssl      = ssl;
  c->c_state    = C_STATE_NEW;
  c->c_sb       = NULL;
  c->c_sb_len   = 0;
  c->c_frame    = NULL;

  return c;
}

void destroy_client_ws(struct ws_client *c)
{
  if (c){
#if 1
    if (c->c_rb)
      free(c->c_rb);
    if (c->c_rb_last != NULL){
      free(c->c_rb_last);
      c->c_rb_last = NULL;
    }
#endif
    if (c->c_ssl){
#ifdef DEBUG
      fprintf(stderr, "wss: SSL about to free client\n");
#endif
      SSL_free(c->c_ssl);
    }
#if 1
    if (c->c_sb != NULL)
      free(c->c_sb);
#endif

    free(c);
  }
}

void destroy_server_ws(struct ws_server *s)
{
  int i;
  if (s){
#ifdef DEBUG
    fprintf(stderr, "wss: total bytes received: %llu\n", s->s_br_count);
#endif
    if (s->s_c){
      for (i=0; i<s->s_c_count; i++){
        destroy_client_ws(s->s_c[i]);
      }
      free(s->s_c);
    }
#if 0
    if (s->s_sb)
      free(s->s_sb);
#endif
    free(s);
  }
}

int add_new_client_ws(struct ws_server *s, struct ws_client *c)
{
  if (s == NULL || c == NULL)
    return -1;

  s->s_c = realloc(s->s_c, sizeof(struct ws_client*) * (s->s_c_count+1));
  if (s->s_c == NULL)
    return -1;

  s->s_c[s->s_c_count] = c;
  s->s_c_count++;

  return 0;
}

int del_client_ws(struct ws_server *s, struct ws_client *c)
{
  int i;

  if (s == NULL || c == NULL)
    return -1;
  
  for (i=0; i<s->s_c_count; i++){
    if (s->s_c[i] == c)
      break;
  }

  s->s_c[i] = s->s_c[s->s_c_count - 1];

  s->s_c = realloc(s->s_c, sizeof(struct ws_client*) * (s->s_c_count - 1));
  s->s_c_count--;
  
  return 0;
}

int register_signals_ws()
{
  struct sigaction sa;
  sigset_t sigmask;
  int err;
  
  err           = 0;
  sa.sa_flags   = SA_RESTART;
  sa.sa_handler = ws_handle;
  
  sigemptyset(&sa.sa_mask);
  err += sigaction(SIGINT, &sa, NULL);
  err += sigaction(SIGTERM, &sa, NULL);
  
  sa.sa_handler = SIG_IGN;

  err += sigaction(SIGPIPE, &sa, NULL);

  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGINT);
  sigaddset(&sigmask, SIGTERM);
  err += sigprocmask(SIG_BLOCK, &sigmask, NULL);

  if (err < 0)
    return -1;

  return 0;
}


int startup_server(struct ws_server *s, char *port)
{
  struct addrinfo hints;
  struct addrinfo *res, *rp;
  int backlog, reuse_addr;

  if (s == NULL || port == NULL)
    return -1;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family     = AF_UNSPEC;
  hints.ai_socktype   = SOCK_STREAM;
  hints.ai_flags      = AI_PASSIVE;
  
  if ((reuse_addr = getaddrinfo(NULL, port, &hints, &res)) != 0) {
#ifdef DEBUG
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(reuse_addr));
#endif
    return -1;
  }

  for (rp = res; rp != NULL; rp = rp->ai_next) {
    if (rp->ai_family == AF_INET6)
      break;
  }

  rp = (rp == NULL) ? res : rp;

  s->s_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
  if (s->s_fd < 0){
#ifdef DEBUG
    fprintf(stderr,"wss: error socket\n");
#endif
    freeaddrinfo(res);
    return -1;
  }

  reuse_addr   = 1;

  setsockopt(s->s_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

  if (bind(s->s_fd, rp->ai_addr, rp->ai_addrlen) < 0){
#ifdef DEBUG
    fprintf(stderr,"wss: error bind on port: %s\n", port);
#endif
    freeaddrinfo(res);
    return -1;
  }

  freeaddrinfo(res);

  backlog      = 10;
#if 0
  memset(&sa, 0, sizeof(struct sockaddr_in));
  sa.sin_family       = AF_INET;
  sa.sin_port         = htons(port);
  sa.sin_addr.s_addr  = INADDR_ANY;
#endif

  if (listen(s->s_fd, backlog) < 0) {
#ifdef DEBUG
    fprintf(stderr,"wss: error listen failed\n");  
#endif
    return -1;
  }

  s->s_hi = s->s_fd;

#ifdef DEBUG
  fprintf(stderr,"wss: server pid: %d running on port: %s\n", getpid(), port);
#endif

  return 0;
}

int handle_new_client_ws(struct ws_server *s)
{
  struct sockaddr_in ca;
  socklen_t len;
  struct ws_client *c;
  int cfd;
  SSL *ssl;

  if (s == NULL)
    return -1;

  len = sizeof(struct sockaddr_in);

  cfd = accept(s->s_fd, (struct sockaddr *) &ca, &len);
  if (cfd < 0) { 
#ifdef DEBUG
    fprintf(stderr,"wss: error in accept new client: %s\n", strerror(errno)); 
#endif
    return -1; 
  }
  
  ssl = SSL_new(s->s_tlsctx);
  if (ssl == NULL){ 
#ifdef DEBUG
    fprintf(stderr, "wss: SSL error: %s\n", ERR_error_string(ERR_get_error(), NULL));
#endif
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "wss: client fd: %d ssl (%p)\n",cfd ,ssl); 
#endif

  SSL_set_fd(ssl, cfd);

  SSL_accept(ssl);

  c = create_client_ws(cfd, ssl);
  if (c == NULL)
    return -1;

  if (add_new_client_ws(s, c) < 0){
    destroy_client_ws(c);
    return -1;
  }

  return 0;  
}

int disconnect_client_ws(struct ws_server *s, struct ws_client *c)
{
  if (s == NULL || c == NULL){
#ifdef DEBUG
    fprintf(stderr, "wss: error disconnect client s:(%p) c:(%p)\n", s, c);
#endif
    return -1;
  }

  switch(c->c_state){
    case C_STATE_UPGRADED:
      
#ifdef DEBUG
      fprintf(stderr, "wss: client is upgraded should send shutdown frame\n");
#endif

      break;
    
    case C_STATE_NEW:
    default:
      break; 
  }

  if (del_client_ws(s, c) < 0){
#ifdef DEBUG
    fprintf(stderr, "wss: error del_client_ws\n");
#endif
    //return -1;
  }


  if (!SSL_shutdown(c->c_ssl)){
#ifdef DEBUG
    //fprintf(stderr, "wss: SSL error: %s\n", ERR_error_string(ERR_get_error(), NULL));
    fprintf(stderr, "wss: sll not shutdown do again\n");
#endif
    shutdown(c->c_fd, SHUT_RDWR);
    SSL_shutdown(c->c_ssl);
  }
#if 0
  if (shutdown(c->c_fd, SHUT_RDWR) < 0){
#ifdef DEBUG
    fprintf(stderr, "wss: error client shutdown: %s\n", strerror(errno));
#endif
  }
#endif

  if (close(c->c_fd) < 0){
#ifdef DEBUG
    fprintf(stderr, "wss: error shutdown: %s\n", strerror(errno));
#endif
  }

  destroy_client_ws(c);

#ifdef DEBUG
  fprintf(stderr,"wss: client has been disconnected\n");
#endif

  return 0;
}

#if 0
void SSL_free_comp_methods(void)
{
  if (ssl_comp_methods == NULL)
    return;
  CRYPTO_w_lock(CRYPTO_LOCK_SSL);
  if (ssl_comp_methods != NULL)
  {
    sk_SSL_COMP_pop_free(ssl_comp_methods,CRYPTO_free);
    ssl_comp_methods = NULL;
  }
  CRYPTO_w_unlock(CRYPTO_LOCK_SSL);
}
#endif

void shutdown_server_ws(struct ws_server *s)
{
  struct ws_client *c;

  if (s){
    while (s->s_c_count > 0){
      c = s->s_c[0];
      if (disconnect_client_ws(s, c) < 0){
#ifdef DEBUG
        fprintf(stderr, "wss: error server disconnect client\n");
#endif
      }
    }
    
    if (s->s_tlsctx){
      SSL_CTX_free(s->s_tlsctx);
    }

    if (shutdown(s->s_fd, SHUT_RDWR) < 0){
#ifdef DEBUG
      fprintf(stderr, "wss: error server shutdown: %s\n", strerror(errno));
#endif
    }

    if (close(s->s_fd) < 0){
#ifdef DEBUG
      fprintf(stderr, "wss: error server shutdown: %s\n", strerror(errno));
#endif
    }
    destroy_server_ws(s);
  }
  
  COMP_zlib_cleanup();
  ERR_remove_state(0);
  ENGINE_cleanup();
  CONF_modules_unload(1);
  ERR_free_strings();
  EVP_cleanup();
  CRYPTO_cleanup_all_ex_data();
#if 0
  SSL_free_comp_methods();
#endif

#ifdef DEBUG
  fprintf(stderr, "wss: server shutdown complete\n");
#endif
} 

int populate_client_data_ws(struct ws_client *c, unsigned char *buf, int n)
{
  uint32_t size; 

  if (c == NULL || buf == NULL)
    return -1;

  size = n * sizeof(unsigned char);
  
#ifdef DEBUG
  fprintf(stderr, "wss: current rb_len: %d size of data: %d\n", c->c_rb_len, size);
#endif

#if 0
  c->c_rb     = buf;
  c->c_rb_len = n;
#endif

  c->c_rb = realloc(c->c_rb, c->c_rb_len + size + 1);

#if 0 
  def DEBUG 
  fprintf(stderr, "wss: c->c_rb: (%p) sizeof: %d\n", c->c_rb, sizeof(c->c_rb));
#endif

  if (c->c_rb == NULL)
    return -1;

  memcpy(c->c_rb + c->c_rb_len, buf, size);

  c->c_rb_len += size;

  memset(c->c_rb + c->c_rb_len, '\0', sizeof(char));

#if 0 
  def DEBUG
  fprintf(stderr, "wss: len: %d\n%s\n", c->c_rb_len, (char *)c->c_rb);
#endif

  return 0;
}

unsigned char *readline_client_ws(struct ws_client *c)
{
  char *temp;
  void *end;

  if (c == NULL || c->c_rb == NULL)
    return NULL;

  temp = NULL;
  end = NULL;

  if (c->c_rb_last != NULL){
    free(c->c_rb_last);
    c->c_rb_last = NULL;
  }

#if 0
  c->c_rb_last  = (unsigned char *) c->c_rb;
  end           = (unsigned char *) strstr((const char *)c->c_rb, HTTP_EOL);
#endif

#if 0
def DEBUG
  fprintf(stderr, "wss: rb: (%p) rb_len: %d strlen(http): %d\n", c->c_rb, c->c_rb_len, strlen(HTTP_EOL));
#endif

  end = memmem(c->c_rb, c->c_rb_len, HTTP_EOL, strlen(HTTP_EOL));

  //c->c_rb_len  -= end - (void *)c->c_rb_last;

  if (end != NULL){
    
    memset(end, '\0', sizeof(unsigned char)*strlen(HTTP_EOL));

    if (asprintf(&temp, "%s", (unsigned char*)c->c_rb) < 0){
#ifdef DEBUG
      fprintf(stderr, "wss: error asprintf\n");
#endif
      return NULL;
    }

    end += sizeof(unsigned char) * strlen(HTTP_EOL);
    
    c->c_rb_len = c->c_rb_len - (end - c->c_rb);
    c->c_rb     = memmove(c->c_rb, end, c->c_rb_len);
  
    c->c_rb_last = (unsigned char*) temp;

  } else {
#ifdef DEBUG
    fprintf(stderr, "wss: readline end is NULL rb_len:%d\n", c->c_rb_len);
#endif
    
    c->c_rb = realloc(c->c_rb, c->c_rb_len + 1);
    if (c->c_rb == NULL)
      return NULL;

    memset(c->c_rb + c->c_rb_len, '\0', sizeof(unsigned char));

    //c->c_rb_len++;

    c->c_rb_last = (unsigned char *) c->c_rb;

    c->c_rb = NULL;
    c->c_rb_len = 0;

  }
 
  return c->c_rb_last;
}

int readdata_client_ws(struct ws_client *c, void *dest, unsigned int n)
{
  if (c == NULL || n <= 0 || c->c_rb_len <= 0)
    return -1;

  if (n > c->c_rb_len){
#ifdef DEBUG
    fprintf(stderr, "wss: readdata trying to read more than %d\n", c->c_rb_len);
#endif
    n = c->c_rb_len;
  }

#ifdef DEBUG
  fprintf(stderr, "wss: readdata [%d]bytes\n", n);
#endif
#if 0 
  c->c_rb_last = realloc(c->c_rb_last, n);
  if (c->c_rb_last == NULL)
    return NULL;
#endif
  memcpy(dest, c->c_rb, n);

  memmove(c->c_rb, c->c_rb + n, c->c_rb_len - n);

  c->c_rb_len -= n;

  c->c_rb = realloc(c->c_rb, c->c_rb_len);
  
  return n;
}

void dropdata_client_ws(struct ws_client *c)
{
  if (c == NULL)
    return;

  if (c->c_rb != NULL){
    free(c->c_rb);
    c->c_rb_len = 0;
    c->c_rb = NULL;
  }
  
  if (c->c_rb_last != NULL){
    free(c->c_rb_last);
    c->c_rb_last = NULL;
  }
}

int get_client_data_ws(struct ws_server *s, struct ws_client *c)
{
  unsigned char readbuffer[READBUFFERSIZE];
  int recv_bytes, rtn;

  if (s == NULL || c == NULL)
    return -1;

  memset(readbuffer, 0, READBUFFERSIZE);
#if 0
  recv_bytes = read(c->c_fd, readbuffer, READBUFFERSIZE);
#endif

  recv_bytes = SSL_read(c->c_ssl, readbuffer, READBUFFERSIZE);

  s->s_br_count += recv_bytes;
  
  if (recv_bytes < 0){
#if 0
    def DEBUG
    fprintf(stderr, "wss: read error %s\n", strerror(errno)); 
#endif
#ifdef DEBUG
    fprintf(stderr, "wss: read_error SSL error: %s\n", ERR_error_string(ERR_get_error(), NULL));
#endif

    return disconnect_client_ws(s, c);
  } else if (recv_bytes == 0){
#ifdef DEBUG
    fprintf(stderr,"wss: client is leaving\n");
#endif
    return disconnect_client_ws(s, c);
  } else {
    
    if (populate_client_data_ws(c, readbuffer, recv_bytes) < 0){
#ifdef DEBUG
      fprintf(stderr, "wss: error couldn't populate client\n");
#endif
      return -1;
    }

    rtn = (*(s->s_cdfn))(c);
    
    if (rtn < 0)
      return -1;

  }

  return 0;
}

int send_client_data_ws(struct ws_server *s, struct ws_client *c)
{
  int b_wrote;

  if (s == NULL || c == NULL)
    return -1;
  
  b_wrote = SSL_write(c->c_ssl, c->c_sb, c->c_sb_len);

#ifdef DEBUG
  fprintf(stderr, "wss: [%d] b_wrote:%d c_sb_len:%d\n", c->c_fd, b_wrote, c->c_sb_len);
#endif

  if (b_wrote <= 0){
#ifdef DEBUG
    fprintf(stderr, "wss: error ssl_write < 0\n");
#endif
    return -1;
  } else if (b_wrote == c->c_sb_len){

    c->c_sb_len = 0;

  } else if (b_wrote < c->c_sb_len) {
    c->c_sb_len -= b_wrote;
    
    memmove(c->c_sb, c->c_sb + b_wrote, c->c_sb_len);

    c->c_sb = realloc(c->c_sb, c->c_sb_len);
    if (c->c_sb == NULL)
      return -1;
  }
  
  return 0;
}


int socks_io_ws(struct ws_server *s) 
{
  struct ws_client *c;
  int i;
  
  if (s == NULL)
    return -1;

  /*check the server listen fd for a new connection*/
  if (FD_ISSET(s->s_fd, &s->s_in)) {
#ifdef DEBUG
    fprintf(stderr,"wss: new incomming connection\n");
#endif
    if (handle_new_client_ws(s) < 0){
#ifdef DEBUG
      fprintf(stderr,"wss: error handle new clientn\n");
#endif
    }
  }

  for (i=0; i<s->s_c_count; i++){
    c = s->s_c[i];
    if (c != NULL){

      if (FD_ISSET(c->c_fd, &s->s_out)){
        if (send_client_data_ws(s, c) < 0){
#ifdef DEBUG
          fprintf(stderr, "wss: send client data error\n");
#endif
        }
      }

      if (FD_ISSET(c->c_fd, &s->s_in)) {
        if (get_client_data_ws(s, c) < 0){
#ifdef DEBUG
          fprintf(stderr, "wss: get client data error\n");
#endif
        }
      } 
        
    }
  }

  return 0;
}

void build_socket_set_ws(struct ws_server *s)
{
  struct ws_client *c;
  int i, fd;

  if (s == NULL)
    return;

  FD_ZERO(&s->s_in);
  FD_ZERO(&s->s_out);
  FD_SET(s->s_fd, &s->s_in);
  
  //FD_SET(STDIN_FILENO, &s->insocks);
  for (i=0; i<s->s_c_count; i++){
    
    c = s->s_c[i];

    if (c != NULL){
      
      fd = c->c_fd;
      
      if (fd > 0) {
        FD_SET(fd, &s->s_in);

        if (fd > s->s_hi)
          s->s_hi = fd;
        
        if (c->c_sb_len > 0){
#ifdef DEBUG
          fprintf(stderr, "wss: must send to [%d] %dbytes\n", c->c_fd, c->c_sb_len);
#endif
          FD_SET(fd, &s->s_out);
        }

      }
    }
  }
}

int run_loop_ws(struct ws_server *s)
{
  sigset_t empty_mask;

  sigemptyset(&empty_mask);

  while (run) {

    build_socket_set_ws(s);  

    if (pselect(s->s_hi + 1, &s->s_in, &s->s_out, (fd_set *) NULL, NULL, &empty_mask) < 0) { 
      switch(errno){
        case EPIPE:
#ifdef DEBUG
          fprintf(stderr, "wss: EPIPE: %s\n", strerror(errno));
#endif
        case EINTR:
        case EAGAIN:
          break;
        default:
#ifdef DEBUG
          fprintf(stderr,"wss: select encountered an error: %s\n", strerror(errno)); 
#endif
          return -1;
      }
    }
    else {
      if (socks_io_ws(s) < 0) {
        //return -1; 
#ifdef DEBUG
        fprintf(stderr, "wss: error in read_socks_ws\n");
#endif
      }
    }

  }

  return 0;
}

int setup_tls_ws(struct ws_server *s)
{
  SSL_CTX *tlsctx;

  if (s == NULL)
    return -1;
  
  SSL_library_init();
  SSL_load_error_strings();
/*
  if (ERR_peek_error() != 0){
#ifdef DEBUG
    fprintf(stderr, "wss: SSL error: %s\n", ERR_error_string(ERR_get_error(), NULL));
#endif
    return -1;
  }
*/

  tlsctx = SSL_CTX_new(TLSv1_server_method());
  if (tlsctx == NULL){
#ifdef DEBUG
    fprintf(stderr, "wss: SSL error: %s\n", ERR_error_string(ERR_get_error(), NULL));
#endif
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "wss: tlsctx (%p)\n", tlsctx);
#endif

  SSL_CTX_set_options(tlsctx, SSL_OP_SINGLE_DH_USE);
/*  if (ERR_peek_error() != 0){
#ifdef DEBUG
    fprintf(stderr, "wss: SSL error: %s\n", ERR_error_string(ERR_get_error(), NULL));
#endif
    return -1;
  }*/

  if (!SSL_CTX_use_certificate_file(tlsctx, TLS_CERT, SSL_FILETYPE_PEM)){
#ifdef DEBUG
    fprintf(stderr, "wss: SSL error: %s\n", ERR_error_string(ERR_get_error(), NULL));
#endif
    SSL_CTX_free(tlsctx);
    return -1;
  }

  if (!SSL_CTX_use_PrivateKey_file(tlsctx, TLS_KEY, SSL_FILETYPE_PEM)) {
#ifdef DEBUG
    fprintf(stderr, "wss: SSL error: %s\n", ERR_error_string(ERR_get_error(), NULL));
#endif
    SSL_CTX_free(tlsctx);
    return -1;
  }

  //SSL_CTX_set_session_cache_mode(tlsctx, SSL_SESS_CACHE_OFF);
  
  s->s_tlsctx = tlsctx;

  return 0;
}

int register_client_handler_server(int (*client_data_fn)(struct ws_client *c), char *port)
{
  struct ws_server *s;
  
  if (register_signals_ws() < 0){
#ifdef DEBUG
    fprintf(stderr, "wss: error register signals\n");
#endif
    return -1;
  }

  s = create_server_ws(client_data_fn);
  if (s == NULL){
#ifdef DEBUG
    fprintf(stderr, "wss: error could not create server\n");
#endif
    return -1;
  }

  if (setup_tls_ws(s) < 0){
#ifdef DEBUG
    fprintf(stderr,"wss: error in tls setup\n");
#endif
    shutdown_server_ws(s);
    return -1;
  }

  if (startup_server(s, port) < 0){
#ifdef DEBUG
    fprintf(stderr,"wss: error in startup\n");
#endif
    shutdown_server_ws(s);
    return -1;
  }

  if (run_loop_ws(s) < 0){ 
#ifdef DEBUG
    fprintf(stderr,"wss: error during run\n");
#endif
    shutdown_server_ws(s);
    return -1;
  }
  
  shutdown_server_ws(s);

#ifdef DEBUG
  fprintf(stderr,"wss: server exiting\n");
#endif

  return 0;
}

int write_to_client_ws(struct ws_client *c, void *buf, int n)
{
  if (c == NULL || buf == NULL)
    return -1;
  
  c->c_sb = realloc(c->c_sb, c->c_sb_len + n);
  if (c->c_sb == NULL)
    return -1;

  memcpy(c->c_sb + c->c_sb_len, buf, n);

  c->c_sb_len += n;

  return n;
}

int upgrade_client_ws(struct ws_client *c)
{
  if (c == NULL)
    return -1;
  
  c->c_state = C_STATE_UPGRADED;

  return 0;
}

