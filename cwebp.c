/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2013 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */
#ifdef WEBP_HAVE_PNG
#include <png.h>
#endif

#ifdef WEBP_HAVE_JPEG
#include <setjmp.h>   // note: this must be included *after* png.h
#include <jpeglib.h>
#endif

#ifdef WEBP_HAVE_TIFF
#include <tiffio.h>
#endif


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cwebp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webp/encode.h>

  #include "php.h"

#ifndef WEBP_DLL
#if defined(__cplusplus) || defined(c_plusplus)
extern "C" 
{
#endif

    extern void* VP8GetCPUInfo;   // opaque forward declaration.

#if defined(__cplusplus) || defined(c_plusplus)
}    // extern "C"
#endif
#endif  // WEBP_DLL

//------------------------------------------------------------------------------

static int verbose = 0;

#ifdef WEBP_HAVE_JPEG
struct my_error_mgr 
{/*{{{*/
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};/*}}}*/

static void my_error_exit(j_common_ptr dinfo) 
{/*{{{*/
    struct my_error_mgr* myerr = (struct my_error_mgr*) dinfo->err;
    (*dinfo->err->output_message) (dinfo);
    longjmp(myerr->setjmp_buffer, 1);
}/*}}}*/

static int ReadJPEG(unsigned char* data,const unsigned int dataSize, WebPPicture* const pic) 
{/*{{{*/
    int ok = 0;
    int stride, width, height;
    uint8_t* rgb = NULL;
    uint8_t* row_ptr = NULL;
    struct jpeg_decompress_struct dinfo;
    struct my_error_mgr jerr;
    JSAMPARRAY buffer;

    dinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    if (setjmp(jerr.setjmp_buffer)) 
    {
Error:
        jpeg_destroy_decompress(&dinfo);
        goto End;
    }

    jpeg_create_decompress(&dinfo);
    jpeg_mem_src(&dinfo, data, dataSize);
//    jpeg_stdio_src(&dinfo, in_file);
    jpeg_read_header(&dinfo, TRUE);

    dinfo.out_color_space = JCS_RGB;
    dinfo.dct_method = JDCT_IFAST;
    dinfo.do_fancy_upsampling = TRUE;

    jpeg_start_decompress(&dinfo);

    if (dinfo.output_components != 3) 
    {
        goto Error;
    }

    width = dinfo.output_width;
    height = dinfo.output_height;
    stride = dinfo.output_width * dinfo.output_components * sizeof(*rgb);

    rgb = (uint8_t*)malloc(stride * height);
    if (rgb == NULL) 
    {
        goto End;
    }
    row_ptr = rgb;

    buffer = (*dinfo.mem->alloc_sarray) ((j_common_ptr) &dinfo,
            JPOOL_IMAGE, stride, 1);
    if (buffer == NULL) 
    {
        goto End;
    }

    while (dinfo.output_scanline < dinfo.output_height) 
    {
        if (jpeg_read_scanlines(&dinfo, buffer, 1) != 1) 
        {
            goto End;
        }
        memcpy(row_ptr, buffer[0], stride);
        row_ptr += stride;
    }

    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);

    // WebP conversion.
    pic->width = width;
    pic->height = height;
    ok = WebPPictureImportRGB(pic, rgb, stride);

End:
    if (rgb) 
    {
        free(rgb);
    }
    return ok;
}/*}}}*/

#else
static int ReadJPEG(unsigned char* data,const unsigned int dataSize, WebPPicture* const pic) 
{/*{{{*/
    php_error_docref(NULL TSRMLS_CC, E_ERROR,
            "JPEG support not compiled. Please install the libjpeg "
            "development package before building.");
    return 0;
}/*}}}*/
#endif

#ifdef WEBP_HAVE_PNG
static void PNGAPI error_function(png_structp png, png_const_charp dummy) 
{/*{{{*/
    (void)dummy;  // remove variable-unused warning
    longjmp(png_jmpbuf(png), 1);
}/*}}}*/

// callback for read png image
static void PngReadCallback(png_structp png_ptr, png_bytep data, png_size_t length)
{/*{{{*/
    ImageSource* isource = (ImageSource*)png_get_io_ptr(png_ptr);
    if(isource->offset + length <= isource->size)
    {
        memcpy(data, isource->data+isource->offset, length);
        isource->offset += length;
    }
    else
    {
        png_error(png_ptr, "PngReaderCallback failed");
    }
}/*}}}*/

static int ReadPNG(unsigned char* data,const unsigned int dataSize, WebPPicture* const pic, int keep_alpha) 
{/*{{{*/
    png_structp png;
    png_infop info;
    int color_type, bit_depth, interlaced;
    int has_alpha;
    int num_passes;
    int p;
    int ok = 0;
    png_uint_32 width, height, y;
    int stride;
    uint8_t* rgb = NULL;

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
    if (png == NULL) 
    {
        goto End;
    }

    png_set_error_fn(png, 0, error_function, NULL);
    if (setjmp(png_jmpbuf(png))) 
    {
Error:
        png_destroy_read_struct(&png, NULL, NULL);
        free(rgb);
        goto End;
    }

    info = png_create_info_struct(png);
    if (info == NULL) goto Error;
    ImageSource imgsource;
    imgsource.data = data;
    imgsource.size = dataSize;
    imgsource.offset = 0;
    png_set_read_fn(png, &imgsource,PngReadCallback);
    png_set_sig_bytes(png, 0);
    png_read_info(png, info);
    if (!png_get_IHDR(png, info,
                &width, &height, &bit_depth, &color_type, &interlaced,
                NULL, NULL)) goto Error;

    png_set_strip_16(png);
    png_set_packing(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
            color_type == PNG_COLOR_TYPE_GRAY_ALPHA) 
    {
        if (bit_depth < 8) 
        {
            png_set_expand_gray_1_2_4_to_8(png);
        }
        png_set_gray_to_rgb(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) 
    {
        png_set_tRNS_to_alpha(png);
        has_alpha = 1;
    } else 
    {
        has_alpha = !!(color_type & PNG_COLOR_MASK_ALPHA);
    }

    if (!keep_alpha) 
    {
        png_set_strip_alpha(png);
        has_alpha = 0;
    }

    num_passes = png_set_interlace_handling(png);
    png_read_update_info(png, info);
    stride = (has_alpha ? 4 : 3) * width * sizeof(*rgb);
    rgb = (uint8_t*)malloc(stride * height);
    if (rgb == NULL) goto Error;
    for (p = 0; p < num_passes; ++p) 
    {
        for (y = 0; y < height; ++y) 
        {
            png_bytep row = rgb + y * stride;
            png_read_rows(png, &row, NULL, 1);
        }
    }
    png_read_end(png, info);
    png_destroy_read_struct(&png, &info, NULL);

    pic->width = width;
    pic->height = height;
    ok = has_alpha ? WebPPictureImportRGBA(pic, rgb, stride)
        : WebPPictureImportRGB(pic, rgb, stride);
    free(rgb);

    if (ok && has_alpha && keep_alpha == 2) 
    {
        WebPCleanupTransparentArea(pic);
    }

End:
    return ok;
}/*}}}*/
#else
static int ReadPNG(unsigned char* data,const unsigned int dataSize, WebPPicture* const pic, int keep_alpha) 
{/*{{{*/
    php_error_docref(NULL TSRMLS_CC, E_ERROR,
            "PNG support not compiled. Please install the libpng "
            "development package before building."));
    return 0;
}/*}}}*/
#endif

static ImageFormat GetImageType(const unsigned char* const blob) 
{/*{{{*/
    ImageFormat format = UNSUPPORTED;
    unsigned int magic;
    unsigned char buf[4];

    if(NULL == blob||!memcpy(buf,blob,4))
    {
        return format;
    }

    magic = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    if (magic == 0x89504E47U) 
    {
        format = PNG_;
    } else if (magic >= 0xFFD8FF00U && magic <= 0xFFD8FFFFU) 
    {
        format = JPEG_;
    } 
    return format;
}/*}}}*/

static int ReadPicture(unsigned char* blob,int datasize, WebPPicture* const pic,
        int keep_alpha) 
{/*{{{*/
    int ok = 0;
    if (blob == NULL||!strlen(blob)) 
    {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, "No image data provided");
        return ok;
    }

    // If no size specified, try to decode it as PNG/JPEG (as appropriate).
    const ImageFormat format = GetImageType(blob);
    if (format == PNG_) 
    {
        ok = ReadPNG(blob,datasize, pic, keep_alpha);
    } else if (format == JPEG_) 
    {
        ok = ReadJPEG(blob,datasize, pic);
    }

    if (!ok) 
    {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, "Not JPEG or PNG image");
    }

    return ok;
}/*}}}*/

//------------------------------------------------------------------------------
static int MemoryWriter(const uint8_t* data, size_t data_size,
        WebPPicture* pic) 
{/*{{{*/
    out_buf_t* const out = (out_buf_t*)pic->custom_ptr;
    if(!data_size)
        return 1;
    memcpy(out->start + out->len, data,data_size);
    out->len += data_size;
    return 1;
}/*}}}*/

// Error messages

static const char* const kErrorMessages[] = 
{/*{{{*/
    "OK",
    "OUT_OF_MEMORY: Out of memory allocating objects",
    "BITSTREAM_OUT_OF_MEMORY: Out of memory re-allocating byte buffer",
    "NULL_PARAMETER: NULL parameter passed to function",
    "INVALID_CONFIGURATION: configuration is invalid",
    "BAD_DIMENSION: Bad picture dimension. Maximum width and height "
        "allowed is 16383 pixels.",
    "PARTITION0_OVERFLOW: Partition #0 is too big to fit 512k.\n"
        "To reduce the size of this partition, try using less segments "
        "with the -segments option, and eventually reduce the number of "
        "header bits using -partition_limit. More details are available "
        "in the manual (`man cwebp`)",
    "PARTITION_OVERFLOW: Partition is too big to fit 16M",
    "BAD_WRITE: Picture writer returned an I/O error",
    "FILE_TOO_BIG: File would be too big to fit in 4G",
    "USER_ABORT: encoding abort requested by user"
};/*}}}*/

int EncodeImage2Webp(unsigned char* blob, int blob_size, out_buf_t* out) 
{/*{{{*/
    int return_value = -1;
    int keep_alpha = 1;
    WebPPicture picture;
    WebPConfig config;

    if (blob == NULL) 
    {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, "No blob specified");
        goto Error;
    }

    if (!WebPPictureInit(&picture) || !WebPConfigInit(&config)) 
    {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, "Version mismatch");
        return -1;
    }

    // Check for unsupported command line options for lossless mode and log
    // warning for such options.

    if (!WebPValidateConfig(&config)) 
    {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, "Invalid configuration");
        goto Error;
    }

    // Read the input
    if (!ReadPicture(blob, blob_size, &picture, keep_alpha)) 
    {
        goto Error;
    }

    picture.progress_hook = NULL;

    // Open the output
    if (out) 
    {
        picture.writer = (WebPWriterFunction)MemoryWriter;
        picture.custom_ptr = (void*)out;
    } else {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, "No output buffer specified");
        goto Error;
    }

    if (!WebPEncode(&config, &picture)) 
    {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, 
            "Can not encode image as WebP format. Error code: %d (%s)",
            picture.error_code, kErrorMessages[picture.error_code]);
        goto Error;
    }

    return_value = 0;

Error:
    WebPPictureFree(&picture);

    return return_value;
}/*}}}*/
