#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sysexits.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include <katcp.h>

#include "kcs.h"

int greeting(char *app) {
  fprintf(stderr,"ROACH Configuration Parser\n\n\tUsage:\t%s -f [filename]\n\n",app);
  return EX_OK;
}

char *rm_whitespace(char *str)
{
  //fprintf(stderr,"old string: %s",str);
  
  char *nstr;
  int i;

  i=1;
  nstr=NULL;
  
  while (*str != '\0'){
    if (!isspace(*str)){
      if (nstr == NULL){
        nstr = malloc(sizeof(char));
      }
      else {
        nstr = realloc(nstr,sizeof(char)*(i+1));
      }
      nstr[i-1] = *str;
      i++;
    }
    str++;
  }

  nstr[i-1] = '\0';
  
  //fprintf(stderr,"len:%d new string: %s\n",i,nstr);

  return nstr;
}

#define OLABEL    91 /*[*/
#define CLABEL    93 /*]*/
#define SETTING   61 /*=*/ 
#define VALUE     44 /*,*/
int process_line(char * buff,struct p_parser *p){

  //fprintf(stderr,"%d CL: %s",strlen(buff),buff);

  switch (buff[0]){
    case '\n':
      return EX_OK;
    case '#':
      return EX_OK;
  }

  if (strlen(buff) == 1)
     return EX_OK;

  char *buf = rm_whitespace(buff);
  
  char *rtn;
  int i;

  /*Search for [ which is start of label*/
  rtn = strchr(buf,OLABEL);
  if (rtn != NULL){
    //fprintf(stderr,"found [ pos: %p %s",rtn,rtn);
    if (p->labels == NULL){
      p->lcount++;
      p->labels = malloc(sizeof(struct p_label*));
    }
    else {
      p->lcount++;
      p->labels = realloc(p->labels,sizeof(struct p_label*)*p->lcount);
    }
    
    char *lend;
    int len;
    
    lend = strchr(buf,CLABEL);
    if (lend == NULL)
      return EIO;
    
    len=(int)(lend-rtn);
    /*create the new label*/
    struct p_label *l;
    l=malloc(sizeof(struct p_label));
    
    l->settings=NULL;
    l->scount=0;
    l->str = malloc(sizeof(char)*(len));
    
    for (i=0;i<len-1;i++){
      l->str[i]=*(rtn+1+i);
    }
    l->str[len-1]='\0';
    
    /*store the new label in the array*/
    p->labels[p->lcount-1] = l;
    
    //fprintf(stderr,"found ] pos: %p %s",lend,lend);
    //fprintf(stderr,"diff: %d\n",len);
    //fprintf(stderr,"[%s]\n",l->str);
  }

  /*search for = which is the divider between setting and value*/
  rtn = strchr(buf,SETTING);
  if (rtn != NULL){
    
    struct p_label *cl;
    cl = p->labels[p->lcount-1];

    if (cl->settings == NULL){
      cl->scount++;
      cl->settings = malloc(sizeof(struct p_setting*));
    }
    else {
      cl->scount++;
      cl->settings = realloc(cl->settings,sizeof(struct p_setting*)*cl->scount);
    }
    
    int len;
    len =(int)(rtn-buf)+1;
    //fprintf(stderr,"len: %d ",len);
    
    /*create the new setting*/
    struct p_setting *s;
    s=malloc(sizeof(struct p_setting));
    
    s->vcount=0;
    s->str = malloc(sizeof(char)*len);
    s->values=NULL;

    for(i=0;i<len-1;i++){
      s->str[i]=*(buf+i);
    }
    s->str[len-1]='\0';
    //fprintf(stderr,"s: %s\n",s->str);   
    /*store the new setting in the array*/
    cl->settings[cl->scount-1] = s;
    
    /*find the values for this setting*/
    char *vstart = rtn+1;
    do {
      if (*rtn == VALUE || *rtn == '\0'){
      
        if (s->values == NULL){
          s->vcount++;
          s->values = malloc(sizeof(struct p_value*));
        }
        else {
          s->vcount++;
          s->values = realloc(s->values,sizeof(struct p_value*)*s->vcount);
        }

        int val_size = (int)(rtn-vstart);
        struct p_value *v;
        v=malloc(sizeof(struct p_value));
        v->str=malloc(sizeof(char)*(val_size+1));

        for(i=0;i<val_size;i++){
          v->str[i] = *vstart++;
        }
        vstart++;

        v->str[val_size]='\0';
        //fprintf(stderr,"val: %s\n",v->str);
        s->values[s->vcount-1] = v;

      }
    } while (*(rtn++) != '\0');
  }

  //fprintf(stderr,"about to FREE: %s\n",buf);
  free(buf); 
  return EX_OK;
}

int start_parser(struct p_parser *p, char *f) {
  
  FILE *file;
  int size=1024,pos,c,rtn;
  char *buffer;
  file = fopen(f,"r");
  
  if (file == NULL){
    fprintf(stderr,"Error Reading File: %s\n",f);
    return errno;
  }
  
  buffer = malloc(size * sizeof(char));

  do {
    pos=0;
    do{
      c=fgetc(file);
      if (c!=EOF)
        buffer[pos++] = (char)c;
      if (pos >= size-1){
        size *= 2;
        buffer = realloc(buffer, size * sizeof(char*));
      }
    } while(c != EOF && c != '\n');
    buffer[pos]=0;
    
    if (c != EOF) {
      rtn = process_line(buffer,p);
      if (rtn == EIO)
        break;
    }
    //fprintf(stderr,"Line Status: %d\n",rtn);

  } while(c != EOF);
  
  free(buffer);
  fclose(file);
  
  if (rtn == EIO)
    return rtn;

  return EX_OK;
}

void show_tree(struct p_parser *p){
  int i,j,k;
  
  struct p_label *cl=NULL;
  struct p_setting *cs=NULL;
  struct p_value *cv=NULL;
  
  fprintf(stderr,"PARSER show tree\n");

  for (i=0;i<p->lcount;i++){
    cl = p->labels[i];
    fprintf(stderr,"%d[%s]\n",i,cl->str);

    for (j=0;j<cl->scount;j++){
      cs = cl->settings[j];
      fprintf(stderr,"  %d\t|__ %s\n",j,cs->str);
      
      for (k=0;k<cs->vcount;k++){
        cv = cs->values[k];
        fprintf(stderr,"\t%c\t%d = %s\n",(j==cl->scount-1)?' ':'|',k,cv->str);
      }
      fprintf(stderr,"\t%c\n",(j==cl->scount-1)?' ':'|');
    }
  }
}

void clean_up_parser(struct p_parser *p){
  //fprintf(stderr,"Starting parser cleanup\n");

  int i,j,k;
  
  struct p_label *cl;
  struct p_setting *cs;
  struct p_value *cv;

  if (p != NULL) {
    for (i=0;i<p->lcount;i++){
      cl = p->labels[i];
      for (j=0;j<cl->scount;j++){
        cs = cl->settings[j];
        for (k=0;k<cs->vcount;k++){
          cv = cs->values[k];
          free(cv->str);
          free(cv);
        }
        //fprintf(stderr,"\t\tPARSER FREE'd %d vals\n",cs->vcount);
        free(cs->str);
        free(cs->values);
        free(cs);
      }
      //fprintf(stderr,"\tPARSER FREE'd %d settings\n",cl->scount);
      free(cl->str);
      free(cl->settings);
      free(cl);
    }
    //fprintf(stderr,"PARSER FREE'd %d labels\n",p->lcount);
  
    if (p->labels != NULL)
      free(p->labels);
    free(p);
    p = NULL;
  }
  
  //fprintf(stderr,"PARSER Finished parser cleanup\n");
}

int parser_load(struct katcp_dispatch *d, char *filename){

  int rtn;
  
  struct kcs_basic *kb;
  struct p_parser *p;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  
  if (kb == NULL)
    return KATCP_RESULT_FAIL;

  p = kb->b_parser;

  if (p != NULL){
    clean_up_parser(p);
  }
  
  p = malloc(sizeof(struct p_parser));
  p->lcount = 0;
  p->labels = NULL;
  
  rtn = start_parser(p,filename);
  
  if (rtn != 0){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"%s",strerror(rtn)); 
    clean_up_parser(p);
    return KATCP_RESULT_FAIL;
  }

  kb->b_parser = p;

  log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Configuration file loaded");

  return KATCP_RESULT_OK;
}

int parser_destroy(struct katcp_dispatch *d){

  struct kcs_basic *kb;
  struct p_parser *p;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  p = kb->b_parser;
  
  fprintf(stderr,"PARSER Destroy called\n");

  if (p != NULL){
    clean_up_parser(p);
    return KATCP_RESULT_OK;
  }
  return KATCP_RESULT_FAIL;
}

int parser_list(struct katcp_dispatch *d){
  
  struct kcs_basic *kb;
  struct p_parser *p;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  p = kb->b_parser;

  if (p != NULL) {
    show_tree(p);
  }
  else {
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"No configuration file loaded yet, use ?parser load [filename]");
    return KATCP_RESULT_FAIL;
  }
  return KATCP_RESULT_OK;
}

#ifdef STANDALONE
struct kcs_basic *tkb;

void * need_current_mode_katcp(struct katcp_dispatch *d, unsigned int mode){
  return tkb;
}

int log_message_katcp(struct katcp_dispatch *d,unsigned int priority,char *name, char *fmt,...){
  return 0;
}

int main(int argc, char **argv) {

  int i,j,c;
  char *param;
  char *filename;

  i=j=1;
  param=NULL;

  if (argc == i) return greeting(argv[0]);

  while (i<argc) {
  
    if (argv[i][0] == '-') {
      c=argv[i][j];
      switch (c) {
        default:

          if (argv[i][2] == '\0') {
            param = argv[i+1];
            i+=2;
          }
          else {
            param = argv[i] + 2;
            i+=1;
          }
          
          if (i > argc) return greeting(argv[0]);
          else if (param[0] == '\0') return greeting(argv[0]);
          else if (param[0] == '-') return greeting(argv[0]);

          switch (c) {
            case 'f':
              filename = param;
              break;
            default:
              return greeting(argv[0]);
              break;
          }

          break;
      }
    }
    else
      return greeting(argv[0]);

  }

  tkb = malloc(sizeof(struct kcs_basic));

  tkb->b_parser=NULL;
  
  fprintf(stderr,"filename: %s\n",filename);

  parser_load(NULL,filename);
  parser_list(NULL);
  parser_destroy(NULL);
  
  free(tkb);

  return EX_OK;
}
#endif
