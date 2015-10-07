cmake_minimum_required (VERSION 2.8)

project(bbp)

include(FindPkgConfig)
include(CheckCCompilerFlag)

string(ASCII 27 Esc)
set(ColourReset "${Esc}[m")
set(ColourBold  "${Esc}[1m")
set(Red         "${Esc}[31m")
set(Green       "${Esc}[32m")
set(Yellow      "${Esc}[33m")
set(Blue        "${Esc}[34m")
set(Magenta     "${Esc}[35m")
set(Cyan        "${Esc}[36m")
set(White       "${Esc}[37m")
set(BoldRed     "${Esc}[1;31m")
set(BoldGreen   "${Esc}[1;32m")
set(BoldYellow  "${Esc}[1;33m")
set(BoldBlue    "${Esc}[1;34m")
set(BoldMagenta "${Esc}[1;35m")
set(BoldCyan    "${Esc}[1;36m")
set(BoldWhite   "${Esc}[1;37m")

option(build_with_simdcomp "also benchmark simdcomp" off)
option(FORCE_ON_SSSE3 "force on SSSE3" off)
option(FORCE_ON_AVX2 "force on AVX2" off)
option(FORCE_OFF_SSSE3 "force on SSSE3" off)
option(FORCE_OFF_AVX2 "force on AVX2" off)

if (build_with_simdcomp)
  add_definitions(-DBBP_USE_SIMDCOMP)
  set(BBP_LINK_BENCHMARK ${BBP_LINK_BENCHMARK} "simdcomp")
endif()

#check if squash is available (for benchmark comparisons)
pkg_check_modules(SQUASH squash-0.5)
include_directories(${SQUASH_INCLUDE_DIRS})
link_directories(${SQUASH_LIBRARY_DIRS})
if (SQUASH_FOUND)
  add_definitions(-DBBP_USE_SQUASH)
  set(BBP_LINK_BENCHMARK ${BBP_LINK_BENCHMARK} ${SQUASH_LIBRARIES})
endif()

if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    # using regular Clang or AppleClang
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g -Wall")
    add_definitions(-DCOMPILER_CLANG)
elseif (CMAKE_C_COMPILER_ID MATCHES "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g -Wall -flto=8 -funroll-loops -funroll-all-loops")
    add_definitions(-DCOMPILER_GCC)
endif()

#TODO
#check wether compiler has switch
#the -Werror is required for at least clang <= 3.5 with cmake <= 3.0.2
#CHECK_C_COMPILER_FLAG("-Werror -mssse3" HAVE_MSSSE3_SWITCH)

execute_process(COMMAND echo "" COMMAND gcc -march=native -dM -E -x c - COMMAND grep SSSE3 OUTPUT_VARIABLE HAVE_MSSSE3_SWITCH)
if((HAVE_MSSSE3_SWITCH OR FORCE_ON_SSSE3) AND NOT FORCE_OFF_SSSE3)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mssse3")
  set(HAVE_VECTOR_INSTR TRUE)
  add_definitions(-DBBP_USE_SSE)
  add_definitions(-DBBP_USE_SIMD)
  
  
  #CHECK_C_COMPILER_FLAG("-Werror -mavx2" HAVE_MAVX2_SWITCH)
  execute_process(COMMAND echo "" COMMAND gcc -march=native -dM -E -x c - COMMAND grep AVX2 OUTPUT_VARIABLE HAVE_MSSSE3_SWITCH)
  IF((HAVE_MAVX_SWITCH OR FORCE_ON_AVX2) AND NOT FORCE_OFF_AVX2)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx2 -mavx")
    add_definitions(-DBBP_USE_AVX2)
    set(BBP_SIMD_STRING "x86 with AVX2 or better")
  else()
    set(BBP_SIMD_STRING "x86 with SSSE3 or better")
  endif()
endif()



#!FIXME this should error out on clang but doesn't!
CHECK_C_COMPILER_FLAG("-Werror -march=native" HAVE_MARCH_NATIVE_SWITCH)
IF(HAVE_MARCH_NATIVE_SWITCH)
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
ENDIF()

if (CMAKE_C_COMPILER_ID MATCHES "Clang")
  #!FIXME mcpu is needed by clang else NEON stays disabled!
  CHECK_C_COMPILER_FLAG("-mfpu=neon -Werror" HAVE_NEON_SWITCH)
else()
  CHECK_C_COMPILER_FLAG(-mfpu=neon HAVE_NEON_SWITCH)
endif()
IF(HAVE_NEON_SWITCH)
  #!FIXME march=native for arm?
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpu=neon -mcpu=cortex-a7")
  #for gcc style to neon conversion, required at least for gcc
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flax-vector-conversions")
  SET(HAVE_VECTOR_INSTR TRUE)
  add_definitions(-DBBP_USE_NEON)
  add_definitions(-DBBP_USE_SIMD)
  set(BBP_SIMD_STRING "ARM neon")
ENDIF()

if (APPLE)
    add_definitions(-DMACHACKS)
    #for cmake < 3.0
    if (NOT DEFINED CMAKE_MACOSX_RPATH)
        set(CMAKE_MACOSX_RPATH 0)
    endif()
  set(BBP_BUILD_BENCHMARK false)
  set(BENCH_ERROR_STR "workaround needed for missing clock_gettime() on OSX")
else()
  set(BBP_BUILD_BENCHMARK true)
endif()

message(STATUS "configuration summary:")
if(HAVE_VECTOR_INSTR)
  message(STATUS "${Green}SIMD support        - yes (${BBP_SIMD_STRING})${ColourReset}")
else()
  message(STATUS "${BoldRed}SIMD support        - no, using c fallback${ColourReset}")
endif()

if(BBP_BUILD_BENCHMARK)
  message(STATUS "${Green}benchmark tools     - yes${ColourReset}")
  if (build_with_simdcomp)
    message(STATUS "${Green}   benchmark simdcomp   - yes${ColourReset}")
  else()
    message(STATUS "${BoldRed}   benchmark simdcomp   - no (enable with \"cmake -D build_with_simdcomp=on\")${ColourReset}")
  endif()
  if (SQUASH_FOUND)
    message(STATUS "${Green}   benchmark squash     - yes${ColourReset}")
  else()
    message(STATUS "${BoldRed}   benchmark squash     - no (not found)${ColourReset}")
  endif()
else()
  message(STATUS "${BoldRed}benchmark tools   - no (${BENCH_ERROR_STR})${ColourReset}")
endif()

add_library(bbp SHARED bbp.c bitstream.c coding.c coding_helpers.c bitpacking.c common.c)

add_executable(bbp_cli bbp_cli.c)
add_executable(bbp_tester bbp_tester.c)
add_executable(benchmarks benchmarks.c)
set_target_properties(bbp_cli PROPERTIES OUTPUT_NAME "bbp")


target_link_libraries(bbp rt)
target_link_libraries(bbp_cli bbp)
target_link_libraries(bbp_tester bbp)
target_link_libraries(benchmarks ${BBP_LINK_BENCHMARK})

configure_file(bbp.pc.in bbp.pc @ONLY)

install(TARGETS bbp DESTINATION lib)
install(TARGETS bbp_cli DESTINATION bin)
install(FILES bbp.h DESTINATION include)
install(FILES bbp.pc DESTINATION lib/pkgconfig)
