#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <time.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <endian.h>

#include <openssl/ssl.h>

#include "server.h"

#define PORT          "6969"

#define WSSECKEY      "Sec-WebSocket-Key: "
#define WSSECPROTO    "Sec-WebSocket-Protocol: "
#define WSPROTO       "katcp"
#define WSGUID        "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define SRVHDR        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Protocol: katcp\r\nSec-WebSocket-Accept: "

int generate_upgrade_response_ws(struct ws_client *c, char *key)
{
  unsigned char   md_value[EVP_MAX_MD_SIZE];
  unsigned int    md_len;
  EVP_MD_CTX      mdctx;
  BIO             *bmem, *b64;
  BUF_MEM         *bptr;
  char            skey[512], temp[100];
  int             len;

//  char srvhdr[] = {  };

  if (c == NULL || key == NULL)
    return -1;
  
  len = snprintf(skey, 512, "%s%s", key, WSGUID);
  if (len < 0)
    return -1;

  bzero(temp, 100);

  EVP_DigestInit(&mdctx, EVP_sha1());
  EVP_DigestUpdate(&mdctx, skey, len);
  EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
  EVP_MD_CTX_cleanup(&mdctx);
  
  b64  = BIO_new(BIO_f_base64());
  if (b64 == NULL){
    return -1;
  }

  bmem = BIO_new(BIO_s_mem());
  if (bmem == NULL){
    BIO_free_all(b64);
    return -1;
  }

  b64  = BIO_push(b64, bmem);
  if (BIO_write(b64, md_value, md_len) < 0){
#ifdef DEBUG
    fprintf(stderr, "wss: BIO write fail\n");
#endif
    BIO_free_all(b64);
    return -1;
  }
  if (BIO_flush(b64) < 0){
    BIO_free_all(b64);
    return -1;
  }
  BIO_get_mem_ptr(b64, &bptr);
  
  memcpy(temp, bptr->data, bptr->length - 1);

  BIO_free_all(b64);

#if 0 
def DEBUG
  for (i=0; i < md_len; i++) 
    fprintf(stderr , "%02x", md_value[i]);
  fprintf(stderr, "\n");
  fprintf(stderr, "wss: server key: <%s> len: %d temp: <%s>\n", skey, len, temp); 
#endif

  len = snprintf(skey, 512, "%s%s\r\n\r\n", SRVHDR, temp);
  if (len < 0)
    return -1;

#ifdef DEBUG
  fprintf(stderr, "wss: srvhdr:[%s]\n", skey);
#endif

  if (write_to_client_ws(c, skey, len * sizeof(char)) < 0){
#ifdef DEBUG
    fprintf(stderr, "wss: error write_to_client\n");
#endif
    return -1;
  }
  
#ifdef DEBUG
  fprintf(stderr, "wss: upgrade response generated and dispatched\n");
#endif

  return 0;
}

int parse_http_proto_ws(struct ws_client *c)
{
  char *line, *key, *proto;

  key   = NULL;
  proto = NULL;

  while ((line = (char*)readline_client_ws(c)) != NULL){
#ifdef DEBUG
    fprintf(stderr, "wss: line [%s]\n", line);
#endif
    if (strstr((const char*)line, WSSECKEY) != NULL) {
      key = strdup(line + strlen(WSSECKEY));
    } else if (strstr((const char*)line, WSSECPROTO) != NULL) {
      proto = strdup(line + strlen(WSSECPROTO));
    }
  }

#ifdef DEBUG 
  fprintf(stderr, "wss: client PROTO: <%s> KEY <%s>\n", proto, key);
#endif
  
  if (key != NULL && proto != NULL && strncmp(proto, WSPROTO, strlen(WSPROTO)) == 0){
   
   if (proto != NULL){
     free(proto);
     proto = NULL;
   }

   if (generate_upgrade_response_ws(c, key) < 0){
#ifdef DEBUG
      fprintf(stderr, "wss: error unable to generate response for client %d\n", c->c_fd);
#endif
      if (key != NULL){
        free(key);
        key = NULL;
      }
      return -1;
    }
    
    if (key != NULL) {
      free(key);
      key = NULL;
    }
    
    if (upgrade_client_ws(c) < 0){
#ifdef DEBUG
      fprintf(stderr, "wss: client [%d] status not upgraded\n", c->c_fd);
#endif
      return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "wss: client [%d] status upgraded\n", c->c_fd);
#endif
  } else {
#ifdef DEBUG
    fprintf(stderr, "wss: error websocket protocol mismatch\n");
#endif
    if (proto != NULL){
      free(proto);
      proto = NULL;
    }
    if (key != NULL){
      free(key);
      key = NULL;
    }
    return -1;
  }

  if (proto != NULL){
    free(proto);
    proto = NULL;
  }
  if (key != NULL){
    free(key);
    key = NULL;
  }
   
  return 0;
}

int parse_websocket_proto_ws(struct ws_client *c)
{
  int i, len, opcode, shift;
  uint8_t hdr[2];
  uint8_t msk[4];
  uint64_t payload;
  unsigned char *data;

  if (c == NULL || c->c_rb == NULL)
    return -1;
  
  shift = 0;
  len = sizeof(hdr);
  //hdr = *((uint8_t *) readdata_client_ws(c, len));
  if (readdata_client_ws(c, &hdr, len) < 0){
#ifdef DEBUG
    fprintf(stderr, "wss: error readdata\n");
#endif
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "wss: HDR: 0x%x\n", (uint16_t)*hdr);
#endif

  if (hdr[0] & WSF_FIN){
#ifdef DEBUG
    fprintf(stderr, "wss: FIN SET\n");
#endif
  }
  
  opcode = hdr[0] & WSF_OPCODE;

#if 0 
def DEBUG
  fprintf(stderr, "wss: HDR: 0x%0x\n", hdr[1]);
#endif

  if (hdr[1] & WSF_MASK){
#ifdef DEBUG
    fprintf(stderr, "wss: MASK SET\n");
#endif
  }

  payload = hdr[1] & WSF_PAYLOAD;
   
#if 1
  switch (payload){
    case WSF_PAYLOAD_16:
#ifdef DEBUG
      fprintf(stderr, "wss: Extended Payload is 16bits\n");
#endif
      //payload = ntohs(*((uint16_t *) readdata_client_ws(c, sizeof(uint16_t))));    
      if (readdata_client_ws(c, &payload, sizeof(uint16_t)) < 0){
#ifdef DEBUG
        fprintf(stderr, "wss: error readdata\n");
#endif
        return -1;
      }
      payload = ntohs(payload);
      break;

    case WSF_PAYLOAD_64:
#ifdef DEBUG
      fprintf(stderr, "wss: Extended Payload is 64bits\n");
#endif
      //payload = be64toh(*((uint64_t *) readdata_client_ws(c, sizeof(uint64_t))));    
      if (readdata_client_ws(c, &payload, sizeof(uint64_t)) < 0){
#ifdef DEBUG
        fprintf(stderr, "wss: error readdata\n");
#endif
        return -1;
      }
      payload = be64toh(payload);
      break;
  }
#endif 

#if 0
  len = sizeof(uint8_t);
  msk[0] = *((uint8_t *) readdata_client_ws(c, len));
  msk[1] = *((uint8_t *) readdata_client_ws(c, len));
  msk[2] = *((uint8_t *) readdata_client_ws(c, len));
  msk[3] = *((uint8_t *) readdata_client_ws(c, len));
#endif

  if (readdata_client_ws(c, msk, sizeof(msk)) < 0){
#ifdef DEBUG
    fprintf(stderr, "wss: error readdata\n");
#endif
    return -1;
  }

#ifdef DEBUG
  fprintf(stderr, "wss: OPCODE 0x%x PAYLOAD: 0x%x MASKKEY: 0x%x\n", opcode, payload, msk);
#endif

  data = (unsigned char *) c->c_rb;
  for(i=0; i<c->c_rb_len; i++)
    data[i] ^= msk[i % 4];
    
#ifdef DEBUG
  for(i=0; i<c->c_rb_len; i++)
    fprintf(stderr,"byte %d 0x%0x %c\n", i, data[i], data[i]);
#endif

  fprintf(stdout, "%s\n", readline_client_ws(c));

  dropdata_client_ws(c);

  return 0;
}

int capture_client_data_ws(struct ws_client *c)
{
  
  switch(c->c_state){
    case C_STATE_NEW:
#ifdef DEBUG
      fprintf(stderr, "wss: parse http\n");
#endif
      return parse_http_proto_ws(c);

    case C_STATE_UPGRADED:
#ifdef DEBUG
      fprintf(stderr, "wss: parse websocket\n");
#endif
      return parse_websocket_proto_ws(c);
  }

  return -1;
}


int main(int argc, char *argv[]) 
{
  return register_client_handler_server(&capture_client_data_ws, PORT);
}
 

#if 0

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

#endif
