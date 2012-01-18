
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <dirent.h>
#include <fcntl.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <katcp.h>
#include <katcl.h>
#include <avltree.h>

#include "tcpborphserver3.h"
#include "loadbof.h"
#include "tg.h"

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

void print_entry(struct katcp_dispatch *d, char *key, void *data)
{
  struct tbs_entry *te;

  te = data;
  if (te) {
    prepend_inform_katcp(d);
    append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, key);
  }
}

void print_entry_size(struct katcp_dispatch *d, char *key, void *data)
{
  struct tbs_entry *te;

  te = data;
  if (te) {
    prepend_inform_katcp(d);
    append_string_katcp(d, KATCP_FLAG_STRING, key);
    append_args_katcp(d, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, "%d:%d", te->e_len_base, te->e_len_offset);
  }
}

void print_entry_detail(struct katcp_dispatch *d, char *key, void *data)
{
  struct tbs_entry *te;

  te = data;
  if (te) {
    prepend_inform_katcp(d);
    append_string_katcp(d, KATCP_FLAG_STRING, key);
    append_args_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "0x%x:%d", te->e_pos_base, te->e_pos_offset);
  }
}

/*********************************************************************/

static void byte_normalise(struct katcl_byte_bit *bb)
{
  struct katcl_byte_bit tmp;

  tmp.b_byte = bb->b_byte;
  tmp.b_bit  = bb->b_bit;

  bb->b_byte = tmp.b_byte + (tmp.b_bit / 8);
  bb->b_bit  = tmp.b_bit % 8;
}

static void word_normalise(struct katcl_byte_bit *bb)
{
  struct katcl_byte_bit tmp;

  byte_normalise(bb);

  tmp.b_byte = bb->b_byte;
  tmp.b_bit  = bb->b_bit;

  bb->b_byte = (tmp.b_byte / 4) * 4;
  bb->b_bit  = ((tmp.b_byte % 4) * 4) + bb->b_bit;
}

/*********************************************************************/

int display_dir_cmd(struct katcp_dispatch *d, char *directory)
{
  DIR *dr;
  struct dirent *de;
  char *label;
  unsigned long count;

  label = arg_string_katcp(d, 0);
  if(label == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "internal logic failure");
    return KATCP_RESULT_FAIL;
  }

  dr = opendir(directory);
  if(dr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to open %s: %s", directory, strerror(errno));
    extra_response_katcp(d, KATCP_RESULT_FAIL, "io");
    return KATCP_RESULT_OWN;
  }

  count = 0;

  while((de = readdir(dr)) != NULL){
    if(de->d_name[0] != '.'){ /* ignore hidden files */
      prepend_inform_katcp(d);
      append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, de->d_name);
#if 0
      send_katcp(d,
          KATCP_FLAG_FIRST | KATCP_FLAG_STRING | KATCP_FLAG_MORE, "#", 
          KATCP_FLAG_STRING, label + 1,
          KATCP_FLAG_LAST | KATCP_FLAG_STRING, de->d_name);
#endif
      count++;
    }
  }

  closedir(dr);

  prepend_reply_katcp(d);

  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_unsigned_long_katcp(d, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, count);

#if 0
  send_katcp(d,
      KATCP_FLAG_FIRST | KATCP_FLAG_STRING | KATCP_FLAG_MORE, "!", 
      KATCP_FLAG_STRING, label + 1,
      KATCP_FLAG_STRING, KATCP_OK, 
      KATCP_FLAG_LAST | KATCP_FLAG_ULONG, count);
#endif

  return KATCP_RESULT_OWN;
}

int listdev_cmd(struct katcp_dispatch *d, int argc)
{
  struct tbs_raw *tr;
  char *a1;
  void *call;

  tr = get_current_mode_katcp(d);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to get raw state");
    return KATCP_RESULT_FAIL;
  }

  if(tr->r_fpga != TBS_FPGA_MAPPED){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga not programmed");
    return KATCP_RESULT_FAIL;
  }

  call = &print_entry;

  if (argc > 1) {
    a1 = arg_string_katcp(d, 1);
    if (strcmp(a1, "size") == 0){
      call = &print_entry_size;
    } else if (strcmp(a1, "detail") == 0){
      call = &print_entry_detail;
    } else {
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to get raw state");
      return KATCP_RESULT_FAIL;
    }
  }

  if (tr->r_registers != NULL){
    print_inorder_avltree(d, tr->r_registers->t_root, call, 0);
    return KATCP_RESULT_OK;
  }

  return KATCP_RESULT_FAIL;
}

int listbof_cmd(struct katcp_dispatch *d, int argc)
{
  struct tbs_raw *tr;

  tr = get_current_mode_katcp(d);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to get raw state");
    return KATCP_RESULT_FAIL;
  }

  return display_dir_cmd(d, tr->r_bof_dir);
}

int delbof_cmd(struct katcp_dispatch *d, int argc)
{
  struct tbs_raw *tr;

  char *name, *ptr;
  int len, result;

  tr = get_current_mode_katcp(d);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to get raw state");
    return KATCP_RESULT_FAIL;
  }

  if(argc <= 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a file to delete");
    return KATCP_RESULT_INVALID;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire first parameter");
    return KATCP_RESULT_FAIL;
  }

  if(strchr(name, '/') || (name[0] == '.')){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "client attempts to specify a path (%s)", name);
    return KATCP_RESULT_FAIL;
  }

  len = strlen(name) + strlen(tr->r_bof_dir) + 1;
  ptr = malloc(len + 1);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes", len + 1);
    extra_response_katcp(d, KATCP_RESULT_FAIL, "allocation");
    return KATCP_RESULT_OWN;
  }

  result = snprintf(ptr, len + 1, "%s/%s", tr->r_bof_dir, name);
  if(result != len){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "major logic failure: expected %d from snprintf, got %d", len, result);
    extra_response_katcp(d, KATCP_RESULT_FAIL, "internal");
    free(ptr);
    return KATCP_RESULT_OWN;
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "attempting to delete %s", ptr);
  result = unlink(ptr);

  if(result < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to delete %s: %s", name, strerror(errno));
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  free(ptr);

  return KATCP_RESULT_OK;
}


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
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a register to write, followed by offset and one or more values");
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
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s is not marked writeable", name);
    return KATCP_RESULT_FAIL;
  }

  start = arg_unsigned_long_katcp(d, 2);
  start *= 4;

  if(te->e_len_base < start + ((argc - 3) * 4) ){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "write offset %u plus %d words overruns register %s of size %u.%u", start, argc - 3, name, te->e_len_base, te->e_len_offset);
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

#if 0
  msync(tr->r_map + te->e_pos_base + start, te->e_len_base, MS_INVALIDATE | MS_SYNC);
#endif
  msync(tr->r_map, tr->r_map_size, MS_SYNC);

  return KATCP_RESULT_OK;
}

int write_cmd(struct katcp_dispatch *d, int argc)
{
  struct tbs_raw *tr;
  struct tbs_entry *te;

  struct katcl_byte_bit off, len, temp;

  unsigned char *buffer;
  unsigned int blen, start_base, i, size_have, start, want_len;
  uint8_t start_offset, current, prev, value, update;

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
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s is not marked writeable", name);
    return KATCP_RESULT_FAIL;
  }
  
  if (arg_byte_bit_katcp(d, 2, &off) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "expect offset in byte:bit format");
    return KATCP_RESULT_FAIL;
  }

  blen = arg_buffer_katcp(d, 3, NULL, 0); 
  if (blen < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot read buffer");
    return KATCP_RESULT_FAIL;
  }

  buffer = malloc(sizeof(unsigned char) * blen);
  if (buffer == NULL){
#ifdef DEBUG
    fprintf(stderr, "raw: write cmd cannot allocate buffer\n");
#endif
    return KATCP_RESULT_FAIL;
  }

  blen = arg_buffer_katcp(d, 3, buffer, blen);
  if (blen < 0){
    if (buffer != NULL)
      free(buffer);
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot read buffer");
    return KATCP_RESULT_FAIL;
  }
  
  size_have     = te->e_len_base * 8 + te->e_len_offset;
  start         = off.b_byte * 8 + off.b_bit;

  /*TODO: allow length to default to size of buffer*/
  if (arg_byte_bit_katcp(d, 4, &len) < 0){
    len.b_byte = 0;
    len.b_bit  = size_have - start;
    byte_normalise(&len);
#ifdef DEBUG
    fprintf(stderr, "no length specified using remaining %lu:%d\n", len.b_byte, len.b_bit);
#endif
  } 

  want_len = len.b_byte * 8 + len.b_bit;

  /*length check*/
#ifdef DEBUG
  fprintf(stderr, "size_have: %d start:%d size_now:%d want_len:%d\n", size_have, start, size_have - start, want_len);
#endif

  if (start > size_have || (size_have - start) < want_len){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "trying to write past the end of the register %s", name);
    if (buffer != NULL)
      free(buffer);
    return KATCP_RESULT_FAIL;
  }
  
  off.b_byte += te->e_pos_base;
  off.b_bit  += te->e_pos_offset;
  
  byte_normalise(&off);

  start_base   = off.b_byte;
  start_offset = off.b_bit;

#ifdef DEBUG
  fprintf(stderr, "raw write: have blen: %d register start offset (0x%lx:%d) len(0x%lx:%d)\n", blen,  off.b_byte, off.b_bit, len.b_byte, len.b_bit); 
#endif

  if (start_offset > 0){
    current = *((uint8_t *)(tr->r_map + start_base));
    prev    = current & (0xff << (8 - start_offset));
  } else {
    prev    = 0;
  }

  for (i=0; i<blen && i<len.b_byte; i++){

    value = buffer[i];
    update = prev | (value >> start_offset);

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing 0x%x to position 0x%x", update, start_base);
#ifdef DEBUG
    fprintf(stderr, "raw write: [%d] got 0x%x write 0x%x\n", i, buffer[i], update);
#endif

    *((uint8_t *)(tr->r_map + start_base)) = update;
    
    prev = value << (8 - start_offset);
    start_base += 1;
  }

  if (len.b_bit > 0 && blen > len.b_byte){

    current = *((uint8_t *)(tr->r_map + start_base));
    value = buffer[i] & (0xff << (8 - len.b_bit));
    update = prev | (value >> start_offset) | (current & (0xff >> (start_offset + len.b_bit)));

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing partial len 0x%x to position 0x%x", update, start_base);
#ifdef DEBUG
    fprintf(stderr, "raw write: [%d] got 0x%x write 0x%x\n", i, buffer[i], update);
#endif

    *((uint8_t *)(tr->r_map + start_base)) = update;

    if (start_offset > 0){

#ifdef DEBUG
      fprintf(stderr, "raw partial with start_offset some new data might need to go to next byte\n");
#endif

      temp.b_byte = 0;
      temp.b_bit  = start_offset + len.b_bit;
      byte_normalise(&temp);

      if (temp.b_byte > 0 && temp.b_bit > 0){
        prev = value << (8 - temp.b_bit);
        start_base += 1;
        if (start_base < te->e_pos_base + te->e_len_base){
          current = (*((uint8_t *)(tr->r_map + start_base))) & (0xff >> temp.b_bit);
          update = prev | current;
          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing final, partial 0x%x to position 0x%x", update, start_base);
          *((uint8_t *)(tr->r_map + start_base)) = update;
        } 
      } 
    }
  } else if (start_offset > 0 && (start_base < te->e_pos_base + te->e_len_base)){

    current = (*((uint8_t *)(tr->r_map + start_base))) & (0xff >> start_offset);
    update = prev | current;

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing final, partial 0x%x to position 0x%x", update, start_base);
#ifdef DEBUG
    fprintf(stderr, "raw write: final write 0x%x\n", update);
#endif

    *((uint8_t *)(tr->r_map + start_base)) = update;

  } 
  #if 0
  else {

#ifdef DEBUG
    fprintf(stderr, "raw partial FATAL doesn't know what to do\n");
#endif
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "raw partial FATAL doesn't know what to do");

    if (buffer != NULL)
      free(buffer);

    return KATCP_RESULT_FAIL;
  }
#endif

  msync(tr->r_map, tr->r_map_size, MS_SYNC);

  if (buffer != NULL)
    free(buffer);
  
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

  j = te->e_pos_base + (start * 4);
  shift = te->e_pos_offset;

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "attempting to read %d words from fpga at 0x%x", length, j);

  /* WARNING: scary logic, attempts to support reading of non-word, non-byte aligned registers, but in word amounts (!) */

  /* defer io for as long as possible, no log messages feasible after we do prepend reply */
  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  flags = KATCP_FLAG_XLONG;

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

int read_cmd(struct katcp_dispatch *d, int argc)
{
  struct katcl_byte_bit bb;
  struct tbs_raw *tr;
  struct tbs_entry *te;
  char *name;
  unsigned int want_base, want_offset, start_base, start_offset, i, j, shift, combined_base, combined_offset, pos_base, pos_offset, len_base, len_offset, grab_base, grab_offset;
  unsigned char *ptr, *buffer, prev, current, mask, tail_mask;

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

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "read on %s invoked with %d args", name, argc);

  /* normalise, could have been specified in words, with 32 bits in offset */
  pos_base   = te->e_pos_base + (te->e_pos_offset / 8);
  pos_offset = te->e_pos_offset % 8;

  len_base   = te->e_len_base + (te->e_len_offset / 8);
  len_offset = te->e_len_offset % 8;

  /* offensive programming */
  te = NULL;

  start_base = 0;
  if(argc > 2){
    if(arg_byte_bit_katcp(d, 2, &bb) < 0){
      return KATCP_RESULT_FAIL;
    }
    byte_normalise(&bb);
    start_base   = bb.b_byte;
    start_offset = bb.b_bit;
  } else {
    start_base   = 0;
    start_offset = 0;
  }

#if 0
  start_base = start_base + (start_offset / 8);
  start_offset = start_offset % 8;
#endif

  /* extra paranoia check within name abstraction, needed to catch wild wrapping if really large start position is used in later calculations */
  if((start_base + (start_offset + 7) / 8) > (len_base + (len_offset + 7) / 8)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "read request starts after end of register %s", name);
    return KATCP_RESULT_FAIL;
  }

  combined_base   = pos_base + start_base + (pos_offset + start_offset) / 8;
  combined_offset = (pos_offset + start_offset) % 8;

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "read request for %s at 0x%x:%u, combined 0x%x:%u", name, start_base, start_offset, combined_base, combined_offset);

  want_base = 1;
  if(argc > 3){
    if(arg_byte_bit_katcp(d, 3, &bb) < 0){
      return KATCP_RESULT_FAIL;
    }
    byte_normalise(&bb);
  } else {
    if(((len_base * 8) + len_offset) <= 32){
      /* WARNING: relies on subsequent normalisation and range checks */
      bb.b_byte = 0;
      bb.b_bit  = ((len_base * 8) + len_offset) - ((start_base * 8) + start_offset);
    } else {
      /* TODO: there might be better choices */
      bb.b_byte  = 1;
      bb.b_bit   = 0;
    }
  }

#if 0
  want_base   = want_base + (want_offset / 8);
  want_offset = want_offset % 8;
#endif
  byte_normalise(&bb);

  want_base   = bb.b_byte;
  want_offset = bb.b_bit;
  if((want_offset <= 0) && (want_base <= 0)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "zero read request length on register %s", name);
    return KATCP_RESULT_FAIL;
  }

  /* check within mmap area, could happen earlier */
  if((pos_base + len_base + ((pos_offset + len_offset + 7) / 8)) >= tr->r_map_size){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s extends beyond end of mapped area of size 0x%x", tr->r_map_size);
    return KATCP_RESULT_FAIL;
  }

  /* check within name abstraction */
  if((((start_base + want_base) * 8) + start_offset + want_offset) > ((len_base * 8) + len_offset)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "read request of %u bits starting within %u bits of register %s overruns size of %u bits", want_base * 8 + want_offset, start_base * 8 + start_offset, name, len_base * 8 + len_offset);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "reading %s (%u:%u) starting at %u:%u amount %u:%u", name, pos_base, pos_offset, start_base, start_offset, want_base, want_offset);

  ptr = tr->r_map;

  if((combined_offset == 0) && (want_offset == 0)){ 
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "fast read, start at %u, read %u complete bytes", combined_base, want_base);
    /* FAST: no bit offset (start at byte, read complete bytes) => no shifts => no alloc, no copy */

    prepend_reply_katcp(d);
    append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
    append_buffer_katcp(d, KATCP_FLAG_BUFFER | KATCP_FLAG_LAST, ptr + combined_base, want_base);

    /* END easy case */
    return KATCP_RESULT_OWN;
  }

  /* complicated case... bit ops */
  buffer = malloc(want_base + 2);
  if(buffer == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %u bytes to extract register %s", want_base + 2, name);
    return KATCP_RESULT_FAIL;
  }

  if(combined_offset == 0){ 
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "medium read, start at %u, read %u complete bytes and %u bits", combined_base, want_base, want_offset);
    /* MEDIUM: start at byte, read incomplete bytes => alloc, copy and clear but no shift  */
#ifdef DEBUG
    if((want_offset == 0) || (want_offset >= 8)){
      fprintf(stderr, "logic problem: want offset %u unreasonable\n", want_offset);
      abort();
    }
#endif
    memcpy(buffer, ptr + combined_base, want_base + 1);
    buffer[want_base] = buffer[want_base] & (0xff << (8 - want_offset));

    prepend_reply_katcp(d);
    append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
    append_buffer_katcp(d, KATCP_FLAG_BUFFER | KATCP_FLAG_LAST, buffer, want_base + 1);

    free(buffer);
    return KATCP_RESULT_OWN;
  }

#ifdef DEBUG
  if(combined_offset <= 0){
    fprintf(stderr, "raw: logic problem: expected to handle the complicated stage\n");
    abort();
  }
#endif

  /* COMPLEX: start at bit offset, read arb bytes and bits => alloc, shift => copy */

  shift = combined_offset;
  grab_base = want_base;
  grab_offset = combined_offset + want_offset;

  if(grab_offset > 8){
    grab_offset -= 8;
    grab_base++;
  }

  mask = ~(0xff << shift);
  j = combined_base;

  prev = (ptr[j]) << shift;
  j++;

  i = 0;

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "complex read, partial first byte shifted is now 0x%02x (shift %u, mask 0x%02x)", prev, shift, mask);

  while(i < grab_base){
    current = ptr[j];

    buffer[i] = prev | (mask & (current >> (8 - shift)));
  
    prev = current << shift;

    i++;
    j++;
  }

  if(want_offset){
    tail_mask = 0xff << (8 - want_offset);

    if(grab_base > 0){
      current = ptr[j];
      buffer[i] = (prev | (mask & (current >> (8 - shift)))) & tail_mask;
    } else {
      buffer[i] = prev & tail_mask;
    }

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "complex read, final byte (%u) needs mask 0x%02x, prev is 0x%02x, result is 0x%02x", i, tail_mask, prev, buffer[i]);

    i++;
  }

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_buffer_katcp(d, KATCP_FLAG_BUFFER | KATCP_FLAG_LAST, buffer, i);

  free(buffer);

  return KATCP_RESULT_OWN;
}

int progdev_cmd(struct katcp_dispatch *d, int argc)
{
  char *file;
  struct bof_state *bs;
  struct tbs_raw *tr;
  char *buffer;
  int len;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to acquire state");
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "attempting to empty fpga");

  if(tr->r_fpga == TBS_FPGA_MAPPED){
    unmap_raw_tbs(d);
    tr->r_fpga = TBS_FPGA_PROGRAMED;
  }

  if(tr->r_fpga == TBS_FPGA_PROGRAMED){
    /* TODO: actually unprogram FPGA */
  }

  if(tr->r_image){
    free(tr->r_image);
    tr->r_image = NULL;
  }

  if(tr->r_registers){
    destroy_avltree(tr->r_registers, &free_entry);
    tr->r_registers = NULL;
  }

  if(argc <= 1){
    return KATCP_RESULT_OK;
  }

  tr->r_registers = create_avltree();
  if(tr->r_registers == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to re-create empty lookup structure");
    return KATCP_RESULT_FAIL;
  }

  file = arg_string_katcp(d, 1);
  if(file == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire file name to program");
    return KATCP_RESULT_FAIL;
  }

  if(strchr(file, '/') != NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "file name %s may not contain a path component", file);
    return KATCP_RESULT_FAIL;
  }

  len = strlen(file) + 1 + strlen(tr->r_bof_dir) + 1;
  buffer = malloc(len);
  if(buffer == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes", len);
    return KATCP_RESULT_FAIL;
  }

  snprintf(buffer, len, "%s/%s", tr->r_bof_dir, file);
  buffer[len - 1] = '\0';

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "attempting to program %s", file);

  bs = open_bof(d, buffer);
  free(buffer);
  buffer = NULL;
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

  if (tr->r_hwmon){
    destroy_avltree(tr->r_hwmon, &destroy_hwsensor_tbs);
    tr->r_hwmon = NULL;
  }

  if(tr->r_fpga == TBS_FPGA_MAPPED){
    /* TODO */

    tr->r_fpga = TBS_FPGA_PROGRAMED;
  }

  if(tr->r_image){
    free(tr->r_image);
    tr->r_image = NULL;
  }
 
  if (tr->r_bof_dir != NULL){
    free(tr->r_bof_dir);
    tr->r_bof_dir = NULL;
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

int make_bofdir_tbs(struct katcp_dispatch *d, struct tbs_raw *tr, char *bofdir)
{
  struct stat st;
  int i;
  char *list[] = { "/bof", "/boffiles", "." };

  if(tr->r_bof_dir){
    free(tr->r_bof_dir);
    tr->r_bof_dir = NULL;
  }

  /* use what we have been told */
  if(bofdir){
    tr->r_bof_dir = strdup(bofdir);
    if(tr->r_bof_dir == NULL){
      return -1;
    }
  } 

  /* try and guess */

  for(i = 0; list[i]; i++){
    if(stat(list[i], &st) == 0){
      if(S_ISDIR(st.st_mode)){
        tr->r_bof_dir = strdup(list[i]);
        if(tr->r_bof_dir){
          return 0;
        }
      }
    }
  }

  return -1;
}

int setup_raw_tbs(struct katcp_dispatch *d, char *bofdir)
{
  struct tbs_raw *tr;
  int result;

  tr = malloc(sizeof(struct tbs_raw));
  if(tr == NULL){
    return -1;
  }

  tr->r_registers = NULL;
  tr->r_hwmon = NULL;
  tr->r_fpga = TBS_FRGA_DOWN;

  tr->r_map = NULL;
  tr->r_map_size = 0;

  tr->r_image = NULL;
  tr->r_bof_dir = NULL;

  tr->r_top_register = 0;

  /* clear out further structure elements */

  /* allocate structure elements */
  tr->r_registers = create_avltree();
  if(tr->r_registers == NULL){
    destroy_raw_tbs(d, tr);
    return -1;
  }

  tr->r_hwmon = create_avltree();
  if(tr->r_hwmon == NULL){
    destroy_raw_tbs(d, tr);
    return -1;
  }


  if(make_bofdir_tbs(d, tr, bofdir) < 0){
    destroy_raw_tbs(d, tr);
    return -1;
  }

  if(store_full_mode_katcp(d, TBS_MODE_RAW, TBS_MODE_RAW_NAME, &enter_raw_tbs, NULL, tr, &release_raw_tbs) < 0){
    return -1;
  }

  if ((result = setup_hwmon_tbs(d)) < 0){
#ifdef DEBUG
    fprintf(stderr, "hwmon: setup returns %d\n", result);
#endif
  }

  result = 0;

  result += register_flag_mode_katcp(d, "?register",     "name a memory location (?register name position bit-offset length)", &register_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?write",    "write data to a named register (?write name byte-offset:bit-offset value byte-length:bit-length)", &write_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?read",         "read data from a named register (?read name byte-offset:bit-offset byte-length:bit-length)", &read_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?wordwrite",    "write data to a named register (?wordwrite name index value+)", &word_write_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?wordread",     "read data from a named register (?wordread name word-offset:bit-offset word-count)", &word_read_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?progdev", "program the fpga (?progdev [filename])", &progdev_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?listdev",      "lists available registers (?listdev [size|detail]", &listdev_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?listbof",      "display available bof files (?listbof)", &listbof_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?delbof",       "deletes a gateware image (?delbof image-file)", &delbof_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?tap-start",    "start a tap instance (?tap-start (?tap-start tap-device register-name ip-address [port [mac]])", &tap_start_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?tap-stop",     "deletes a tap instance (?tap-stop register-name)", &tap_stop_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?tap-info",     "displays diagnostics for a tap instance (?tap-info register-name)", &tap_info_cmd, 0, TBS_MODE_RAW);

  return result;
}

