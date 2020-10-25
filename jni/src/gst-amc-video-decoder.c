/*
 ============================================================================
 Name        : gst-amc-video-decoder.c
 Author      : Heiher <r@hev.cc>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2014 everyone.
 Description : 
 ============================================================================
 */

#include <string.h>
#include <gst/video/videooverlay.h>

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
    N_PROPERTIES
};

typedef struct _BufferIdentification BufferIdentification;
typedef struct _GstAmcVideoDecoderPrivate GstAmcVideoDecoderPrivate;

#define GST_AMC_VIDEO_DECODER_GET_PRIVATE(obj) (gst_amc_video_decoder_get_instance_private(obj))

struct _BufferIdentification
{
    guint64 timestamp;
};

struct _GstAmcVideoDecoderPrivate
{
    GstAmcCodec *codec;
    GstAmcBuffer *input_buffers;
    gsize n_input_buffers;

    GstVideoCodecState *input_state;
    gboolean input_state_changed;

    const gchar *mime;
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

    /* TRUE if the component is drained currently */
    gboolean drained;

    GstFlowReturn downstream_flow_ret;

    jobject surface;

    gint width;
    gint height;
};

static GstStaticPadTemplate gst_amc_video_decoder_src_template =
GST_STATIC_PAD_TEMPLATE (
            "src",
            GST_PAD_SRC,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS ("video/x-amc-direct"));

static void gst_amc_video_decoder_video_overlay_init (gpointer iface, gpointer iface_data);

#define gst_amc_video_decoder_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmcVideoDecoder, gst_amc_video_decoder,
            GST_TYPE_VIDEO_DECODER,
            G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
                gst_amc_video_decoder_video_overlay_init);
            G_ADD_PRIVATE (GstAmcVideoDecoder));

extern void *orc_memcpy (void *dest, const void *src, size_t n);

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
static GstFlowReturn gst_amc_video_decoder_drain (GstAmcVideoDecoder * self);

static BufferIdentification *
buffer_identification_new (GstClockTime timestamp)
{
    BufferIdentification *id = g_slice_new (BufferIdentification);

    id->timestamp = timestamp;

    return id;
}

static void
buffer_identification_free (BufferIdentification * id)
{
    g_slice_free (BufferIdentification, id);
}

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

static const gchar *
caps_to_mime (GstCaps *caps)
{
    if (caps) {
        GstStructure *s = gst_caps_get_structure (caps, 0);
        if (s) {
            const gchar *name = gst_structure_get_name (s);
            if (strcmp (name, "video/x-h263") == 0) {
                return "video/3gpp";
            } else if (strcmp (name, "video/x-h265") == 0) {
                return "video/hevc";
            } else if (strcmp (name, "video/mpeg") == 0) {
                gint mpegversion = 0;
                gst_structure_get_int (s, "mpegversion", &mpegversion);
                switch (mpegversion) {
                case 1:
                case 2:
                    return "video/mpeg2";
                case 4:
                    return "video/mp4v-es";
                }
            } else if (strcmp (name, "video/x-vp8") == 0) {
                return "video/x-vnd.on2.vp8";
            } else if (strcmp (name, "video/x-vp9") == 0) {
                return "video/x-vnd.on2.vp9";
            } else if (strcmp (name, "video/x-divx") == 0) {
                return "video/mp4v-es";
            }
        }
    }

    return "video/avc";
}

const gchar *
mpeg4_profile_to_string (gint profile)
{
    static const struct
    {
        gint id;
        const gchar *str;
    } mpeg4_profile_mapping_table[] = {
        {
        MPEG4ProfileSimple, "simple"}, {
        MPEG4ProfileSimpleScalable, "simple-scalable"}, {
        MPEG4ProfileCore, "core"}, {
        MPEG4ProfileMain, "main"}, {
        MPEG4ProfileNbit, "n-bit"}, {
        MPEG4ProfileScalableTexture, "scalable"}, {
        MPEG4ProfileSimpleFace, "simple-face"}, {
        MPEG4ProfileSimpleFBA, "simple-fba"}, {
        MPEG4ProfileBasicAnimated, "basic-animated-texture"}, {
        MPEG4ProfileHybrid, "hybrid"}, {
        MPEG4ProfileAdvancedRealTime, "advanced-real-time"}, {
        MPEG4ProfileCoreScalable, "core-scalable"}, {
        MPEG4ProfileAdvancedCoding, "advanced-coding-efficiency"}, {
        MPEG4ProfileAdvancedCore, "advanced-core"}, {
        MPEG4ProfileAdvancedScalable, "advanced-scalable-texture"}, {
        MPEG4ProfileAdvancedSimple, "advanced-simple"}
    };
    gint i;

    for (i = 0; i < G_N_ELEMENTS (mpeg4_profile_mapping_table); i++) {
        if (mpeg4_profile_mapping_table[i].id == profile)
            return mpeg4_profile_mapping_table[i].str;
    }

    return NULL;
}

gint
h263_profile_to_gst_id (gint profile)
{
    static const struct
    {
        gint id;
        gint gst_id;
    } h263_profile_mapping_table[] = {
        {
        H263ProfileBaseline, 0}, {
        H263ProfileH320Coding, 1}, {
        H263ProfileBackwardCompatible, 2}, {
        H263ProfileISWV2, 3}, {
        H263ProfileISWV3, 4}, {
        H263ProfileHighCompression, 5}, {
        H263ProfileInternet, 6}, {
        H263ProfileInterlace, 7}, {
        H263ProfileHighLatency, 8}
    };
    gint i;

    for (i = 0; i < G_N_ELEMENTS (h263_profile_mapping_table); i++) {
        if (h263_profile_mapping_table[i].id == profile)
            return h263_profile_mapping_table[i].gst_id;
    }

    return -1;
}

const gchar *
avc_profile_to_string (gint profile, const gchar ** alternative)
{
    static const struct
    {
        gint id;
        const gchar *str;
        const gchar *alt_str;
    } avc_profile_mapping_table[] = {
        {
        AVCProfileBaseline, "baseline", "constrained-baseline"}, {
        AVCProfileMain, "main", NULL}, {
        AVCProfileExtended, "extended", NULL}, {
        AVCProfileHigh, "high"}, {
        AVCProfileHigh10, "high-10", "high-10-intra"}, {
        AVCProfileHigh422, "high-4:2:2", "high-4:2:2-intra"}, {
        AVCProfileHigh444, "high-4:4:4", "high-4:4:4-intra"}
    };
    gint i;

    for (i = 0; i < G_N_ELEMENTS (avc_profile_mapping_table); i++) {
        if (avc_profile_mapping_table[i].id == profile) {
            *alternative = avc_profile_mapping_table[i].alt_str;
            return avc_profile_mapping_table[i].str;
        }
    }

    return NULL;
}

const gchar *
hevc_profile_to_string (gint profile)
{
    static const struct
    {
        gint id;
        const gchar *str;
    } hevc_profile_mapping_table[] = {
        {
        HEVCProfileMain, "main"}, {
        HEVCProfileMain10, "main-10"}
    };
    gint i;

    for (i = 0; i < G_N_ELEMENTS (hevc_profile_mapping_table); i++) {
        if (hevc_profile_mapping_table[i].id == profile) {
            return hevc_profile_mapping_table[i].str;
        }
    }

    return NULL;
}

static void
codec_info_to_caps (GstCaps *caps, const gchar *mime,
            GstAmcCodecProfileLevel *profile_levels, gsize n_profile_levels)
{
    GstStructure *tmp, *tmp2, *tmp3;

    if (!g_str_has_prefix (mime, "video/"))
        return;

    if (strcmp (mime, "video/mp4v-es") == 0) {
        gboolean have_profile_level = FALSE;
        gint j;

        tmp = gst_structure_new ("video/mpeg",
                    "width", GST_TYPE_INT_RANGE, 16, 4096,
                    "height", GST_TYPE_INT_RANGE, 16, 4096,
                    "framerate", GST_TYPE_FRACTION_RANGE,
                    0, 1, G_MAXINT, 1,
                    "mpegversion", G_TYPE_INT, 4,
                    "systemstream", G_TYPE_BOOLEAN, FALSE,
                    "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

        if (n_profile_levels) {
            for (j = n_profile_levels - 1; j >= 0; j--) {
                const gchar *profile;

                profile = mpeg4_profile_to_string (profile_levels[j].profile);
                if (!profile) {
                    GST_ERROR ("Unable to map MPEG4 profile 0x%08x",
                                profile_levels[j].profile);
                    continue;
                }

                tmp2 = gst_structure_copy (tmp);
                gst_structure_set (tmp2, "profile", G_TYPE_STRING, profile, NULL);

                caps = gst_caps_merge_structure (caps, tmp2);
                have_profile_level = TRUE;
            }
        }

        if (!have_profile_level)
            caps = gst_caps_merge_structure (caps , tmp);
        else
            gst_structure_free (tmp);

        tmp = gst_structure_new ("video/x-divx",
                    "width", GST_TYPE_INT_RANGE, 16, 4096,
                    "height", GST_TYPE_INT_RANGE, 16, 4096,
                    "framerate", GST_TYPE_FRACTION_RANGE,
                    0, 1, G_MAXINT, 1,
                    "divxversion", GST_TYPE_INT_RANGE, 3, 5,
                    "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
        caps = gst_caps_merge_structure (caps, tmp);
    } else if (strcmp (mime, "video/3gpp") == 0) {
        gboolean have_profile_level = FALSE;
        gint j;

        tmp = gst_structure_new ("video/x-h263",
                    "width", GST_TYPE_INT_RANGE, 16, 4096,
                    "height", GST_TYPE_INT_RANGE, 16, 4096,
                    "framerate", GST_TYPE_FRACTION_RANGE,
                    0, 1, G_MAXINT, 1,
                    "parsed", G_TYPE_BOOLEAN, TRUE,
                    "variant", G_TYPE_STRING, "itu", NULL);

        if (n_profile_levels) {
            for (j = n_profile_levels - 1; j >= 0; j--) {
                gint profile;

                profile = h263_profile_to_gst_id (profile_levels[j].profile);
                if (profile == -1) {
                      GST_ERROR ("Unable to map h263 profile 0x%08x",
                                  profile_levels[j].profile);
                      continue;
                }

                tmp2 = gst_structure_copy (tmp);
                gst_structure_set (tmp2, "profile", G_TYPE_UINT, profile, NULL);

                caps = gst_caps_merge_structure (caps, tmp2);
                have_profile_level = TRUE;
            }
        }

       if (!have_profile_level)
            caps = gst_caps_merge_structure (caps, tmp);
        else
            gst_structure_free (tmp);
    } else if (strcmp (mime, "video/avc") == 0) {
        gboolean have_profile_level = FALSE;
        gint j;

        tmp = gst_structure_new ("video/x-h264",
                    "width", GST_TYPE_INT_RANGE, 16, 4096,
                    "height", GST_TYPE_INT_RANGE, 16, 4096,
                    "framerate", GST_TYPE_FRACTION_RANGE,
                    0, 1, G_MAXINT, 1,
                    "parsed", G_TYPE_BOOLEAN, TRUE,
                    "stream-format", G_TYPE_STRING, "byte-stream",
                    "alignment", G_TYPE_STRING, "au", NULL);

        if (n_profile_levels) {
            for (j = n_profile_levels - 1; j >= 0; j--) {
                const gchar *profile, *alternative = NULL;

                profile = avc_profile_to_string (profile_levels[j].profile,
                            &alternative);
                if (!profile) {
                    GST_ERROR ("Unable to map H264 profile 0x%08x",
                                profile_levels[j].profile);
                    continue;
                }

                tmp2 = gst_structure_copy (tmp);
                gst_structure_set (tmp2, "profile", G_TYPE_STRING, profile, NULL);

                if (alternative) {
                    tmp3 = gst_structure_copy (tmp);
                    gst_structure_set (tmp3, "profile", G_TYPE_STRING, alternative, NULL);
                } else {
                    tmp3 = NULL;
                }

                caps = gst_caps_merge_structure (caps, tmp2);
                if (tmp3)
                    caps = gst_caps_merge_structure (caps, tmp3);
                have_profile_level = TRUE;
            }
        }

        if (!have_profile_level)
            caps = gst_caps_merge_structure (caps, tmp);
        else
            gst_structure_free (tmp);
    } else if (strcmp (mime, "video/hevc") == 0) {
        gboolean have_profile_level = FALSE;
        gint j;

        tmp = gst_structure_new ("video/x-h265",
                    "width", GST_TYPE_INT_RANGE, 16, 4096,
                    "height", GST_TYPE_INT_RANGE, 16, 4096,
                    "framerate", GST_TYPE_FRACTION_RANGE,
                    0, 1, G_MAXINT, 1,
                    "parsed", G_TYPE_BOOLEAN, TRUE,
                    "stream-format", G_TYPE_STRING, "byte-stream",
                    "alignment", G_TYPE_STRING, "au", NULL);

        if (n_profile_levels) {
            for (j = n_profile_levels - 1; j >= 0; j--) {
                const gchar *profile;

                profile = hevc_profile_to_string (profile_levels[j].profile);

                if (!profile) {
                    GST_ERROR ("Unable to map H265 profile 0x%08x",
                                profile_levels[j].profile);
                    continue;
                }

                tmp2 = gst_structure_copy (tmp);
                gst_structure_set (tmp2, "profile", G_TYPE_STRING, profile, NULL);

                caps = gst_caps_merge_structure (caps, tmp2);
                have_profile_level = TRUE;
            }
        }

        if (!have_profile_level)
            caps = gst_caps_merge_structure (caps, tmp);
        else
            gst_structure_free (tmp);
    } else if (strcmp (mime, "video/x-vnd.on2.vp8") == 0) {
        tmp = gst_structure_new ("video/x-vp8",
                    "width", GST_TYPE_INT_RANGE, 16, 4096,
                    "height", GST_TYPE_INT_RANGE, 16, 4096,
                    "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

        caps = gst_caps_merge_structure (caps, tmp);
    } else if (strcmp (mime, "video/x-vnd.on2.vp9") == 0) {
        tmp = gst_structure_new ("video/x-vp9",
                    "width", GST_TYPE_INT_RANGE, 16, 4096,
                    "height", GST_TYPE_INT_RANGE, 16, 4096,
                    "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

          caps = gst_caps_merge_structure (caps, tmp);
    } else if (strcmp (mime, "video/mpeg2") == 0) {
        tmp = gst_structure_new ("video/mpeg",
                "width", GST_TYPE_INT_RANGE, 16, 4096,
                "height", GST_TYPE_INT_RANGE, 16, 4096,
                "framerate", GST_TYPE_FRACTION_RANGE,
                0, 1, G_MAXINT, 1,
                "mpegversion", GST_TYPE_INT_RANGE, 1, 2,
                "systemstream", G_TYPE_BOOLEAN, FALSE,
                "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

        caps = gst_caps_merge_structure (caps, tmp);
    } else {
        GST_WARNING ("Unsupported mimetype '%s'", mime);
    }
}

static void
gst_amc_video_decoder_class_init (GstAmcVideoDecoderClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
    GstVideoDecoderClass *videodec_class = GST_VIDEO_DECODER_CLASS (klass);
    GstPadTemplate *templ;
    GstCaps *caps;

    parent_class = g_type_class_peek_parent (klass);

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

    caps = gst_amc_codeclist_to_caps (codec_info_to_caps);
    templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
    gst_caps_unref (caps);
    gst_element_class_add_pad_template (element_class, templ);

    gst_element_class_add_pad_template (element_class,
            gst_static_pad_template_get (&gst_amc_video_decoder_src_template));

    gst_element_class_set_static_metadata (element_class, "Amc Video Decoder",
            "Decoder/Video/Amc",
            "Android Media codec video decoder",
            "Heiher <r@hev.cc>");

    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "amcvideodecoder", 0, "AmcVideoDecoder");
}

static void
gst_amc_video_decoder_video_overlay_set_window_handle (GstVideoOverlay * overlay,
            guintptr handle)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (overlay);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    JNIEnv *env = gst_amc_jni_get_env ();
    jobject surface = (jobject) handle;

    if (priv->surface)
        gst_amc_jni_object_unref (env, priv->surface);

    if (surface)
        priv->surface = gst_amc_jni_object_ref (env, surface);
}

static void
gst_amc_video_decoder_video_overlay_init (gpointer iface, gpointer iface_data)
{
    GstVideoOverlayInterface *overlay = (GstVideoOverlayInterface*) iface;
    overlay->set_window_handle = gst_amc_video_decoder_video_overlay_set_window_handle;
}

static void
gst_amc_video_decoder_init (GstAmcVideoDecoder * self)
{
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);
    gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (self), TRUE);

    priv->mime = caps_to_mime (NULL);
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

    priv->codec = gst_amc_decoder_new_from_type (priv->mime, &err);
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

#define MAX_FRAME_DIST_TIME  (5 * GST_SECOND)
#define MAX_FRAME_DIST_FRAMES (100)

static GstVideoCodecFrame *
_find_nearest_frame (GstAmcVideoDecoder * self, GstClockTime reference_timestamp)
{
    GList *l, *best_l = NULL;
    GList *finish_frames = NULL;
    GstVideoCodecFrame *best = NULL;
    guint64 best_timestamp = 0;
    guint64 best_diff = G_MAXUINT64;
    BufferIdentification *best_id = NULL;
    GList *frames;

    frames = gst_video_decoder_get_frames (GST_VIDEO_DECODER (self));

    for (l = frames; l; l = l->next) {
        GstVideoCodecFrame *tmp = l->data;
        BufferIdentification *id = gst_video_codec_frame_get_user_data (tmp);
        guint64 timestamp, diff;

        /* This happens for frames that were just added but
         * which were not passed to the component yet. Ignore
         * them here!
         */
        if (!id)
            continue;

        timestamp = id->timestamp;

        if (timestamp > reference_timestamp)
            diff = timestamp - reference_timestamp;
        else
            diff = reference_timestamp - timestamp;

        if (best == NULL || diff < best_diff) {
            best = tmp;
            best_timestamp = timestamp;
            best_diff = diff;
            best_l = l;
            best_id = id;

            /* For frames without timestamp we simply take the first frame */
            if ((reference_timestamp == 0 && timestamp == 0) || diff == 0)
                break;
        }
    }

    if (best_id) {
        for (l = frames; l && l != best_l; l = l->next) {
            GstVideoCodecFrame *tmp = l->data;
            BufferIdentification *id = gst_video_codec_frame_get_user_data (tmp);
            guint64 diff_time, diff_frames;

            if (id->timestamp > best_timestamp)
                break;

            if (id->timestamp == 0 || best_timestamp == 0)
                diff_time = 0;
            else
                diff_time = best_timestamp - id->timestamp;
            diff_frames = best->system_frame_number - tmp->system_frame_number;

            if (diff_time > MAX_FRAME_DIST_TIME || diff_frames > MAX_FRAME_DIST_FRAMES) {
                finish_frames =
                    g_list_prepend (finish_frames, gst_video_codec_frame_ref (tmp));
            }
        }
    }

    if (finish_frames) {
        g_warning ("%s: Too old frames, bug in decoder -- please file a bug",
            GST_ELEMENT_NAME (self));
        for (l = finish_frames; l; l = l->next)
            gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), l->data);
    }

    if (best)
        gst_video_codec_frame_ref (best);

    g_list_foreach (frames, (GFunc) gst_video_codec_frame_unref, NULL);
    g_list_free (frames);

    return best;
}

static gboolean
gst_amc_video_decoder_set_src_caps (GstAmcVideoDecoder * self)
{
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GstVideoCodecState *state;
    gboolean ret;

    state = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
                GST_VIDEO_FORMAT_ENCODED, priv->width, priv->height,
                priv->input_state);
    if (state->caps)
        gst_caps_unref (state->caps);
    state->caps = gst_static_pad_template_get_caps (&gst_amc_video_decoder_src_template);
    ret = gst_video_decoder_negotiate (GST_VIDEO_DECODER (self));
    gst_video_codec_state_unref (state);

    return ret;
}

static void
gst_amc_video_decoder_free_buffer (gpointer data)
{
    GstAmcSinkBufferData *buffer_data = data;
    GError *error = NULL;

    if (buffer_data->codec) {
        if (!gst_amc_codec_release_output_buffer (buffer_data->codec, buffer_data->index,
                        FALSE, 0, &error)) {
            GST_ERROR ("Release output buffer fail: %s", error->message);
            g_error_free (error);
        }
    }

    g_slice_free (GstAmcSinkBufferData, buffer_data);
}

static GstBuffer *
gst_amc_video_decoder_new_buffer (GstAmcVideoDecoder * self, gint idx)
{
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GstMapInfo minfo;
    GstBuffer *outbuf;
    GstAmcSinkBufferData *buffer_data;
    const gsize buffer_data_size = sizeof (GstAmcSinkBufferData);

    buffer_data = g_slice_new (GstAmcSinkBufferData);
    if (!buffer_data)
        return NULL;

    buffer_data->codec = priv->codec;
    buffer_data->index = idx;

    outbuf = gst_buffer_new_wrapped_full (0, buffer_data, buffer_data_size, 0,
                buffer_data_size, buffer_data, gst_amc_video_decoder_free_buffer);
    if (!outbuf)
        g_slice_free (GstAmcSinkBufferData, buffer_data);

    return outbuf;
}

static void
gst_amc_video_decoder_loop (GstAmcVideoDecoder * self)
{
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);
    GstFlowReturn flow_ret = GST_FLOW_OK;
    gboolean release_buffer = TRUE;
    GstAmcBufferInfo buffer_info;
    GstVideoCodecFrame *frame;
    GError *err = NULL;
    GstBuffer *outbuf;
    gboolean is_eos;
    gint idx;

    GST_VIDEO_DECODER_STREAM_LOCK (self);

retry:
    GST_DEBUG_OBJECT (self, "Waiting for available output buffer");
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    /* Wait at most 100ms here, some codecs don't fail dequeueing if
    * the codec is flushing, causing deadlocks during shutdown */
    idx = gst_amc_codec_dequeue_output_buffer (priv->codec, &buffer_info, 100000, &err);
    GST_VIDEO_DECODER_STREAM_LOCK (self);

    if (idx < 0) {
        if (priv->flushing) {
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

            if (!gst_amc_video_decoder_set_src_caps (self))
                goto format_error;

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

    frame = _find_nearest_frame (self,
                gst_util_uint64_scale (buffer_info.presentation_time_us, GST_USECOND, 1));

    is_eos = !!(buffer_info.flags & BUFFER_FLAG_END_OF_STREAM);

    if (frame && (gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (self), frame)) < 0) {
        flow_ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
    } else if (buffer_info.size > 0) {
        if (!(outbuf = gst_amc_video_decoder_new_buffer (self, idx))) {
            if (!gst_amc_codec_release_output_buffer (priv->codec, idx, FALSE, 0, &err))
                GST_ERROR_OBJECT (self, "Failed to release output buffer index %d", idx);
            if (err && !priv->flushing)
                GST_ELEMENT_WARNING_FROM_ERROR (self, err);
            g_clear_error (&err);
            goto flow_error;
        }

        if (frame) {
            frame->output_buffer = outbuf;
            gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
        } else {
            GST_BUFFER_PTS (outbuf) =
                gst_util_uint64_scale (buffer_info.presentation_time_us, GST_USECOND, 1);
            flow_ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), outbuf);
        }
        release_buffer = FALSE;
    } else if (frame != NULL) {
        flow_ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
    }

    if (release_buffer) {
        if (!gst_amc_codec_release_output_buffer (priv->codec, idx, FALSE, 0, &err)) {
            if (priv->flushing) {
                g_clear_error (&err);
                goto flushing;
            }
            goto failed_release;
        }
    }

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
}

static gboolean
gst_amc_video_decoder_start (GstVideoDecoder * decoder)
{
    GstAmcVideoDecoder *self = GST_AMC_VIDEO_DECODER (decoder);
    GstAmcVideoDecoderPrivate *priv = GST_AMC_VIDEO_DECODER_GET_PRIVATE (self);

    priv->last_upstream_ts = 0;
    priv->drained = TRUE;
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
    priv->drained = TRUE;
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
    const gchar *mime;

    GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, state->caps);
    mime = caps_to_mime (state->caps);

    needs_disable |= priv->mime != mime;
    needs_disable |= priv->started;

    /* Check if the caps change is a real format change or if only irrelevant
    * parts of the caps have changed or nothing at all.
    */
    is_format_change |= priv->mime != mime;
    is_format_change |= priv->width != state->info.width;
    is_format_change |= priv->height != state->info.height;
    priv->mime = mime;
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
        gst_amc_video_decoder_drain (self);
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

    format = gst_amc_format_new_video (priv->mime, state->info.width,
                state->info.height, &err);
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

    if (!priv->surface)
        gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (decoder));

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
    priv->drained = TRUE;
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

        if (idx < 0 || priv->downstream_flow_ret == GST_FLOW_FLUSHING) {
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

        if (priv->flushing) {
            memset (&buffer_info, 0, sizeof (buffer_info));
            gst_amc_codec_queue_input_buffer (priv->codec, idx, &buffer_info, NULL);
            goto flushing;
        }

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

        if (offset == 0) {
            BufferIdentification *id = buffer_identification_new (timestamp + timestamp_offset);
            if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame))
                buffer_info.flags |= BUFFER_FLAG_SYNC_FRAME;
            gst_video_codec_frame_set_user_data (frame, id,
                (GDestroyNotify) buffer_identification_free);
        }

        offset += buffer_info.size;
        GST_DEBUG_OBJECT (self,
                    "Queueing buffer %d: size %d time %" G_GINT64_FORMAT " flags 0x%08x",
                    idx, buffer_info.size, buffer_info.presentation_time_us,
                    buffer_info.flags);
        if (!gst_amc_codec_queue_input_buffer (priv->codec, idx, &buffer_info, &err))
          goto queue_error;
        priv->drained = FALSE;
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
                ("Invalid input buffer index %d of %zu", idx, priv->n_input_buffers));
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

    return gst_amc_video_decoder_drain (self);
}

static GstFlowReturn
gst_amc_video_decoder_drain (GstAmcVideoDecoder * self)
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

    /* Don't send drain buffer twice, this doesn't work */
    if (priv->drained) {
        GST_DEBUG_OBJECT (self, "Codec is drained already");
        return GST_FLOW_OK;
    }

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

        priv->drained = TRUE;
        priv->draining = FALSE;
        g_mutex_unlock (&priv->drain_lock);
        GST_VIDEO_DECODER_STREAM_LOCK (self);
    } else if (idx >= priv->n_input_buffers) {
        GST_ERROR_OBJECT (self, "Invalid input buffer index %d of %zu",
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

