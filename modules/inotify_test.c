#define _XOPEN_SOURCE 500
#include <ftw.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>


#if 0
#include <katcp.h>
#endif 

#define BUFSIZE 1024

static int see_file(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
  int i;
  char *buffer, *token, *delims = "/ []()-_.&#@!%^*+=";

  if (tflag == FTW_F){
#if 0
    def DEBUG
    for (i=0; i<ftwbuf->level; i++)
      fprintf(stderr, "\t");
#endif
#ifdef DEBUG
    fprintf(stderr, "[%d] file: %s\n", ftwbuf->level, fpath );//+ ftwbuf->base);
#endif
      
    
    buffer = strdup(fpath);

    for(i=0; (token = strtok((i>0)?NULL:buffer, delims)) != NULL; i++){
#ifdef DEBUG
      fprintf(stderr, "<%s> ", token);
#endif
    }

    
    free(buffer);

#ifdef DEBUG
    fprintf(stderr,"\n\n");
#endif
  } 
#if 0
  else if (tflag == FTW_DP || tflag == FTW_D){
#ifdef DEBUG
    fprintf(stderr, "\n");
    for (i=0; i<ftwbuf->level; i++)
      fprintf(stderr, "\t");
#endif
#ifdef DEBUG
    fprintf(stderr, "[%d] dir: %s\n", ftwbuf->level, fpath);
#endif
  }
#endif

  return 0;
}

int setup_watches(int fd, char *basedir, uint32_t mask)
{
  
  if (nftw(basedir, &see_file, BUFSIZE, FTW_PHYS) == -1){
#ifdef DEBUG
    fprintf(stderr, "error: nftw\n");
#endif
    return -1;
  }
  
  return -1;
}

int main(int argc, char *argv[])
{
  char                    *basedir;
  int                     fd, rtn, run, len, i;
  uint32_t                mask;
  unsigned char           buf[BUFSIZE];
  struct inotify_event    *e;
  uint32_t                bits[] = {IN_ACCESS, IN_ATTRIB, IN_CLOSE_WRITE,  IN_CLOSE_NOWRITE, IN_CREATE,  IN_DELETE,  IN_DELETE_SELF,  IN_MODIFY,  IN_MOVE_SELF,  IN_MOVED_FROM,  IN_MOVED_TO,  IN_OPEN};

  mask =  IN_ACCESS | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO | IN_OPEN;

  rtn     = 0;
  //basedir = "/srv/temp/Build.it.Bigger.S05E07.Building.Mumbais.Modern.Airport.720p.HDTV.x264-MOMENTUM";
  //basedir = "/home/adam/Music";
  basedir = "/srv/temp";
  run     = 1;
  e       = NULL;
  len     = sizeof(bits) / sizeof(uint32_t);

  fd = inotify_init();
  if (fd < 0){
#ifdef DEBUG
    fprintf(stderr, "error: %s\n", strerror(errno));
#endif
    return 1;
  }
  
  if (setup_watches(fd, basedir, mask) < 0){
    close(fd);
    return 1;
  }

#if 0
  rtn = inotify_add_watch(fd, basedir, mask);
  if (rtn < 0){
#ifdef DEBUG
    fprintf(stderr, "error: %s\n", strerror(errno));
#endif
    close(fd);
    return 1;
  }
#endif

#ifdef DEBUG
  fprintf(stderr, "fd:%d rtn:%d len:%d\n", fd, rtn, len);
#endif
  
  do {

    rtn = read(fd, buf, BUFSIZE);
    if (rtn < 0){
#ifdef DEBUG
      fprintf(stderr, "read error: %s\n", strerror(errno));
#endif
    } else if (rtn == 0){
#ifdef DEBUG
      fprintf(stderr, "read EOF... ending\n");
#endif
      run = 0;
    } else {
      
      e = (struct inotify_event *) buf;
      if (e != NULL){
#ifdef DEBUG
        fprintf(stderr, "event on:[%d] name[len:%d]:%s\n", e->wd, e->len, (e->len > 0)?e->name:"<unknown>");
#endif
        
        for (i=0; i<len; i++){
          switch(bits[i] & e->mask){
            case IN_ACCESS:
#ifdef DEBUG
              fprintf(stderr, "\tFile was accessed\n");
#endif
              break;
            case IN_ATTRIB:
#ifdef DEBUG
              fprintf(stderr, "\tMetadata changed\n");
#endif
              break;
            case IN_CLOSE_WRITE:
#ifdef DEBUG
              fprintf(stderr, "\tFile opened for writing was closed\n");
#endif
              break;
            case IN_CLOSE_NOWRITE:
#ifdef DEBUG
              fprintf(stderr, "\tFile not opened for writing was closed\n");
#endif
              break;
            case IN_CREATE:
#ifdef DEBUG
              fprintf(stderr, "\tFile created\n");
#endif
              break;
            case IN_DELETE:
#ifdef DEBUG
              fprintf(stderr, "\tFile deleted\n");
#endif
              break;
            case IN_DELETE_SELF: 
#ifdef DEBUG
              fprintf(stderr, "\tWatch deleted\n");
#endif
              break;
            case IN_MODIFY:
#ifdef DEBUG
              fprintf(stderr, "\tFile modified\n");
#endif
              break;
            case IN_MOVE_SELF:
#ifdef DEBUG
              fprintf(stderr, "\tWatch modified\n");
#endif
              break;
            case IN_MOVED_FROM:
#ifdef DEBUG
              fprintf(stderr, "\tMoved out\n");
#endif
              break;
            case IN_MOVED_TO:
#ifdef DEBUG
              fprintf(stderr, "\tMoved in\n");
#endif
              break;
            case IN_OPEN:
#ifdef DEBUG
              fprintf(stderr, "\tFile opened\n");
#endif
              break;
          }


        }


      }

    }

  } while (run);

  
  
  
  close(fd);

  return 0;
}
