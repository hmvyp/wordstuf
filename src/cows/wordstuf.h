// Constant overhead "Word stuffing" utility: similar to COBS but uses 4 bytes marker
// and 4 bytes offsets.
// Up to 1GB frame size with constant overhead (+8 bytes).

#ifndef wordstuf_h
#define wordstuf_h

#include <stdint.h>
#include <string.h>


// all user-defined buffers shall reserve additional space:
#define COWS_BUFFERS_EXTRA_SPACE 8

// marker requirements: all 4 bytes shall be different and all >= 0x80
#define COWS_MARKER 0xCAFEB8DA

typedef enum CowsParserStatus{
  COWS_PARSING = 0,
  COWS_ERROR,
  COWS_ERROR_DST_OVERRUN,
  COWS_ERROR_OFFSET_TOO_BIG,
  COWS_ERROR_OFFSET_OUT_OF_BOUNDS,
  COWS_ERROR_OFFSET_ODD
}CowsParserStatus;

typedef struct CowsParserState{
  CowsParserStatus status;
  // size_t pos; // source buffer position (next after parsed stuff)
  unsigned char* dst_buffer; // shall be at least frame size + 8 to hold first offset and marker at the end !!!
  size_t dst_len;
  size_t dst_pos;
}CowsParserState;

typedef void (*CowsOnframeCallback) (void* frame_data, uint32_t frame_length, void* user);

typedef struct CowsParser{
  CowsParserState state;
  uint32_t bb; // current 4 bytes
  CowsOnframeCallback onframe;
  void* onframe_user;
} CowsParser;

static inline void
cowsInitParser(CowsParser* ps, void* frame_buf, uint32_t frame_buf_size, CowsOnframeCallback cb, void* cb_user){
  CowsParser tmp = {0};
  *ps = tmp;
  ps->state.dst_buffer = frame_buf;
  ps->state.dst_len = frame_buf_size;
  ps->onframe = cb;
  ps->onframe_user = cb_user;
}

static inline void
cowsWriteUint32(unsigned char* b, uint32_t w){ // buffer capacity shall be at least 4 bytes
  b[0] = (unsigned char) w;
  b[1] = (unsigned char) (((uint32_t)w) >> 8);
  b[2] = (unsigned char) (((uint32_t)w) >> 16);
  b[3] = (unsigned char) (((uint32_t)w) >> 24);
}

// offset code has first and last bytes < 128 to avoid accidental overlapping with marker bytes
static inline uint32_t
cowsReadOffset(unsigned char* b){
  return (((uint32_t)b[0] << 1) | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24)) >> 1;
}

static inline void
cowsWrieOffset(unsigned char* b, uint32_t off){
  // least significant byte represents 7 least significant bits of the offset
  // other offset bits (7..31) just shifted to the left
  uint32_t off_code = ((off << 1) & ~(uint32_t)0xFF)  | (off & 0x7F);
  cowsWriteUint32(b, off_code);
}

static inline size_t
findMarker(unsigned char* buf, size_t len, uint32_t* current4bytes){
  uint32_t bb = *current4bytes;
  size_t i;
  for(i = 0; i < len; ++i){
    bb = (bb >> 8) | (((uint32_t) buf[i]) << 24);
    if(bb == COWS_MARKER){
      *current4bytes = bb;
      return i+1; // position next to the marker;
    }
  }

  *current4bytes = bb;
  return -1; // not found (though *current4bytes may contain a part of marker sequence)
}

static inline void
cowsCopyData(CowsParser* ps, void* pstart, size_t limit){
  if(ps->state.dst_len - ps->state.dst_pos < limit) {
    ps->state.status = COWS_ERROR_DST_OVERRUN;
  }else{
    memcpy(ps->state.dst_buffer + ps->state.dst_pos, pstart, limit);
    ps->state.dst_pos += limit;
  }
}

static inline void
doReplacements(CowsParser* ps){
  unsigned char* b = ps->state.dst_buffer;
  uint32_t n = 0;
  uint32_t lim = ps->state.dst_pos;
  uint32_t off = 0;
  while(n + 4 <= lim){
    off =  cowsReadOffset(b + n);
    if(off > ((uint32_t)-1) >> 2) {
      ps->state.status = COWS_ERROR_OFFSET_TOO_BIG;
      return;
    }
    if(n > lim){
      ps->state.status = COWS_ERROR_OFFSET_OUT_OF_BOUNDS;
      return;
    }else if(n + 4 > lim) {
      ps->state.status = COWS_ERROR_OFFSET_ODD;
      return;
    }else if (n == lim) {
      break; // end of frame
    }else{
      if(n != 0) { // if not the first offset then replace offset with marker bytes:
        cowsWriteUint32(b + n, COWS_MARKER);
      }
      n += (off + 4);
   }
  }
}

static inline void
cowsOnMarkerFound(CowsParser* ps){
  if(ps->state.dst_pos > 0  && !ps->state.status){ // dst buffer not empty and no errors
    doReplacements(ps);
    ps->onframe(ps->state.dst_buffer + 4, ps->state.dst_pos - 4, ps->onframe_user);
  }

  ps->state.status = COWS_PARSING; // clear errors
  ps->state.dst_pos = 0;
  ps->bb = 0; // redundant?
}

static inline void
cowsParseChunk(CowsParser* ps, void* src, size_t src_length){
  unsigned char* pstart = (unsigned char*)src;
  size_t limit = src_length;

  while(limit > 0) {
    size_t mpos = findMarker(pstart, limit, &ps->bb); // returns position next to the marker
    if(mpos != -1) { // if marker found
      if(mpos > 4) {
        cowsCopyData(ps, pstart, mpos - 4);
      }else { // marker (or its tail) found at the beginning of the source data (the previous frame is over)
        ps->state.dst_pos -= (4 - mpos); // exclude (possible) marker bytes from the frame tail (is it safe????)
      }
      cowsOnMarkerFound(ps);
      pstart += mpos;
      limit -= mpos;
    }else{ // marker not found
      cowsCopyData(ps, pstart, limit);
      limit = 0;
    }
  }
}
/**
 * fr_dst shall reserve at least (fr_len + COWS_BUFFERS_EXTRA_SPACE)
 * the function returns size of the encoded buffer
 */
static inline uint32_t
cowsEncodeFrame(void* fr_src, uint32_t fr_len, void* fr_dst){
  unsigned char* pd = (unsigned char*) fr_dst; // destination as char array
  uint32_t dlen = fr_len + 4; // destination data length (including the first offset field, but excluding final marker)
  uint32_t bb = 0;
  uint32_t last_offset_idx = 4; // next to the offset bytes

  memset(fr_dst, 0, 4); // reserve space for 1st offset
  memcpy(fr_dst + 4, fr_src, fr_len);

  for(;;){
    size_t mp = findMarker(pd + last_offset_idx , dlen - last_offset_idx , &bb); // next to the marker or -1
    if(mp == -1) { // marker not found
      cowsWrieOffset(fr_dst + last_offset_idx - 4, dlen - last_offset_idx); // offset to the end of data (i.e final marker)
      cowsWriteUint32(fr_dst + dlen, COWS_MARKER);
      return dlen + 4; // the whole frame size (first offset + data + final marker)
    }else{
      cowsWrieOffset(fr_dst + last_offset_idx - 4, mp - 4);
      last_offset_idx += mp;
    }
  }

  return 0;// unreachable, just calm gcc
}

#endif
