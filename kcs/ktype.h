#ifndef _TYPE_TREES_H_
#define _TYPE_TREES_H_

#include <katcp.h>
#include <katpriv.h>

int register_type_katcp(struct katcp_dispatch *d, char *name, int (*fn_print)(struct katcp_dispatch *, void *), void (*fn_free)(void *));
int deregister_type_katcp(struct katcp_dispatch *d, char *name);
void destroy_type_katcp(struct katcp_type *t);
void destroy_type_list_katcp(struct katcp_dispatch *d);

#endif

