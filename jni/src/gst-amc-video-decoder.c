/*
 ============================================================================
 Name        : gst-amc-video-decoder.c
 Author      : Heiher <r@hev.cc>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2014 everyone.
 Description : 
 ============================================================================
 */

#include "gst-amc-video-decoder.h"
#include "gst-amc.h"
#include "gst-amc-sink.h"
#include "gst-jni-utils.h"

GST_DEBUG_CATEGORY_STATIC (gst_amc_video_decoder_debug);
#define GST_CAT_DEFAULT gst_amc_video_decoder_debug

#define GST_VIDEO_DECODER_ERROR_FROM_ERROR(el, err) G_STMT_START { \
  gchar *__dbg = g_strdup (err->message);                               \
  GstVideoDecoder *__dec = GST_VIDEO_DECODER (el);                      \
  GST_WARNING_OBJECT (el, "error: %s", __dbg);                          \
  _gst_video_decoder_error (__dec, 1,                                   \
    err->domain, err->code,                                             \
    NULL, __dbg, __FILE__, GST_FUNCTION, __LINE__);                     \
  g_clear_error (&err); \
} G_STMT_END

enum
{
    PROP_ZERO,
    PROP_SURFACE,
    N_PROPERTIES
};

typedef struct _GstAmcVideoDecoderPrivate GstAmcVideoDecoderPrivate;

#define GST_AMC_VIDEO_DECODER_GET_PRIVATE(obj) (gst_amc_video_decoder_get_instance_private(obj))

struct _GstAmcVideoDecoderPrivate
{
    GstAmcCodec *codec;
    GstAmcBuffer *input_buffers;
    gsize n_input_buffers;

    GstVideoCodecState *input_state;
    gboolean input_state_changed;

    guint8 *codec_data;
    gsize codec_data_size;
    /* TRUE if the component is configured and saw
    * the first buffer */
    gboolean started;
    gboolean flushing;

    GstClockTime last_upstream_ts;

    /* Draining state */
    GMutex drain_lock;
    GCond drain_cond;
    /* TRUE if EOS buffers shouldn't be forwarded */
    gboolean draining;

    /* TRUE if upstream is EOS */
    gboolean eos;

    GstFlowReturn downstream_flow_ret;

    jobject surface;

    gint width;
    gint height;
};

static GstStaticPadTemplate gst_amc_video_decoder_sink_template =
GST_STATIC_PAD_TEMPLATE (
            "sink",
            GST_PAD_SINK,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS ("video/x-h264, "
                "stream-format= (string) byte-stream, "
                "alignment= (string) au"));

static GstStaticPadTemplate gst_amc_video_decoder_src_template =
GST_STATIC_PAD_TEMPLATE (
            "src",
            GST_PAD_SRC,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS ("application/x-amc-direct"));

#define gst_amc_video_decoder_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstAmcVideoDecoder, gst_amc_video_decoder, GST_TYPE_VIDEO_DECODER);

static GstStateChangeReturn
gst_amc_video_decoder_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_amc_video_decoder_open (GstVideoDecoder * decoder);
static gboolean gst_amc_video_decoder_close (GstVideoDecoder * decoder);
static gboolean gst_amc_video_decoder_start (GstVideoDecoder * decoder);
static gboolean gst_amc_video_decoder_stop (GstVideoDecoder * decoder);
static gboolean gst_amc_video_decoder_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state);
static gboolean gst_amc_video_decoder_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_amc_video_decoder_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame);
static GstFlowReturn gst_amc_video_decoder_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_amc_video_decoder_drain (GstAmcVideoDecoder * self, gboolean at_eos);

static void
gst_amc_video_decoder_finalize (GObject * object)
{
  GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (object);
  GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);

  g_mutex_clear (&priv->drain_lock);
  g_cond_clear (&priv->drain_cond);

  if (priv->surface) {
        JNIEnv *env = gst_amc_jni_get_env ();
        gst_amc_jni_object_unref (env, priv->surface);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_amc_video_decoder_set_property (GObject *obj, guint id,
            const GValue *value, GParamSpec *pspec)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (obj);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    switch (id) {
    case PROP_SURFACE:
        {
            JNIEnv *env = gst_amc_jni_get_env ();
            jobject surface;
            if (priv->surface)
              gst_amc_jni_object_unref (env, priv->surface);
            surface = g_value_get_pointer (value);
            if (surface)
              priv->surface = gst_amc_jni_object_ref (env, surface);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static void
gst_amc_video_decoder_get_property (GObject *obj, guint id,
            GValue *value, GParamSpec *pspec)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (obj);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);

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
gst_amc_video_decoder_class_init (GstAmcVideoDecoderClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
    GstVideoDecoderClass *videodec_class = GST_VIDEO_DECODER_CLASS (klass);

    parent_class = g_type_class_peek_parent (klass);

    gobject_class->set_property = gst_amc_video_decoder_set_property;
    gobject_class->get_property = gst_amc_video_decoder_get_property;
    gobject_class->finalize = gst_amc_video_decoder_finalize;

    element_class->change_state =
        GST_DEBUG_FUNCPTR (gst_amc_video_decoder_change_state);

    videodec_class->start = GST_DEBUG_FUNCPTR (gst_amc_video_decoder_start);
    videodec_class->stop = GST_DEBUG_FUNCPTR (gst_amc_video_decoder_stop);
    videodec_class->open = GST_DEBUG_FUNCPTR (gst_amc_video_decoder_open);
    videodec_class->close = GST_DEBUG_FUNCPTR (gst_amc_video_decoder_close);
    videodec_class->flush = GST_DEBUG_FUNCPTR (gst_amc_video_decoder_flush);
    videodec_class->set_format = GST_DEBUG_FUNCPTR (gst_amc_video_decoder_set_format);
    videodec_class->handle_frame = GST_DEBUG_FUNCPTR (gst_amc_video_decoder_handle_frame);
    videodec_class->finish = GST_DEBUG_FUNCPTR (gst_amc_video_decoder_finish);

    g_object_class_install_property (gobject_class, PROP_SURFACE,
                g_param_spec_pointer ("surface", "Android surface",
                    "The video display on the surface.", G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

    gst_element_class_add_pad_template (element_class,
            gst_static_pad_template_get (&gst_amc_video_decoder_sink_template));
    gst_element_class_add_pad_template (element_class,
            gst_static_pad_template_get (&gst_amc_video_decoder_src_template));
    gst_element_class_set_static_metadata (element_class, "Amc Video Decoder",
            "Decoder/Video/Amc",
            "Android Media codec video decoder",
            "Heiher <r@hev.cc>");

    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "amcvideodecoder", 0, "AmcVideoDecoder");
}

static void
gst_amc_video_decoder_init (GstAmcVideoDecoder * self)
{
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

    g_mutex_init (&priv->drain_lock);
    g_cond_init (&priv->drain_cond);
}

static gboolean
gst_amc_video_decoder_open (GstVideoDecoder * decoder)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (decoder);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GstAmcVideoDecoderClass *klass = GST_AMC_VIDEO_DECODER_GET_CLASS (self);
    GError *err = NULL;

    GST_DEBUG_OBJECT (self, "Opening decoder");

    priv->codec = gst_amc_decoder_new_from_type ("video/avc", &err);
    if (!priv->codec) {
        GST_ELEMENT_ERROR_FROM_ERROR (self, err);
        return FALSE;
    }
    priv->started = FALSE;
    priv->flushing = TRUE;

    GST_DEBUG_OBJECT (self, "Opened decoder");

    return TRUE;
}

static gboolean
gst_amc_video_decoder_close (GstVideoDecoder * decoder)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (decoder);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);

    GST_DEBUG_OBJECT (self, "Closing decoder");

    if (priv->codec) {
        GError *err = NULL;

        gst_amc_codec_release (priv->codec, &err);
        if (err)
          GST_ELEMENT_WARNING_FROM_ERROR (self, err);

        gst_amc_codec_free (priv->codec);
    }
    priv->codec = NULL;

    priv->started = FALSE;
    priv->flushing = TRUE;

    GST_DEBUG_OBJECT (self, "Closed decoder");

    return TRUE;
}

static GstStateChangeReturn
gst_amc_video_decoder_change_state (GstElement * element, GstStateChange transition)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (element);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    GError *err = NULL;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        priv->downstream_flow_ret = GST_FLOW_OK;
        priv->draining = FALSE;
        priv->started = FALSE;
        break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        priv->flushing = TRUE;
        gst_amc_codec_flush (priv->codec, &err);
        if (err)
        GST_ELEMENT_WARNING_FROM_ERROR (self, err);
        g_mutex_lock (&priv->drain_lock);
        priv->draining = FALSE;
        g_cond_broadcast (&priv->drain_cond);
        g_mutex_unlock (&priv->drain_lock);
        break;
    default:
        break;
    }

    if (ret == GST_STATE_CHANGE_FAILURE)
      return ret;

    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

    if (ret == GST_STATE_CHANGE_FAILURE)
      return ret;

    switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        priv->downstream_flow_ret = GST_FLOW_FLUSHING;
        priv->started = FALSE;
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        break;
    default:
        break;
    }

    return ret;
}

static GstBuffer *
gst_amc_video_decoder_new_buffer (GstAmcVideoDecoder * self, gint idx)
{
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GstMapInfo minfo;
    GstBuffer *outbuf;
    GstAmcSinkBufferData *buffer_data;

    outbuf = gst_buffer_new_allocate (NULL, sizeof (GstAmcSinkBufferData), NULL);
    if (!outbuf)
      return NULL;

    if (!gst_buffer_map (outbuf, &minfo, GST_MAP_WRITE)) {
        gst_buffer_unref (outbuf);
        return NULL;
    }

    buffer_data = (GstAmcSinkBufferData *) minfo.data;
    buffer_data->codec = priv->codec;
    buffer_data->index = idx;

    gst_buffer_unmap (outbuf, &minfo);

    return outbuf;
}

static void
gst_amc_video_decoder_loop (GstAmcVideoDecoder * self)
{
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GstFlowReturn flow_ret = GST_FLOW_OK;
    GstClockTimeDiff deadline;
    gboolean is_eos;
    GstAmcBufferInfo buffer_info;
    gint idx;
    GstBuffer *outbuf;
    GError *err = NULL;

    GST_VIDEO_DECODER_STREAM_LOCK (self);

retry:
    GST_DEBUG_OBJECT (self, "Waiting for available output buffer");
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    /* Wait at most 100ms here, some codecs don't fail dequeueing if
    * the codec is flushing, causing deadlocks during shutdown */
    idx = gst_amc_codec_dequeue_output_buffer (priv->codec, &buffer_info, 100000, &err);
    GST_VIDEO_DECODER_STREAM_LOCK (self);

    if (idx < 0) {
        if (priv->flushing || priv->downstream_flow_ret == GST_FLOW_FLUSHING) {
            g_clear_error (&err);
            goto flushing;
        }

        switch (idx) {
        case INFO_OUTPUT_BUFFERS_CHANGED:
            GST_DEBUG_OBJECT (self, "Output buffers have changed");

            /* If the decoder is configured with a surface, get_output_buffers returns null */
            break;
        case INFO_OUTPUT_FORMAT_CHANGED:
        {
            GstAmcFormat *format;
            gchar *format_string;

            GST_DEBUG_OBJECT (self, "Output format has changed");

            format = gst_amc_codec_get_output_format (priv->codec, &err);
            if (!format)
              goto format_error;

            format_string = gst_amc_format_to_string (format, &err);
            if (!format) {
                gst_amc_format_free (format);
                goto format_error;
            }
            GST_DEBUG_OBJECT (self, "Got new output format: %s", format_string);
            g_free (format_string);
            gst_amc_format_free (format);

            goto retry;
            break;
        }
        case INFO_TRY_AGAIN_LATER:
            GST_DEBUG_OBJECT (self, "Dequeueing output buffer timed out");
            goto retry;
            break;
            case G_MININT:
            GST_ERROR_OBJECT (self, "Failure dequeueing output buffer");
            goto dequeue_error;
            break;
        default:
            g_assert_not_reached ();
            break;
        }

        goto retry;
    }

    GST_DEBUG_OBJECT (self, "Got output buffer at index %d: size %d time %" G_GINT64_FORMAT
                " flags 0x%08x", idx, buffer_info.size, buffer_info.presentation_time_us,
                buffer_info.flags);

    is_eos = !!(buffer_info.flags & BUFFER_FLAG_END_OF_STREAM);

    /* This sometimes happens at EOS or if the input is not properly framed,
    * let's handle it gracefully by allocating a new buffer for the current
    * caps and filling it
    */

    if (!(outbuf = gst_amc_video_decoder_new_buffer (self, idx))) {
        if (!gst_amc_codec_release_output_buffer (priv->codec, idx, &err))
          GST_ERROR_OBJECT (self, "Failed to release output buffer index %d", idx);
        if (err)
          GST_ELEMENT_WARNING_FROM_ERROR (self, err);
        goto invalid_buffer;
    }

    GST_BUFFER_PTS (outbuf) = gst_util_uint64_scale (buffer_info.presentation_time_us, GST_USECOND, 1);
    flow_ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), outbuf);

    if (is_eos || flow_ret == GST_FLOW_EOS) {
        GST_VIDEO_DECODER_STREAM_UNLOCK (self);
        g_mutex_lock (&priv->drain_lock);
        if (priv->draining) {
            GST_DEBUG_OBJECT (self, "Drained");
            priv->draining = FALSE;
            g_cond_broadcast (&priv->drain_cond);
        } else if (flow_ret == GST_FLOW_OK) {
            GST_DEBUG_OBJECT (self, "Component signalled EOS");
            flow_ret = GST_FLOW_EOS;
        }
        g_mutex_unlock (&priv->drain_lock);
        GST_VIDEO_DECODER_STREAM_LOCK (self);
    } else {
        GST_DEBUG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));
    }

    priv->downstream_flow_ret = flow_ret;

    if (flow_ret != GST_FLOW_OK)
      goto flow_error;

    GST_VIDEO_DECODER_STREAM_UNLOCK (self);

    return;

dequeue_error:
    GST_ELEMENT_ERROR_FROM_ERROR (self, err);
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    priv->downstream_flow_ret = GST_FLOW_ERROR;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;

format_error:
    if (err)
      GST_ELEMENT_ERROR_FROM_ERROR (self, err);
    else
      GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL), ("Failed to handle format"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    priv->downstream_flow_ret = GST_FLOW_ERROR;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
failed_release:
    GST_VIDEO_DECODER_ERROR_FROM_ERROR (self, err);
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    priv->downstream_flow_ret = GST_FLOW_ERROR;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
flushing:
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    priv->downstream_flow_ret = GST_FLOW_FLUSHING;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
flow_error:
    if (flow_ret == GST_FLOW_EOS) {
        GST_DEBUG_OBJECT (self, "EOS");
        gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self),
        gst_event_new_eos ());
        gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    } else if (flow_ret < GST_FLOW_EOS) {
        GST_ELEMENT_ERROR (self, STREAM, FAILED,
        ("Internal data stream error."), ("stream stopped, reason %s",
        gst_flow_get_name (flow_ret)));
        gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self),
        gst_event_new_eos ());
        gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    } else if (flow_ret == GST_FLOW_FLUSHING) {
        GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
        gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    }
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;

invalid_buffer:
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Invalid sized input buffer"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    priv->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
}

static gboolean
gst_amc_video_decoder_start (GstVideoDecoder * decoder)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (decoder);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);

    priv->last_upstream_ts = 0;
    priv->eos = FALSE;
    priv->downstream_flow_ret = GST_FLOW_OK;
    priv->started = FALSE;
    priv->flushing = TRUE;

    return TRUE;
}

static gboolean
gst_amc_video_decoder_stop (GstVideoDecoder * decoder)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (decoder);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GError *err = NULL;

    GST_DEBUG_OBJECT (self, "Stopping decoder");
    priv->flushing = TRUE;
    if (priv->started) {
        gst_amc_codec_flush (priv->codec, &err);
        if (err)
          GST_ELEMENT_WARNING_FROM_ERROR (self, err);
        gst_amc_codec_stop (priv->codec, &err);
        if (err)
          GST_ELEMENT_WARNING_FROM_ERROR (self, err);
        priv->started = FALSE;
        if (priv->input_buffers)
          gst_amc_codec_free_buffers (priv->input_buffers, priv->n_input_buffers);
        priv->input_buffers = NULL;
    }
    gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (decoder));

    priv->downstream_flow_ret = GST_FLOW_FLUSHING;
    priv->eos = FALSE;
    g_mutex_lock (&priv->drain_lock);
    priv->draining = FALSE;
    g_cond_broadcast (&priv->drain_cond);
    g_mutex_unlock (&priv->drain_lock);
    g_free (priv->codec_data);
    priv->codec_data_size = 0;
    if (priv->input_state)
      gst_video_codec_state_unref (priv->input_state);
    priv->input_state = NULL;
    GST_DEBUG_OBJECT (self, "Stopped decoder");

    return TRUE;
}

static gboolean
gst_amc_video_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (decoder);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GstAmcFormat *format;
    gboolean is_format_change = FALSE;
    gboolean needs_disable = FALSE;
    gchar *format_string;
    guint8 *codec_data = NULL;
    gsize codec_data_size = 0;
    GError *err = NULL;

    GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, state->caps);

    /* Check if the caps change is a real format change or if only irrelevant
    * parts of the caps have changed or nothing at all.
    */
    is_format_change |= priv->width != state->info.width;
    is_format_change |= priv->height != state->info.height;
    priv->width = state->info.width;
    priv->height = state->info.height;
    if (state->codec_data) {
        GstMapInfo cminfo;

        gst_buffer_map (state->codec_data, &cminfo, GST_MAP_READ);
        codec_data = g_memdup (cminfo.data, cminfo.size);
        codec_data_size = cminfo.size;

        is_format_change |= (!priv->codec_data
            || priv->codec_data_size != codec_data_size
            || memcmp (priv->codec_data, codec_data, codec_data_size) != 0);
        gst_buffer_unmap (state->codec_data, &cminfo);
    } else if (priv->codec_data) {
        is_format_change |= TRUE;
    }

    needs_disable = priv->started;

    /* If the component is not started and a real format change happens
    * we have to restart the component. If no real format change
    * happened we can just exit here.
    */
    if (needs_disable && !is_format_change) {
        g_free (codec_data);
        codec_data = NULL;
        codec_data_size = 0;

        /* Framerate or something minor changed */
        priv->input_state_changed = TRUE;
        if (priv->input_state)
          gst_video_codec_state_unref (priv->input_state);
        priv->input_state = gst_video_codec_state_ref (state);
        GST_DEBUG_OBJECT (self,
            "Already running and caps did not change the format");
        return TRUE;
    }

    if (needs_disable && is_format_change) {
        gst_amc_video_decoder_drain (self, FALSE);
        GST_VIDEO_DECODER_STREAM_UNLOCK (self);
        gst_amc_video_decoder_stop (GST_VIDEO_DECODER (self));
        GST_VIDEO_DECODER_STREAM_LOCK (self);
        gst_amc_video_decoder_close (GST_VIDEO_DECODER (self));
        if (!gst_amc_video_decoder_open (GST_VIDEO_DECODER (self))) {
            GST_ERROR_OBJECT (self, "Failed to open codec again");
            return FALSE;
        }

        if (!gst_amc_video_decoder_start (GST_VIDEO_DECODER (self))) {
            GST_ERROR_OBJECT (self, "Failed to start codec again");
        }
    }
    /* srcpad task is not running at this point */
    if (priv->input_state)
      gst_video_codec_state_unref (priv->input_state);
    priv->input_state = NULL;

    g_free (priv->codec_data);
    priv->codec_data = codec_data;
    priv->codec_data_size = codec_data_size;

    format =
        gst_amc_format_new_video ("video/avc", state->info.width, state->info.height, &err);
    if (!format) {
        GST_ERROR_OBJECT (self, "Failed to create video format");
        GST_ELEMENT_ERROR_FROM_ERROR (self, err);
        return FALSE;
    }

    /* FIXME: This buffer needs to be valid until the codec is stopped again */
    if (priv->codec_data) {
        gst_amc_format_set_buffer (format, "csd-0", priv->codec_data,
            priv->codec_data_size, &err);
        if (err)
          GST_ELEMENT_WARNING_FROM_ERROR (self, err);
    }

    format_string = gst_amc_format_to_string (format, &err);
    if (err)
      GST_ELEMENT_WARNING_FROM_ERROR (self, err);
    GST_DEBUG_OBJECT (self, "Configuring codec with format: %s", GST_STR_NULL (format_string));
    g_free (format_string);

    if (!gst_amc_codec_configure (priv->codec, format, priv->surface, 0, &err)) {
        GST_ERROR_OBJECT (self, "Failed to configure codec");
        GST_ELEMENT_ERROR_FROM_ERROR (self, err);
        return FALSE;
    }

    gst_amc_format_free (format);

    if (!gst_amc_codec_start (priv->codec, &err)) {
        GST_ERROR_OBJECT (self, "Failed to start codec");
        GST_ELEMENT_ERROR_FROM_ERROR (self, err);
        return FALSE;
    }

    if (priv->input_buffers)
      gst_amc_codec_free_buffers (priv->input_buffers, priv->n_input_buffers);
    priv->input_buffers = gst_amc_codec_get_input_buffers (priv->codec, &priv->n_input_buffers, &err);
    if (!priv->input_buffers) {
        GST_ERROR_OBJECT (self, "Failed to get input buffers");
        GST_ELEMENT_ERROR_FROM_ERROR (self, err);
        return FALSE;
    }

    priv->started = TRUE;
    priv->input_state = gst_video_codec_state_ref (state);
    priv->input_state_changed = TRUE;

    /* Start the srcpad loop again */
    priv->flushing = FALSE;
    priv->downstream_flow_ret = GST_FLOW_OK;
    gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
                (GstTaskFunction) gst_amc_video_decoder_loop, decoder, NULL);

    return TRUE;
}

static gboolean
gst_amc_video_decoder_flush (GstVideoDecoder * decoder)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (decoder);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GError *err = NULL;

    GST_DEBUG_OBJECT (self, "Flushing decoder");

    if (!priv->started) {
        GST_DEBUG_OBJECT (self, "Codec not started yet");
        return TRUE;
    }

    priv->flushing = TRUE;
    /* Wait until the srcpad loop is finished,
    * unlock GST_VIDEO_DECODER_STREAM_LOCK to prevent deadlocks
    * caused by using this lock from inside the loop function */
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    GST_PAD_STREAM_LOCK (GST_VIDEO_DECODER_SRC_PAD (self));
    GST_PAD_STREAM_UNLOCK (GST_VIDEO_DECODER_SRC_PAD (self));
    GST_VIDEO_DECODER_STREAM_LOCK (self);
    gst_amc_codec_flush (priv->codec, &err);
    if (err)
      GST_ELEMENT_WARNING_FROM_ERROR (self, err);
    priv->flushing = FALSE;

    /* Start the srcpad loop again */
    priv->last_upstream_ts = 0;
    priv->eos = FALSE;
    priv->downstream_flow_ret = GST_FLOW_OK;
    gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
                (GstTaskFunction) gst_amc_video_decoder_loop, decoder, NULL);

    GST_DEBUG_OBJECT (self, "Flushed decoder");

    return TRUE;
}

static GstFlowReturn
gst_amc_video_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (decoder);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    gint idx;
    GstAmcBuffer *buf;
    GstAmcBufferInfo buffer_info;
    guint offset = 0;
    GstClockTime timestamp, duration, timestamp_offset = 0;
    GstMapInfo minfo;
    GError *err = NULL;

    memset (&minfo, 0, sizeof (minfo));

    GST_DEBUG_OBJECT (self, "Handling frame");

    if (!priv->started) {
        GST_ERROR_OBJECT (self, "Codec not started yet");
        gst_video_codec_frame_unref (frame);
        return GST_FLOW_NOT_NEGOTIATED;
    }

    if (priv->eos) {
        GST_WARNING_OBJECT (self, "Got frame after EOS");
        gst_video_codec_frame_unref (frame);
        return GST_FLOW_EOS;
    }

    if (priv->flushing)
      goto flushing;

    if (priv->downstream_flow_ret != GST_FLOW_OK)
      goto downstream_error;

    timestamp = frame->pts;
    duration = frame->duration;

    gst_buffer_map (frame->input_buffer, &minfo, GST_MAP_READ);

    while (offset < minfo.size) {
        /* Make sure to release the base class stream lock, otherwise
        * _loop() can't call _finish_frame() and we might block forever
        * because no input buffers are released */
        GST_VIDEO_DECODER_STREAM_UNLOCK (self);
        /* Wait at most 100ms here, some codecs don't fail dequeueing if
        * the codec is flushing, causing deadlocks during shutdown */
        idx = gst_amc_codec_dequeue_input_buffer (priv->codec, 100000, &err);
        GST_VIDEO_DECODER_STREAM_LOCK (self);

        if (idx < 0) {
            if (priv->flushing) {
                g_clear_error (&err);
                goto flushing;
            }

            switch (idx) {
            case INFO_TRY_AGAIN_LATER:
                GST_DEBUG_OBJECT (self, "Dequeueing input buffer timed out");
                continue;             /* next try */
                break;
            case G_MININT:
                GST_ERROR_OBJECT (self, "Failed to dequeue input buffer");
                goto dequeue_error;
            default:
                g_assert_not_reached ();
                break;
            }

            continue;
        }

        if (idx >= priv->n_input_buffers)
          goto invalid_buffer_index;

        if (priv->flushing)
          goto flushing;

        if (priv->downstream_flow_ret != GST_FLOW_OK) {
            memset (&buffer_info, 0, sizeof (buffer_info));
            gst_amc_codec_queue_input_buffer (priv->codec, idx, &buffer_info, &err);
            if (err)
              GST_ELEMENT_WARNING_FROM_ERROR (self, err);
            goto downstream_error;
        }

        /* Now handle the frame */

        /* Copy the buffer content in chunks of size as requested
        * by the port */
        buf = &priv->input_buffers[idx];

        memset (&buffer_info, 0, sizeof (buffer_info));
        buffer_info.offset = 0;
        buffer_info.size = MIN (minfo.size - offset, buf->size);

        orc_memcpy (buf->data, minfo.data + offset, buffer_info.size);

        /* Interpolate timestamps if we're passing the buffer
        * in multiple chunks */
        if (offset != 0 && duration != GST_CLOCK_TIME_NONE) {
            timestamp_offset = gst_util_uint64_scale (offset, duration, minfo.size);
        }

        if (timestamp != GST_CLOCK_TIME_NONE) {
            buffer_info.presentation_time_us =
            gst_util_uint64_scale (timestamp + timestamp_offset, 1, GST_USECOND);
            priv->last_upstream_ts = timestamp + timestamp_offset;
        }
        if (duration != GST_CLOCK_TIME_NONE)
          priv->last_upstream_ts += duration;

        offset += buffer_info.size;
        GST_DEBUG_OBJECT (self,
                    "Queueing buffer %d: size %d time %" G_GINT64_FORMAT " flags 0x%08x",
                    idx, buffer_info.size, buffer_info.presentation_time_us,
                    buffer_info.flags);
        if (!gst_amc_codec_queue_input_buffer (priv->codec, idx, &buffer_info, &err))
          goto queue_error;
    }

    gst_buffer_unmap (frame->input_buffer, &minfo);
    gst_video_codec_frame_unref (frame);

    return priv->downstream_flow_ret;

downstream_error:
    GST_ERROR_OBJECT (self, "Downstream returned %s",
    gst_flow_get_name (priv->downstream_flow_ret));
    if (minfo.data)
      gst_buffer_unmap (frame->input_buffer, &minfo);
    gst_video_codec_frame_unref (frame);
    return priv->downstream_flow_ret;
invalid_buffer_index:
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
                ("Invalid input buffer index %d of %d", idx, priv->n_input_buffers));
    if (minfo.data)
      gst_buffer_unmap (frame->input_buffer, &minfo);
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
dequeue_error:
    GST_VIDEO_DECODER_ERROR_FROM_ERROR (self, err);
    if (minfo.data)
      gst_buffer_unmap (frame->input_buffer, &minfo);
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
queue_error:
    GST_ELEMENT_ERROR_FROM_ERROR (self, err);
    if (minfo.data)
      gst_buffer_unmap (frame->input_buffer, &minfo);
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
flushing:
    GST_DEBUG_OBJECT (self, "Flushing -- returning FLUSHING");
    if (minfo.data)
      gst_buffer_unmap (frame->input_buffer, &minfo);
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_FLUSHING;
}

static GstFlowReturn
gst_amc_video_decoder_finish (GstVideoDecoder * decoder)
{
    GstAmcVideoDecoder *self;

    self = GST_AMC_VIDEO_DECODER (decoder);

    return gst_amc_video_decoder_drain (self, TRUE);
}

static GstFlowReturn
gst_amc_video_decoder_drain (GstAmcVideoDecoder * self, gboolean at_eos)
{
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GstFlowReturn ret;
    gint idx;
    GError *err = NULL;

    GST_DEBUG_OBJECT (self, "Draining codec");
    if (!priv->started) {
        GST_DEBUG_OBJECT (self, "Codec not started yet");
        return GST_FLOW_OK;
    }

    /* Don't send EOS buffer twice, this doesn't work */
    if (priv->eos) {
        GST_DEBUG_OBJECT (self, "Codec is EOS already");
        return GST_FLOW_OK;
    }
    if (at_eos)
      priv->eos = TRUE;

    /* Make sure to release the base class stream lock, otherwise
    * _loop() can't call _finish_frame() and we might block forever
    * because no input buffers are released */
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    /* Send an EOS buffer to the component and let the base
    * class drop the EOS event. We will send it later when
    * the EOS buffer arrives on the output port.
    * Wait at most 0.5s here. */
    idx = gst_amc_codec_dequeue_input_buffer (priv->codec, 500000, &err);
    GST_VIDEO_DECODER_STREAM_LOCK (self);

    if (idx >= 0 && idx < priv->n_input_buffers) {
        GstAmcBufferInfo buffer_info;

        GST_VIDEO_DECODER_STREAM_UNLOCK (self);
        g_mutex_lock (&priv->drain_lock);
        priv->draining = TRUE;

        memset (&buffer_info, 0, sizeof (buffer_info));
        buffer_info.size = 0;
        buffer_info.presentation_time_us =
            gst_util_uint64_scale (priv->last_upstream_ts, 1, GST_USECOND);
        buffer_info.flags |= BUFFER_FLAG_END_OF_STREAM;

        if (gst_amc_codec_queue_input_buffer (priv->codec, idx, &buffer_info, &err)) {
            GST_DEBUG_OBJECT (self, "Waiting until codec is drained");
            g_cond_wait (&priv->drain_cond, &priv->drain_lock);
            GST_DEBUG_OBJECT (self, "Drained codec");
            ret = GST_FLOW_OK;
        } else {
            GST_ERROR_OBJECT (self, "Failed to queue input buffer");
            GST_ELEMENT_WARNING_FROM_ERROR (self, err);
            ret = GST_FLOW_ERROR;
        }

        g_mutex_unlock (&priv->drain_lock);
        GST_VIDEO_DECODER_STREAM_LOCK (self);
    } else if (idx >= priv->n_input_buffers) {
        GST_ERROR_OBJECT (self, "Invalid input buffer index %d of %d",
                    idx, priv->n_input_buffers);
        ret = GST_FLOW_ERROR;
    } else {
        GST_ERROR_OBJECT (self, "Failed to acquire buffer for EOS: %d", idx);
        if (err)
          GST_ELEMENT_WARNING_FROM_ERROR (self, err);
        ret = GST_FLOW_ERROR;
    }

    return ret;
}

/* vim:set tabstop=4 softtabstop=4 shiftwidth=4 expandtab: */

