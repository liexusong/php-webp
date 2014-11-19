#ifndef CWEBP_H
#define CWEBP_H

#include <stdint.h>

typedef struct
{
    unsigned char* data;
    int size;
    int offset;
} ImageSource;

typedef struct {
    uint8_t* start;
    int len;
} out_buf_t;

typedef enum 
{/*{{{*/
    PNG_ = 0,
    JPEG_,
    UNSUPPORTED
} ImageFormat;/*}}}*/

int EncodeImage2Webp(unsigned char* blob,int datasize,out_buf_t* out);

#endif	/* WEBP_H*/
