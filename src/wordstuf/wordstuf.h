// Constant overhead "Word stuffing" utility to unambiguously split byte stream into frames
// The algorithm is conceptually similar to COBS but uses 4 bytes marker and 4 bytes offsets.
// Supports up to 1gb frame size, guarantees constant space overhead (+8 bytes).
//
// This file contains public interface

#ifndef wordstuf_h
#define wordstuf_h

#include <stdint.h>
#include <string.h>


// all user-defined destination buffers shall reserve additional space:
#define COWS_BUFFERS_EXTRA_SPACE 8

#ifndef COWS_MARKER
  // marker requirements: all 4 bytes shall be different and all >= 0x80
  // to exclude overlapping with offset codes and with each other.
  // The marker can be theoretically redefined but I hope the following is good enough:
# define COWS_MARKER 0xCAFEB8DA
#endif

typedef struct CowsParser CowsParser;

typedef enum CowsParserStatus{
  COWS_PARSING = 0,
  COWS_ERROR,
  COWS_ERROR_DST_OVERRUN,
  COWS_ERROR_OFFSET_TOO_BIG,
  COWS_ERROR_OFFSET_OUT_OF_BOUNDS,
  COWS_ERROR_OFFSET_ODD
}CowsParserStatus;

static inline CowsParserStatus
cowsParserGetStatus(CowsParser* ps);

typedef void (*CowsOnframeCallback) (void* frame_data, uint32_t frame_length, void* user);


static inline void
cowsInitParser(
    CowsParser* ps,
    void* frame_buf, // destination buffer to store decoded frame
    uint32_t frame_buf_size, // the buffer capacity
                             // (shall reserve COWS_BUFFERS_EXTRA_SPACE in addition to expected data size)
    CowsOnframeCallback cb,  // user-defined callback that accepts the whole decoded frame
    void* cb_user  // user-provided context for callback
);


static inline void
cowsWriteUint32(unsigned char* b, uint32_t w); // utility function (may be useful)


// Incremental decoder.
// May be called multiple times accepting subsequent chunks of encoded stream.
// On each fully decoded frame user-defined callback is called
static inline void
cowsParseChunk(CowsParser* ps, void* src, size_t src_length);


// returns exactly fr_len (on success; ToDo: check fr_len bounds)
static inline uint32_t
cowsEncodeFrameInPlace(
    void* fr_src,
    uint32_t fr_len,
    uint32_t* fr_head_bytes, // uint32_t just used as 4-byte space to put 1st offset
    uint32_t* fr_foot_bytes  // uint32_t just used as 4-byte space to put final marker
);


// The encoder (non-incremental, non-inplace)
// the function returns size of the encoded data
static inline uint32_t
cowsEncodeFrame(
    void* fr_src,
    uint32_t fr_len,
    void* fr_dst //fr_dst shall reserve at least (fr_len + COWS_BUFFERS_EXTRA_SPACE)
);

// include implementation details not intended to direct use:
#include "../wordstuf/wordstuf_impl.h"

#endif
