#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <katpriv.h>
#include <katcp.h>

#define COL ':'
#define WAK '/'

#define S_SCHEME 2
#define S_HOST 3
#define S_PORT 4
#define S_PATH 5
#define S_END 0
#define S_CMD 6

char *copy_kurl_string_katcp(struct katcp_url *ku,char *path){
  char *str;
  int i,found=0;

#ifdef DEBUG
  fprintf(stderr,"Inside kurl string\n");
#endif
  
  str = NULL;
  i = snprintf(NULL,0,"%s://%s:%d/",ku->scheme,ku->host,ku->port);
  str = malloc(sizeof(char)*(i+1));
  i = snprintf(str,i+1,"%s://%s:%d/",ku->scheme,ku->host,ku->port);
  str[i]='\0'; 
  if (!path)
    return str;

  for (i=0;i<ku->pcount;i++){
    if (strcmp(ku->path[i],path) == 0){
      found = i;
      break;
    }
  }
  if (!found){
    free(str);
    return NULL;
  }
  i = snprintf(NULL,0,"%s%s",str,ku->path[found]);
  str = realloc(str,sizeof(char)*(i+1));
  str = strcat(str,ku->path[found]);
  str[i] = '\0';
  
#ifdef DEBUG
  fprintf(stderr,"Found the path in the kurl returning %s\n",str);
#endif

  return str;
}

char *kurl_string_with_path_id(struct katcp_url *ku, int id){
  char * str;
  int len;
  str = NULL;
  len = snprintf(NULL,0,"%s://%s:%d/%s",ku->scheme,ku->host,ku->port,ku->path[id]);
  str = malloc(sizeof(char)*(len+1));
  snprintf(str,len+1,"%s://%s:%d/%s",ku->scheme,ku->host,ku->port,ku->path[id]);
  //str[len+1] = '\0';
#ifdef DEBUG
  fprintf(stderr,"kurl_string_with_id len: %d returning: %s\n",len,str);
#endif
  return str;
}

struct katcp_url *create_kurl_from_string_katcp(char *url){
  int i,j, state, spos, epos, len;
  char c;
  char *temp;
  struct katcp_url *ku;

#ifdef DEBUG
  fprintf(stderr,"katcp_url about to parse: %s\n", url);
#endif
  if (url == NULL)
    return NULL;

  ku = malloc(sizeof(struct katcp_url));
  if (!ku)
    return NULL;
  ku->str    = strdup(url);
  ku->scheme = NULL;
  ku->host   = NULL;
  ku->port   = 0;
  ku->path   = NULL;
  ku->pcount = 0;
  ku->cmd    = NULL;

  state = S_SCHEME;
  spos  = 0;
  epos  = 0;
  len   = 0;
  temp  = NULL;
  j=0;
  
  for (i=0; state != S_END; ){
    
    switch(state){
      case S_SCHEME:
        c = url[i];
        switch(c){
          case '\0':
            destroy_kurl_katcp(ku);
            return NULL;
          case COL:
            epos = i+1;
            len = epos-spos;
            ku->scheme = malloc(sizeof(char)*len);
            ku->scheme = strncpy(ku->scheme,url+spos,len-1);
            ku->scheme[len-1] = '\0';
            //fprintf(stderr,"scheme: %s %d\n",ku->scheme,epos);
            if (strcasecmp(ku->scheme,"katcp") == 0)
              state = S_HOST;
            else if (strcasecmp(ku->scheme,"exec") == 0)
              state = S_CMD;
            spos = i+1;
            break;
        }
        i++;
        break;
      case S_CMD:
        c= url[i];
        switch (c){
          case WAK:
            j++;
            if (j < 3)
              spos = i+1;
            break;
          case '\r':
          case '\n':
          case '\0':
            epos = i+1;
            len = epos-spos;
            ku->cmd = malloc(sizeof(char)*len);
            ku->cmd = strncpy(ku->cmd,url+spos,len-1);
            ku->cmd[len-1] = '\0';
            state = S_END;
            break;
        }
        i++;
        break;
      case S_HOST:
        c = url[i];
        switch(c){
          case '\0':
            destroy_kurl_katcp(ku);
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
            //ku->path = malloc(sizeof(char)*len);
            //ku->path = strncpy(ku->path,url+spos,len-1);
            //ku->path[len-1] = '\0';
            ku->path = malloc(sizeof(char *));
            ku->path[ku->pcount] = malloc(sizeof(char)*len);
            ku->path[ku->pcount] = strncpy(ku->path[ku->pcount],url+spos,len-1);
            ku->path[ku->pcount][len-1] = '\0'; 
            ku->pcount++;
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

struct katcp_url *create_kurl_katcp(char *scheme, char *host, int port, char *path){
  struct katcp_url *ku;
  ku = NULL;
  ku = malloc(sizeof(struct katcp_url));
  if (!ku)
    return NULL;
  ku->scheme = strdup(scheme);
  ku->host   = strdup(host);
  ku->port   = port;
  ku->str    = copy_kurl_string_katcp(ku,NULL);
  ku->path = realloc(ku->path,sizeof(char*)*++ku->pcount);
  ku->path[ku->pcount-1] = strdup(path);
  return ku;
}

char *add_kurl_path_copy_string_katcp(struct katcp_url *ku, char *npath){
  ku->path = realloc(ku->path,sizeof(char*)*++ku->pcount);
  ku->path[ku->pcount-1] = strdup(npath);
  return kurl_string_with_path_id(ku,ku->pcount-1);
}

void destroy_kurl_katcp(struct katcp_url *ku){
  int i;
  if (!ku)
    return;
  if (ku->str)    { free(ku->str);    ku->str = NULL; }
  if (ku->scheme) { free(ku->scheme); ku->scheme = NULL; }
  if (ku->host)   { free(ku->host);   ku->host = NULL; }
  ku->port = 0;
  if (ku->path)   { 
    for (i=0;i<ku->pcount;i++)
      if (ku->path[i]) { 
        free(ku->path[i]);
        ku->path[i] = NULL;
      }
    free(ku->path);   
    ku->path = NULL; 
  }
  if (ku->cmd)    { free(ku->cmd);    ku->cmd = NULL; }
  if (ku)         { free(ku);         ku = NULL; }
#ifdef DEBUG
  fprintf(stderr,"KURL: Destroyed a kurl\n");
#endif
}

#ifdef STANDALONE
void kurl_print(struct katcp_url *ku){
  int i;
  fprintf(stderr,"KURL Scheme: %s\n",ku->scheme);
  fprintf(stderr,"KURL Host  : %s\n",ku->host);
  fprintf(stderr,"KURL Port  : %d\n",ku->port);
  for (i=0; i<ku->pcount; i++)
    fprintf(stderr,"KURL Path%d : %s\n",i,ku->path[i]);
  fprintf(stderr,"KURL String: %s\n",ku->str);
  fprintf(stderr,"KURL CMD   : %s\n",ku->cmd);
}

int main(int argc, char **argv){
  
  struct katcp_url *ku, *ku2;

  ku = create_kurl_from_string_katcp("katcp://host.domain:7147/");
  ku2 = create_kurl_from_string_katcp("exec:///bin/ls");
  
  if (ku != NULL){
    char *temp;
    if (!(temp = copy_kurl_string_katcp(ku,"?disconnect")))
      temp = add_kurl_path_copy_string_katcp(ku,"?disconnect");
    fprintf(stderr,"%s\n",temp);
    free(temp);
    kurl_print(ku);
    destroy_kurl_katcp(ku);
  }
  if (ku2 != NULL){
    kurl_print(ku2);
    destroy_kurl_katcp(ku2);
    return 0;
  }

  fprintf(stderr,"kurl did not parse\n");
  return 0;
}
#endif
