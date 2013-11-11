#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <katpriv.h>
#include <katcl.h>

int make_bb_katcl(struct katcl_byte_bit *bb, unsigned long byte, unsigned long bit)
{
  unsigned long delta;

  delta = bit / 8;

  /* WARNING: don't use normalise, sizeof bit is larger than structure can hold */
  if((byte + delta) < byte){
    /* unsigned inputs can wrap, making it an invalid address */
    return -1;
  }

  bb->b_byte = byte + delta;
  bb->b_bit  = bit - (delta * 8);

  if(bb->b_byte % 4){
    bb->b_align = KATCL_ALIGN_BYTE;
  } else {
    bb->b_align = KATCL_ALIGN_WORD | KATCL_ALIGN_BYTE;
  }

#ifdef KATCP_CONSISTENCY_CHECKS
  if(bb->b_bit >= 8){
    fprintf(stderr, "byte: internal error, bit field is %u", bb->b_bit);
    abort();
  }
#endif

  return 0;
}

int byte_normalise_bb_katcl(struct katcl_byte_bit *bb)
{
  struct katcl_byte_bit tmp;

  if(bb->b_align & KATCL_ALIGN_BYTE){
    return 0;
  }

  tmp.b_byte = bb->b_byte;
  tmp.b_bit  = bb->b_bit;

  bb->b_byte = tmp.b_byte + (tmp.b_bit / 8);
  bb->b_bit  = tmp.b_bit % 8;

  if(bb->b_byte < tmp.b_byte){
    return -1;
  }

  if(bb->b_byte % 4){
    bb->b_align = KATCL_ALIGN_BYTE;
  } else { 
    bb->b_align = KATCL_ALIGN_WORD | KATCL_ALIGN_BYTE;
  }   

  return 0;
}

int word_normalise_bb_katcl(struct katcl_byte_bit *bb)
{   
  struct katcl_byte_bit tmp;

  if(bb->b_align & KATCL_ALIGN_WORD){
    return 0;
  }

  tmp.b_byte  = bb->b_byte;
  tmp.b_bit   = bb->b_bit;
  tmp.b_align = bb->b_align;

  if(!(bb->b_align & KATCL_ALIGN_BYTE)){
    if(byte_normalise_bb_katcl(&tmp) < 0){
      return -1;
    }
  }

  bb->b_byte = (tmp.b_byte / 4) * 4;
  bb->b_bit  = ((tmp.b_byte % 4) * 8) + tmp.b_bit;

  if(bb->b_bit < 8){
    bb->b_align = KATCL_ALIGN_WORD | KATCL_ALIGN_BYTE;
  } else {
    bb->b_align = KATCL_ALIGN_WORD;
  }

  return 0;
}

int exceeds_bb_katcl(struct katcl_byte_bit *bb, struct katcl_byte_bit *limit)
{
  struct katcl_byte_bit tmp;

  /* FAST case, normalisations intersect */
  if((bb->b_align & limit->b_align) > 0){
    if(bb->b_byte < limit->b_byte){
      return 0;
    }
    if(bb->b_byte > limit->b_byte){
      return 1;
    }
    /* now bytes are equal */
    if(bb->b_bit > limit->b_bit){
      return 1;
    }
    return 0;
  }

  /* SLOW, uncommon cases: up to two recursions for cases where things don't intersect */
  if(bb->b_align & KATCL_ALIGN_BYTE){
    tmp.b_byte  = limit->b_byte;
    tmp.b_bit   = limit->b_bit;
    tmp.b_align = limit->b_align;
    if(byte_normalise_bb_katcl(&tmp) < 0){
      return -1;
    }
    return exceeds_bb_katcl(bb, &tmp);
  } else {
    tmp.b_byte  = bb->b_byte;
    tmp.b_bit   = bb->b_bit;
    tmp.b_align = bb->b_align;
    if(byte_normalise_bb_katcl(&tmp) < 0){
      return -1;
    }
    return exceeds_bb_katcl(&tmp, limit);
  }

  return -1;
}

int add_bb_katcl(struct katcl_byte_bit *sigma, struct katcl_byte_bit *alpha, struct katcl_byte_bit *beta)
{
  struct katcl_byte_bit tmp;

  tmp.b_byte = alpha->b_byte + beta->b_byte;
  if(tmp.b_byte < alpha->b_byte){
    return -1;
  }

  tmp.b_bit = alpha->b_bit + beta->b_bit;
  if(tmp.b_bit < alpha->b_bit){
    return -1;
  }
  tmp.b_align = 0;

  if((alpha->b_align & beta->b_align) | KATCL_ALIGN_WORD){
    word_normalise_bb_katcl(&tmp);
  } else {
    byte_normalise_bb_katcl(&tmp);
  }

  sigma->b_byte  = tmp.b_byte;
  sigma->b_bit   = tmp.b_bit;
  sigma->b_align = tmp.b_align;

  return 0;
}

#ifdef UNIT_TEST_BYTE_BIT

int check_bb_katcl(struct katcl_byte_bit *bb, unsigned long byte, unsigned char bit, unsigned char align)
{
  if(bb->b_byte != byte){
    fprintf(stderr, "check: byte was %lu, not %lu\n", bb->b_byte, byte);
    abort();
  }

  if(bb->b_bit != bit){
    fprintf(stderr, "check: bit was %u, not %u\n", bb->b_bit, bit);
    abort();
  }

  if((bb->b_align & align) != align){
    fprintf(stderr, "check: align was 0x%x, not 0x%x\n", bb->b_align, align);
    abort();
  }

  printf("check ok: %lu:%u (0x%x)\n", bb->b_byte, bb->b_bit, bb->b_align);
  return 0;
}

#include <unistd.h>

int main()
{
  struct katcl_byte_bit alpha, beta, gamma;
  int i;

  make_bb_katcl(&alpha, 4, 32);
  make_bb_katcl(&beta,  0,  1);

  byte_normalise_bb_katcl(&alpha);
  check_bb_katcl(&alpha, 8, 0, KATCL_ALIGN_WORD | KATCL_ALIGN_BYTE);

  add_bb_katcl(&gamma, &alpha, &beta);
  check_bb_katcl(&gamma, 8, 1, KATCL_ALIGN_WORD);

  make_bb_katcl(&beta,  0,  8);
  add_bb_katcl(&gamma, &gamma, &beta);
  byte_normalise_bb_katcl(&gamma);
  check_bb_katcl(&gamma, 9, 1, KATCL_ALIGN_BYTE);

  if(exceeds_bb_katcl(&gamma, &gamma)){
    fprintf(stderr, "check: limit failed\n");
    abort();
  }

  make_bb_katcl(&beta,  0,  1);
  add_bb_katcl(&alpha, &gamma, &beta);

  if(exceeds_bb_katcl(&gamma, &alpha)){
    fprintf(stderr, "check: limit failed\n");
    abort();
  }

  if(exceeds_bb_katcl(&alpha, &gamma) != 1){
    fprintf(stderr, "check: limit failed\n");
    abort();
  }

  srand(getpid());

  for(i = 0; i < 1000000; i++){
    make_bb_katcl(&alpha, rand() % 100000, rand() % 100000);

    beta.b_byte  = alpha.b_byte;
    beta.b_bit   = alpha.b_bit;
    beta.b_align = alpha.b_align;

    word_normalise_bb_katcl(&alpha);
    byte_normalise_bb_katcl(&alpha);
    word_normalise_bb_katcl(&alpha);

    if(exceeds_bb_katcl(&beta, &alpha) || exceeds_bb_katcl(&alpha, &beta)){
      fprintf(stderr, "check: normalisation of %lu:%u 0x%x broke into %lu:%u 0x%x\n", beta.b_byte, beta.b_bit, beta.b_align, alpha.b_byte, alpha.b_bit, alpha.b_align);
      abort();
    } 

    if(!(i % 16)){
      printf("\r%d", i);
    }

  }

  printf("\rrandom test: %d ok\n", i);

  return 0;
}

#endif
