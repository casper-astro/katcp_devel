/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "katcp.h"
#include "katcl.h"
#include "katpriv.h"
#include "netc.h"

int startup_services_katcp(struct katcp_dispatch *d)
{
  int rtn;

  rtn = 0;

#ifdef KATCP_DEPRECATED
  rtn += register_name_type_katcp(d, KATCP_TYPE_STRING, KATCP_DEP_BASE, &print_string_type_katcp, &destroy_string_type_katcp, NULL, NULL, &parse_string_type_katcp, NULL);
  
  rtn += register_name_type_katcp(d, KATCP_TYPE_DBASE, KATCP_DEP_BASE, &print_dbase_type_katcp, &destroy_dbase_type_katcp, NULL, NULL, &parse_dbase_type_katcp, &getkey_dbase_type_katcp);
  
  rtn += register_name_type_katcp(d, KATCP_TYPE_DICT, KATCP_DEP_BASE, &print_dict_type_katcp, &destroy_dict_type_katcp, NULL, NULL, &parse_dict_type_katcp, NULL);

  rtn += register_name_type_katcp(d, KATCP_TYPE_TAG, KATCP_DEP_BASE, &print_tag_katcp, &destroy_tag_katcp, NULL, &compare_tag_katcp, &parse_tag_katcp, &getkey_tag_katcp);

  rtn += register_katcp(d, KATCP_DICT_REQUEST, "dict [key] {key0:value0,key1:value1,...}", &dict_cmd_katcp);

  rtn += register_katcp(d, KATCP_GET_REQUEST, "get [key] (n) from database (n optional)", &get_dbase_cmd_katcp);
  rtn += register_katcp(d, KATCP_SET_REQUEST, "set [key] [value value ...] to database", &set_dbase_cmd_katcp);

  rtn += register_katcp(d, KATCP_SEARCH_REQUEST, "search [tag1 tag2 ...]", &search_cmd_katcp);
#endif

  return rtn;
}

