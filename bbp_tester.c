/*
 *
 *  BBP - high speed image compressor using block-wise bitpacking
 *
 *  Copyright (C) 2014-2015 Hendrik Siedelmann <hendrik.siedelmann@googlemail.com>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <time.h>

#include "bbp.h"


#define COMP_CHUNK_SIZE (16*1024*1024)

void check_decode(uint8_t *in, uint8_t *comp, int len)
{  
  int i;
  uint32_t size, size_c;
  uint8_t *dec, *comp_cpy;
  
  bbp_header_sizes(comp, &size, &size_c);
  
  assert(size == len);
  
  comp_cpy = malloc(size_c);
  dec = malloc(size);
  //printf("dec: %p\n", dec);
  
  memcpy(comp_cpy, comp, size_c);
  
  bbp_decode(comp_cpy, dec);
  
  for(i=0;i<len;i++)
    if (dec[i] != in[i])
      abort();
    
  free(dec);
  free(comp_cpy);
}

int main(int argc, char *argv[])
{
  int len;
  assert(argc == 2);
  FILE *f = fopen(argv[1], "r");
  assert(f);
  
  uint8_t *in_buf, *out_buf;
  in_buf = malloc(COMP_CHUNK_SIZE);
  out_buf = malloc(bbp_max_compressed_size(COMP_CHUNK_SIZE));
  
  len = fread(in_buf, 1, COMP_CHUNK_SIZE, f);
  fclose(f);
  
  bbp_init();
  
  assert(len == COMP_CHUNK_SIZE);
  
  for(len=1;len<COMP_CHUNK_SIZE;len++) {
    printf("testing length %d\n", len);
    bbp_code_offset(in_buf, out_buf, 4, 4, len, 16);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 8, 4, len, 16);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 16, 8, len, 16);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 32, 128, len, 16);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 128, 32, len, 16);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 2048, 32, len, 16);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 2048, 2048, len, 16);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 4, 4, len, 91);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 8, 4, len, 91);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 16, 8, len, 91);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 32, 128, len, 91);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 128, 32, len, 91);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 2048, 32, len, 91);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 2048, 2048, len, 91);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 4, 4, len, 1281);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 8, 4, len, 1281);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 16, 8, len, 1281);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 32, 128, len, 1281);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 128, 32, len, 1281);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 2048, 32, len, 1281);
    check_decode(in_buf, out_buf, len);
    bbp_code_offset(in_buf, out_buf, 2048, 2048, len, 1281);
    check_decode(in_buf, out_buf, len);
    if (len > 16000)
      len += rand() % len/10;
  }
  
  bbp_shutdown();
  
  return EXIT_SUCCESS;
}
