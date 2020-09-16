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

typedef struct _GstAmcCodec GstAmcCodec;
typedef struct _GstAmcFormat GstAmcFormat;
typedef struct _GstAmcBufferInfo GstAmcBufferInfo;

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
gboolean gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index, GError **err);


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

