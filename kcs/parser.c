#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sysexits.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

int store_comment(char *buff,struct p_parser *p){
  
  if (p->comments == NULL){
    p->comcount++;
    p->comments = malloc(sizeof(struct p_comment*));
  }
  else {
    p->comcount++;
    p->comments = realloc(p->comments,sizeof(struct p_comment*)*p->comcount);
  }

  if (p->comments == NULL)
    return EIO;

  struct p_comment *c;
  c = malloc(sizeof(struct p_comment));
  c->str = NULL;
  c->flag = 0;

  if ((c->str = strdup(buff)) == NULL){
    free(c);
    return EIO;
  }

  p->comments[p->comcount-1] = c;

  return EX_OK;
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
      return store_comment(buff,p);
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
    l->comcount=0;
    l->comments=NULL;
    
    struct p_comment *cc;
    for(i=0;i<p->comcount;i++){
      cc = p->comments[i];
      if (cc->flag == 0){
        l->comcount++;
        l->comments=realloc(l->comments,sizeof(struct p_comment*)*l->comcount);
        l->comments[l->comcount-1] = cc;
        cc->flag=1;
      }
    }

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
    
    s->comcount=0;
    s->comments=NULL;
    
    struct p_comment *cc;
    for(i=0;i<p->comcount;i++){
      cc = p->comments[i];
      if (cc->flag == 0){
        s->comcount++;
        s->comments=realloc(s->comments,sizeof(struct p_comment*)*s->comcount);
        s->comments[s->comcount-1] = cc;
        cc->flag=1;
      }
    }
    
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

  struct stat file_stats;
  if (stat(f,&file_stats) != 0)
    return errno;
  
  p->open_time = file_stats.st_atime;
  
  //fprintf(stderr,"fd: %d atime:%d\n",fd,p->open_time);

  fclose(file);
  
  if (rtn == EIO)
    return rtn;

  return EX_OK;
}

void show_tree(struct katcp_dispatch *d, struct p_parser *p){
  int i,j,k;
  
  struct p_label *cl=NULL;
  struct p_setting *cs=NULL;
  struct p_value *cv=NULL;
  
  //fprintf(stderr,"PARSER show tree\n");

  for (i=0;i<p->lcount;i++){
    cl = p->labels[i];
#ifdef STANDALONE    
    for (j=0;j<cl->comcount;j++)
      fprintf(stderr,"%s",cl->comments[j]->str);
    fprintf(stderr,"%d[%s]\n",i,cl->str);
#endif
    for (j=0;j<cl->scount;j++){
      cs = cl->settings[j];
#ifdef STANDALONE      
      for (k=0;k<cs->comcount;k++)
        fprintf(stderr,"%s",cs->comments[k]->str);
      fprintf(stderr,"  %d\t|__ %s\n",j,cs->str);
#endif      
      for (k=0;k<cs->vcount;k++){
        cv = cs->values[k];
#ifdef STANDALONE
        fprintf(stderr,"\t%c\t%d = %s\n",(j==cl->scount-1)?' ':'|',k,cv->str);
#endif
#ifndef STANDALONE
        prepend_reply_katcp(d);
        append_string_katcp(d,KATCP_FLAG_STRING,"list");
        append_string_katcp(d,KATCP_FLAG_STRING,cl->str);
        append_string_katcp(d,KATCP_FLAG_STRING,cs->str);
        append_string_katcp(d,KATCP_FLAG_STRING | KATCP_FLAG_LAST,cv->str);
#endif
      }
#ifdef STANDALONE
      fprintf(stderr,"\t%c\n",(j==cl->scount-1)?' ':'|');
#endif
    }
  }
  
 /* struct p_comment *cc;
  for (i=0;i<p->comcount;i++){
    cc = p->comments[i];
    fprintf(stderr,"%s",cc->str);
  }
*/

}

void clean_up_parser(struct p_parser *p){
  //fprintf(stderr,"Starting parser cleanup\n");

  int i,j,k;
  
  struct p_label *cl;
  struct p_setting *cs;
  struct p_value *cv;
  struct p_comment *cc;

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
        for (k=0;k<cs->comcount;k++){
          cs->comments[k] = NULL;
        }
        if (cs->comments != NULL)
          free(cs->comments);
        //fprintf(stderr,"\t\tPARSER FREE'd %d vals\n",cs->vcount);
        free(cs->str);
        free(cs->values);
        free(cs);
      }
      for (j=0;j<cl->comcount;j++){
        cl->comments[j] = NULL;
      }
      if (cl->comments != NULL)
        free(cl->comments);
      //fprintf(stderr,"\tPARSER FREE'd %d settings\n",cl->scount);
      free(cl->str);
      free(cl->settings);
      free(cl);
    }
    
    //fprintf(stderr,"PARSER FREE'd %d labels\n",p->lcount);
  
    if (p->labels != NULL)
      free(p->labels);
    if (p->filename != NULL)
      free(p->filename);
    
    for (i=0;i<p->comcount;i++){
      cc = p->comments[i];
      free(cc->str);
      free(cc);
    }
    free(p->comments);

    free(p);
    p = NULL;
  }  
  //fprintf(stderr,"PARSER Finished parser cleanup\n");
}

int save_tree(struct p_parser *p,char *filename){
  FILE *file;
  int i,j,k;
  struct p_label *cl=NULL;
  struct p_setting *cs=NULL;
  struct p_value *cv=NULL;
  int pid;

  char tempname[50];

  pid=getpid();
  sprintf(tempname,"./tempconf.%d",pid);
  //fprintf(stderr,"in file writing function pid:%d %s\n",pid,tempname);

  file = fopen(tempname,"w");

  if (file == NULL){
    fprintf(stderr,"Error reading file: %s\n",strerror(errno));
    return KATCP_RESULT_FAIL;
  }
  
  struct stat file_stats;
  if (stat(filename,&file_stats) != 0){
    fprintf(stderr,"%d %s\n",errno,strerror(errno));
    if (errno != 2)
      return KATCP_RESULT_FAIL;
  }

  if (strcmp(filename,p->filename) == 0 && p->open_time < file_stats.st_atime){
    fprintf(stderr,"filename: %s opentime: %d accesstime: %d\n",filename,p->open_time,file_stats.st_atime);
    return KATCP_RESULT_FAIL;
  }

  
  for (i=0;i<p->lcount;i++){
    cl = p->labels[i];

    for (j=0;j<cl->comcount;j++)
      fprintf(file,"%s",cl->comments[j]->str);
    fprintf(file,"[%s]\n",cl->str);
    
    for (j=0;j<cl->scount;j++){
      cs = cl->settings[j];

      for (k=0;k<cs->comcount;k++)
        fprintf(file,"%s",cs->comments[k]->str);
      fprintf(file,"  %s = ",cs->str);
      
      for (k=0;k<cs->vcount;k++){
        cv = cs->values[k];
        fprintf(file,"%s%c",cv->str,(k==cs->vcount-1)?' ':',');
      }
      fprintf(file,"\n");
    }
  }

  fclose(file);
  //fprintf(stderr,"done writing file\n");
  //fprintf(stderr,"moving file from tempname to new name\n");
  if (rename(tempname,filename) != 0){
    fprintf(stderr,"could not rename file to other name\n");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}


struct p_value * get_label_setting_value(struct katcp_dispatch *d,struct p_parser *p, char *srcl, char *srcs, unsigned long vidx){

  int i,j;

  struct p_label *cl;
  struct p_setting *cs;

  for (i=0;i<p->lcount;i++){
    cl = p->labels[i];
    if (strcmp(srcl,cl->str)==0){
      for (j=0;j<cl->scount;j++){
        cs = cl->settings[j];
        if (strcmp(srcs,cs->str)==0){
          if (cs->values[vidx] != NULL){
            return cs->values[vidx];
          }
        }
      }
    }
  }

  log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Could not find [%s] %s(%d)",srcl,srcs,vidx);
  return NULL;
}

int set_label_setting_value(struct katcp_dispatch *d,struct p_parser *p, char *srcl, char *srcs, unsigned long vidx, char *newval){

  int i,j;

  struct p_label *cl;
  struct p_setting *cs;
  struct p_value *cv;

  for (i=0;i<p->lcount;i++){
    cl = p->labels[i];
    
    if (strcmp(srcl,cl->str)==0){ //if label exists
      for (j=0;j<cl->scount;j++){
        cs = cl->settings[j];
        
        if (strcmp(srcs,cs->str)==0){ //if settings exists
          
          if (cs->vcount > vidx && cs->values[vidx] != NULL){ //if value index exists
            cv = cs->values[vidx];
            if (cv->str != NULL){
              log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"OLD Value: %s",cv->str);
              free(cv->str);
            }
            cv->str = strdup(newval);
            
           // fprintf(stderr,"label,setting,value exist ... update\n");

            log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Updateing value for %s/%s",srcl,srcs);
            
            return KATCP_RESULT_OK;
          }
          else { //can find the value at vidx so create a new value for setting at scount+1
            if (cs->values == NULL){
              cs->vcount++;
              cs->values = malloc(sizeof(struct p_value*));
            }
            else {
              cs->vcount++;
              cs->values = realloc(cs->values,sizeof(struct p_value*)*cs->vcount);
            }

            struct p_value *nv;
            nv = malloc(sizeof(struct p_value));
            nv->str = strdup(newval);

            cs->values[cs->vcount-1] = nv;

            log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Adding new value for %s/%s",srcl,srcs);
            //fprintf(stderr,"label,setting exist ... create new value\n");
            return KATCP_RESULT_OK;
          }

        }
      }
      
      if (cl->settings == NULL){
        cl->scount++;
        cl->settings = malloc(sizeof(struct p_setting*));
      }
      else {
        cl->scount++;
        cl->settings = realloc(cl->settings,sizeof(struct p_setting*)*cl->scount);
      }
      struct p_setting *ns;
      ns = malloc(sizeof(struct p_setting));

      ns->str = strdup(srcs);
      ns->values = malloc(sizeof(struct p_value*));
      ns->vcount = 1;

      struct p_value *nv;
      nv = malloc(sizeof(struct p_value));
      nv->str = strdup(newval);

      ns->values[0] = nv;
      cl->settings[cl->scount-1] = ns;
      
      log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Adding setting and value for %s",srcl);
      //fprintf(stderr,"label exists ... create new setting and value\n");
      return KATCP_RESULT_OK;
    } 
  }
  
  if (p->labels == NULL){
    p->lcount++;
    p->labels = malloc(sizeof(struct p_label*));
  }
  else {
    p->lcount++;
    p->labels = realloc(p->labels,sizeof(struct p_label*)*p->lcount);
  }

  struct p_label *nl;
  nl = malloc(sizeof(struct p_label));
  nl->scount = 1;
  nl->str = strdup(srcl);
  nl->settings = malloc(sizeof(struct p_setting*));

  struct p_setting *ns;
  ns = malloc(sizeof(struct p_setting));
  ns->str = strdup(srcs);
  ns->values = malloc(sizeof(struct p_value*));
  ns->vcount=1;

  struct p_value *nv;
  nv = malloc(sizeof(struct p_value));
  nv->str = strdup(newval);

  ns->values[0] = nv;
  nl->settings[0] = ns;
  p->labels[p->lcount-1] = nl;

  log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Added new label setting and value");
  //fprintf(stderr,"... none exist\n");
  return KATCP_RESULT_OK;
}

struct p_value * parser_get(struct katcp_dispatch *d, char *srcl, char *srcs, unsigned long vidx){

  struct kcs_basic *kb;
  struct p_parser *p;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  if (kb == NULL)
    return NULL;

  p = kb->b_parser;

  if (p != NULL) {
    return get_label_setting_value(d,p,srcl,srcs,vidx);
  }
  
  log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"No configuration file loaded yet, use ?parser load [filename]");
  return NULL;
}

int parser_set(struct katcp_dispatch *d, char *srcl, char *srcs, unsigned long vidx, char *nv){

  struct kcs_basic *kb;
  struct p_parser *p;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  if (kb == NULL)
    return KATCP_RESULT_FAIL;

  p = kb->b_parser;

  if (p != NULL) {
    return set_label_setting_value(d,p,srcl,srcs,vidx,nv);
  }
  
  log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"No configuration file loaded yet, use ?parser load [filename]");
  return KATCP_RESULT_FAIL;
}

int parser_save(struct katcp_dispatch *d, char *filename){
  
  struct kcs_basic *kb;
  struct p_parser *p;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  if (kb == NULL)
    return KATCP_RESULT_FAIL;

  p = kb->b_parser;

  if (p != NULL) {
    int rtn;
    if (filename == NULL){
      rtn = save_tree(p,p->filename);
      if (rtn == KATCP_RESULT_FAIL){
        log_message_katcp(d,KATCP_LEVEL_WARN,NULL,"File has been edited behind your back!!! use ?parser save newfilename or force save ?parser forcesave");
        return KATCP_RESULT_FAIL;
      }
    }
    else
      rtn = save_tree(p,filename);

    if (rtn == KATCP_RESULT_OK){
      log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Saved configuration file as %s",filename);
      return rtn;
    }
  }
  
  log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"No configuration file loaded yet, use ?parser load [filename]");
  return KATCP_RESULT_FAIL;
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
  p->comments = NULL;
  p->comcount = 0;
  
  rtn = start_parser(p,filename);
  
  if (rtn != 0){
    log_message_katcp(d,KATCP_LEVEL_ERROR,NULL,"%s",strerror(rtn)); 
    clean_up_parser(p);
    return KATCP_RESULT_FAIL;
  }

  p->filename = strdup(filename);
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
    show_tree(d,p);
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
  //parser_list(NULL);
  //parser_save(NULL,"output.conf");
  /*struct p_value *tval;
  tval = parser_get(NULL,"borphserver","bitstream",0);
  if (tval != NULL)
    fprintf(stderr,"%s\n",tval->str);
  else
    fprintf(stderr,"CANNOT FIND\n");
 */
 // parser_set(NULL,"k","a",1,"hello world");
  parser_save(NULL,"oldtest");
//  parser_list(NULL);

  parser_destroy(NULL);
  
  free(tkb);

  return EX_OK;
}
#endif
