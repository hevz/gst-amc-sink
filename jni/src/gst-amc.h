/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_AMC_H__
#define __GST_AMC_H__

#include <jni.h>
#include <gst/gst.h>

#include "gst-jni-utils.h"

G_BEGIN_DECLS

enum
{
    INFO_TRY_AGAIN_LATER = -1,
    INFO_OUTPUT_FORMAT_CHANGED = -2,
    INFO_OUTPUT_BUFFERS_CHANGED = -3
};

enum
{
    BUFFER_FLAG_SYNC_FRAME = 1,
    BUFFER_FLAG_CODEC_CONFIG = 2,
    BUFFER_FLAG_END_OF_STREAM = 4
};

enum
{
    MPEG4ProfileSimple = 0x01,
    MPEG4ProfileSimpleScalable = 0x02,
    MPEG4ProfileCore = 0x04,
    MPEG4ProfileMain = 0x08,
    MPEG4ProfileNbit = 0x10,
    MPEG4ProfileScalableTexture = 0x20,
    MPEG4ProfileSimpleFace = 0x40,
    MPEG4ProfileSimpleFBA = 0x80,
    MPEG4ProfileBasicAnimated = 0x100,
    MPEG4ProfileHybrid = 0x200,
    MPEG4ProfileAdvancedRealTime = 0x400,
    MPEG4ProfileCoreScalable = 0x800,
    MPEG4ProfileAdvancedCoding = 0x1000,
    MPEG4ProfileAdvancedCore = 0x2000,
    MPEG4ProfileAdvancedScalable = 0x4000,
    MPEG4ProfileAdvancedSimple = 0x8000
};

enum
{
    H263ProfileBaseline = 0x01,
    H263ProfileH320Coding = 0x02,
    H263ProfileBackwardCompatible = 0x04,
    H263ProfileISWV2 = 0x08,
    H263ProfileISWV3 = 0x10,
    H263ProfileHighCompression = 0x20,
    H263ProfileInternet = 0x40,
    H263ProfileInterlace = 0x80,
    H263ProfileHighLatency = 0x100
};

enum
{
    AVCProfileBaseline = 0x01,
    AVCProfileMain = 0x02,
    AVCProfileExtended = 0x04,
    AVCProfileHigh = 0x08,
    AVCProfileHigh10 = 0x10,
    AVCProfileHigh422 = 0x20,
    AVCProfileHigh444 = 0x40
};

enum
{
    HEVCProfileMain    = 0x01,
    HEVCProfileMain10  = 0x02
};

typedef struct _GstAmcCodec GstAmcCodec;
typedef struct _GstAmcFormat GstAmcFormat;
typedef struct _GstAmcBufferInfo GstAmcBufferInfo;
typedef struct _GstAmcCodecInfoHandle GstAmcCodecInfoHandle;
typedef struct _GstAmcCodecCapabilitiesHandle GstAmcCodecCapabilitiesHandle;
typedef struct _GstAmcCodecProfileLevel GstAmcCodecProfileLevel;
typedef void (*GstAmcCodecForeachFunc) (GstCaps *, const gchar *, GstAmcCodecProfileLevel*, gsize);

struct _GstAmcCodec {
  /* < private > */
  jobject object; /* global reference */
};

struct _GstAmcFormat {
  /* < private > */
  jobject object; /* global reference */
};

struct _GstAmcBufferInfo {
  gint flags;
  gint offset;
  gint64 presentation_time_us;
  gint size;
};

struct _GstAmcCodecProfileLevel
{
  gint profile;
  gint level;
};

GstAmcCodec * gst_amc_codec_new (const gchar *name, GError **err);
GstAmcCodec * gst_amc_decoder_new_from_type (const gchar *type, GError **err);
GstAmcCodec * gst_amc_encoder_new_from_type (const gchar *type, GError **err);
void gst_amc_codec_free (GstAmcCodec * codec);

gboolean gst_amc_codec_configure (GstAmcCodec * codec, GstAmcFormat * format, jobject surface, gint flags, GError **err);
GstAmcFormat * gst_amc_codec_get_output_format (GstAmcCodec * codec, GError **err);

gboolean gst_amc_codec_start (GstAmcCodec * codec, GError **err);
gboolean gst_amc_codec_stop (GstAmcCodec * codec, GError **err);
gboolean gst_amc_codec_flush (GstAmcCodec * codec, GError **err);
gboolean gst_amc_codec_release (GstAmcCodec * codec, GError **err);

GstAmcBuffer * gst_amc_codec_get_output_buffers (GstAmcCodec * codec, gsize * n_buffers, GError **err);
GstAmcBuffer * gst_amc_codec_get_input_buffers (GstAmcCodec * codec, gsize * n_buffers, GError **err);
void gst_amc_codec_free_buffers (GstAmcBuffer * buffers, gsize n_buffers);

gint gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs, GError **err);
gint gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec, GstAmcBufferInfo *info, gint64 timeoutUs, GError **err);

gboolean gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index, const GstAmcBufferInfo *info, GError **err);
gboolean gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index, gboolean render, gint64 delay, GError **err);


GstAmcFormat * gst_amc_format_new_audio (const gchar *mime, gint sample_rate, gint channels, GError **err);
GstAmcFormat * gst_amc_format_new_video (const gchar *mime, gint width, gint height, GError **err);
void gst_amc_format_free (GstAmcFormat * format);

gchar * gst_amc_format_to_string (GstAmcFormat * format, GError **err);

gboolean gst_amc_format_contains_key (GstAmcFormat *format, const gchar *key, GError **err);

gboolean gst_amc_format_get_float (GstAmcFormat *format, const gchar *key, gfloat *value, GError **err);
gboolean gst_amc_format_set_float (GstAmcFormat *format, const gchar *key, gfloat value, GError **err);
gboolean gst_amc_format_get_int (GstAmcFormat *format, const gchar *key, gint *value, GError **err);
gboolean gst_amc_format_set_int (GstAmcFormat *format, const gchar *key, gint value, GError **err);
gboolean gst_amc_format_get_string (GstAmcFormat *format, const gchar *key, gchar **value, GError **err);
gboolean gst_amc_format_set_string (GstAmcFormat *format, const gchar *key, const gchar *value, GError **err);
gboolean gst_amc_format_get_buffer (GstAmcFormat *format, const gchar *key, guint8 **data, gsize *size, GError **err);
gboolean gst_amc_format_set_buffer (GstAmcFormat *format, const gchar *key, guint8 *data, gsize size, GError **err);


gboolean gst_amc_codeclist_get_count (gint * count, GError **err);
GstAmcCodecInfoHandle * gst_amc_codeclist_get_codec_info_at (gint index,
    GError **err);

GstCaps * gst_amc_codeclist_to_caps (GstAmcCodecForeachFunc func);

void gst_amc_codec_info_handle_free (GstAmcCodecInfoHandle * handle);
gchar * gst_amc_codec_info_handle_get_name (GstAmcCodecInfoHandle * handle,
    GError ** err);
gboolean gst_amc_codec_info_handle_is_encoder (GstAmcCodecInfoHandle * handle,
    gboolean * is_encoder, GError ** err);
gchar ** gst_amc_codec_info_handle_get_supported_types (
    GstAmcCodecInfoHandle * handle, gsize * length, GError ** err);
GstAmcCodecCapabilitiesHandle * gst_amc_codec_info_handle_get_capabilities_for_type (
    GstAmcCodecInfoHandle * handle, const gchar * type, GError ** err);

void gst_amc_codec_capabilities_handle_free (
    GstAmcCodecCapabilitiesHandle * handle);
GstAmcCodecProfileLevel * gst_amc_codec_capabilities_handle_get_profile_levels (
    GstAmcCodecCapabilitiesHandle * handle, gsize * length, GError ** err);


#define GST_ELEMENT_ERROR_FROM_ERROR(el, err) G_STMT_START { \
  gchar *__dbg = g_strdup (err->message);                               \
  GST_WARNING_OBJECT (el, "error: %s", __dbg);                          \
  gst_element_message_full (GST_ELEMENT(el), GST_MESSAGE_ERROR,         \
    err->domain, err->code,                                             \
    NULL, __dbg, __FILE__, GST_FUNCTION, __LINE__);                     \
  g_clear_error (&err); \
} G_STMT_END

#define GST_ELEMENT_WARNING_FROM_ERROR(el, err) G_STMT_START { \
  gchar *__dbg = g_strdup (err->message);                               \
  GST_WARNING_OBJECT (el, "error: %s", __dbg);                          \
  gst_element_message_full (GST_ELEMENT(el), GST_MESSAGE_WARNING,       \
    err->domain, err->code,                                             \
    NULL, __dbg, __FILE__, GST_FUNCTION, __LINE__);                     \
  g_clear_error (&err); \
} G_STMT_END

jmethodID gst_amc_codec_get_release_method_id (GstAmcCodec *codec);

gboolean gst_amc_init (void);

G_END_DECLS

#endif /* __GST_AMC_H__ */

/* vim:set tabstop=4 softtabstop=4 shiftwidth=4 expandtab: */

