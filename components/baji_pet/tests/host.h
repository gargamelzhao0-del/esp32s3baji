#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_RESPONSE, ESP_ERR_NO_MEM };
typedef struct { uint16_t x,y,width,height; } baji_rect_t;
enum { MALLOC_CAP_INTERNAL=1, MALLOC_CAP_SPIRAM=2, MALLOC_CAP_8BIT=4 };
static inline void *heap_caps_malloc(size_t n,unsigned f) {(void)f;return malloc(n);}
static inline void *heap_caps_calloc(size_t n,size_t size,unsigned f) {(void)f;return calloc(n,size);}
#define heap_caps_free free
typedef int tinfl_decompressor;
typedef int tinfl_status;
enum { TINFL_STATUS_DONE=0,TINFL_FLAG_PARSE_ZLIB_HEADER=1,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF=2 };
#define tinfl_init(d) ((void)(d))
typedef int (*inflate_fn)(const uint8_t *,size_t *,uint8_t *,size_t *);
typedef uint32_t (*checksum_fn)(uint32_t,const uint8_t *,size_t);
extern inflate_fn inflate_host;
extern checksum_fn checksum_host;
static inline int tinfl_decompress(tinfl_decompressor *d,const uint8_t *s,size_t *n,
                                   uint8_t *start,uint8_t *dst,size_t *out,unsigned flags)
{ (void)d;(void)start;(void)flags;return inflate_host(s,n,dst,out); }
#define esp_rom_crc32_le checksum_host
