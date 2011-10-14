
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <katcp.h>
#include <avltree.h>

#include "tcpborphserver3.h"

int enter_raw_tbs(struct katcp_dispatch *d, struct katcp_notice *n, char *flags, unsigned int from)
{
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "now running in raw mode");

  return 0;
}

int read_cmd(struct katcp_dispatch *d, int argc)
{
  struct tbs_raw *tr;
  struct tbs_entry *te;
  char *name;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL){
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

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "attempting to read from %u:%u", te->e_pos_base, te->e_pos_offset);

  return KATCP_RESULT_FAIL;
}

int progdev_cmd(struct katcp_dispatch *d, int argc)
{
  char *file;

  if(argc > 1){
    file = arg_string_katcp(d, 1);
    if(file == NULL){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to acquire file name to program");
      return KATCP_RESULT_FAIL;
    }
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "attempting to program %s", file);
  } else {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "attempting to empty fpga");
  }

  return KATCP_RESULT_FAIL;
}

void free_entry(void *data)
{
  struct tbs_entry *te;

  te = data;

  /* TODO: maybe add magic field to check we are deleting the correct thing */
  if(te){
    free(te);
  }
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

void destroy_raw_tbs(struct katcp_dispatch *d, struct tbs_raw *tr)
{
  if(tr == NULL){
    return;
  }

  if(tr->r_registers){
    destroy_avltree(tr->r_registers, &free_entry);
    tr->r_registers = NULL;
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

int setup_raw_tbs(struct katcp_dispatch *d)
{
  struct tbs_raw *tr;
  int result;

  tr = malloc(sizeof(struct tbs_raw));
  if(tr == NULL){
    return -1;
  }

  tr->r_registers = NULL;
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
  result += register_flag_mode_katcp(d, "?read",     "read data from a defined register (?read name TODO)", &read_cmd, 0, TBS_MODE_RAW);

  return 0;
}

