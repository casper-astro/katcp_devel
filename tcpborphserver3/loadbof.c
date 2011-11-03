#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <katcp.h>
#include <avltree.h>

#include "tcpborphserver3.h"
#include "bof.h"
#include "loadbof.h"


struct bof_state
{
  int b_fd;
  int b_xinu;
  unsigned long b_file_size;

  unsigned long b_bit_offset;
  unsigned long b_bit_size;

  unsigned long b_str_offset;
  unsigned long b_str_size;

  unsigned long b_hwr_offset;
  unsigned long b_reg_count;

  char *b_strings;
};

/*************************************************************************/

#define flip16(a)     ((0xff & ((a) >> 8)) | (0xff00 & ((a) << 8)))
#define flip32(a)     ((0xff & ((a) >> 24)) | (0xff00 & ((a) >> 8)) | (0xff0000 & ((a) << 8)) | (0xff000000 & ((a) << 24)))

void flip_ioreg_bof(struct bofioreg *br)
{
  br->name = flip16(br->name);
  br->mode = flip16(br->mode);
  br->loc  = flip32(br->loc);
  br->len  = flip32(br->len);
}

void flip_hwrhdr_bof(struct hwrhdr *hh)
{
  hh->flag       = flip32(hh->flag);
  hh->addr.class = flip16(hh->addr.class);
  hh->addr.addr  = flip16(hh->addr.addr);
  hh->pl_off     = flip32(hh->pl_off);
  hh->pl_len     = flip32(hh->pl_len);
  hh->nr_symbol  = flip32(hh->nr_symbol);
  hh->strtab_off = flip32(hh->strtab_off);
  hh->next_hwr   = flip32(hh->next_hwr);
}

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

/*************************************************************************/

int check_ioreg_bof(struct katcp_dispatch *d, struct bof_state *bs, struct bofioreg *br)
{
  if(bs->b_xinu){
    flip_ioreg_bof(br);
  }

  if(br->name >= bs->b_str_size){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register name supposedly at location %d which is outside string table", br->name);
    return -1;
  }

  return 0;
}

int check_hwrhdr_bof(struct katcp_dispatch *d, struct bof_state *bs, struct hwrhdr *hh)
{

  if(bs->b_xinu){
    flip_hwrhdr_bof(hh);
  }

  bs->b_bit_offset = hh->pl_off;
  bs->b_bit_size = hh->pl_len;

  bs->b_reg_count = hh->nr_symbol;

  bs->b_str_offset = hh->strtab_off;
  if(bs->b_str_offset >= bs->b_bit_offset){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "string table at 0x%x not before bit data at 0x%x", bs->b_str_offset, bs->b_bit_offset);
    return -1;
  }
  bs->b_str_size = bs->b_bit_offset - bs->b_str_offset;

  if(bs->b_str_size < (bs->b_reg_count * 2)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "string table unreasonably small with %lu bytes, expected at least %lu", bs->b_str_size, bs->b_reg_count);
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "bit data at 0x%x of length %d", bs->b_bit_offset, bs->b_bit_size);

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "register data at 0x%x of count %d", bs->b_hwr_offset + sizeof(struct hwrhdr), bs->b_reg_count);

  return 0;
}

int check_bofhdr_bof(struct katcp_dispatch *d, struct bof_state *bs, struct bofhdr *bh)
{
  char magic[4] = { 0x19, 'B', 'O', 'F' };
  uint32_t check;
  int disk, memory;

  if(memcmp(magic, bh->ident, 4)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "bad magic in file bof header");
    return -1;
  }

  disk = bh->ident[BI_ENDIAN];

  switch(disk){
    case BOFDATA2LSB :
    case BOFDATA2MSB :
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown endianess of bof file");
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
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "running on an alien architecture. word codes as 0x%x", check);
      return -1;
  }

  if(disk != memory){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "data is in xinu format, need to exchange bytes");
    bs->b_xinu = 1;
    flip_bofhdr_bof(bh);
  } else {
    bs->b_xinu = 0;
  }

  bs->b_hwr_offset = bh->b_hwoff;

  if(bs->b_hwr_offset >= bs->b_file_size){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unreasonably large hardware offset at 0x%x compared to file size %ld", bs->b_hwr_offset, bs->b_file_size);
    return -1;
  }

  return 0;
}

/**************************************************************************/

void close_bof(struct katcp_dispatch *d, struct bof_state *bs)
{
  if(bs == NULL){
    return;
  }

  if(bs->b_fd >= 0){
    close(bs->b_fd);
    bs->b_fd = (-1);
  }

  bs->b_xinu = 0;
  bs->b_file_size = 0;

  bs->b_bit_offset = 0;
  bs->b_bit_size = 0;

  bs->b_str_offset = 0;
  bs->b_str_size = 0;

  bs->b_hwr_offset = 0;
  bs->b_reg_count = 0;

  if(bs->b_strings){
    free(bs->b_strings);
    bs->b_strings = NULL;
  }

  free(bs);
}

struct bof_state *open_bof(struct katcp_dispatch *d, char *name)
{
  struct bof_state *bs;
  int rr, have;
  struct stat st;
  struct bofhdr bh;
  struct hwrhdr hh;

  bs = malloc(sizeof(struct bof_state));
  if(bs == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate state of %d bytes", sizeof(struct bof_state));
    return NULL;
  }

  bs->b_fd = (-1);

  bs->b_xinu = 0;
  bs->b_file_size = 0;

  bs->b_bit_offset = 0;
  bs->b_bit_size = 0;

  bs->b_str_offset = 0;
  bs->b_str_size = 0;

  bs->b_hwr_offset = 0;
  bs->b_reg_count = 0;

  bs->b_strings = NULL;

  bs->b_fd = open(name, O_RDONLY);
  if(bs->b_fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to open boffile %s: %s", name, strerror(errno));
    close_bof(d, bs);
    return NULL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "opened boffile %s", name);

  if(fstat(bs->b_fd, &st) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to stat file %s: %s", name, strerror(errno));
    close_bof(d, bs);
    return NULL;
  }

  bs->b_file_size = st.st_size;

  rr = read(bs->b_fd, &bh, sizeof(struct bofhdr));
  if(rr != sizeof(struct bofhdr)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to read header of %d bytes", sizeof(struct bofhdr));
    close_bof(d, bs);
    return NULL;
  }

  if(check_bofhdr_bof(d, bs, &bh) < 0){
    close_bof(d, bs);
    return NULL;
  }

  if(lseek(bs->b_fd, bs->b_hwr_offset, SEEK_SET) != bs->b_hwr_offset){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to seek to gateware header at location 0x%lx", bs->b_hwr_offset);
    close_bof(d, bs);
    return NULL;
  }

  rr = read(bs->b_fd, &hh, sizeof(struct hwrhdr));
  if(rr != sizeof(struct hwrhdr)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to read gateware header of %d bytes", sizeof(struct hwrhdr));
    close_bof(d, bs);
    return NULL;
  }

  if(check_hwrhdr_bof(d, bs, &hh)){
    close_bof(d, bs);
    return NULL;
  }

  bs->b_strings = malloc(bs->b_str_size + 1);
  if(bs->b_strings == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %lu bytes for string table", bs->b_str_size);
    close_bof(d, bs);
    return NULL;
  }

  if(lseek(bs->b_fd, bs->b_str_offset, SEEK_SET) != bs->b_str_offset){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to seek to string table location at 0x%lx", bs->b_str_offset);
    close_bof(d, bs);
    return NULL;
  }

  have = 0;
  do {
    rr = read(bs->b_fd, bs->b_strings + have, bs->b_str_size - have);
    switch(rr){
      case -1 : 
        switch(errno){
          case EAGAIN :
          case EINTR : 
            break;
          default :
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "string table read failed: %s", strerror(errno));
            close_bof(d, bs);
            return NULL;
        }
        break;
      case  0 :
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "encountered end of file while reading string table");
        close_bof(d, bs);
        return NULL;
      default : 
        have += rr;
        break;
    }
  } while(have < bs->b_str_size);
            
  bs->b_strings[bs->b_str_size] = '\0';

  return bs;
}

int program_bof(struct katcp_dispatch *d, struct bof_state *bs, char *device)
{
#define BUFFER 4096
  int dfd, rr, wr, can, need, have;
  char buffer[BUFFER];

  if(lseek(bs->b_fd, bs->b_bit_offset, SEEK_SET) != (bs->b_bit_offset)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "seek to bitstream start at 0x%lx failed", bs->b_bit_offset);
    return -1;
  }

#ifdef __PPC__
  dfd = open(device, O_WRONLY);
#else
  /* for debugging, simply write out the bitstream */
  dfd = open(device, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
#endif
  if(dfd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to open device %s: %s", device, strerror(errno));
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "attempting to program bitstream of %u bytes to device %s", bs->b_bit_size, device);

  need = bs->b_bit_size;
  do{
    can = (need > BUFFER) ? BUFFER : need;
    rr = read(bs->b_fd, buffer, can);
    switch(rr){
      case -1 :
        switch(errno){
          case EAGAIN :
          case EINTR  :
            continue; /* WARNING */
          default :
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "read from bof file failed: %s", strerror(errno));
            close(dfd);
            return -1;
        }
        break;
      case  0 :
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "encountered EOF in bitstream with %d bytes still to load", need);
        close(dfd);
        return -1;
      default : 
        need -= rr;
        have = 0;
        do{
          wr = write(dfd, buffer + have, rr - have);
          switch(wr){
            case -1 :
              switch(errno){
                case EAGAIN :
                case EINTR  :
                  break;
                default :
                  log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "write to fpga failed: %s", strerror(errno));
                  close(dfd);
                  return -1;
              }
              break;
            case 0 :
              log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "write to fpga failed: %s", strerror(errno));
              close(dfd);
              return -1;
            default : 
              have += wr;
              break;
          }
        } while(have < rr);
        break;
    }
  } while(need > 0);

  if(close(dfd) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to program fpga with %d bytes", need);
    return -1;
  }

  return 0;
#undef BUFFER
}

int index_bof(struct katcp_dispatch *d, struct bof_state *bs)
{
  int rr;
  struct bofioreg br;
  unsigned int i;
  /* WARNING: no longer a generic program, depends on *_raw */
  struct tbs_raw *tr;
  struct tbs_entry *te;
  unsigned int top;
  char *name;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(lseek(bs->b_fd, bs->b_hwr_offset + sizeof(struct hwrhdr), SEEK_SET) != (bs->b_hwr_offset + sizeof(struct hwrhdr))){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "seek to register index at 0x%lx failed", bs->b_hwr_offset + sizeof(struct hwrhdr));
    return -1;
  }

  for(i = 0; i < bs->b_reg_count; i++){
    rr = read(bs->b_fd, &br, sizeof(struct bofioreg));
    if(rr < sizeof(struct bofioreg)){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to read register descriptor structure number %u from disk: %s", i, (rr < 0) ? strerror(errno) : "incomplete read");
      return -1;
    }

    if(check_ioreg_bof(d, bs, &br) < 0){
      return -1;
    }

    if(find_data_avltree(tr->r_registers, bs->b_strings + br.name)){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register called %s already defined", bs->b_strings + br.name);
      return -1;
    }

    te = malloc(sizeof(struct tbs_entry));
    if(te == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes for register entry %u", sizeof(struct tbs_entry), i);
      return -1;
    }

    name = bs->b_strings + br.name;

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "found register name (%s) at 0x%x, fpga location 0x%x, size %d, mode %d", name, br.name, br.loc, br.len, br.mode);

    te->e_pos_base = br.loc;
    te->e_pos_offset = 0;

    te->e_len_base = br.len;
    te->e_len_offset = 0;

    top = br.loc + br.len;
    if(tr->r_top_register < top){
      tr->r_top_register = top;
    }

    switch(br.mode){
      case IORM_READ : 
        te->e_mode = TBS_READABLE;
        break;
      case IORM_WRITE : 
        te->e_mode = TBS_WRITABLE;
        break;
      case IORM_READWRITE : 
        te->e_mode = TBS_WRABLE;
        break;
      default :
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unsupported access mode %d for register %s", br.mode, name);
        te->e_mode = 0;
        break;
    }

    if(store_named_node_avltree(tr->r_registers, name, te) < 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to store definition of register %s", name);
      free(te);
      return -1;
    }
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "address range needs to be at least %u", tr->r_top_register);

  return 0;
}

#if 0
/* too many dependencies: supposes an allocated tbs structure */
#include <sysexits.h>

int main(int argc, char **argv)
{
  struct bof_state *bs;

  if(argc <= 1){
    fprintf(stderr, "usage: %s bofile\n", argv[0]);
    return EX_USAGE;
  }

  bs = open_bof(NULL, argv[1]);
  if(bs == NULL){
    return EX_OSERR;
  }

  close_bof(NULL, bs);

  return EX_OK;
}
#endif
