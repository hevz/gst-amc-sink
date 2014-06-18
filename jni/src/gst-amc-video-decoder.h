/*
 ============================================================================
 Name        : gst-amc-video-decoder.h
 Author      : Heiher <r@hev.cc>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2014 everyone.
 Description : 
 ============================================================================
 */

#ifndef __GST_AMC_VIDEO_DECODER_H__
#define __GST_AMC_VIDEO_DECODER_H__

#include <gst/gst.h>
#include <gst/video/gstvideodecoder.h>

#include "gst-amc.h"

G_BEGIN_DECLS

#define GST_TYPE_AMC_VIDEO_DECODER \
    (gst_amc_video_decoder_get_type())
#define GST_AMC_VIDEO_DECODER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMC_VIDEO_DECODER,GstAmcVideoDecoder))
#define GST_AMC_VIDEO_DECODER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMC_VIDEO_DECODER,GstAmcVideoDecoderClass))
#define GST_AMC_VIDEO_DECODER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMC_VIDEO_DECODER,GstAmcVideoDecoderClass))
#define GST_IS_AMC_VIDEO_DECODER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMC_VIDEO_DECODER))
#define GST_IS_AMC_VIDEO_DECODER_CLASS(obj) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMC_VIDEO_DECODER))

typedef struct _GstAmcVideoDecoder GstAmcVideoDecoder;
typedef struct _GstAmcVideoDecoderClass GstAmcVideoDecoderClass;

struct _GstAmcVideoDecoder
{
    GstVideoDecoder parent;
};

struct _GstAmcVideoDecoderClass
{
    GstVideoDecoderClass parent_class;
};

GType gst_amc_video_decoder_get_type (void);

G_END_DECLS

#endif /* __GST_AMC_VIDEO_DECODER_H__ */

/* vim:set tabstop=4 softtabstop=4 shiftwidth=4 expandtab: */

