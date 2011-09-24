/***
 *  CWSS - A C WevSocket Server
 *
 *    Writen by: A Barta 
 *
 *    use this to connect a browser websocket client 
 *    stream stdin via JSON to the client
 *
 * */

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

#include <time.h>
#include <sys/stat.h>

#include <openssl/evp.h>

/**
 * Client Object
 *  conn_state - connection state
 *    0: connected not upgraded
 *    1: connected and upgraded to websocket status
 *  fd - client file descriptor 
 *  data - char string of websocket connection header
 */
struct client {
  int fd;
  int conn_state;
  char * data;
  int data_len;

  uint8_t *send_buffer;
  int sb_len;
};

/**
 * Server Object
 *  fd - server listen socket
 *  insocks - sockets ready for reading
 *  outsocks - sockets ready for writing
 *  highsock - largest fd number for select
 *  conncount - count of client connections
 *  clients - array of client objects
 */
struct server {
  int fd; 
  
  fd_set insocks;
  fd_set outsocks;
  
  int highsock;
  
  int conncount;
  struct client **clients;
  int up_ccount;

  uint8_t *data;
  int data_len;
};

/*Main select loop variable*/
static volatile int run = 1;

/**
 * Signal handler callback
 */
void handle(int signum) 
{
  run = 0;
}

/**
 *  setup_server function
 *
 *    Create the listening server on port
 */
int setup_server(struct server *s, int port)
{
  struct sockaddr_in sa;
  int backlog, reuse_addr;
  
  reuse_addr   = 1;
  backlog      = 10;
  s->conncount = 0;
  s->clients   = NULL;
  s->data      = NULL;
  s->data_len  = 0;
  s->up_ccount = 0;

  s->fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s->fd < 0){
#ifdef DEBUG
    fprintf(stderr,"Cannot Create Socket\n");
#endif
    return -1;
  }

  memset(&sa, 0, sizeof(struct sockaddr_in));

  /*fix time wait problems*/
  setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

  sa.sin_family       = AF_INET;
  sa.sin_port         = htons(port);
  sa.sin_addr.s_addr  = INADDR_ANY;

  if (bind(s->fd, (const struct sockaddr *) &sa, sizeof(struct sockaddr_in)) < 0){
#ifdef DEBUG
    fprintf(stderr,"Could not Bind on Port: %d\n", port);
#endif
    return -1;
  }

  if (listen(s->fd, backlog) < 0) {
#ifdef DEBUG
    fprintf(stderr,"Error Listen Failed\n");  
#endif
    return -1;
  }

  s->highsock = s->fd;

#ifdef DEBUG
  fprintf(stderr,"Server PID: %d running on port: %d\n", getpid(), port);
#endif

  return 0;
}

/**
 *  build_socket_set function
 *    
 *     First function called during the run loop
 *     builds the file descriptor set and resets
 *     the highsock value to the highest used file 
 *     descriptor
 */
void build_socket_set(struct server *s)
{
  int i;

  if (s == NULL)
    return;

  FD_ZERO(&s->insocks);
  FD_SET(s->fd, &s->insocks);
  FD_SET(STDIN_FILENO, &s->insocks);
  
  for (i=0; i<s->conncount; i++){
    
    if (s->clients[i]->fd > 0) {

      FD_SET(s->clients[i]->fd, &s->insocks);
      
      if (s->clients[i]->fd > s->highsock)
        s->highsock = s->clients[i]->fd;

    }

  }

}
  
/**
 *  handle_new_client function
 *
 *    called when a new client connects to the server
 *    init connect state of the client to 0
 */
int handle_new_client(struct server *s)
{
  struct sockaddr_in ca;
  socklen_t len;
  struct client *c;
  int cfd, i;

  if (s == NULL)
    return -1;

  len = sizeof(struct sockaddr_in);

  cfd = accept(s->fd, (struct sockaddr *) &ca, &len);
  if (cfd < 0) { 
#ifdef DEBUG
    fprintf(stderr,"Error while trying to accept new client: %s\n",strerror(errno)); 
#endif
    return -1; 
  }

  
  c = malloc(sizeof(struct client));
  if (c == NULL)
    return -1;
  
  //fprintf(stderr,"Created new Client Struct: %p\n",c);

  c->fd          = cfd;
  c->conn_state  = 0;
  c->data        = NULL;
  c->data_len    = 0;
  c->send_buffer = NULL;
  c->sb_len      = 0;
  
  s->clients = realloc(s->clients, sizeof(struct client*) * (s->conncount+1));
  if (s->clients == NULL){
    free(c);
    return -1;
  }

  s->clients[s->conncount] = c;
  
  s->conncount++;

#ifdef DEBUG
  for (i=0; i<s->conncount; i++) {
    fprintf(stderr,"%d[fd:%d cs:%d] ", i, s->clients[i]->fd, s->clients[i]->conn_state);
  }
  fprintf(stderr,"\n");
#endif
  
  return 0;  
}

/**
 *  disconnect_client function
 *    
 *    used to cleanly disconnect one client and swap it
 *    and the last client to manage the memory correctly 
 */
void disconnect_client(struct server *s, int i)
{
  shutdown(s->clients[i]->fd, SHUT_RDWR);     
  close(s->clients[i]->fd);

  free(s->clients[i]->data);
  
  if (s->clients[i]->send_buffer != NULL)
    free(s->clients[i]->send_buffer);

  if (s->clients[i]->conn_state)
    s->up_ccount--;
#ifdef DEBUG
  fprintf(stderr,"fd: %d client has been disconnected\n",s->clients[i]->fd);
#endif
  free(s->clients[i]);
  
  s->clients[i] = s->clients[s->conncount-1];
  s->conncount--;
  
  s->clients = realloc(s->clients,sizeof(struct client*) * s->conncount);
}

int send_403_err(struct server *s, struct client *c)
{
  char content[200];
  char serv_resp[1000];

  memset(serv_resp,'\0',1000);
  memset(content,'\0',200);

  sprintf(content,"<html><head><title>403</title></head><body style=\"background-color:#313131;\"><span style=\"color:white;font-size:20px;\">403 Forbidden err..</span></body></html>");
  sprintf(serv_resp,"HTTP/1.1 403 Forbidden\r\nContent-type: text/html\r\nContent-length: %d\r\nConnection: Close\r\n\r\n%s",(int)strlen(content),content);
  
  c->sb_len = strlen(serv_resp);
  c->send_buffer = realloc(c->send_buffer,(c->sb_len+1)*sizeof(char));
  strncpy((char*)c->send_buffer,serv_resp,c->sb_len);
  
  c->send_buffer[c->sb_len] = '\0';
  
  FD_SET(c->fd,&s->outsocks);
  
 // fprintf(stderr,"server sending to CFD:%d stuff: %s",c->fd,c->send_buffer);

  return 0;
}

int send_home_page(struct server *s, struct client *c, FILE *file, char *doctype){

  char serv_resp[1000];
  struct stat stats;
  struct tm *mt;
  char time_buf[100];

  memset(serv_resp,'\0',1000);

  fseek(file,0,SEEK_END);
  c->sb_len = ftell(file);
  fseek(file,0,SEEK_SET);

  if (fstat(fileno(file),&stats) != 0)
    return -1;

  mt = localtime(&stats.st_mtime);

  strftime(time_buf,sizeof(time_buf),"%a %Y-%m-%d %H:%M:%S %Z",mt);

  sprintf(serv_resp,"HTTP/1.1 200 OK\r\nContent-type: %s\r\nContent-length: %d\r\nConnection: Close\r\nLast-Modified: %s\r\n\r\n",doctype,c->sb_len,time_buf);
  c->send_buffer = realloc(c->send_buffer, sizeof(char)*(c->sb_len + strlen(serv_resp)));

  if (!c->send_buffer){
#ifdef DEBUG
    fprintf(stderr,"cannot malloc\n");
#endif
    return -1;
  }
  strncpy((char*)c->send_buffer,serv_resp,strlen(serv_resp));
  fread(c->send_buffer + strlen(serv_resp),c->sb_len,1,file);
  fclose(file);

  c->sb_len += strlen(serv_resp);

  FD_SET(c->fd,&s->outsocks);
  
  //lfprintf(stderr,"server sending to CFD:%d stuff: %s",c->fd,c->send_buffer);

  return 0;
}

/**
*   upgrade_to_websocket
*     
*     this function will upgrade the connection to websocket
*     algo:  
*       grab numbers from Sec-WebSocket-Key1, concat and divide by
*       number of spaces = 32bit number
*       grab numbers from Sec-WebSocket-Key2, concat and divide by
*       number of spaces = 32bit number
*       use these numbers to handshake with server
*       
*/
int upgrade_to_websocket(struct server *s, struct client *c){

  char *key1, *key2;
  unsigned char *key3;  
  char dkey1[100];
  char dkey2[100];
  int sc_k1, sc_k2,i;

  struct servkey {
    uint32_t key1;
    uint32_t key2;
    uint8_t key3[8];
  } __attribute__ ((packed)) sk;

  sc_k1=0;
  sc_k2=0;

  key1 = strstr(c->data,"Sec-WebSocket-Key1: ");
  key2 = strstr(c->data,"Sec-WebSocket-Key2: ");

  key1 += strlen("Sec-WebSocket-Key1: ");
  key2 += strlen("Sec-WebSocket-Key2: ");

  i=0;
  do {
    //fprintf(stderr,"processing: %c 0x%X\n",*key1,*key1);
    if(*key1 >= 0x30 && *key1 <= 0x39){
      dkey1[i] = *key1;
      i++;
    }else if (*key1 == 0x20){
      sc_k1++;
    }
    key1++;
  }while (*key1 != '\r' && *(key1+1) != '\n');
  dkey1[i]='\0';
  //fprintf(stderr,"\n");
  i=0;
  do {
    //fprintf(stderr,"processing: %c 0x%X\n",*key2,*key2);
    if(*key2 >= 0x30 && *key2 <= 0x39){
      dkey2[i] = *key2;
      i++;
    }else if (*key2 == 0x20){
      sc_k2++;
    }
    key2++;
  }while (*key2 != '\r' && *(key2+1) != '\n');
  dkey2[i]='\0';
  
  //fprintf(stderr,"DECODED: %s nos: %d\n",dkey1,sc_k1);
  //fprintf(stderr,"DECODED: %s nos: %d\n",dkey2,sc_k2);

  sk.key1 = htonl(atoll(dkey1) / sc_k1);
  sk.key2 = htonl(atoll(dkey2) / sc_k2);
  
  //fprintf(stderr,"ints: 0x%X 0x%X\n",sk.key1,sk.key2);

  key3 = (unsigned char *)strstr(c->data,"\r\n\r\n");
  key3 += strlen("\r\n\r\n");

  for(i=0;i<8;i++){
    sk.key3[i]= *(key3+i);
    //fprintf(stderr,"KEY3: 0x%X saved: 0x%X\n",*(key3+i),sk.key3[i]);
  }

  //for (i=0;i<16;i++){
    //fprintf(stderr,"SK byte %d: %X\n",i, *((uint8_t *)&sk+i));
  //}
  
  //fprintf(stderr,"128bit srv string: %d\n",(int)sizeof(sk));
  
  EVP_MD_CTX mdctx;
  unsigned char md_value[EVP_MAX_MD_SIZE];
  unsigned int md_len;

  EVP_DigestInit(&mdctx, EVP_md5());
  EVP_DigestUpdate(&mdctx, (void *) &sk, sizeof(sk));
  EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
  EVP_MD_CTX_cleanup(&mdctx);

  for(i=0;i<md_len;i++){
#ifdef DEBUG
    fprintf(stderr,"%d: %02x\n",i,md_value[i]);
#endif
  }

  char serv_resp[1000] = "HTTP/1.1 101 WebSocket Protocol Handshake\r\nUpgrade: WebSocket\r\nConnection: Upgrade\r\nSec-WebSocket-Origin: http://localhost:6969\r\nSec-WebSocket-Location: ws://localhost:6969/\r\n\r\n";
  
  strncat(serv_resp,(char*)md_value,md_len);

  c->sb_len = strlen(serv_resp);
  c->send_buffer = realloc(c->send_buffer,(c->sb_len)*sizeof(char));
  strncpy((char*)c->send_buffer,serv_resp,c->sb_len);
  
  //c->send_buffer[c->sb_len-1] = 0x00FF;
  //for (i=0; i<c->sb_len; i++)
  //  fprintf(stderr,"sending byte: %d 0x%02X %c\n",i,c->send_buffer[i],c->send_buffer[i]);

  FD_SET(c->fd,&s->outsocks);
  
  //fprintf(stderr,"server sending to CFD:%d stuff: %s",c->fd,c->send_buffer);
  
  return 1;
}

int parse_http_header(struct server *s, struct client *c){

  FILE *file;

  if (strstr(c->data,"User-Agent:") != NULL){
    //fprintf(stderr,"found user agent\n"); 
    
    if (strstr(c->data,"GET / HTTP/1.1") != NULL){
      file = fopen("./html5/websock.htm","r");
      if (file == NULL)
        return send_403_err(s,c); 
      else
        return send_home_page(s,c,file,"text/html");
    } else if (strstr(c->data,"GET /jquery.flot.js HTTP/1.1") != NULL){
      file = fopen("./html5/jquery.flot.js","r");
      if (file == NULL)
        return send_403_err(s,c); 
      else
        return send_home_page(s,c,file,"text/javascript");
    } else if (strstr(c->data,"GET /jquery.js HTTP/1.1") != NULL){
      file = fopen("./html5/jquery.js","r");
      if (file == NULL)
        return send_403_err(s,c); 
      else
        return send_home_page(s,c,file,"text/javascript");
    } else if (strstr(c->data,"GET /favicon.ico HTTP/1.1") != NULL){
      file = fopen("./html5/favicon.ico","r");
      if (file == NULL)
        return send_403_err(s,c); 
      else
        return send_home_page(s,c,file,"image/x-icon");
    } 
    else
      send_403_err(s,c);

  } else if(strstr(c->data,"Upgrade: WebSocket") != NULL){
#ifdef DEBUG
    fprintf(stderr,"found websocket upgrade\n");
#endif
    return upgrade_to_websocket(s,c); 
  } 

  return -1;
}

/**
 *  upgrade_connection
 *
 *    this function reads data from the client in order to store the 
 *    websocket header and upgrade the connection to connected
 *
 *    return conn_state
 */
#define READBUFFERSIZE 1000
#define CONN_UPGRADED 1
int upgrade_connection(struct server *s,struct client *c, char *buf, int bytes){
  
  int i;

  if (c->data == NULL) {
    //fprintf(stderr,"malloc data object of len:%d\n",bytes);
    c->data = malloc(sizeof(char) * (bytes+1));
    /*c->data = strncpy(c->data,buf,bytes);*/
    for (i=0;i<bytes;i++){
      c->data[i] = buf[i];
    }
  }
  else {
   // fprintf(stderr,"data exisits must realloc\n\tcurrent len:%d\tbufferlen:%d\n",c->data_len,bytes);
    c->data = realloc(c->data,sizeof(char) * (c->data_len + bytes+1));
    /*c->data = strncat(c->data, buf, bytes);*/
    for (i=0;i<bytes;i++){
      c->data[c->data_len+i] = buf[i]; 
    }
  }
 
  //fprintf(stderr,"==[Buffer Contains]==[%s]==\n",c->data);
  c->data_len += bytes;

  /*this null char should get overwritten at next write*/
  c->data[c->data_len]='\0';
  
  if (c->data_len > 4) /*more than 4 bytes*/
    if(strstr(c->data,"\r\n\r\n") != NULL){ /*find the end of the http header*/
        return parse_http_header(s,c);
    }

  return 0;
}

/**
 *  get_client_data function
 *
 *    receive comms from the client, check the conn_state variable
 *    and call upgrade_connection if the conn state is still 0
 */
int get_client_data(struct server *s, int i)
{  
  uint8_t readbuffer[READBUFFERSIZE];
  int recv_bytes, j;

  memset(readbuffer, 0, READBUFFERSIZE);
  recv_bytes = read(s->clients[i]->fd,readbuffer,READBUFFERSIZE);

  if (recv_bytes == 0) /*client disconnection*/{
#ifdef DEBUG
    fprintf(stderr,"A client is leaving\n");
#endif
    disconnect_client(s,i);
    return 0;
  }
  else {

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

    return 0;
  }
}


/**
 *  get_server_broadcast function
 *    
 *    read data from the servers stdin buffer
 *    and broadcast it to all the clients
 *    
 *    for now assuming the server will send less than 1500bytes
 *
 */
int get_server_broadcast(int sifd,struct server *s)
{  
  char readbuffer[READBUFFERSIZE];
  int recv_bytes;
  int i;
/*
  struct wsframe {
    uint8_t ftype;
    uint32_t plsize[2];
  } __attribute__ ((packed));

  struct wsframe frame;
  */

  memset(readbuffer,'\0',READBUFFERSIZE);
  recv_bytes = read(sifd,readbuffer,READBUFFERSIZE);
  
  /*
  frame.ftype = 0xFF;
  frame.plsize[0] = 0x00000000;
  frame.plsize[1] = htonl(recv_bytes);
*/
  
  if (s->up_ccount == 0)
    return 0;


  if (s->data_len == 0) {
    s->data_len = recv_bytes+1;
    s->data = realloc(s->data,s->data_len * sizeof(uint8_t));
    
    s->data[0] = 0x00;
    for (i=1; i<s->data_len; i++)
      s->data[i] = readbuffer[i-1];

  } else {
    s->data_len += recv_bytes;
    s->data = realloc(s->data,s->data_len * sizeof(uint8_t));

    for (i=0;i<recv_bytes; i++)
      s->data[s->data_len - recv_bytes + i] = readbuffer[i];
  }
  
  
  //datalen = sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint8_t)*recv_bytes;
  //datalen = (recv_bytes+2);

  //data = malloc(sizeof(uint8_t) * datalen);
  //memset(data,0,datalen);
  
  
/*  for (i=0; i<sizeof(frame); i++){
    data[i] = *((uint8_t *) &frame + i);
  }
  for (; i<recv_bytes+sizeof(frame); i++)
    data[i]=readbuffer[i-sizeof(frame)];
  */
  
/*  data[0] = 0x00;
  for(i=1; i<datalen-1;i++)
    data[i]=readbuffer[i-1];
  data[datalen-1] = 0xFF;
  */


  //s->data = data;
  //s->data_len = datalen;

  if (s->data[s->data_len-1] == 0x0A) {
    s->data_len += 1;
    s->data = realloc(s->data,s->data_len * sizeof(uint8_t));
    s->data[s->data_len-1] = 0xFF;

   /* for(i=0; i<s->data_len;i++)
      fprintf(stderr,"byte %d 0x%02X %c\n",i,s->data[i],s->data[i]);
    */
    
    fprintf(stderr,"Sending: %d bytes\n",s->data_len);
    
    FD_ZERO(&s->outsocks);
    for (i=0;i<s->conncount;i++){
      if (s->clients[i]->conn_state > 0) /*only write to clients with upgraded connections*/
        FD_SET(s->clients[i]->fd,&s->outsocks);
    }
  }
  return 0;
}

/**
 *
 *
 */
int send_to_client(struct server *s, int i)
{
  int write_bytes;
  struct client *c;
  //int j;

  c = s->clients[i];
 
  if (c->send_buffer != NULL){
    
    write_bytes = write(c->fd, c->send_buffer, c->sb_len);
    
    FD_CLR(c->fd, &s->outsocks);

#ifdef DEBUG
    fprintf(stderr,"CLIENT REPLY wrote: %d bytes to client on FD: %d array index: %d\n",write_bytes,c->fd,i);
#endif

    /*for(j=0; j<c->sb_len; j++)
      fprintf(stderr,"WRITE byte %d 0x%02X %c\n",j,c->send_buffer[j],c->send_buffer[j]);
    */
    
    if (c->send_buffer != NULL){
      free(c->send_buffer);
      c->send_buffer = NULL;
      c->sb_len = 0;
    }

  } else if (s->data != NULL && c->conn_state > 0) {

    write_bytes = write(c->fd, s->data, s->data_len);
    
    FD_CLR(c->fd, &s->outsocks);

#ifdef DEBUG
    fprintf(stderr,"SERVER BROADCAST wrote: %d bytes to client on FD: %d array index: %d\n",write_bytes,c->fd,i);
#endif

    /*for(j=0; j<s->data_len; j++)
      fprintf(stderr,"WRITE byte %d 0x%02X %c\n",j,s->data[j],s->data[j]);
    */
    
    if (s->data != NULL){
      free(s->data);
      s->data = NULL;
      s->data_len=0;
    }
  }

  return 0;
}

/**
 *  read_socks function
 *    
 *    called after select returns in the run loop
 *    check the server listen socket, if set call
 *    handle_new_clinet
 *    otherwise check the client file descriptors for
 *    which one is set
 */
int read_socks(struct server *s) 
{
  int i;

  /*check the server listen fd for a new connection*/
  if (FD_ISSET(s->fd, &s->insocks)) {
#ifdef DEBUG
    fprintf(stderr,"New incomming connection\n");
#endif
    if (handle_new_client(s) < 0) { 
      return -1; 
    }
  }

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
  
  /*check for data on the line from connected clients*/
  for (i=0; i<s->conncount; i++){
    /*once a client is connected*/
    if (FD_ISSET(s->clients[i]->fd, &s->insocks)) {
      return get_client_data(s, i);
    }
  }
  return 0;
}

/**
 *  run_loop function
 *
 *    this function contains the main loop which calls 
 *    select which blocks untill there is some action on the
 *    file descriptors
 */
int run_loop(struct server *s)
{
  while (run) {

    build_socket_set(s);  

    if (select(s->highsock + 1, &s->insocks, &s->outsocks, (fd_set *) NULL, NULL) < 0) { 
#ifdef DEBUG
      fprintf(stderr,"wss: select encountered an error: %s\n", strerror(errno)); 
#endif
      return -1; 
    }
    else {
      if (read_socks(s) < 0) { 
        return -1; 
      }
    }

  }

  return 0;
}

/**
 *  disconnect_all_conn function
 *
 *    this function is used to clean up the active connections 
 *    just before the server is about to exit.
 *    eg
 *      SIGINT, SIGTERM
 */
void disconnect_all_conn(struct server *s)
{
  int i;

  for (i=0; i<s->conncount;i++){
    shutdown(s->clients[i]->fd, SHUT_RDWR);     
    close(s->clients[i]->fd);
    free(s->clients[i]->data);

    if (s->clients[i]->send_buffer != NULL)
      free(s->clients[i]->send_buffer);

#ifdef DEBUG
    fprintf(stderr,"Client on fd %d closed\n",s->clients[i]->fd);
#endif

    free(s->clients[i]);

    if (s->clients[i]->conn_state)
      s->up_ccount--;
  }

  free(s->clients);

#ifdef DEBUG
  fprintf(stderr,"Disconnected all clients\n");
#endif

  free(s->data);

  shutdown(s->fd,SHUT_RDWR);
  close(s->fd);
}

/**
 *  main function
 *
 *    sets up signal handlers
 *    creates server object
 */
int main(int argc, char *argv[]) 
{
  struct server *s;
  struct sigaction sag;
  
  struct sigaction sa = {
    .sa_handler = SIG_IGN
  }; 

  sigfillset(&(sag.sa_mask));
  sag.sa_flags = 0;
  sag.sa_handler = handle;

  sigaction(SIGINT, &sag, NULL);
  sigaction(SIGTERM, &sag, NULL);
  sigaction(SIGCHLD, &sa, NULL);

  s = malloc(sizeof(struct server));
  if (s == NULL)
    exit(1);
  
  if (setup_server(s, 6969) < 0){
    free(s);
    exit(1);
  }

  if (run_loop(s) < 0){ 
    free(s);
    exit(1);
  }
  
  disconnect_all_conn(s); 

  if (s != NULL)
    free(s);

#ifdef DEBUG
  fprintf(stderr,"wss: Server exiting cleanly ;)\n");
#endif

  return 0;
}
