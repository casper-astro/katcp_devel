#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "bof.h"

struct bof_state
{
  int b_fd;
  int b_xinu;
  unsigned long b_size;
};

#define flip16(a)     ((0xff & ((a) >> 8)) | (0xff00 & ((a) << 8)))
#define flip32(a)     ((0xff & ((a) >> 24)) | (0xff00 & ((a) >> 8)) | (0xff0000 & ((a) << 8)) | (0xff000000 & ((a) << 24)))

void flip_bofhdr_bof(struct bofhdr *bh)
{
  bh->b_version    = flip32(bh->b_version);
  bh->b_machine    = flip16(bh->b_machine);
  bh->b_elfmachine = flip16(bh->b_elfmachine);
  bh->b_numchip    = flip32(bh->b_numchip);
  bh->b_elfoff     = flip32(bh->b_elfoff);
  bh->b_hwoff      = flip32(bh->b_hwoff);
  bh->b_ekver      = flip32(bh->b_ekver);
}

int check_header_bof(struct bof_state *bs, struct bofhdr *bh)
{
  char magic[4] = { 0x19, 'B', 'O', 'F' };
  uint32_t check;
  int disk, memory;

  if(memcmp(magic, bh->ident, 4)){
#ifdef DEBUG
    fprintf(stderr, "bof: bad magic in bof header\n");
#endif
    return -1;
  }

  disk = bh->ident[BI_ENDIAN];

  switch(disk){
    case BOFDATA2LSB :
    case BOFDATA2MSB :
      break;
    default :
#ifdef DEBUG
      fprintf(stderr, "bof: unable to handle disk data format %d\n", disk);
#endif
      return -1;
  }

  memcpy(&check, "word", 4);
  switch(check){
    case 0x776f7264 :
      memory = BOFDATA2MSB;
      break;
    case 0x64726f77 :
      memory = BOFDATA2LSB;
      break;
    default :
#ifdef DEBUG
      fprintf(stderr, "bof: running on an alien architecture. word is 0x%x\n", check);
#endif
      return -1;
  }

  if(disk != memory){
#ifdef DEBUG
    fprintf(stderr, "bof: data is in xinu format, need to exchange bytes\n");
#endif
    bs->b_xinu = 1;
    flip_bofhdr_bof(bh);
  } else {
    bs->b_xinu = 0;
  }

  return 0;
}

void close_bof(struct bof_state *bs)
{
  if(bs == NULL){
    return;
  }

  if(bs->b_fd >= 0){
    close(bs->b_fd);
    bs->b_fd = (-1);
  }

  bs->b_xinu = 0;
  bs->b_size = 0;

  free(bs);
}

struct bof_state *open_bof(char *name)
{
  struct bof_state *bs;
  int rr;
  struct stat st;
  struct bofhdr bh;

  bs = malloc(sizeof(struct bof_state));
  if(bs == NULL){
    return NULL;
  }

  bs->b_fd = (-1);
  bs->b_xinu = 0;
  bs->b_size = 0;

  bs->b_fd = open(name, O_RDONLY);
  if(bs->b_fd < 0){
    close_bof(bs);
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "bof: openend boffile %s\n", name);
#endif

  if(fstat(bs->b_fd, &st) < 0){
#ifdef DEBUG
  fprintf(stderr, "bof: unable to stat %s\n", name);
#endif
    close_bof(bs);
    return NULL;
  }

  bs->b_size = st.st_size;

  rr = read(bs->b_fd, &bh, sizeof(struct bofhdr));

  if(rr != sizeof(struct bofhdr)){
#ifdef DEBUG
  fprintf(stderr, "bof: unable to read header of %d bytes\n", sizeof(struct bofhdr));
#endif
    close_bof(bs);
    return NULL;
  }

  if(check_header_bof(bs, &bh) < 0){
    close_bof(bs);
    return NULL;
  }

#ifdef DEBUG
  fprintf(stderr, "bof: hwofset is 0x%x, elfoff is 0x%x\n", bh.b_hwoff, bh.b_elfoff);
#endif

  return bs;
}

#ifdef STANDALONE 
#include <sysexits.h>

int main(int argc, char **argv)
{
  struct bof_state *bs;

  if(argc <= 1){
    fprintf(stderr, "usage: %s bofile\n", argv[0]);
    return EX_USAGE;
  }

  bs = open_bof(argv[1]);
  if(bs == NULL){
    return EX_OSERR;
  }

  close_bof(bs);

  return EX_OK;
}
#endif
