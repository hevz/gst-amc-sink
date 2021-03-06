/*
 ============================================================================
 Name        : gst-amc-video-sink.h
 Author      : Heiher <r@hev.cc>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2014 everyone.
 Description : 
 ============================================================================
 */

#ifndef __GST_AMC_VIDEO_SINK_H__
#define __GST_AMC_VIDEO_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_AMC_VIDEO_SINK (gst_amc_video_sink_get_type ())
#define GST_AMC_VIDEO_SINK(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AMC_VIDEO_SINK, GstAmcVideoSink))
#define GST_IS_AMC_VIDEO_SINK(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AMC_VIDEO_SINK))
#define GST_AMC_VIDEO_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AMC_VIDEO_SINK, GstAmcVideoSinkClass))
#define GST_IS_AMC_VIDEO_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AMC_VIDEO_SINK))
#define GST_AMC_VIDEO_SINK_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_AMC_VIDEO_SINK, GstAmcVideoSinkClass))

typedef struct _GstAmcVideoSink GstAmcVideoSink;
typedef struct _GstAmcVideoSinkClass GstAmcVideoSinkClass;

struct _GstAmcVideoSink
{
    GstBin parent_instance;
};

struct _GstAmcVideoSinkClass
{
    GstBinClass parent_class;
};

GType gst_amc_video_sink_get_type (void);

G_END_DECLS

#endif /* __GST_AMC_VIDEO_SINK_H__ */

/* vim:set tabstop=4 softtabstop=4 shiftwidth=4 expandtab: */

