#ifndef TG_H_
#define TG_H_

int tap_stop_cmd(struct katcp_dispatch *d, int argc);
int tap_start_cmd(struct katcp_dispatch *d, int argc);
int tap_info_cmd(struct katcp_dispatch *d, int argc);

#endif
