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
#include <stdint.h>

#ifdef BBP_USE_SIMDCOMP
#include <simdcomp.h>
#endif
#ifdef BBP_USE_SQUASH
#include <squash/squash.h>
#endif

#include <time.h>

#ifdef __APPLE__
#define clock_gettime(A,B)
#endif

double ms_delta(struct timespec start, struct timespec stop)
{
  return (stop.tv_sec - start.tv_sec) * 1000.0 + (stop.tv_nsec - start.tv_nsec) / 1000000.0;
}

#ifdef BBP_USE_SIMDCOMP
/* from simdcomp's example.c */
/* compresses data from datain to buffer, returns how many bytes written */
size_t compress(uint32_t * datain, size_t length, uint8_t * buffer) {
    uint32_t offset;
    uint8_t * initout;
    size_t k;
	if(length/SIMDBlockSize*SIMDBlockSize != length) {
	    printf("Data length should be a multiple of %i \n",SIMDBlockSize);
	}
    offset = 0;
    initout = buffer;
	for(k = 0; k < length / SIMDBlockSize; ++k) {
        uint32_t b = simdmaxbitsd1(offset,
                    datain + k * SIMDBlockSize);
		*buffer++ = b;
		simdpackwithoutmaskd1(offset, datain + k * SIMDBlockSize, (__m128i *) buffer,
                    b);
        offset = datain[k * SIMDBlockSize + SIMDBlockSize - 1];
        buffer += b * sizeof(__m128i);
	}
	return buffer - initout;
}
#endif

//cached 128KiB (fread/fwrite) 2060MiB/s
#define CHUNK_SIZE (64*1024)
//#define USE_MMAP_READ
//#define USE_MMAP_WRITE

#define STR(X) #X

#define BENCHMARK_ITERATIONS 16

#ifdef USE_MMAP_READ
#define USE_MMAP
#endif
#ifdef USE_MMAP_WRITE
#define USE_MMAP
#endif

void help(void)
{
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
  int ret;
  off_t size = 0, size_c = 0;
  size_t len, len_c;
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
  
  if (argc < 4 || argc > 6)
    help();
  if (argc == 5 || argc == 6)
    assert(*argv[1] == 's');
  if (strlen(argv[1]) != 1)
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
  in_buf = malloc(CHUNK_SIZE);
#else
  in_map = mmap(NULL, full_len, PROT_READ, MAP_SHARED, fileno(in), 0);
  if (in_map == MAP_FAILED) {
    printf("WARNING: mmap failed for %s, using regular write\n", argv[2]);
    in_map = NULL;
    in_buf = malloc(2*CHUNK_SIZE);
  }
  else
    in_buf = in_map;
#endif
#ifndef USE_MMAP_WRITE
  out_buf = malloc(2*CHUNK_SIZE);
#else
  ftruncate(out_fd, out_mapped);
  out_map = mmap(NULL, out_mapped, PROT_WRITE, MAP_SHARED, out_fd, 0);
  if (out_map == MAP_FAILED) {
    printf("WARNING: mmap failed for %s, using regular write\n", argv[3]);
    out_map = NULL;
    out_buf = malloc(2*CHUNK_SIZE);
  }
  else
    out_buf = out_map;
#endif
  
  switch(argv[1][0]) {
#ifdef BBP_USE_SIMDCOMP
    case 'e' :
      clock_gettime(CLOCK_MONOTONIC, &start_full);
#ifndef USE_MMAP_READ
      while ((len = fread(in_buf, 1, CHUNK_SIZE, in))) {
#else
      for(in_buf=in_map;in_buf-in_map<full_len;in_buf+=CHUNK_SIZE) {
        len = full_len - (in_buf-in_map);
        if (len > CHUNK_SIZE)
          len = CHUNK_SIZE;
        if (len % (128*4))
          break;
#endif
        int b;
	clock_gettime(CLOCK_MONOTONIC, &start);
        //compression
        for(b=0;b<BENCHMARK_ITERATIONS;b++)
          len_c = compress(in_buf, len/4, out_buf);
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
      printf("compressed at %.3f MB/s / %.3fMB/s ratio %.2f\n",(float)size*BENCHMARK_ITERATIONS/1024/1024*1000/time, (float)size/1024/1024*1000/ms_delta(start_full, stop_full), (float)size/size_c);
      printf("%.2f %.3f simdcomp\n",(float)size/size_c, (float)size*BENCHMARK_ITERATIONS/1024/1024*1000/time);
      break;
#endif
#ifdef BBP_USE_SQUASH
    case 's' :
      assert(argc == 5 || argc == 6);
      SquashCodec* codec = squash_get_codec(argv[4]);
      SquashOptions *options;
      assert(codec);
      
      if (argc == 6) {
        options = squash_options_new(codec, NULL);
        ret = squash_options_parse_option(options, "level", argv[5]);
        if (ret != SQUASH_OK) {
          if (ret == SQUASH_BAD_PARAM) printf("ERROR: SQUASH: bad param\n");
          if (ret == SQUASH_BAD_VALUE) printf("ERROR: SQUASH: bad value\n");
          break;
        }
      }
      else
        options = NULL;
      
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
        len_c = 2*CHUNK_SIZE;
        if (options)
          squash_codec_compress_with_options(codec, &len_c, out_buf, len, in_buf, options);
        else
          ret = squash_compress(argv[4], &len_c, out_buf, len, in_buf, NULL);
        if (ret != SQUASH_OK)
          printf("ERROR: SQUASH (%s): %s\n",squash_status_to_string(ret), argv[4]);
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
      printf("compressed at %.3fMB/s / %.3fMB/s ratio %.2f\n",(float)size/1024/1024*1000/time, (float)size/1024/1024*1000/ms_delta(start_full, stop_full), (float)size/size_c);
      printf("%.2f %.3f %s\n",(float)size/size_c, (float)size/1024/1024*1000/time, argv[4]);
      break;
#endif
  }
  
  if (in_map)
    munmap(in_map, full_len);
  fclose(in);
  
  if (out_map)
    munmap(out_map, full_len*2);
  ftruncate(out_fd, out_len);
  close(out_fd);

  
  return EXIT_SUCCESS;
}
