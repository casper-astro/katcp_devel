/***
  init statemachine states
  to be compiled with 
***/
  
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *sym_list_mod[] = {
  "get_xports_mod",
  "count_xports_mod",
  "poweron_xports_mod",
  "count_watch_announce_mod",
  NULL
};

int get_xports_mod(int param)
{
  fprintf(stderr, "mod: in get_xports_mod() with %d\n", param);
  return 0;
}

int count_xports_mod(int param)
{
  fprintf(stderr, "mod: in count_xports_mod() with %d\n", param);
  return 0;
}

int poweron_xports_mod(int param)
{
  fprintf(stderr, "mod: in poweron_xports_mod() with %d\n", param);
  return 0;
}

int count_watch_announce_mod(int param)
{
  fprintf(stderr, "mod: in count_watch_xports_mod() with %d\n", param);
  return 0;
}
