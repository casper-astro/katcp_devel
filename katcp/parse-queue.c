#ifdef KATCP_EXPERIMENTAL

#include <katcl.h>
#include <katpriv.h>

void void_destroy_parse_katcl(void *v)
{
  struct katcl_parse *px;

  px = v;

  destroy_parse_katcl(px);
}

struct katcl_gueue *create_parse_gueue_katcl()
{
  return create_gueue_katcl(&void_destroy_parse_katcl);
}

#endif
