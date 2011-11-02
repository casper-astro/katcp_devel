#ifndef LOAD_BOF_H_
#define LOAD_BOF_H_

struct bof_state;

struct bof_state *open_bof(struct katcp_dispatch *d, char *name);
void close_bof(struct katcp_dispatch *d, struct bof_state *bs);

int program_bof(struct katcp_dispatch *d, struct bof_state *bs, char *device);
int index_bof(struct katcp_dispatch *d, struct bof_state *bs);

#endif
