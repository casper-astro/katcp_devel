#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>

#include <katcp.h>

#include "kcs.h"

/* makes things explicit (and portable, in case anybody wants to move to EBCDIC ;) */

#define OLABEL    '['
#define CLABEL    ']'
#define SETTING   '='
#define VALUE     ',' 
#define COMMENT   '#'

#define S_START  -1
#define S_COMMENT 0
#define S_LABEL   1
#define S_SETTING 2
#define S_VALUE   3
#define S_MLVALUE 4

#define OKAY 1
#define FAIL 0

int greeting(char *app)
{
  fprintf(stderr,"ROACH Configuration Parser\n\n\tUsage:\t%s -f [filename]\n\n",app);
  return EX_OK;
}

char *rm_whitespace(char *str)
{
  int i,len,pos;
  char *buf;

  if (str == NULL){
#ifdef DEBUG
    fprintf(stderr,"Cannot remove whitespace from NULL string\n");
#endif
    return NULL;
  }

  len = strlen(str);
  buf = malloc(sizeof(char)*len+1);
  pos = 0;
  i   = 0;

  for (;i<len;i++){
    switch(str[i]){
      case 0x09:
      case 0x0a:
      case 0x0b:
      case 0x0c:
      case 0x0d:
      case 0x20:
        break;
      default:
        buf[pos++] = str[i];
        break;
    }
  }
  buf[pos] = '\0';
  free(str);

  return buf;
}


int store_comment(struct p_parser *p, char *buf, int start, int end){

  struct p_comment *c;
  struct p_label *cl;
  struct p_setting *cs;
  int len;

  c = malloc(sizeof(struct p_comment));

  if (c == NULL)
    return FAIL;

  c->str  = NULL;
  c->flag = 0;

  len = end - start;
  c->str = malloc(sizeof(char)*len+1);

  if (c->str == NULL){
    free(c);
    return FAIL;
  }

  c->str = memcpy(c->str,buf+start,len);
  c->str[len] = '\0';

#ifdef DEBUG
  fprintf(stderr,"COMMENT: %s\n",c->str);
#endif
  
  if (p->lcount > 0){
    cl = p->labels[p->lcount-1];
    if (cl->scount > 0){
      cs = cl->settings[cl->scount-1];
      //store as setting comment
      cs->comments = realloc(cs->comments,sizeof(struct p_comment)*(++cs->comcount));
      cs->comments[cs->comcount-1] = c;
    } else {
      //store as label comment
      cl->comments = realloc(cl->comments,sizeof(struct p_comment)*(++cl->comcount));
      cl->comments[cl->comcount-1] = c;
    }
  } else {
    //store global comment
    p->comments = realloc(p->comments,sizeof(struct p_comment)*(++p->comcount));
    p->comments[p->comcount-1] = c;
  }

  return OKAY;
}

int store_label(struct p_parser *p, char *buf, int start, int end){
  
  struct p_label *l;
  int len;

  l = malloc(sizeof(struct p_label));

  if (l == NULL)
    return FAIL;

  l->settings = NULL;
  l->scount   = 0;
  l->str      = NULL;
  l->comments = NULL;
  l->comcount = 0;

  len = end - start;
  l->str = malloc(sizeof(char)*len+1);
  
  if (l->str == NULL){
    free(l);
    return FAIL;
  }

  l->str = memcpy(l->str,buf+start,len);
  l->str[len] = '\0';

  l->str = rm_whitespace(l->str);

#ifdef DEBUG
  fprintf(stderr,"LABEL: [%s]\n",l->str);
#endif
    
  p->labels = realloc(p->labels,sizeof(struct p_label*)*(++p->lcount));
  p->labels[p->lcount-1] = l;

  return OKAY;
}

int store_setting(struct p_parser *p, char *buf, int start, int end){
  
  struct p_label *l;
  struct p_setting *s;
  int len;

  s = malloc(sizeof(struct p_setting));

  if (s == NULL)
    return FAIL;

  s->values   = NULL;
  s->vcount   = 0;
  s->str      = NULL;
  s->comments = NULL;
  s->comcount = 0;

  len = end - start;
  s->str = malloc(sizeof(char)*len+1);

  if(s->str == NULL){
    free(s);
    return FAIL;
  }
  
  s->str = memcpy(s->str,buf+start,len);
  s->str[len] = '\0';

  s->str = rm_whitespace(s->str);

#ifdef DEBUG
  fprintf(stderr,"SETTING: {%s}\n",s->str);
#endif

  l = p->labels[p->lcount-1];
  l->settings = realloc(l->settings,sizeof(struct p_setting*)*(++l->scount));
  l->settings[l->scount-1] = s;
  
  return OKAY;
}

int store_value(struct p_parser *p, char *buf, int start, int end){
  
  struct p_label *l;
  struct p_setting *s;
  struct p_value *v;
  int len;

  v = malloc(sizeof(struct p_value));

  if (v == NULL)
    return FAIL;

  v->str = NULL;

  len = end - start;
  v->str = malloc(sizeof(char)*len+1);

  if(v->str == NULL){
    free(v);
    return FAIL;
  }

  v->str = memcpy(v->str,buf+start,len);
  v->str[len] = '\0';

  if (p->state == S_VALUE)
    v->str = rm_whitespace(v->str);

#ifdef DEBUG
  fprintf(stderr,"VALUE: (%s)\n",v->str); 
#endif

  l = p->labels[p->lcount-1];
  s = l->settings[l->scount-1];
  s->values = realloc(s->values,sizeof(struct p_values*)*(++s->vcount));
  s->values[s->vcount-1] = v;

  return OKAY;
}

int start_parser(struct p_parser *p, char *f) {
  
  FILE *file;
  int i,fd,pos;
  char c;
  char *buffer, *temp;
  struct stat file_stats;

  temp   = NULL;
  buffer = NULL;
  file   = fopen(f,"r");
  fd     = fileno(file);

  if (file == NULL){
    fprintf(stderr,"Error Reading File: %s\n",f);
    return errno;
  }
  if (stat(f,&file_stats) != 0)
    return errno;
  
  p->open_time = file_stats.st_atime;
  p->fsize     = file_stats.st_size; 

  buffer = mmap(NULL,p->fsize,PROT_READ,MAP_SHARED,fd,0);

  if (buffer == MAP_FAILED){
    fprintf(stderr,"mmap failed: %s\n",strerror(errno));
    fclose(file);
    return EIO;
  }

  fprintf(stderr,"fd: %d st_atime:%d st_size:%d mmap:%p\n",fd,(int)p->open_time,(int)p->fsize,buffer);

  p->state = S_START; 
  pos=0;

  for (i=0;i<p->fsize;i++){
    c = buffer[i];
    
    switch (p->state){
      
      case S_COMMENT:
        switch(c) {
          case '\n':
          case '\r':
            if (!store_comment(p,buffer,pos,i))
              return KATCP_RESULT_FAIL;
            p->state = S_START;
            pos = i+1;
            break;
        }
        break;

      case S_LABEL:
        switch(c){
          case CLABEL:
            if (!store_label(p,buffer,pos,i))
              return KATCP_RESULT_FAIL;
            p->state = S_START;
            pos = i;
            break;
        }
        break;

      case S_SETTING:
        if (!store_setting(p,buffer,pos,i-1))
          return KATCP_RESULT_FAIL;
        pos = i;
        p->state = S_VALUE;
        break;
      
      case S_VALUE:
        switch(c){
          case OLABEL:
            p->state = S_MLVALUE;
            pos = i;
            break;
          case VALUE:
            if (!store_value(p,buffer,pos,i))
              return KATCP_RESULT_FAIL;
            p->state = S_VALUE;
            pos = i+1;
            break;
          case '\n':
          case '\r':
            if (!store_value(p,buffer,pos,i))
              return KATCP_RESULT_FAIL;
            p->state = S_START;
            pos = i+1;
            break;
        }
        break;

      case S_MLVALUE:
          switch(c){
            case CLABEL:
              if (!store_value(p,buffer,pos,i+1))
                return KATCP_RESULT_FAIL;
              p->state = S_START;
              break;
          }
        break;

      default:
        switch (c){
          case COMMENT:
            p->state = S_COMMENT;
            pos = i;
            break;
          case OLABEL:
            p->state = S_LABEL;
            pos = i+1;
            break;
          case SETTING:
            p->state = S_SETTING;
            break;
          case '\n':
          case '\r':
            p->state = S_START;
            pos = i+1;
            break;
        }
    }
  }

  munmap(buffer,p->fsize);
  fclose(file);
  
  return EX_OK;
}

void show_tree(struct katcp_dispatch *d, struct p_parser *p){
  int i,j,k;
  
  struct p_label *cl=NULL;
  struct p_setting *cs=NULL;
  struct p_value *cv=NULL;
  
  //fprintf(stderr,"PARSER show tree\n");

#ifdef STANDALONE   
  for (j=0;j<p->comcount;j++)
    fprintf(stderr,"%s\n",p->comments[j]->str);
#endif

  for (i=0;i<p->lcount;i++){
    cl = p->labels[i];
#ifdef STANDALONE    
    for (j=0;j<cl->comcount;j++)
      fprintf(stderr,"%s\n",cl->comments[j]->str);
    fprintf(stderr,"%d[%s]\n",i,cl->str);
#endif
    for (j=0;j<cl->scount;j++){
      cs = cl->settings[j];
#ifdef STANDALONE      
      for (k=0;k<cs->comcount;k++)
        fprintf(stderr,"%s\n",cs->comments[k]->str);
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

int save_tree(struct p_parser *p,char *filename, int force){
  FILE *file;
  int i,j,k;
  struct p_label *cl=NULL;
  struct p_setting *cs=NULL;
  struct p_value *cv=NULL;
  int pid;

  struct stat file_stats;
  
  char tempname[50];

  pid=getpid();
  sprintf(tempname,"./tempconf.%d",pid);
  //fprintf(stderr,"in file writing function pid:%d %s\n",pid,tempname);

  file = fopen(tempname,"w");

  if (file == NULL){
    fprintf(stderr,"Error reading file: %s\n",strerror(errno));
    return KATCP_RESULT_FAIL;
  }
  
  if (stat(filename,&file_stats) != 0){
    fprintf(stderr,"%d %s\n",errno,strerror(errno));
    if (errno != 2){
      fclose(file);
      return KATCP_RESULT_FAIL;
    }
  }

  if (!force && strcmp(filename,p->filename) == 0 && p->open_time < file_stats.st_atime){
    fprintf(stderr,"PARSER filename: %s opentime: %ld accesstime: %ld\n",filename,p->open_time,file_stats.st_atime);
    return KATCP_RESULT_FAIL;
  }

  for (j=0;j<p->comcount;j++)
    fprintf(file,"%s\n",p->comments[j]->str);

  for (i=0;i<p->lcount;i++){
    cl = p->labels[i];

    fprintf(file,"[%s]\n",cl->str);
    for (j=0;j<cl->comcount;j++)
      fprintf(file,"%s\n",cl->comments[j]->str);
    
    for (j=0;j<cl->scount;j++){
      cs = cl->settings[j];

      fprintf(file,"  %s = ",cs->str);
      
      for (k=0;k<cs->vcount;k++){
        cv = cs->values[k];
        fprintf(file,"%s%c",cv->str,(k==cs->vcount-1)?' ':',');
      }
     
      fprintf(file,"\n");
      
      for (k=0;k<cs->comcount;k++)
        fprintf(file,"%s\n",cs->comments[k]->str);
      
    }
    fprintf(file,"\n");
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

int parser_save(struct katcp_dispatch *d, char *filename, int force){
  
  struct kcs_basic *kb;
  struct p_parser *p;
  kb = need_current_mode_katcp(d,KCS_MODE_BASIC);
  if (kb == NULL)
    return KATCP_RESULT_FAIL;

  p = kb->b_parser;

  if (p != NULL) {
    int rtn;
    if (filename == NULL && !force){
      rtn = save_tree(p,p->filename,force);
      if (rtn == KATCP_RESULT_FAIL){
        log_message_katcp(d,KATCP_LEVEL_WARN,NULL,"File has been edited behind your back!!! use ?parser save newfilename or force save ?parser forcesave");
        return KATCP_RESULT_FAIL;
      }
    }
    else if (force)
      rtn = save_tree(p,p->filename,force);
    else
      rtn = save_tree(p,filename,force);

    if (rtn == KATCP_RESULT_OK){
      log_message_katcp(d,KATCP_LEVEL_INFO,NULL,"Saved configuration file as %s",(filename == NULL)?p->filename:filename);
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
  p->lcount   = 0;
  p->labels   = NULL;
  p->comments = NULL;
  p->comcount = 0;
  p->fsize    = 0;
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
  //parser_save(NULL,"oldtest");
  parser_list(NULL);

  parser_destroy(NULL);
  
  free(tkb);

  return EX_OK;
}
#endif
