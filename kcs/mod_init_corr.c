/***
  init statemachine states
  to be compiled with 
***/
  
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <katcp.h>
#include "kcs.h"
#include "avl_tree.h"

#if 0
const char *sym_list_mod[] = {
  "get_xports_mod",
  "count_xports_mod",
  "poweron_xports_mod",
  "count_watch_announce_mod",
  NULL
};
#endif


int get_xports_mod(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  fprintf(stderr, "mod: in get_xports_mod() with %d\n", param);
  return 0;
}

int count_xports_mod(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  fprintf(stderr, "mod: in count_xports_mod() with %d\n", param);
  return 0;
}

int poweron_xports_mod(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  fprintf(stderr, "mod: in poweron_xports_mod() with %d\n", param);
  return 0;
}

int count_watch_announce_mod(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  fprintf(stderr, "mod: in count_watch_xports_mod() with %d\n", param);
  return 0;
}

int init_mod(struct katcp_dispatch *d)
{
  struct avl_tree *t;
  struct avl_node *n;
  struct kcs_mod_store *ms;

  int (*funcs)(struct katcp_dispatch *, struct katcp_notice *, void *)[] = {
    &get_xports_mod,
    &count_xports_mod,
    &poweron_xports_mod,
    &count_watch_announce_mod,
    NULL
  };
  const char *sym_list_mod[] = {
    "get_xports_mod",
    "count_xports_mod",
    "poweron_xports_mod",
    "count_watch_announce_mod",
    NULL
  };

  t = get_datastore_tree_kcs(d);
  if (t == NULL)
    return -1;
  
  n = find_name_node_avltree(t, MOD_STORE);
  if (n == NULL)
    return -1;

  ms = get_node_data_avltree(n);
  if (ms == NULL)
    return -1;

 /* 
  n = create_node_avltree(*sl, call);

  if (add_node_avltree(t, n) < 0){
#ifdef DEBUG
    fprintf(stderr, "statemachine: could not add: %s to tree\n", get_node_name_avltree(n));
#endif
    free_node_avltree(n);
    return KATCP_RESULT_FAIL;
  }

  if (add_node_mod_store_kcs(ms, n, MOD_STORE_TYPE_SYMBOL) < 0){
#ifdef DEBUG
    fprintf(stderr, "statemachine: could not add: %s to mod store symbol list\n", get_node_name_avltree(n));
#endif
    return KATCP_RESULT_FAIL;
  }
*/


  

  return 0;
}

