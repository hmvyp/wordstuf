#ifndef wordstuf_test_h
#define wordstuf_test_h

#include "../wordstuf/wordstuf.h"

static unsigned char b[5000]; // 5000
#define bsize (sizeof(b))

static void
init_src_buf(){
  int i;
  for(i= 0; i != bsize; ++i){
    b[i] = i;
  }

  for(i=0; i + 4 < bsize; i += (4 + i/10)){
    cowsWriteUint32(b + i, COWS_MARKER);
  }
}

static unsigned char sb[bsize + 100]; // send buffer (for encoded frames)

static unsigned char rb[bsize + COWS_BUFFERS_EXTRA_SPACE]; // buffer to receive (decoded) frames;

static int rb_idx; // current position in rb

static void onframe(void* frame_data, uint32_t frame_length, void* user){
  (void)user;
  // printf("\n onframe() called\n");
  memcpy(rb + rb_idx, frame_data, frame_length);
  rb_idx += frame_length;
}

static char rb_tmp[bsize + COWS_BUFFERS_EXTRA_SPACE]; // for parser


static int cows1test(int k, int m){
  // encode 3 frames and put them sequentially into sb:
  uint32_t f0 = cowsEncodeFrame(b, k, sb);
  uint32_t f1 = cowsEncodeFrame(b + k, m - k, sb + f0);
  uint32_t f2 = cowsEncodeFrame(b + m, bsize - m, sb + f0 + f1);

  uint32_t ssize = f0 + f1 + f2; // size of "send" buffer

  CowsParser ps;
  cowsInitParser(&ps, rb_tmp, sizeof(rb_tmp), onframe, (void*) 0);

  // break send buffer into 2 "network packets":
  int i;
  for(i = 0; i < ssize; ++i){

      rb_idx = 0;
      memset(rb, 0, sizeof(rb));
      cowsParseChunk(&ps, sb, i);
      // printf("\n parsing status code (1) %d \n", ps.state.status);
      cowsParseChunk(&ps, sb + i, ssize - i);
      // printf("\n parsing status code (2) %d \n", ps.state.status);
      if(memcmp(b, rb, bsize) != 0){
        printf("\n error occurred \n");
        return -1;
      }
  }

  return 0;
}

static int cows_all_tests(){
  int res =0;
  int k, m; // boundaries between 3 frames
  int count =0;
  init_src_buf();

  printf("\n starting word stuffing tests... \n");

  for(k =0; k < bsize; k += 1 + k/10 ){
    for(m = k; m < bsize; m += 1 + (m -k)/100){
      ++count;
      if(count % 100 == 0) {
        printf("\n k =%d   m = %d \n", k, m);
      }
      res = cows1test(k,m);
      if(res != 0){
        return res; // error
      }
    }
  }
  printf("\n ... tests passed \n");
  return res;
}

#endif
