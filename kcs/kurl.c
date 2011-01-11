#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <katcp.h>

#include "kcs.h"

#define COL ':'
#define WAK '/'

#define S_SCHEME 2
#define S_HOST 3
#define S_PORT 4
#define S_PATH 5
#define S_END 0

char *kurl_string(struct kcs_url *ku){
  char *str;
  int ccount;
  str = NULL;
  ccount = 20 + strlen(ku->scheme) + strlen(ku->host) + strlen(ku->path);
  str = malloc(sizeof(char)*ccount);
  sprintf(str,"%s://%s:%d/%s",ku->scheme,ku->host,ku->port,ku->path);
  return str;
}

void kurl_print(struct kcs_url *ku){
  fprintf(stderr,"KURL Scheme: %s\n",ku->scheme);
  fprintf(stderr,"KURL Host  : %s\n",ku->host);
  fprintf(stderr,"KURL Port  : %d\n",ku->port);
  fprintf(stderr,"KURL Path  : %s\n",ku->path);
  fprintf(stderr,"KURL String: %s\n",ku->str);
}

struct kcs_url *kurl_create_url_from_string(char *url){
  int i, state, spos, epos, len;
  char c;
  char *temp;
  struct kcs_url *ku;

  ku = malloc(sizeof(struct kcs_url));
  if (!ku)
    return NULL;
  ku->str    = create_str(url);
  ku->scheme = NULL;
  ku->host   = NULL;
  ku->port   = 0;
  ku->path   = NULL;

  state = S_SCHEME;
  spos  = 0;
  epos  = 0;
  len   = 0;
  temp  = NULL;
  
  for (i=0; state != S_END; ){
    
    switch(state){
      case S_SCHEME:
        c = url[i];
        switch(c){
          case '\0':
            kurl_destroy(ku);
            return NULL;
          case COL:
            epos = i+1;
            len = epos-spos;
            ku->scheme = malloc(sizeof(char)*len);
            ku->scheme = strncpy(ku->scheme,url+spos,len-1);
            ku->scheme[len-1] = '\0';
            //fprintf(stderr,"scheme: %s %d\n",ku->scheme,epos);
            state = S_HOST;
            spos = i+1;
            break;
        }
        i++;
        break;
      case S_HOST:
        c = url[i];
        switch(c){
          case '\0':
            kurl_destroy(ku);
            return NULL;
          case WAK:
            spos = i+1;
            break;
            free(temp);
            temp = NULL;
          case COL:
            epos = i+1;
            len = epos-spos;
            ku->host = malloc(sizeof(char)*len);
            ku->host = strncpy(ku->host,url+spos,len-1);
            ku->host[len-1] = '\0';
           // fprintf(stderr,"host: %s %d\n",ku->host,epos);
            state = S_PORT;
            spos = i+1;
            break;
        }
        i++;
        break;
      case S_PORT:
        c = url[i];
        switch(c){
          case COL:
            spos = i+1;
            break;
          case '\0':
          case '\n':
          case '\r':
          case WAK:
            epos = i+1;
            len = epos-spos;
            temp = malloc(sizeof(char)*len);
            temp = strncpy(temp,url+spos,len-1);
            temp[len-1] = '\0';
            ku->port = atoi(temp);
            //fprintf(stderr,"port: %s %d %d\n",temp,ku->port,epos);
            state = S_PATH;
            free(temp);
            temp = NULL;
            spos = i+1;
            break;
        }
        i++;
        break;
      case S_PATH:
        c = url[i];
        switch(c){
          case '\0':
          case '\n':
          case '\r':
            epos = i+1;
            len = epos-spos;
            ku->path = malloc(sizeof(char)*len);
            ku->path = strncpy(ku->path,url+spos,len-1);
            ku->path[len-1] = '\0';
           // fprintf(stderr,"path: %s %d\n",ku->path,epos);
            state = S_END;
            break;
        }
        i++;
        break;
    }
  }
  
  //fprintf(stderr,"\n");

  return ku;
}

struct kcs_url *kurl_create_url(char *scheme, char *host, int port, char *path){
  struct kcs_url *ku;
  ku = NULL;
  ku = malloc(sizeof(struct kcs_url));
  if (!ku)
    return NULL;
  ku->scheme = create_str(scheme);
  ku->host   = create_str(host);
  ku->port   = port;
  ku->path   = create_str(path);
  return ku;
}

void kurl_destroy(struct kcs_url *ku){
  if (!ku)
    return;
  if (ku->str)    { free(ku->str);    ku->str = NULL; }
  if (ku->scheme) { free(ku->scheme); ku->scheme = NULL; }
  if (ku->host)   { free(ku->host);   ku->host = NULL; }
  ku->port = 0;
  if (ku->path)   { free(ku->path);   ku->path = NULL; }
  if (ku)         { free(ku);         ku = NULL; }
#ifdef DEBUG
  fprintf(stderr,"KURL: Destroyed a kurl\n");
#endif
}

#ifdef STANDALONE
int main(int argc, char **argv){
  
  struct kcs_url *ku;

  ku = kurl_create_url_from_string("katcp://host.domain:7147");
  
  if (ku != NULL){
    char *temp;
    temp = kurl_string(ku);
    fprintf(stderr,"%s\n",temp);
    free(temp);
    
    kurl_print(ku);

    kurl_destroy(ku);
  } else{
    fprintf(stderr,"kurl did not parse\n");
  }


  return 0;
}
#endif
