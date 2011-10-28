
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>

#include <sys/mman.h>

#include <katcp.h>
#include <avltree.h>

#include "tcpborphserver3.h"
#include "loadbof.h"

/*********************************************************************/

int map_raw_tbs(struct katcp_dispatch *d);
int unmap_raw_tbs(struct katcp_dispatch *d);

/*********************************************************************/

void free_entry(void *data)
{
  struct tbs_entry *te;

  te = data;

  /* TODO: maybe add magic field to check we are deleting the correct thing */
  if(te){
    free(te);
  }
}

/*********************************************************************/

int word_write_cmd(struct katcp_dispatch *d, int argc)
{
  struct tbs_raw *tr;
  struct tbs_entry *te;

  unsigned int i, start, shift, j;
  uint32_t value, prev, update, current;
  char *name;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire raw mode state");
    return KATCP_RESULT_FAIL;
  }

  if(tr->r_fpga != TBS_FPGA_MAPPED){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga not programmed");
    return KATCP_RESULT_FAIL;
  }

  if(argc <= 3){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a register to read, followed by offset and one or more values");
    return KATCP_RESULT_INVALID;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register name inaccessible");
    return KATCP_RESULT_FAIL;
  }

  te = find_data_avltree(tr->r_registers, name);
  if(te == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s not defined", name);
    return KATCP_RESULT_FAIL;
  }

  if(!(te->e_mode & TBS_WRITABLE)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s is not marked readable", name);
    return KATCP_RESULT_FAIL;
  }

  start = arg_unsigned_long_katcp(d, 2);
  start *= 4;

  if(te->e_len_base < start + ((argc - 2) * 4) ){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "write offset %u + %d words overruns register %s of size %u.%u", start, argc - 2, name, te->e_len_base, te->e_len_offset);
    return KATCP_RESULT_FAIL;
  }

  shift = te->e_pos_offset;
  j = te->e_pos_base + start;
  if(shift > 0){
    current = *((uint32_t *)(tr->r_map + j));
    prev = current & (0xffffffff << (32 - shift));
  } else {
    prev = 0;
  }

  for(i = 3; i < argc; i++){
    if(arg_null_katcp(d, i)){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "parameter %u is null", i);
      return KATCP_RESULT_FAIL;
    }

    value = arg_unsigned_long_katcp(d, i);
    update = prev | (value >> shift);

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing 0x%x to position 0x%x", update, j);
    *((uint32_t *)(tr->r_map + j)) = update;

    prev = value << (32 - shift);
    j += 4;
  }

  if(shift > 0){
    current = (*((uint32_t *)(tr->r_map + j))) & (0xffffffff >> shift);
    update = prev | current;
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing final, partial 0x%x to position 0x%x", update, j);
    *((uint32_t *)(tr->r_map + j)) = update;
  }

  return KATCP_RESULT_OK;
}

int word_read_cmd(struct katcp_dispatch *d, int argc)
{
  struct tbs_raw *tr;
  struct tbs_entry *te;
  char *name;
  uint32_t value, prev, current;
  unsigned int length, start, i, j, shift, flags;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(tr->r_fpga != TBS_FPGA_MAPPED){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga not programmed");
    return KATCP_RESULT_FAIL;
  }

  if(argc <= 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a register to read, followed by optional offset and count");
    return KATCP_RESULT_INVALID;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register name inaccessible");
    return KATCP_RESULT_FAIL;
  }

  te = find_data_avltree(tr->r_registers, name);
  if(te == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s not defined", name);
    return KATCP_RESULT_FAIL;
  }

  if(!(te->e_mode & TBS_READABLE)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s is not marked readable", name);
    return KATCP_RESULT_FAIL;
  }

  start = 0;
  if(argc > 2){
    start = arg_unsigned_long_katcp(d, 2);
  }

  length = 1;
  if(argc > 3){
    length = arg_unsigned_long_katcp(d, 3);
  }

  if((te->e_pos_base + ((start + length) * 4)) >= tr->r_map_size){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s is outside mapped range", name);
    return KATCP_RESULT_FAIL;
  }

  if(((start + length) * 4) > te->e_len_base){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "read request @%u+%u extends beyond end of register %s", start * 4, length * 4, name);
    return KATCP_RESULT_FAIL;
  }

  if(length <= 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "zero read request length on register %s", name);
    return KATCP_RESULT_FAIL;
  }

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  flags = KATCP_FLAG_XLONG;

  j = te->e_pos_base + (start * 4);
  shift = te->e_pos_offset;

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "attempting to read %d words from fpga at 0x%x", length, j);

  /* WARNING: scary logic, attempts to support reading of non-word, non-byte aligned registers, but in word amounts (!) */

  if(shift > 0){
    current = *((uint32_t *)(tr->r_map + j));
    prev = (current << shift);
    j += 4;
  } else {
    shift = 32;
    prev = 0;
  }

  for(i = 0; i < length; i++){
    current = *((uint32_t *)(tr->r_map + j));
    /* WARNING: masking would be wise here, just in case sign extension happens */
    value = (current >> (32 - shift)) | prev;

    prev = (current << shift);
    j += 4;
    if(i + 1 >= length){
      flags |= KATCP_FLAG_LAST;
    }

    append_hex_long_katcp(d, flags, value);
  }

#if 0
  value = *((uint32_t *)(tr->r_map + te->e_pos_base));
  append_hex_long_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_XLONG, value);
#endif

  return KATCP_RESULT_OWN;
}

int progdev_cmd(struct katcp_dispatch *d, int argc)
{
  char *file;
  struct bof_state *bs;
  struct tbs_raw *tr;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to acquire state");
    return KATCP_RESULT_FAIL;
  }

  if(argc <= 1){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "attempting to empty fpga");

    if(tr->r_fpga == TBS_FPGA_MAPPED){
      unmap_raw_tbs(d);
      tr->r_fpga = TBS_FPGA_PROGRAMED;
    }

    if(tr->r_fpga == TBS_FPGA_PROGRAMED){
      /* TODO: actually unprogram FPGA */
    }

    destroy_avltree(tr->r_registers, &free_entry);
    tr->r_registers = NULL;

    tr->r_registers = create_avltree();
    if(tr->r_registers == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to re-create empty lookup structure");
      return KATCP_RESULT_FAIL;
    }

    return KATCP_RESULT_OK;
  }

  file = arg_string_katcp(d, 1);
  if(file == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to acquire file name to program");
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "attempting to program %s", file);
  bs = open_bof(d, file);
  if(bs == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(program_bof(d, bs, TBS_FPGA_CONFIG) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to program bit stream from %s to %s", file, TBS_FPGA_CONFIG);
    close_bof(d, bs);
    return KATCP_RESULT_FAIL;
  }
  tr->r_fpga = TBS_FPGA_PROGRAMED;

  if(index_bof(d, bs) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to load register mapping of %s", file);
    close_bof(d, bs);
    return KATCP_RESULT_FAIL;
  }

  if(map_raw_tbs(d) < 0){
    close_bof(d, bs);
    return KATCP_RESULT_FAIL;
  }

  close_bof(d, bs);
  return KATCP_RESULT_OK;
}

int register_cmd(struct katcp_dispatch *d, int argc)
{
  char *name, *position, *end, *extra, *length;
  struct tbs_entry entry, *te;
  unsigned int mod, div;
  struct tbs_raw *tr;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(argc <= 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  position = arg_string_katcp(d, 2);

  if((name == NULL) || (position == NULL)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return KATCP_RESULT_FAIL;
  }

  if(find_data_avltree(tr->r_registers, name)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register called %s already defined", name);
    return KATCP_RESULT_FAIL;
  }

  entry.e_pos_base = strtoul(position, &end, 0);
  if(*end == ':'){
    extra = end + 1;
    entry.e_pos_offset = strtoul(extra, NULL, 0);
  } else {
    entry.e_pos_offset = 0;
  }
  
  mod = (entry.e_pos_base % 4);
  entry.e_pos_base = entry.e_pos_base - mod;
  entry.e_pos_offset = entry.e_pos_offset + (8 * mod);

  div = entry.e_pos_offset / 32;
  mod = entry.e_pos_offset % 32;

  entry.e_pos_base += div;
  entry.e_pos_offset = mod;
  
  length = arg_string_katcp(d, 3);
  if(length){
    entry.e_len_base = strtoul(length, &end, 0);
    if(*end == ':'){
      extra = end + 1;
      entry.e_len_offset = strtoul(extra, NULL, 0);
    } else {
      entry.e_len_offset = 0;
    }
  } else {
    if(entry.e_pos_offset > 0){
      entry.e_len_base = 0;
      entry.e_len_offset = 32 - entry.e_pos_offset;
    } else {
      entry.e_len_base = 4;
      entry.e_len_offset = 0;
    }
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "guessed register size of %u bytes and %u bits", entry.e_len_base, entry.e_len_offset);
  }

  mod = (entry.e_len_base % 4);
  entry.e_len_base = entry.e_len_base - mod;
  entry.e_len_offset = entry.e_len_offset + (8 * mod);

  div = entry.e_len_offset / 32;
  mod = entry.e_len_offset % 32;

  entry.e_len_base += div;
  entry.e_len_offset = mod;

  entry.e_mode = TBS_WRABLE;

  if((entry.e_len_base == 0) && (entry.e_len_offset == 0)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "size of register %s malformed", name);
    return KATCP_RESULT_FAIL;
  }
  if((entry.e_len_base > 0) && (entry.e_len_offset > 0)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unsual size of register %s with %u bytes and %d bits", name, entry.e_pos_base, entry.e_pos_offset);
  } 
  
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "start of %s at 0x%x and bit %d with size of %u bytes and %u bits", name, entry.e_pos_base, entry.e_pos_offset, entry.e_len_base, entry.e_len_offset);

  te = malloc(sizeof(struct tbs_entry));
  if(te == NULL){
    return KATCP_RESULT_FAIL;
  }

  memcpy(te, &entry, sizeof(struct tbs_entry));

  if(store_named_node_avltree(tr->r_registers, name, te) < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to store definition of register %s", name);
    free(te);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

/*********************************************************************/

int unmap_raw_tbs(struct katcp_dispatch *d)
{
  struct tbs_raw *tr;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    return -1;
  }

  if(tr->r_fpga != TBS_FPGA_MAPPED){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "nothing mapped");
    return 0;
  }

  munmap(tr->r_map, tr->r_map_size);
  tr->r_fpga = TBS_FPGA_PROGRAMED;

  tr->r_map_size = 0;
  tr->r_map = NULL;

  return 0;
}

int map_raw_tbs(struct katcp_dispatch *d)
{
  struct tbs_raw *tr;
  unsigned int power;
  int fd;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    return -1;
  }

  if(tr->r_fpga == TBS_FPGA_MAPPED){
    unmap_raw_tbs(d);
  }

  if(tr->r_top_register <= 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no registes defined");
    return -1;
  }

  power = 4096;
  while(power < tr->r_top_register){
    power *= 2;
  }

  tr->r_map_size = power;
  if(tr->r_map_size > 0x08000000){ 
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "requesting to map a rather large area of 0x%x", tr->r_map_size);
  }

  fd = open(TBS_FPGA_MEM, O_RDWR);
  if(fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to open file %s: %s", TBS_FPGA_MEM, strerror(errno));
    return -1;
  }

  tr->r_map = mmap(NULL, tr->r_map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  if(tr->r_map == MAP_FAILED){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to map file %s: %s", TBS_FPGA_MEM, strerror(errno));
    close(fd);
    return -1;
  }

  close(fd); /* TODO: maybe retain file descriptor ? */
  tr->r_fpga = TBS_FPGA_MAPPED;

  return 0;
}

/*********************************************************************/

void destroy_raw_tbs(struct katcp_dispatch *d, struct tbs_raw *tr)
{
  if(tr == NULL){
    return;
  }

  if(tr->r_registers){
    destroy_avltree(tr->r_registers, &free_entry);
    tr->r_registers = NULL;
  }

  if(tr->r_fpga == TBS_FPGA_MAPPED){
    /* TODO */

    tr->r_fpga = TBS_FPGA_PROGRAMED;
  }

  free(tr);
}

void release_raw_tbs(struct katcp_dispatch *d, unsigned int mode)
{
  struct tbs_raw *tr;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr){
    destroy_raw_tbs(d, tr);
  }
}

int enter_raw_tbs(struct katcp_dispatch *d, struct katcp_notice *n, char *flags, unsigned int from)
{
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "now running in raw mode");

  return 0;
}

int setup_raw_tbs(struct katcp_dispatch *d)
{
  struct tbs_raw *tr;
  int result;

  tr = malloc(sizeof(struct tbs_raw));
  if(tr == NULL){
    return -1;
  }

  tr->r_registers = NULL;
  tr->r_fpga = TBS_FRGA_DOWN;

  tr->r_map = NULL;
  tr->r_map_size = 0;

  tr->r_top_register = 0;

  /* clear out further structure elements */

  /* allocate structure elements */
  tr->r_registers = create_avltree();
  if(tr->r_registers == NULL){
    destroy_raw_tbs(d, tr);
    return -1;
  }

  if(store_full_mode_katcp(d, TBS_MODE_RAW, TBS_MODE_RAW_NAME, &enter_raw_tbs, NULL, tr, &release_raw_tbs) < 0){
    return -1;
  }

  result = 0;

  result += register_flag_mode_katcp(d, "?progdev", "program the fpga (?progdev [filename])", &progdev_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?register", "name a memory location (?register name position bit-offset length)", &register_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?wordread",     "read data from a named register (?wordread name index count)", &word_read_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?wordwrite",    "write data to a named register (?wordwrite name index value+)", &word_write_cmd, 0, TBS_MODE_RAW);

  return 0;
}

