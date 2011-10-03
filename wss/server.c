#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sysexits.h>
#include <signal.h>

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
  
  c->c_fd    = fd;
  c->c_rb    = NULL;
  c->c_rb_len= 0;
  c->c_ssl   = ssl;
  c->c_state = C_STATE_NEW;
#if 0
  c->c_sb    = NULL;
  c->c_sb_len= 0;
#endif
  return c;
}

void destroy_client_ws(struct ws_client *c)
{
  if (c){
    if (c->c_rb)
      free(c->c_rb);
    if (c->c_ssl){
#ifdef DEBUG
      fprintf(stderr, "wss: SSL about to free client\n");
#endif
      SSL_free(c->c_ssl);
    }
#if 0
    if (c->c_sb)
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


int startup_server(struct ws_server *s, int port)
{
  struct sockaddr_in sa;
  int backlog, reuse_addr;

  if (s == NULL || port == 0)
    return -1;

  backlog      = 10;
  reuse_addr   = 1;
  
  s->s_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s->s_fd < 0){
#ifdef DEBUG
    fprintf(stderr,"wss: error socket\n");
#endif
    return -1;
  }

  memset(&sa, 0, sizeof(struct sockaddr_in));

  setsockopt(s->s_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

  sa.sin_family       = AF_INET;
  sa.sin_port         = htons(port);
  sa.sin_addr.s_addr  = INADDR_ANY;

  if (bind(s->s_fd, (const struct sockaddr *) &sa, sizeof(struct sockaddr_in)) < 0){
#ifdef DEBUG
    fprintf(stderr,"wss: error bind on port: %d\n", port);
#endif
    return -1;
  }

  if (listen(s->s_fd, backlog) < 0) {
#ifdef DEBUG
    fprintf(stderr,"wss: error listen failed\n");  
#endif
    return -1;
  }

  s->s_hi = s->s_fd;

#ifdef DEBUG
  fprintf(stderr,"wss: server pid: %d running on port: %d\n", getpid(), port);
#endif

  return 0;
}

void build_socket_set_ws(struct ws_server *s)
{
  int i, fd;

  if (s == NULL)
    return;

  FD_ZERO(&s->s_in);
  FD_SET(s->s_fd, &s->s_in);
  
  //FD_SET(STDIN_FILENO, &s->insocks);
  for (i=0; i<s->s_c_count; i++){
    if (s->s_c[i] != NULL){
      
      fd = s->s_c[i]->c_fd;

      if (fd > 0) {
        FD_SET(fd, &s->s_in);
        if (fd > s->s_hi)
          s->s_hi = fd;
      }
    }
  }
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
  if (c == NULL || buf == NULL)
    return -1;
 
  c->c_rb     = buf;
  c->c_rb_len = n;

#if 0
  c->c_rb = realloc(c->c_rb, sizeof(unsigned char*) * (c->c_rb_len + n + 1));
  if (c->c_rb == NULL)
    return -1;

  strncpy((char *)c->c_rb + c->c_rb_len, (char *) buf, n);

  c->c_rb_len += n;

  c->c_rb[c->c_rb_len] = '\0';
#endif

  return 0;
}

unsigned char *readline_client_ws(struct ws_client *c)
{
  unsigned char *start, *end;
  
  if (c == NULL || c->c_rb == NULL)
    return NULL;

  start = c->c_rb;
  end   = (unsigned char *) strstr((const char *)c->c_rb, HTTP_EOL);
  
  if (end != NULL){
    c->c_rb = end + 2;
    *end = '\0';
  } else {
    c->c_rb = NULL;
#ifdef DEBUG
    fprintf(stderr, "wss: readline found end of data\n");
#endif
  }
 
  return start;
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
  }
  else {
    
    if (populate_client_data_ws(c, readbuffer, recv_bytes) < 0){
#ifdef DEBUG
      fprintf(stderr, "wss: error couldn't populate client\n");
#endif
      return -1;
    }

    rtn = (*(s->s_cdfn))(c);
    
    if (rtn < 0)
      return -1;

#if 0
    if (s->clients[i]->conn_state > 0) {
      /*when the client sends data and it has built a connection*/
#ifdef DEBUG
      fprintf(stderr,"connection is upgraded! got data: (bytes:%d)\n", recv_bytes);
      for (j=0;j<recv_bytes;j++)
        fprintf(stderr,"byte: %d 0x%02X %c\n",j,readbuffer[j],readbuffer[j]);
#endif

    }
    else {
      /*the client has just connected and sent websocket http header*/
      //fprintf(stderr,"bytes read: %d\n",recv_bytes);
      s->clients[i]->conn_state = upgrade_connection(s, s->clients[i], (char *) readbuffer, recv_bytes);
      if (s->clients[i]->conn_state){
        s->up_ccount++;
      }
    }
#endif

  }

  return 0;
}

int read_socks_ws(struct ws_server *s) 
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
    return handle_new_client_ws(s);
  }

  /*check for data on the line from connected clients*/
  for (i=0; i<s->s_c_count; i++){
    c = s->s_c[i];
    if (c != NULL){
      if (FD_ISSET(c->c_fd, &s->s_in)) {
        return get_client_data_ws(s, c);
      }
    }
  }

#if 0
  /*check if there is data waiting to go to the clients*/
  for (i=0; i < s->conncount; i++) {
    if (FD_ISSET(s->clients[i]->fd, &s->outsocks)) {
      return send_to_client(s, i);
    }
  }

  /*check the server stdin file descriptor*/
  if (FD_ISSET(STDIN_FILENO, &s->insocks)) {
    return get_server_broadcast(STDIN_FILENO, s);
  }
  
#endif
  return -1;
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
      if (read_socks_ws(s) < 0) {
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

int register_client_handler_server(int (*client_data_fn)(struct ws_client *c), int port)
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
