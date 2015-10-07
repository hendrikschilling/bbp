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

#ifdef __APPLE__
#define clock_gettime(A,B)
#endif

double ms_delta(struct timespec start, struct timespec stop)
{
  return (stop.tv_sec - start.tv_sec) * 1000.0 + (stop.tv_nsec - start.tv_nsec) / 1000000.0;
}

#define CHUNK_SIZE (64*1024)
//#define USE_MMAP_READ
//#define USE_MMAP_WRITE
//#define VERIFY_DECODE

#define BENCHMARK_ITERATIONS 16

#define STR(X) #X

#ifdef USE_MMAP_READ
#define USE_MMAP
#endif
#ifdef USE_MMAP_WRITE
#define USE_MMAP
#endif

void help(void)
{
  printf("usage: bbp_test <mode> <in> <out> <blocksize> <blocksize2> <offset>\n");
  printf("where mode is either 'e' for encoding or 'd' for decoding and\n");
  printf("blocksizes must be a power of 2 between 4 and " STR(BBP_MAX_BLOCK_SIZE) " (0 for default)\n");
  printf("offset gives the coding distance and should be the line width in bytes\n");
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
  int b;
  off_t size = 0, size_c = 0;
  uint32_t len, len_c;
  int bs, bs2;
  int offset;
  FILE *in;
  void *in_buf, *out_buf;
  uint64_t full_len, out_len;
  struct stat st;
  void *in_map = NULL;
  void *out_map = NULL;
  int out_fd;
  double time = 0.0;
#ifdef USE_MMAP
  uint64_t out_mapped;
#endif
  
  struct timespec start, stop, start_full, stop_full;
  
  if (argc != 7 && argc != 4)
    help();
  if (strlen(argv[1]) != 1)
    help();
  if (argc == 7) {
    bs = atoi(argv[4]);
    bs2 = atoi(argv[5]);
    if (bs && (bs < 4 || bs > BBP_MAX_BLOCK_SIZE))
      help();
    if (bs2 > 0 && (bs2 < 4 || bs > BBP_MAX_BLOCK_SIZE))
      help();
    offset = atoi(argv[6]);
    assert(offset >= 16);
  }
  else
    if (argv[1][0] != 'd')
      help();
  
  //file handling
  in = fopen(argv[2], "r");
  assert(in);

  out_fd = open(argv[3], O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  assert(out_fd != -1);
  
  fstat(fileno(in), &st);
  full_len = st.st_size;
  
#ifdef USE_MMAP
  switch(argv[1][0]) {
    case 'e' : out_mapped = full_len*2;   break;
    case 'd' : out_mapped = CHUNK_SIZE*8; break;
    case 'm' : out_mapped = full_len; break;
  }
#endif
  
  //initialisation
#ifndef USE_MMAP_READ
  posix_memalign(&in_buf, BBP_ALIGNMENT, bbp_max_compressed_size(CHUNK_SIZE));
  //in_buf = malloc(bbp_max_compressed_size(CHUNK_SIZE));
#else
  in_map = mmap(NULL, full_len, PROT_READ, MAP_SHARED, fileno(in), 0);
  if (in_map == MAP_FAILED) {
    printf("WARNING: mmap failed for %s, using regular write\n", argv[2]);
    in_map = NULL;
    posix_memalign(&in_buf, BBP_ALIGNMENT, bbp_max_compressed_size(CHUNK_SIZE));
    //in_buf = malloc(bbp_max_compressed_size(CHUNK_SIZE));
  }
  else
    in_buf = in_map;
#endif
#ifndef USE_MMAP_WRITE
  posix_memalign(&out_buf, BBP_ALIGNMENT, bbp_max_compressed_size(CHUNK_SIZE));
  //out_buf = malloc(bbp_max_compressed_size(CHUNK_SIZE));
#else
  ftruncate(out_fd, out_mapped);
  out_map = mmap(NULL, out_mapped, PROT_WRITE, MAP_SHARED, out_fd, 0);
  if (out_map == MAP_FAILED) {
    printf("WARNING: mmap failed for %s, using regular write\n", argv[3]);
    out_map = NULL;
    posix_memalign(&out_buf, BBP_ALIGNMENT, bbp_max_compressed_size(CHUNK_SIZE));
    //out_buf = malloc(bbp_max_compressed_size(CHUNK_SIZE));
  }
  else
    out_buf = out_map;
#endif
  
  bbp_init();
  
  switch(argv[1][0]) {
    case 'e' :
      clock_gettime(CLOCK_MONOTONIC, &start_full);
#ifndef USE_MMAP_READ
      while ((len = fread(in_buf, 1, CHUNK_SIZE, in))) {
#else
      for(in_buf=in_map;in_buf-in_map<full_len;in_buf+=CHUNK_SIZE) {
        len = full_len - (in_buf-in_map);
        if (len > CHUNK_SIZE)
          len = CHUNK_SIZE;
#endif
	clock_gettime(CLOCK_MONOTONIC, &start);
        //compression
        for(b=0;b<BENCHMARK_ITERATIONS;b++)
          len_c = bbp_code_offset(in_buf, out_buf, bs, bs2, len, offset);
	clock_gettime(CLOCK_MONOTONIC, &stop);
        time += ms_delta(start, stop);
	size += len;
	size_c += len_c;

#ifdef VERIFY_DECODE
	bbp_header_sizes(out_buf, &len, &len_c);
        void *test_buf1, test_buf2;
        posix_memalign(&test_buf1, BBP_ALIGNMENT, len_c);
        posix_memalign(&test_buf2, BBP_ALIGNMENT, len);
        memcpy(test_buf1, out_buf, len_c);
        bbp_decode(out_buf, test_buf2);
        assert(!memcmp(test_buf2, in_buf, len));
        free(test_buf1);
        free(test_buf2);
#endif
        
        if (!out_map) {
          len = write(out_fd, out_buf, len_c);
          assert(len == len_c);
        }
        else
          out_buf += len_c;
      }
      out_len = size_c;
      
      clock_gettime(CLOCK_MONOTONIC, &stop_full);
      printf("compressed at %.3fMB/s / %.3fMB/s ratio %.2f\n",(float)size*BENCHMARK_ITERATIONS/1024/1024*1000/time, (float)size/1024/1024*1000/ms_delta(start_full, stop_full), (float)size/size_c);
      printf("%.2f %.3f bbp-%d-%d\n",(float)size/size_c, (float)size*BENCHMARK_ITERATIONS/1024/1024*1000/time, offset, bs);
      break;
    case 'm' :
      clock_gettime(CLOCK_MONOTONIC, &start_full);
#ifndef USE_MMAP_READ
      while ((len = fread(in_buf, 1, CHUNK_SIZE, in))) {
#else
      for(in_buf=in_map;in_buf-in_map<full_len;in_buf+=CHUNK_SIZE) {
        len = full_len - (in_buf-in_map);
        if (len > CHUNK_SIZE)
          len = CHUNK_SIZE;
#endif
	clock_gettime(CLOCK_MONOTONIC, &start);
        //compression
        for(b=0;b<BENCHMARK_ITERATIONS;b++)
          memcpy(out_buf, in_buf, len);
        len_c = len;
	clock_gettime(CLOCK_MONOTONIC, &stop);
        time += ms_delta(start, stop);
	size += len;
	size_c += len_c;
        if (!out_map) {
          len = write(out_fd, out_buf, len_c);
          assert(len == len_c);
        }
        else
          out_buf += len_c;
      }
      out_len = size_c;
      
      clock_gettime(CLOCK_MONOTONIC, &stop_full);
      printf("compressed at %.3fMB/s / %.3fMB/s ratio %.2f\n",(float)size*BENCHMARK_ITERATIONS/1024/1024*1000/time, (float)size/1024/1024*1000/ms_delta(start_full, stop_full), (float)size/size_c);
      printf("%.2f %.3f memcpy\n",(float)size/size_c, (float)size*BENCHMARK_ITERATIONS/1024/1024*1000/time);
      break;
    case 'd' :
      clock_gettime(CLOCK_MONOTONIC, &start_full);
#ifndef USE_MMAP_READ
      while ((len = fread(in_buf, 1, 64, in)) == 64) {
	bbp_header_sizes(in_buf, &len, &len_c);
	len = fread(in_buf+64, 1, len_c-64, in);
	assert(len == len_c - 64);
#else
      for(in_buf=in_map;in_buf-in_map+64<full_len;in_buf+=len_c) {
        if (out_buf-out_map+CHUNK_SIZE >= out_mapped) {
          munmap(out_map, out_mapped);
          out_mapped *= 2;
          ftruncate(out_fd, out_mapped);
          out_map = mmap(NULL, out_mapped, PROT_WRITE, MAP_SHARED, out_fd, 0);
          assert(out_map != MAP_FAILED);
          out_buf = out_map + size;
        }
        
	bbp_header_sizes(in_buf, &len, &len_c);
	if (len_c > full_len - (in_buf-in_map)) {
          printf("ERROR: corrupt input!\n");
          break;
        }
#endif
	clock_gettime(CLOCK_MONOTONIC, &start);
        //decompression
        len = bbp_decode(in_buf, out_buf);
	clock_gettime(CLOCK_MONOTONIC, &stop);
        time += ms_delta(start, stop);
	size += len;
	size_c += len_c;
        if (!out_map)
          write(out_fd, out_buf, len);
        else
          out_buf += len;
      }
      out_len = size;
      
      clock_gettime(CLOCK_MONOTONIC, &stop_full);
      printf("decompressed at %.3fMB/s / %.3fMB/s ratio %.2f\n",(float)size/1024/1024*1000/time, (float)size/1024/1024*1000/ms_delta(start_full, stop_full), (float)size/size_c);
      break;
  }
  
  if (in_map)
    munmap(in_map, full_len);
  fclose(in);
  
  if (out_map)
    munmap(out_map, full_len*2);
  ftruncate(out_fd, out_len);
  close(out_fd);
  
  bbp_shutdown();
  
  return EXIT_SUCCESS;
}
