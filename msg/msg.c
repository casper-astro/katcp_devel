#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <time.h>

#include <katcp.h>
#include <katcl.h>

#define DEFAULT_LEVEL "info"

void usage(char *app)
{
  printf("usage: %s [options] message ...\n", app);
  printf("-h                 this help\n");
  printf("-l level           specify level (default is %s)\n", DEFAULT_LEVEL);
  printf("-m                 enable multiple messages\n");
  printf("-s subsystem       specify the subsystem\n");
}

int main(int argc, char **argv)
{
  int i, j, c, extra, pos, size;
  int multi, level, result;
  char *system, *tmp, *buffer;
  struct katcl_line *k;
  char *app;

  multi = 0;
  i = j = 1;
  level = KATCP_LEVEL_INFO;
  system = "notifier";
  buffer = NULL;
  size = 0;
  pos = 0;

  app = argv[0];

  k = create_katcl(STDOUT_FILENO);
  if(k == NULL){
    fprintf(stderr, "%s: unable to create katcp message logic\n", app);
    return EX_OSERR;
  }

  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(app);
          return 0;

        case 'm' : 
          multi = 1;
          j++;
          break;

        case 'l' :
        case 's' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }

          if (i >= argc) {
            sync_message_katcl(k, KATCP_LEVEL_ERROR, system, "option -%c needs a parameter");
            return EX_USAGE;;
          }

          switch(c){
            case 's' :
              system = argv[i] + j;
              break;
            case 'l' :
              tmp = argv[i] + j;
              level = log_to_code_katcl(tmp);
              if(level < 0){
                sync_message_katcl(k, KATCP_LEVEL_ERROR, system, "unknown log level %s", tmp);
                level = KATCP_LEVEL_ERROR;
              }
              break;
          }

          i++;
          j = 1;
          break;

        case '-' :
          j++;
          break;
        case '\0':
          j = 1;
          i++;
          break;
        default:
          fprintf(stderr, "%s: unknown option -%c\n", app, argv[i][j]);
          return 2;
      }
    } else {
      if(multi){
        log_message_katcl(k, level, system, "%s", argv[i]);
      } else {
        extra = strlen(argv[i]);
        size += extra + 1;
        tmp = realloc(buffer, size);
        if(tmp == NULL){
          return EX_OSERR;
        }
        buffer = tmp;
        if(pos > 0){
          buffer[pos++] = ' ';
        }
        strcpy(buffer + pos, argv[i]);
        pos += extra;
      } 
      i++;
    }
  }

  if(buffer){
    log_message_katcl(k, level, system, "%s", buffer);
    free(buffer);
  }

  while((result = write_katcl(k)) == 0);

  destroy_katcl(k, 0);

  return (result < 0) ? EX_OSERR : EX_OK;
}
