
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <katpriv.h>
#include <katcp.h>
#include <katcl.h>
#include <avltree.h>

#include "tcpborphserver3.h"
#include "loadbof.h"
#include "tg.h"

/*********************************************************************/

int status_fpga_tbs(struct katcp_dispatch *d, int status);

/*********************************************************************/

static volatile int bus_error_happened;

void handle_bus_error(int signal)
{
  bus_error_happened = 1;
}

static int check_bus_error(struct katcp_dispatch *d)
{
#if 0
  if(bus_error_happened){

    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "bus error happened, probably a problem on the fpga bus, don't expect the system to be reliable from now on");

    bus_error_happened = 0;

    return -1;
  }
#endif

  return 0;
}

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

#if 0
static int word_compare(struct katcl_byte_bit *alpha, struct katcl_byte_bit *beta)
{
  unsigned int extra, delta;
  int sign;

  if(alpha->b_byte > beta->b_byte){
    if(alpha->b_bit >= beta->b_bit){
      return 1;
    }
    delta = beta->b_bit - alpha->b_bit;
    extra = delta / 8;
    delta = delta % 8;
    sign = 1;
  }

  if(alpha->b_byte < beta->b_byte){
    if(alpha->b_bit <= beta->b_bit){
      return -1;
    }
    delta = alpha->b_bit - beta->b_bit;
    extra = delta / 8;
    delta = delta % 8;
    sign = (-1);
  }

  if(alpha->b_byte == beta->b_byte){
    if(alpha->b_bit < beta->b_bit){
      return -1;
    }
    if(alpha->b_bit > beta->b_bit){
      return 1;
    }
    return 0;
  }
}
#endif

#if 0
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

  tmp.b_byte = bb->b_byte;
  tmp.b_bit  = bb->b_bit;

  byte_normalise(&tmp); /* now bits can't be larger than 7 */

  bb->b_byte = (tmp.b_byte / 4) * 4;
  bb->b_bit  = ((tmp.b_byte % 4) * 8) + bb->b_bit;
}
#endif

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

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "bof directory is %s", tr->r_bof_dir);

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

  if(check_bus_error(d) < 0){
    return KATCP_RESULT_FAIL;
  }  

  return KATCP_RESULT_OK;
}

int write_cmd(struct katcp_dispatch *d, int argc)
{
  struct tbs_raw *tr;
  struct tbs_entry *te;

  struct katcl_byte_bit off, len;

  uint32_t *buffer;
  unsigned int blen, ptr_base, ptr_offset, i, prefix_bits, register_bits, start_bits, copy_bits, copy_words_floor, remaining_bits;
  uint32_t current, prev, value, update;

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
  
  if (arg_bb_katcp(d, 2, &off) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "expect offset in byte:bit format");
    return KATCP_RESULT_FAIL;
  }

#if 0
  /* WARNING: not strictly needed, comes later */
  word_normalise(&off);
#endif

  blen = arg_buffer_katcp(d, 3, NULL, 0); 
  if (blen < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot read buffer");
    return KATCP_RESULT_FAIL;
  }

  buffer = malloc(sizeof(uint32_t) * ((blen + 3) / 4));
  if (buffer == NULL){
#ifdef DEBUG
    fprintf(stderr, "raw: write cmd cannot allocate buffer of %d bytes\n", blen);
#endif
    return KATCP_RESULT_FAIL;
  }

  blen = arg_buffer_katcp(d, 3, buffer, blen);
  if (blen < 0){
    if (buffer != NULL){
      free(buffer);
    }
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "cannot read buffer");
    return KATCP_RESULT_FAIL;
  }
  
  register_bits     = (te->e_len_base * 8) + te->e_len_offset;
  start_bits         = (off.b_byte * 8) + off.b_bit;

  if (arg_bb_katcp(d, 4, &len) < 0){ 
    /* no length given, assume all data given is data  */

    len.b_bit = 0;
    len.b_byte = blen;

    word_normalise_bb_katcl(&len);
    copy_bits = len.b_byte * 8 + len.b_bit;

#ifdef DEBUG
    fprintf(stderr, "no length specified, defaulting to data %lu:%d\n", len.b_byte, len.b_bit);
#endif

  } else {

    word_normalise_bb_katcl(&len);
    copy_bits = len.b_byte * 8 + len.b_bit;

    if((blen * 8) < copy_bits){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "requested %u bits to copy, buffer only contains %u", copy_bits, blen * 8);
      return KATCP_RESULT_FAIL;
    }
  }


  /*length check*/
#ifdef DEBUG
  fprintf(stderr, "bit checks: total register=%d start_bits=%d copy_bits=%d\n", register_bits, start_bits, copy_bits);
#endif

  if((start_bits + copy_bits) > register_bits){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "trying to write past the end of the register %s of bits %u, start bits %u, payload %u bits", name, register_bits, start_bits, copy_bits);
    if (buffer != NULL){
      free(buffer);
    }
    return KATCP_RESULT_FAIL;
  }
  
#ifdef DEBUG
  fprintf(stderr, "raw write: bytes-in-buffer=%d register offset (0x%lx:%d) len(0x%lx:%d)\n", blen,  off.b_byte, off.b_bit, len.b_byte, len.b_bit); 
#endif

  off.b_byte += te->e_pos_base;
  off.b_bit  += te->e_pos_offset;
  
  word_normalise_bb_katcl(&off);

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing to %s@0x%lx:%d: start position 0x%lx:%d, payload length 0x%lx:%d, register size 0x%lx:%d", name, te->e_pos_base, te->e_pos_offset, off.b_byte, off.b_bit, len.b_byte, len.b_bit, te->e_len_base, te->e_len_offset);

  ptr_base   = off.b_byte;
  ptr_offset = off.b_bit;

  if (ptr_offset > 0){
    current = *((uint32_t *)(tr->r_map + ptr_base));
    prev    = current & (0xffffffff << (32 - ptr_offset));
  } else {
    prev    = 0;
  }

#ifdef DEBUG
  if((off.b_byte % 4) || (off.b_bit >= 32)){
    fprintf(stderr, "write: word normalise didn't\n");
    abort();
  }
#endif

  copy_words_floor = len.b_byte / 4;

  /* the easy part, whole words */
  for (i = 0; i < copy_words_floor; i++){

    /* implicit is a ntohs */
    value = buffer[i];

    update = prev | (value >> ptr_offset);

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing 0x%x to position 0x%x", update, ptr_base);

    *((uint32_t *)(tr->r_map + ptr_base)) = update;
    
    prev = value << (32 - ptr_offset);
    ptr_base += 4;
  }

  /* WARNING, WARNING, WARNING: still not correct from down here onwards */

  /* now sort out the left over bits */

  if(copy_words_floor > 0){
    remaining_bits = ptr_offset + copy_bits - (copy_words_floor * 32);
    prefix_bits = 0; 
  } else {
    /* no full word writes completed is special, we have to account for leading bits from fpga when we write out final word */
    remaining_bits = copy_bits;
    prefix_bits = ptr_offset;
  }

  if(remaining_bits > 0){

    value = buffer[i];
    prev = prev | (value >> ptr_offset);

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "have %u bits outstanding (prefix %u), holdover is 0x%x", remaining_bits, prefix_bits, prev);

    /* two steps: the first case where we get to write another full destination word */
    if((ptr_offset + remaining_bits) >= 32){

      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing penultimate 0x%x to position 0x%x", prev, ptr_base);

      *((uint32_t *)(tr->r_map + ptr_base)) = prev;

      prev = value << (32 - ptr_offset);
      ptr_base += 4;

      remaining_bits = (ptr_offset + remaining_bits - 32);
    }


#ifdef DEBUG
    if(remaining_bits >= 32){
      fprintf(stderr, "write: logic problem, remaining bits too large at %u", remaining_bits);
      abort();
    }
#endif

    /* now write a partial destination, so need to load in some bits */
    if(remaining_bits > 0){

      current = *((uint32_t *)(tr->r_map + ptr_base));

      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "read value 0x%x from 0x%x", current, ptr_base);

      update = (prev & (0xffffffff << (32 - (prefix_bits + remaining_bits)))) | (current & (0xffffffff >> (prefix_bits + remaining_bits)));

      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing final 0x%x to position 0x%x", update, ptr_base);

      *((uint32_t *)(tr->r_map + ptr_base)) = update;

    }
  }

#if 0

  if (len.b_bit > 0 && blen > len.b_byte){

#if 0
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "you are using extended features of a temp hack. Don't");
    return KATCP_RESULT_FAIL;
#else 



    current = *((uint8_t *)(tr->r_map + ptr_base));
    value = buffer[i] & (0xff << (8 - len.b_bit));
    update = prev | (value >> ptr_offset) | (current & (0xff >> (ptr_offset + len.b_bit)));

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing partial len 0x%x to position 0x%x", update, ptr_base);
#ifdef DEBUG
    fprintf(stderr, "raw write: [%d] got 0x%x write 0x%x\n", i, buffer[i], update);
#endif

    *((uint8_t *)(tr->r_map + ptr_base)) = update;

    if (ptr_offset > 0){

#ifdef DEBUG
      fprintf(stderr, "raw partial with ptr_offset some new data might need to go to next byte\n");
#endif

      temp.b_byte = 0;
      temp.b_bit  = ptr_offset + len.b_bit;
      byte_normalise(&temp);

      if (temp.b_byte > 0 && temp.b_bit > 0){
        prev = value << (8 - temp.b_bit);
        ptr_base += 1;
        if (ptr_base < te->e_pos_base + te->e_len_base){
          current = (*((uint8_t *)(tr->r_map + ptr_base))) & (0xff >> temp.b_bit);
          update = prev | current;
          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing final, partial 0x%x to position 0x%x", update, ptr_base);
          *((uint8_t *)(tr->r_map + ptr_base)) = update;
        } 
      } 
    }
  } else if (ptr_offset > 0 && (ptr_base < te->e_pos_base + te->e_len_base)){

    current = (*((uint8_t *)(tr->r_map + ptr_base))) & (0xff >> ptr_offset);
    update = prev | current;

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "writing final, partial 0x%x to position 0x%x", update, ptr_base);
#ifdef DEBUG
    fprintf(stderr, "raw write: final write 0x%x\n", update);
#endif

    *((uint8_t *)(tr->r_map + ptr_base)) = update;

#endif
  } 
#endif

  msync(tr->r_map, tr->r_map_size, MS_SYNC);

  if (buffer != NULL){
    free(buffer);
  }

  if(check_bus_error(d) < 0){
    return KATCP_RESULT_FAIL;
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

  check_bus_error(d);

  return KATCP_RESULT_OWN;
}

#ifdef DEBUG
void check_read_results(int *v, int floor)
{
  int i, total;

  total = 0;
  for(i = 0; i < 3; i++){
    if(v[i] < 0){
      fprintf(stderr, "major problem: read arg[%d] is %d\n", i, v[i]);
#ifdef FAILFAST
      abort();
#endif
      return;
    }
    total += v[i];
  }

  /* "?read ok " plus "\n" plus limit of data */

  if(total < (floor + 10)){
    fprintf(stderr, "data added is %d, needed at least 10 + %d", total, floor);
#ifdef FAILFAST
    abort();
#endif
  }
}
#endif

int read_register(struct katcp_dispatch *d, struct tbs_entry *te, struct katcl_byte_bit *start, struct katcl_byte_bit *amount, void *buffer, unsigned int size)
{
  struct katcl_byte_bit sum, total, reg_len, reg_start, combined_start, limit;
  struct tbs_raw *tr;
  unsigned int shift, grab_base, grab_offset, round_left;
  unsigned long i, j;
  uint32_t *ptr, prev, current, mask, tail_mask;
  int transfer;
#ifdef PROFILE
  struct timeval then, now, delta;

  gettimeofday(&then, NULL);
#endif

#ifdef KATCP_CONSISTENCY_CHECKS
  if(buffer == NULL){
    return -1;
  }
  if(size <= 0){
    return -1;
  }
#endif

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    return -1;
  }

  if(tr->r_fpga != TBS_FPGA_MAPPED){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga not programmed");
    return -1;
  }

  if(!(te->e_mode & TBS_READABLE)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register is not marked readable");
    return -1;
  }

  /* basic sanity tests on register layout */

  if(make_bb_katcl(&reg_start, te->e_pos_base, te->e_pos_offset) < 0){
    return -1;
  }
  if(word_normalise_bb_katcl(&reg_start) < 0){
    return -1;
  }

  if(make_bb_katcl(&reg_len, te->e_len_base, te->e_len_offset) < 0){
    return -1;
  }
  if(word_normalise_bb_katcl(&reg_len) < 0){
    return -1;
  }

  if(add_bb_katcl(&sum, &reg_start, &reg_len) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register definition wraps in address space");
    return -1;
  }

  if(make_bb_katcl(&limit, tr->r_map_size, 0) < 0){
    return -1;
  }
  if(word_normalise_bb_katcl(&limit) < 0){
    return -1;
  }

  if(exceeds_bb_katcl(&sum, &limit)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register (0x%x:%x+0x%x:%x) falls outside mapped fpga range", reg_start.b_byte, reg_start.b_bit, reg_len.b_byte, reg_len.b_bit);
    return -1;
  }

  /* redundant, but helpful "virtual test" */
  if(word_normalise_bb_katcl(start) < 0){
    return -1;
  }
  if(exceeds_bb_katcl(start, &reg_len)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "read request starts after end of register");
    return -1;
  }

  /* essential "virtual test" */
  if(word_normalise_bb_katcl(amount) < 0){
    return -1;
  }
  if(add_bb_katcl(&total, start, amount) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "read request for wraps in address space");
    return -1;
  }
  word_normalise_bb_katcl(&total);

  if(exceeds_bb_katcl(&total, &reg_len)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "read request exceeds size available (request 0x%x:%x at 0x%x:%x larger than 0x%x:%x", amount->b_byte, amount->b_bit, start->b_byte, start->b_bit, reg_len.b_byte, reg_len.b_bit);
    return -1;
  }

  round_left = (amount->b_bit + 7) / 8;
  /* how much we expect to copy into the supplied buffer */
  transfer = amount->b_byte + round_left;
  if(transfer <= 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "zero read request length or request size wrapped");
    return KATCP_RESULT_FAIL;
  }
  if(transfer > size){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "supplied buffer of size %lu can not hold request %lu:%u", size, amount->b_byte, amount->b_bit);
    return -1;
  }

  /* work out memory locations */
  if(add_bb_katcl(&combined_start, &reg_start, start) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register start wraps in address space");
    return KATCP_RESULT_FAIL;
  }
  word_normalise_bb_katcl(&combined_start);


  if(combined_start.b_bit == 0){

    /* FAST: no bit offset => no shifts needed */
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "fast read start at %u:%u of 0x%x:%u maps to pos 0x%x:%u copied into %u bytes", start->b_byte, start->b_bit, amount->b_byte, amount->b_bit, combined_start.b_byte, combined_start.b_bit, transfer);

#ifdef USE_MEMCPY
    if(amount->b_bit > 0){
      memcpy(buffer, tr->r_map + combined.start.b_byte, transfer);
      buffer[amount->b_byte + amount->b_bit / 8] &= (~(0xff >> (amount->b_bit % 8)));
    } else {
      memcpy(buffer, tr->r_map + combined.start.b_byte, transfer);
    }
#else 
    /* WTF moments right here: FPGA 32 bit issues */
    for(i = 0; i < amount->b_byte; i += 4){
      current = *((uint32_t *)(tr->r_map + combined_start.b_byte + i));
      memcpy(buffer + i, &current, 4);
    }
    if(amount->b_bit){
      current = *((uint32_t *)(tr->r_map + combined_start.b_byte + i));
      current = current & (~(0xffffffff >> (amount->b_bit)));
      memcpy(buffer + i, &current, round_left);
    }

#ifdef KATCP_CONSISTENCY_CHECKS
    if((i + round_left) != transfer){
      fprintf(stderr, "read: read the incorrect number of bytes, needed %d\n", transfer);
      abort();
    }
#endif

#endif

    /* END easy case */
    return transfer;
  }

  /* COMPLEX: start at bit offset, read arb bytes and bits => shift, then copy */

  shift = combined_start.b_bit;
  grab_base = amount->b_byte;
  grab_offset = combined_start.b_bit + amount->b_bit;

  if(grab_offset > 32){
    grab_offset -= 32;
    grab_base += 4;
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "complex read starting at %u:%u of 0x%x:%u maps to pos 0x%x:%u with grab %u:%u shifted by %u copied into %u bytes", start->b_byte, start->b_bit, amount->b_byte, amount->b_bit, combined_start.b_byte, combined_start.b_bit, grab_base, grab_offset, shift, transfer);

  mask = ~(0xffffffff << shift);
  ptr = (uint32_t *)(tr->r_map + combined_start.b_byte);

  prev = (ptr[0]) << shift;
  j = 1;
  i = 0;

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "complex read, partial first byte shifted is now 0x%08x (shift %u, mask 0x%08x)", prev, shift, mask);

  while(i < grab_base){
    current = ptr[j];

    current = prev | (mask & (ptr[j] >> (32 - shift)));
    memcpy(buffer + i, &current, 4);
  
    prev = current << shift;

    j++;

    i += 4;
  }

  if(grab_offset){

    if(grab_base > 0){
      tail_mask = 0xffffffff << (32 - grab_offset);
      current = (prev | (mask & ((ptr[j] & tail_mask) >> (32 - shift))));
    } else {
#ifdef KATCP_CONSISTENCY_CHECKS
      if(shift > grab_offset){
        fprintf(stderr, "read: expected at least one bit (shifted %u bits, last data bit %u", shift, grab_offset);
        abort();
      }
#endif
      tail_mask = 0xffffffff << (32 - (grab_offset - shift));
      current = prev & tail_mask;
    }
    memcpy(buffer + i, &current, round_left);
#ifdef KATCP_CONSISTENCY_CHECKS
    if((i + round_left) != transfer){
      fprintf(stderr, "read: read the incorrect number of bytes, needed %d\n", transfer);
      abort();
    }
#endif

    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "complex read, final word %u needs mask 0x%08x, prev is 0x%08x, result is 0x%08x", i, tail_mask, prev, current);
  }

  return transfer;
}

int read_cmd(struct katcp_dispatch *d, int argc)
{
  struct katcl_byte_bit start, amount;
  struct tbs_raw *tr;
  struct tbs_entry *te;
  char *name;
  unsigned int space;
  void *ptr;
  int results[3], result;
#ifdef PROFILE
  struct timeval then, now, delta;

  gettimeofday(&then, NULL);
#endif

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

  make_bb_katcl(&start, 0, 0);
  if(argc > 2){
    if(arg_bb_katcp(d, 2, &start) < 0){
      return KATCP_RESULT_FAIL;
    }
    word_normalise_bb_katcl(&start);
  }

  if(argc > 3){
    if(arg_bb_katcp(d, 3, &amount) < 0){
      return KATCP_RESULT_FAIL;
    }
  } else {
    make_bb_katcl(&amount, 1, 0);

    /* TODO: could do a check against want+size, then trim as needed */

  }
  word_normalise_bb_katcl(&amount);

  space = amount.b_byte + ((amount.b_bit + 7) / 8);

  ptr = malloc(space);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %u bytes", space);
    return KATCP_RESULT_FAIL;
  }

  result = read_register(d, te, &start, &amount, ptr, space);

  if(result != space){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "requested %u bytes but got %d", space, result);
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  results[0] = prepend_reply_katcp(d);
  results[1] = append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  results[2] = append_buffer_katcp(d, KATCP_FLAG_BUFFER | KATCP_FLAG_LAST, ptr, result);

  free(ptr);

#ifdef DEBUG
  check_read_results(results, space + 1);
#endif
  check_bus_error(d);

  return KATCP_RESULT_OWN;

#if 0
  /* normalise, could have been specified in words, with 32 bits in offset */
  make_bb_katcl(&reg_start, te->e_pos_base, te->e_pos_offset);
  word_normalise_bb_katcl(&reg_start);
  
  make_bb_katcl(&reg_len, te->e_len_base, te->e_len_offset);
  word_normalise_bb_katcl(&reg_len);

  /* offensive programming */
  te = NULL;

  if(add_bb_katcl(&sum, &reg_start, &reg_len) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s definition wraps in address space", name);
    return KATCP_RESULT_FAIL;
  }

  make_bb_katcl(&limit, tr->r_map_size, 0);
  word_normalise_bb_katcl(&limit);

  if(exceeds_bb_katcl(&sum, &limit)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s (0x%x:%x + 0x%x:%x) falls outside mapped fpga range", name, reg_start.b_byte, reg_start.b_bit, reg_len.b_byte, reg_len.b_bit);
    return KATCP_RESULT_FAIL;
  }

  if(add_bb_katcl(&size, &read_start, &want) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "read request for %s wraps in address space", name);
    return KATCP_RESULT_FAIL;
  }
  word_normalise_bb_katcl(&size);

  if(exceeds_bb_katcl(&size, &reg_len)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "read request of %s exceeds size available (request 0x%x:%x at 0x%x:%x larger than 0x%x:%x", name, want.b_byte, want.b_bit, read_start.b_byte, want.b_bit, reg_len.b_byte, reg_len.b_bit);
    return KATCP_RESULT_FAIL;
  }

  if((want.b_byte <= 0) && (want.b_bit <= 0)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "zero read request length on register %s", name);
    return KATCP_RESULT_FAIL;
  }

  if(add_bb_katcl(&combined_start, &reg_start, &read_start) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "register %s start wraps in address space", name);
    return KATCP_RESULT_FAIL;
  }
  word_normalise_bb_katcl(&combined_start);


  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "reading %s at 0x%x:%u, combined 0x%x:%u", name, read_start.byte, read_start.bit, combined_start.b_byte, combined_start.b_bit);

#if 0
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "reading %s (%u:%u) starting at %u:%u amount %u:%u", name, pos_base, pos_offset, start_base, start_offset, want_base, want_offset);
#endif

  ptr = tr->r_map;

  if((combined_start.b_bit == 0) && (want.b_bit == 0)){ 
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "fast read, start at 0x%x, read %u complete bytes", combined_start.b_byte, want.b_byte);
    /* FAST: no bit offset (start at byte, read complete bytes) => no shifts => no alloc, no copy */

    results[0] = prepend_reply_katcp(d);
    results[1] = append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
    results[2] = append_buffer_katcp(d, KATCP_FLAG_BUFFER | KATCP_FLAG_LAST, ptr + combined_start.b_byte, want.b_byte);

#ifdef DEBUG
    check_read_results(results, want.b_byte);
#endif

#ifdef PROFILE
    gettimeofday(&now, NULL);
    sub_time_katcp(&delta, &now, &then);
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "fast read of %u bytes took %lu.%06lus", want_base, delta.tv_sec, delta.tv_usec);
#endif

    check_bus_error(d);

    /* END easy case */
    return KATCP_RESULT_OWN;
  }

  /* more complicated cases... bit ops */
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

    results[0] = prepend_reply_katcp(d);
    results[1] = append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
    results[2] = append_buffer_katcp(d, KATCP_FLAG_BUFFER | KATCP_FLAG_LAST, buffer, want_base + 1);

    free(buffer);

#ifdef DEBUG
    check_read_results(results, want_base + 1);
#endif
    check_bus_error(d);

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

  results[0] = prepend_reply_katcp(d);
  results[1] = append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  results[2] = append_buffer_katcp(d, KATCP_FLAG_BUFFER | KATCP_FLAG_LAST, buffer, i);

  free(buffer);

#ifdef DEBUG
  check_read_results(results, want_base + 1);
#endif
  check_bus_error(d);

  return KATCP_RESULT_OWN;
#endif
}

int fpgastatus_cmd(struct katcp_dispatch *d, int argc)
{
#if 0
  struct bof_state *bs;
#endif
  struct tbs_raw *tr;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to acquire state");
    return KATCP_RESULT_FAIL;
  }

  status_fpga_tbs(d, -1); /* side effect, generate message */

  switch(tr->r_fpga){
    case TBS_FPGA_DOWN :
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "fpga not programmed");
      return KATCP_RESULT_FAIL;
    case TBS_FPGA_PROGRAMMED :
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "fpga programmed but not mapped into processor");
      return KATCP_RESULT_FAIL;
    case TBS_FPGA_MAPPED :
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "fpga programmed and mapped with %s", tr->r_image ? tr->r_image : "<unknown file>");
      return KATCP_RESULT_OK;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "corrupted fpga state tracking");
      return KATCP_RESULT_FAIL;
  }
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

  stop_fpga_tbs(d);

  if(argc <= 1){
    return KATCP_RESULT_OK;
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

  if(start_fpga_tbs(d, bs) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to program bit stream from %s", file, TBS_FPGA_CONFIG);
    close_bof(d, bs);
    return KATCP_RESULT_FAIL;
  }

  /* WARNING: no check here as such a failure is survivable */
  tr->r_image = strdup(file);

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

  if(tr->r_fpga != TBS_FPGA_MAPPED){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga not programmed");
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

int status_fpga_tbs(struct katcp_dispatch *d, int status)
{
  struct tbs_raw *tr;
  int actual;
#if TBS_STATES_FPGA != 3 
#error "fpga state variable set inconsistent"
#endif
  char *fpga_states[TBS_STATES_FPGA] = { "down", "loaded", "ready" };

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to acquire state");
    return -1;
  }

  if(status >= TBS_STATES_FPGA){
    return -1;
  }

  if(status < 0){
    actual = tr->r_fpga;
  } else {
    actual = status;
    tr->r_fpga = status;
  }

  broadcast_inform_katcp(d, TBS_FPGA_STATUS, fpga_states[actual]);

  return 0;
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
  status_fpga_tbs(d, TBS_FPGA_PROGRAMMED);

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
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no registers defined");
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
  status_fpga_tbs(d, TBS_FPGA_MAPPED);

  return 0;
}

/*********************************************************************/

int stop_fpga_tbs(struct katcp_dispatch *d)
{
  struct tbs_raw *tr;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to acquire state");
    return -1;
  }

  stop_all_getap(d, 0);

  if(tr->r_fpga == TBS_FPGA_MAPPED){

    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unmapping fpga");
    unmap_raw_tbs(d);

    status_fpga_tbs(d, TBS_FPGA_PROGRAMMED);
  }

  if(tr->r_fpga == TBS_FPGA_PROGRAMMED){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "should deprogram fpga");
#if 0
    status_fpga_tbs(d, TBS_FPGA_DOWN);
#endif

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

  return 0;
}

int start_fpga_tbs(struct katcp_dispatch *d, struct bof_state *bs)
{
  struct tbs_raw *tr;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to acquire state");
    return -1;
  }

  if((tr->r_registers)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga seems already programmed");
    return -1;
  }

  tr->r_registers = create_avltree();
  if(tr->r_registers == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create register lookup structure");
    return -1;
  }

  if(program_bof(d, bs, TBS_FPGA_CONFIG) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to program bit stream to %s", TBS_FPGA_CONFIG);
    return -1;
  }

  status_fpga_tbs(d, TBS_FPGA_PROGRAMMED);

  if(index_bof(d, bs) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to load register mapping");
    return -1;
  }

  if(map_raw_tbs(d) < 0){
    return -1;
  }

  return 0;
}

/*********************************************************************/

void destroy_raw_tbs(struct katcp_dispatch *d, struct tbs_raw *tr)
{
  if(tr == NULL){
    return;
  }

  /* slight duplication of stop_fpga_tbs */

  stop_all_getap(d, 1);

  if(tr->r_fpga == TBS_FPGA_MAPPED){
    /* TODO */

    tr->r_fpga = TBS_FPGA_PROGRAMMED;
    /* status_fpga_tbs(d, TBS_FPGA_PROGRAMMED); would be noisy */
  }

  if(tr->r_registers){
    destroy_avltree(tr->r_registers, &free_entry);
    tr->r_registers = NULL;
  }

  if(tr->r_image){
    free(tr->r_image);
    tr->r_image = NULL;
  }

  /**********************/

  if (tr->r_hwmon){
    destroy_avltree(tr->r_hwmon, &destroy_hwsensor_tbs);
    tr->r_hwmon = NULL;
  }

  if(tr->r_chassis){
    unlink_arb_katcp(d, tr->r_chassis);
    tr->r_chassis = NULL;
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
    return 0;
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

int setup_raw_tbs(struct katcp_dispatch *d, char *bofdir, int argc, char **argv)
{
  struct tbs_raw *tr;
  int result;
#if 0
  struct sigaction sa;
#endif

  tr = malloc(sizeof(struct tbs_raw));
  if(tr == NULL){
    return -1;
  }

  tr->r_registers = NULL;
  tr->r_hwmon = NULL;
  tr->r_fpga = TBS_FPGA_DOWN;

  tr->r_map = NULL;
  tr->r_map_size = 0;

  tr->r_image = NULL;
  tr->r_bof_dir = NULL;

  tr->r_top_register = 0;

  tr->r_argc = argc;
  tr->r_argv = argv;

  tr->r_chassis = NULL;

  tr->r_taps = NULL;
  tr->r_instances = 0;

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

#if 0
  sa.sa_handler = handle_bus_error;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&(sa.sa_mask));

  sigaction(SIGBUS, &sa, NULL);
#endif

  bus_error_happened = 0;

  result = 0;

  result += register_flag_mode_katcp(d, "?uploadbof",    "upload a (possibly compressed) boffile (?uploadbof port filename [length [timeout]])", &uploadbof_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?upload",       "upload and program a (possibly compressed) boffile (?upload [port [length [timeout]]])", &upload_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?register",     "name a memory location (?register name position bit-offset length)", &register_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?write",        "write binary data to a named register (?write name byte-offset:bit-offset value byte-length:bit-length)", &write_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?read",         "read binary data from a named register (?read name byte-offset:bit-offset byte-length:bit-length)", &read_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?wordwrite",    "write hex words to a named register (?wordwrite name index value+)", &word_write_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?wordread",     "read hex words from a named register (?wordread name word-offset:bit-offset word-count)", &word_read_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?progdev",      "program the fpga (?progdev [filename])", &progdev_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?fpgastatus",   "display if the fpga is programmed (?fpgastatus)", &fpgastatus_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?status",       "compatebility alias for fpgastatus, use fpgastatus in new code (?status)", &fpgastatus_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?listdev",      "lists available registers (?listdev [size|detail]", &listdev_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?listbof",      "display available bof files (?listbof)", &listbof_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?delbof",       "deletes a gateware image (?delbof image-file)", &delbof_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?tap-start",    "start a tap instance (?tap-start (?tap-start tap-device register-name ip-address [port [mac]])", &tap_start_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?tap-stop",     "deletes a tap instance (?tap-stop register-name)", &tap_stop_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?tap-info",     "displays diagnostics for a tap instance (?tap-info register-name)", &tap_info_cmd, 0, TBS_MODE_RAW);

  result += register_flag_mode_katcp(d, "?chassis-start",  "initialise chassis interface", &start_chassis_cmd, 0, TBS_MODE_RAW);
  result += register_flag_mode_katcp(d, "?chassis-led",    "set a chassis led (?chassis-led led state)", &led_chassis_cmd, 0, TBS_MODE_RAW);

  tr->r_chassis = chassis_init_tbs(d, TBS_ROACH_CHASSIS);
  if(tr->r_chassis){
    hook_commands_katcp(d, KATCP_HOOK_PRE, &pre_hook_led_cmd);
    hook_commands_katcp(d, KATCP_HOOK_POST, &post_hook_led_cmd);
  }

  return result;
}

