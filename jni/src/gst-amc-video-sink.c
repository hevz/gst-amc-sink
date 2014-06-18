/*
 ============================================================================
 Name        : gst-amc-video-sink.c
 Author      : Heiher <r@hev.cc>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2014 everyone.
 Description : 
 ============================================================================
 */

#include "gst-amc-video-sink.h"
#include "gst-amc-video-decoder.h"
#include "gst-amc-sink.h"

GST_DEBUG_CATEGORY_STATIC (gst_amc_video_sink_debug);
#define GST_CAT_DEFAULT gst_amc_video_sink_debug

enum
{
    PROP_ZERO,
    PROP_SURFACE,
    N_PROPERTIES
};

#define GST_AMC_VIDEO_SINK_GET_PRIVATE(obj) (gst_amc_video_sink_get_instance_private(obj))

typedef struct _GstAmcVideoSinkPrivate GstAmcVideoSinkPrivate;

struct _GstAmcVideoSinkPrivate
{
    jobject surface;
    GstElement *video_decoder;
};

static GstStaticPadTemplate gst_amc_video_sink_sink_template =
GST_STATIC_PAD_TEMPLATE (
            "sink",
            GST_PAD_SINK,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS_ANY);

#define gst_amc_video_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstAmcVideoSink, gst_amc_video_sink, GST_TYPE_BIN);

static void
gst_amc_video_sink_dispose (GObject *obj)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_amc_video_sink_finalize (GObject *obj)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GObject *
gst_amc_video_sink_constructor (GType type,
            guint n,
            GObjectConstructParam *param)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    return G_OBJECT_CLASS (parent_class)->constructor (type, n, param);
}

static void
gst_amc_video_sink_constructed (GObject *obj)
{
    GstAmcVideoSink *self = GST_AMC_VIDEO_SINK (obj);
    GstAmcVideoSinkPrivate *priv = GST_AMC_VIDEO_SINK_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (parent_class)->constructed (obj);
}

static void
gst_amc_video_sink_set_property (GObject *obj, guint id,
            const GValue *value, GParamSpec *pspec)
{
    GstAmcVideoSink *self = GST_AMC_VIDEO_SINK (obj);
    GstAmcVideoSinkPrivate *priv = GST_AMC_VIDEO_SINK_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    switch (id) {
    case PROP_SURFACE:
        priv->surface = g_value_get_pointer (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static void
gst_amc_video_sink_get_property (GObject *obj, guint id,
            GValue *value, GParamSpec *pspec)
{
    GstAmcVideoSink *self = GST_AMC_VIDEO_SINK (obj);
    GstAmcVideoSinkPrivate *priv = GST_AMC_VIDEO_SINK_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    switch (id) {
    case PROP_SURFACE:
        g_value_set_pointer (value, priv->surface);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static void
gst_amc_video_sink_class_init (GstAmcVideoSinkClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    obj_class->constructor = gst_amc_video_sink_constructor;
    obj_class->constructed = gst_amc_video_sink_constructed;
    obj_class->set_property = gst_amc_video_sink_set_property;
    obj_class->get_property = gst_amc_video_sink_get_property;
    obj_class->dispose = gst_amc_video_sink_dispose;
    obj_class->finalize = gst_amc_video_sink_finalize;

    g_object_class_install_property (obj_class, PROP_SURFACE,
                g_param_spec_pointer ("surface", "Android surface",
                    "The video display on the surface.", G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

    gst_element_class_set_static_metadata (element_class, "Amc Video Sink",
            "Sink/Video/Amc",
            "Android Media codec video sink",
            "Heiher <r@hev.cc>");

    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "amcvideosink", 0, "AmcVideoSink");
}

static void
gst_amc_video_sink_init (GstAmcVideoSink *self)
{
    GstAmcVideoSinkPrivate *priv = GST_AMC_VIDEO_SINK_GET_PRIVATE (self);
    GstElement *sink;
    GstPad *pad;
    GstPad *gpad;
    GstPadTemplate *pad_tmpl;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    priv->video_decoder = g_object_new (GST_TYPE_AMC_VIDEO_DECODER,
                "name", "amc-video-sink-decoder", "surface", priv->surface, NULL);
    g_object_bind_property (self, "surface", priv->video_decoder, "surface", G_BINDING_DEFAULT);
    sink = g_object_new (GST_TYPE_AMC_SINK,
                "name", "amc-video-sink-sink", NULL);

    gst_bin_add_many (GST_BIN (self), priv->video_decoder, sink, NULL);
    gst_element_link (priv->video_decoder, sink);

    /* get the sinkpad */
    pad = gst_element_get_static_pad (priv->video_decoder, "sink");

    /* get the pad template */
    pad_tmpl = gst_static_pad_template_get (&gst_amc_video_sink_sink_template);

    /* ghost the sink pad to ourself */
    gpad = gst_ghost_pad_new_from_template ("sink", pad, pad_tmpl);
    gst_pad_set_active (gpad, TRUE);
    gst_element_add_pad (GST_ELEMENT (self), gpad);

    gst_object_unref (pad_tmpl);
    gst_object_unref (pad);
}

/* vim:set tabstop=4 softtabstop=4 shiftwidth=4 expandtab: */

