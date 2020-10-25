/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2015, Sebastian Dröge <sebastian@centricular.com>
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

#include <string.h>

#include "gst-amc.h"
#include "gst-jni-utils.h"

GST_DEBUG_CATEGORY (gst_amc_debug);
#define GST_CAT_DEFAULT gst_amc_debug

struct _GstAmcCodecInfoHandle
{
  jobject object;
};

struct _GstAmcCodecCapabilitiesHandle
{
  jobject object;
};

/* Global cached references */
static struct
{
  jclass klass;
  jmethodID nano_time;
} system;

static struct
{
  jclass klass;
  jmethodID configure;
  jmethodID create_by_codec_name;
  jmethodID create_decoder_by_type;
  jmethodID create_encoder_by_type;
  jmethodID dequeue_input_buffer;
  jmethodID dequeue_output_buffer;
  jmethodID flush;
  jmethodID get_input_buffers;
  jmethodID get_output_buffers;
  jmethodID get_output_format;
  jmethodID queue_input_buffer;
  jmethodID release;
  jmethodID release_output_buffer;
  jmethodID release_output_buffer_time;
  jmethodID start;
  jmethodID stop;
} media_codec;

static struct
{
  jclass klass;
  jmethodID constructor;
  jfieldID flags;
  jfieldID offset;
  jfieldID presentation_time_us;
  jfieldID size;
} media_codec_buffer_info;

static struct
{
  jclass klass;
  jmethodID create_audio_format;
  jmethodID create_video_format;
  jmethodID to_string;
  jmethodID contains_key;
  jmethodID get_float;
  jmethodID set_float;
  jmethodID get_integer;
  jmethodID set_integer;
  jmethodID get_string;
  jmethodID set_string;
  jmethodID get_byte_buffer;
  jmethodID set_byte_buffer;
} media_format;

static struct
{
  jclass klass;
  jmethodID get_codec_count;
  jmethodID get_codec_info_at;
} media_codeclist;

static struct
{
  jclass klass;
  jmethodID get_capabilities_for_type;
  jmethodID get_name;
  jmethodID get_supported_types;
  jmethodID is_encoder;
} media_codecinfo;

static struct
{
  jclass klass;
  jfieldID profile_levels;
} media_codeccapabilities;

static struct
{
  jclass klass;
  jfieldID level;
  jfieldID profile;
} media_codecprofilelevel;

GstAmcCodec *
gst_amc_codec_new (const gchar * name, GError ** err)
{
  JNIEnv *env;
  GstAmcCodec *codec = NULL;
  jstring name_str;
  jobject object = NULL;

  g_return_val_if_fail (name != NULL, NULL);

  env = gst_amc_jni_get_env ();

  name_str = gst_amc_jni_string_from_gchar (env, err, FALSE, name);
  if (!name_str) {
    goto error;
  }

  codec = g_slice_new0 (GstAmcCodec);

  if (!gst_amc_jni_call_static_object_method (env, err, media_codec.klass,
          media_codec.create_by_codec_name, &object, name_str))
    goto error;

  codec->object = gst_amc_jni_object_make_global (env, object);
  object = NULL;

  if (!codec->object) {
    gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
        GST_LIBRARY_ERROR_SETTINGS, "Failed to create global codec reference");
    goto error;
  }

done:
  if (name_str)
    gst_amc_jni_object_local_unref (env, name_str);
  name_str = NULL;

  return codec;

error:
  if (codec)
    g_slice_free (GstAmcCodec, codec);
  codec = NULL;
  goto done;
}

static GstAmcCodec *
gst_amc_codec_new_from_type (jmethodID method_id, const gchar * type, GError ** err)
{
  JNIEnv *env;
  GstAmcCodec *codec = NULL;
  jstring type_str;
  jobject object = NULL;

  g_return_val_if_fail (type != NULL, NULL);

  env = gst_amc_jni_get_env ();

  type_str = (*env)->NewStringUTF (env, type);
  if (type_str == NULL) {
    gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
        "Failed to create Java String");
    goto error;
  }

  codec = g_slice_new0 (GstAmcCodec);

  object =
      (*env)->CallStaticObjectMethod (env, media_codec.klass, method_id, type_str);
  if ((*env)->ExceptionCheck (env) || !object) {
    gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
        "Failed to create decoder by type '%s'", type);
    goto error;
  }

  codec->object = (*env)->NewGlobalRef (env, object);
  if (!codec->object) {
    gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
        "Failed to create global codec reference");
    goto error;
  }

done:
  if (object)
    (*env)->DeleteLocalRef (env, object);
  if (type_str)
    (*env)->DeleteLocalRef (env, type_str);
  type_str = NULL;

  return codec;

error:
  if (codec)
    g_slice_free (GstAmcCodec, codec);
  codec = NULL;
  goto done;
}

GstAmcCodec *
gst_amc_decoder_new_from_type (const gchar * type, GError ** err)
{
    return gst_amc_codec_new_from_type (media_codec.create_decoder_by_type, type, err);
}

GstAmcCodec *
gst_amc_encoder_new_from_type (const gchar * type, GError ** err)
{
    return gst_amc_codec_new_from_type (media_codec.create_encoder_by_type, type, err);
}

void
gst_amc_codec_free (GstAmcCodec * codec)
{
  JNIEnv *env;

  g_return_if_fail (codec != NULL);

  env = gst_amc_jni_get_env ();
  gst_amc_jni_object_unref (env, codec->object);
  g_slice_free (GstAmcCodec, codec);
}

jmethodID
gst_amc_codec_get_release_method_id (GstAmcCodec * codec)
{
  g_return_val_if_fail (codec != NULL, FALSE);

  return media_codec.release_output_buffer;
}

gboolean
gst_amc_codec_configure (GstAmcCodec * codec, GstAmcFormat * format,
    jobject surface, gint flags, GError ** err)
{
  JNIEnv *env;

  g_return_val_if_fail (codec != NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  return gst_amc_jni_call_void_method (env, err, codec->object,
      media_codec.configure, format->object, surface, NULL, flags);
}

GstAmcFormat *
gst_amc_codec_get_output_format (GstAmcCodec * codec, GError ** err)
{
  JNIEnv *env;
  GstAmcFormat *ret = NULL;
  jobject object = NULL;

  g_return_val_if_fail (codec != NULL, NULL);

  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_call_object_method (env, err, codec->object,
          media_codec.get_output_format, &object))
    goto done;

  ret = g_slice_new0 (GstAmcFormat);

  ret->object = gst_amc_jni_object_make_global (env, object);
  if (!ret->object) {
    gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
        GST_LIBRARY_ERROR_SETTINGS, "Failed to create global format reference");
    g_slice_free (GstAmcFormat, ret);
    ret = NULL;
  }

done:

  return ret;
}

gboolean
gst_amc_codec_start (GstAmcCodec * codec, GError ** err)
{
  JNIEnv *env;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  return gst_amc_jni_call_void_method (env, err, codec->object,
      media_codec.start);
}

gboolean
gst_amc_codec_stop (GstAmcCodec * codec, GError ** err)
{
  JNIEnv *env;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  return gst_amc_jni_call_void_method (env, err, codec->object,
      media_codec.stop);
}

gboolean
gst_amc_codec_flush (GstAmcCodec * codec, GError ** err)
{
  JNIEnv *env;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  return gst_amc_jni_call_void_method (env, err, codec->object,
      media_codec.flush);
}

gboolean
gst_amc_codec_release (GstAmcCodec * codec, GError ** err)
{
  JNIEnv *env;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  return gst_amc_jni_call_void_method (env, err, codec->object,
      media_codec.release);
}

GstAmcBuffer *
gst_amc_codec_get_output_buffers (GstAmcCodec * codec, gsize * n_buffers,
    GError ** err)
{
  JNIEnv *env;
  jobject output_buffers = NULL;
  GstAmcBuffer *ret = NULL;

  g_return_val_if_fail (codec != NULL, NULL);
  g_return_val_if_fail (n_buffers != NULL, NULL);

  *n_buffers = 0;
  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_call_object_method (env, err, codec->object,
          media_codec.get_output_buffers, &output_buffers))
    goto done;

  gst_amc_jni_get_buffer_array (env, err, output_buffers, &ret, n_buffers);

done:
  if (output_buffers)
    gst_amc_jni_object_local_unref (env, output_buffers);

  return ret;
}

GstAmcBuffer *
gst_amc_codec_get_input_buffers (GstAmcCodec * codec, gsize * n_buffers,
    GError ** err)
{
  JNIEnv *env;
  jobject input_buffers = NULL;
  GstAmcBuffer *ret = NULL;

  g_return_val_if_fail (codec != NULL, NULL);
  g_return_val_if_fail (n_buffers != NULL, NULL);

  *n_buffers = 0;
  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_call_object_method (env, err, codec->object,
          media_codec.get_input_buffers, &input_buffers))
    goto done;

  gst_amc_jni_get_buffer_array (env, err, input_buffers, &ret, n_buffers);

done:
  if (input_buffers)
    gst_amc_jni_object_local_unref (env, input_buffers);

  return ret;
}

void
gst_amc_codec_free_buffers (GstAmcBuffer * buffers, gsize n_buffers)
{
  JNIEnv *env;

  env = gst_amc_jni_get_env ();
  gst_amc_jni_free_buffer_array (env, buffers, n_buffers);
}

gint
gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs,
    GError ** err)
{
  JNIEnv *env;
  gint ret = G_MININT;

  g_return_val_if_fail (codec != NULL, G_MININT);

  env = gst_amc_jni_get_env ();


  if (!gst_amc_jni_call_int_method (env, err, codec->object,
          media_codec.dequeue_input_buffer, &ret, timeoutUs))
    return G_MININT;
  return ret;
}

static gboolean
gst_amc_codec_fill_buffer_info (JNIEnv * env, jobject buffer_info,
    GstAmcBufferInfo * info, GError ** err)
{
  g_return_val_if_fail (buffer_info != NULL, FALSE);

  if (!gst_amc_jni_get_int_field (env, err, buffer_info,
          media_codec_buffer_info.flags, &info->flags))
    return FALSE;

  if (!gst_amc_jni_get_int_field (env, err, buffer_info,
          media_codec_buffer_info.offset, &info->offset))
    return FALSE;

  if (!gst_amc_jni_get_long_field (env, err, buffer_info,
          media_codec_buffer_info.presentation_time_us,
          &info->presentation_time_us))
    return FALSE;

  if (!gst_amc_jni_get_int_field (env, err, buffer_info,
          media_codec_buffer_info.size, &info->size))
    return FALSE;

  return TRUE;
}

gint
gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec,
    GstAmcBufferInfo * info, gint64 timeoutUs, GError ** err)
{
  JNIEnv *env;
  gint ret = G_MININT;
  jobject info_o = NULL;

  g_return_val_if_fail (codec != NULL, G_MININT);

  env = gst_amc_jni_get_env ();

  info_o =
      gst_amc_jni_new_object (env, err, FALSE, media_codec_buffer_info.klass,
      media_codec_buffer_info.constructor);
  if (!info_o)
    goto done;

  if (!gst_amc_jni_call_int_method (env, err, codec->object,
          media_codec.dequeue_output_buffer, &ret, info_o, timeoutUs)) {
    ret = G_MININT;
    goto done;
  }

  if (!gst_amc_codec_fill_buffer_info (env, info_o, info, err)) {
    ret = G_MININT;
    goto done;
  }

done:
  if (info_o)
    gst_amc_jni_object_local_unref (env, info_o);
  info_o = NULL;

  return ret;
}

gboolean
gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info, GError ** err)
{
  JNIEnv *env;

  g_return_val_if_fail (codec != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  return gst_amc_jni_call_void_method (env, err, codec->object,
      media_codec.queue_input_buffer, index, info->offset, info->size,
      info->presentation_time_us, info->flags);
}

gboolean
gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index,
    gboolean render, gint64 delay, GError ** err)
{
  JNIEnv *env;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  if (render) {
    gint64 time;
    if (gst_amc_jni_call_static_long_method (env, err, system.klass,
                    system.nano_time, &time))
        return gst_amc_jni_call_void_method (env, err, codec->object,
            media_codec.release_output_buffer_time, index, time + delay);
    g_clear_error (err);
  }

  return gst_amc_jni_call_void_method (env, err, codec->object,
      media_codec.release_output_buffer, index, JNI_FALSE);
}

GstAmcFormat *
gst_amc_format_new_audio (const gchar * mime, gint sample_rate, gint channels,
    GError ** err)
{
  JNIEnv *env;
  GstAmcFormat *format = NULL;
  jstring mime_str;

  g_return_val_if_fail (mime != NULL, NULL);

  env = gst_amc_jni_get_env ();

  mime_str = gst_amc_jni_string_from_gchar (env, err, FALSE, mime);
  if (!mime_str)
    goto error;

  format = g_slice_new0 (GstAmcFormat);

  format->object =
      gst_amc_jni_new_object_from_static (env, err, TRUE, media_format.klass,
      media_format.create_audio_format, mime_str, sample_rate, channels);
  if (!format->object)
    goto error;

done:
  if (mime_str)
    gst_amc_jni_object_local_unref (env, mime_str);
  mime_str = NULL;

  return format;

error:
  if (format)
    g_slice_free (GstAmcFormat, format);
  format = NULL;
  goto done;
}

GstAmcFormat *
gst_amc_format_new_video (const gchar * mime, gint width, gint height,
    GError ** err)
{
  JNIEnv *env;
  GstAmcFormat *format = NULL;
  jstring mime_str;

  g_return_val_if_fail (mime != NULL, NULL);

  env = gst_amc_jni_get_env ();

  mime_str = gst_amc_jni_string_from_gchar (env, err, FALSE, mime);
  if (!mime_str)
    goto error;

  format = g_slice_new0 (GstAmcFormat);

  format->object =
      gst_amc_jni_new_object_from_static (env, err, TRUE, media_format.klass,
      media_format.create_video_format, mime_str, width, height);
  if (!format->object)
    goto error;

done:
  if (mime_str)
    gst_amc_jni_object_local_unref (env, mime_str);
  mime_str = NULL;

  return format;

error:
  if (format)
    g_slice_free (GstAmcFormat, format);
  format = NULL;
  goto done;
}

void
gst_amc_format_free (GstAmcFormat * format)
{
  JNIEnv *env;

  g_return_if_fail (format != NULL);

  env = gst_amc_jni_get_env ();
  gst_amc_jni_object_unref (env, format->object);
  g_slice_free (GstAmcFormat, format);
}

gchar *
gst_amc_format_to_string (GstAmcFormat * format, GError ** err)
{
  JNIEnv *env;
  jstring v_str = NULL;
  gchar *ret = NULL;

  g_return_val_if_fail (format != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_call_object_method (env, err, format->object,
          media_format.to_string, &v_str))
    goto done;

  ret = gst_amc_jni_string_to_gchar (env, v_str, TRUE);

done:

  return ret;
}

gboolean
gst_amc_format_contains_key (GstAmcFormat * format, const gchar * key,
    GError ** err)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  key_str = gst_amc_jni_string_from_gchar (env, err, FALSE, key);
  if (!key_str)
    goto done;

  if (!gst_amc_jni_call_boolean_method (env, err, format->object,
          media_format.contains_key, &ret, key_str))
    goto done;

done:
  if (key_str)
    gst_amc_jni_object_local_unref (env, key_str);

  return ret;
}

gboolean
gst_amc_format_get_float (GstAmcFormat * format, const gchar * key,
    gfloat * value, GError ** err)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  *value = 0;
  env = gst_amc_jni_get_env ();

  key_str = gst_amc_jni_string_from_gchar (env, err, FALSE, key);
  if (!key_str)
    goto done;

  if (!gst_amc_jni_call_float_method (env, err, format->object,
          media_format.get_float, value, key_str))
    goto done;
  ret = TRUE;

done:
  if (key_str)
    gst_amc_jni_object_local_unref (env, key_str);

  return ret;
}

gboolean
gst_amc_format_set_float (GstAmcFormat * format, const gchar * key,
    gfloat value, GError ** err)
{
  JNIEnv *env;
  jstring key_str = NULL;
  gboolean ret = FALSE;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  key_str = gst_amc_jni_string_from_gchar (env, err, FALSE, key);
  if (!key_str)
    goto done;

  if (!gst_amc_jni_call_void_method (env, err, format->object,
          media_format.set_float, key_str, value))
    goto done;

  ret = TRUE;

done:
  if (key_str)
    gst_amc_jni_object_local_unref (env, key_str);

  return ret;
}

gboolean
gst_amc_format_get_int (GstAmcFormat * format, const gchar * key, gint * value,
    GError ** err)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  *value = 0;
  env = gst_amc_jni_get_env ();

  key_str = gst_amc_jni_string_from_gchar (env, err, FALSE, key);
  if (!key_str)
    goto done;

  if (!gst_amc_jni_call_int_method (env, err, format->object,
          media_format.get_integer, value, key_str))
    goto done;
  ret = TRUE;

done:
  if (key_str)
    gst_amc_jni_object_local_unref (env, key_str);

  return ret;
}

gboolean
gst_amc_format_set_int (GstAmcFormat * format, const gchar * key, gint value,
    GError ** err)
{
  JNIEnv *env;
  jstring key_str = NULL;
  gboolean ret = FALSE;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  key_str = gst_amc_jni_string_from_gchar (env, err, FALSE, key);
  if (!key_str)
    goto done;

  if (!gst_amc_jni_call_void_method (env, err, format->object,
          media_format.set_integer, key_str, value))
    goto done;

  ret = TRUE;

done:
  if (key_str)
    gst_amc_jni_object_local_unref (env, key_str);

  return ret;
}

gboolean
gst_amc_format_get_string (GstAmcFormat * format, const gchar * key,
    gchar ** value, GError ** err)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;
  jstring v_str = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  *value = 0;
  env = gst_amc_jni_get_env ();

  key_str = gst_amc_jni_string_from_gchar (env, err, FALSE, key);
  if (!key_str)
    goto done;

  if (!gst_amc_jni_call_object_method (env, err, format->object,
          media_format.get_string, &v_str, key_str))
    goto done;

  *value = gst_amc_jni_string_to_gchar (env, v_str, TRUE);

  ret = TRUE;

done:
  if (key_str)
    gst_amc_jni_object_local_unref (env, key_str);

  return ret;
}

gboolean
gst_amc_format_set_string (GstAmcFormat * format, const gchar * key,
    const gchar * value, GError ** err)
{
  JNIEnv *env;
  jstring key_str = NULL;
  jstring v_str = NULL;
  gboolean ret = FALSE;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  key_str = gst_amc_jni_string_from_gchar (env, err, FALSE, key);
  if (!key_str)
    goto done;

  v_str = gst_amc_jni_string_from_gchar (env, err, FALSE, value);
  if (!v_str)
    goto done;

  if (!gst_amc_jni_call_void_method (env, err, format->object,
          media_format.set_string, key_str, v_str))
    goto done;

  ret = TRUE;

done:
  if (key_str)
    gst_amc_jni_object_local_unref (env, key_str);
  if (v_str)
    gst_amc_jni_object_local_unref (env, v_str);

  return ret;
}

gboolean
gst_amc_format_get_buffer (GstAmcFormat * format, const gchar * key,
    guint8 ** data, gsize * size, GError ** err)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;
  jobject v = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size != NULL, FALSE);

  *data = NULL;
  *size = 0;
  env = gst_amc_jni_get_env ();

  key_str = gst_amc_jni_string_from_gchar (env, err, FALSE, key);
  if (!key_str)
    goto done;

  if (!gst_amc_jni_call_object_method (env, err, format->object,
          media_format.get_byte_buffer, &v, key_str))
    goto done;

  *data = (*env)->GetDirectBufferAddress (env, v);
  if (*data == NULL) {
    gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
        GST_LIBRARY_ERROR_FAILED, "Failed get buffer address");
    goto done;
  }
  *size = (*env)->GetDirectBufferCapacity (env, v);
  *data = g_memdup (*data, *size);

  ret = TRUE;

done:
  if (key_str)
    gst_amc_jni_object_local_unref (env, key_str);
  if (v)
    gst_amc_jni_object_local_unref (env, v);

  return ret;
}

gboolean
gst_amc_format_set_buffer (GstAmcFormat * format, const gchar * key,
    guint8 * data, gsize size, GError ** err)
{
  JNIEnv *env;
  jstring key_str = NULL;
  jobject v = NULL;
  gboolean ret = FALSE;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  key_str = gst_amc_jni_string_from_gchar (env, err, FALSE, key);
  if (!key_str)
    goto done;

  /* FIXME: The memory must remain valid until the codec is stopped */
  v = (*env)->NewDirectByteBuffer (env, data, size);
  if (!v) {
    gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
        GST_LIBRARY_ERROR_FAILED, "Failed create Java byte buffer");
    goto done;
  }

  if (!gst_amc_jni_call_void_method (env, err, format->object,
          media_format.set_byte_buffer, key_str, v))
    goto done;

  ret = TRUE;

done:
  if (key_str)
    gst_amc_jni_object_local_unref (env, key_str);
  if (v)
    gst_amc_jni_object_local_unref (env, v);

  return ret;
}

gboolean
gst_amc_codeclist_get_count (gint * count, GError ** err)
{
  JNIEnv *env;

  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_call_static_int_method (env, err, media_codeclist.klass,
          media_codeclist.get_codec_count, count))
    return FALSE;

  return TRUE;
}

GstAmcCodecInfoHandle *
gst_amc_codeclist_get_codec_info_at (gint index, GError ** err)
{
  GstAmcCodecInfoHandle *ret;
  jobject object;
  JNIEnv *env;

  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_call_static_object_method (env, err, media_codeclist.klass,
          media_codeclist.get_codec_info_at, &object, index))
    return NULL;

  ret = g_new0 (GstAmcCodecInfoHandle, 1);
  ret->object = object;
  return ret;
}

GstCaps *
gst_amc_codeclist_to_caps (GstAmcCodecForeachFunc func)
{
  GstCaps *caps = gst_caps_new_empty ();
  GError *error = NULL;
  gint codec_count, i;

  if (!gst_amc_codeclist_get_count (&codec_count, &error)) {
    GST_ERROR ("Failed to get number of available codecs");
    goto done;
  }

  for (i = 0; i < codec_count; i++) {
    GstAmcCodecInfoHandle *codec_info = NULL;
    gchar *name_str = NULL;
    gboolean is_encoder;
    gchar **supported_types = NULL;
    gsize n_supported_types;
    gsize j;

    codec_info = gst_amc_codeclist_get_codec_info_at (i, &error);
    if (!codec_info) {
      GST_ERROR ("Failed to get codec info %d", i);
      goto next_codec;
    }

    name_str = gst_amc_codec_info_handle_get_name (codec_info, &error);
    if (!name_str) {
      GST_ERROR ("Failed to get codec name");
      goto next_codec;
    }

    GST_INFO ("Checking codec '%s'", name_str);

    /* Compatibility codec names */
    if (strcmp (name_str, "AACEncoder") == 0 ||
      strcmp (name_str, "OMX.google.raw.decoder") == 0) {
      GST_INFO ("Skipping compatibility codec '%s'", name_str);
      goto next_codec;
    }

    if (g_str_has_suffix (name_str, ".secure")) {
      GST_INFO ("Skipping DRM codec '%s'", name_str);
      goto next_codec;
    }

    /* FIXME: Non-Google codecs usually just don't work and hang forever
     * or crash when not used from a process that started the Java
     * VM via the non-public AndroidRuntime class. Can we somehow
     * initialize all this?
     */
    if (gst_amc_jni_is_vm_started () &&
      !g_str_has_prefix (name_str, "OMX.google.")) {
      GST_INFO ("Skipping non-Google codec '%s' in standalone mode", name_str);
      goto next_codec;
    }

    if (g_str_has_prefix (name_str, "OMX.ARICENT.")) {
      GST_INFO ("Skipping possible broken codec '%s'", name_str);
      goto next_codec;
    }

    if (!gst_amc_codec_info_handle_is_encoder (codec_info, &is_encoder, &error)) {
      GST_ERROR ("Failed to detect if codec is an encoder");
      goto next_codec;
    }

    if (is_encoder) {
      /* Skip encoder */
      goto next_codec;
    }

    supported_types = gst_amc_codec_info_handle_get_supported_types (codec_info,
                &n_supported_types, &error);
    if (!supported_types) {
      GST_ERROR ("Failed to get supported types");
      goto next_codec;
    }

    GST_INFO ("Codec '%s' has %" G_GSIZE_FORMAT " supported types", name_str,
                n_supported_types);

    if (n_supported_types == 0) {
      GST_ERROR ("Codec has no supported types");
      goto next_codec;
    }

    for (j = 0; j < n_supported_types; j++) {
      const gchar *mime;
      GstAmcCodecCapabilitiesHandle *capabilities = NULL;
      GstAmcCodecProfileLevel *profile_levels;
      gsize n_profile_levels;
      gint k;

      mime = supported_types[j];
      GST_INFO ("Supported type '%s'", mime);

      capabilities =
          gst_amc_codec_info_handle_get_capabilities_for_type (codec_info, mime, &error);
      if (!capabilities) {
          GST_ERROR ("Failed to get capabilities for supported type");
          goto next_supported_type;
      }

      profile_levels =
          gst_amc_codec_capabilities_handle_get_profile_levels (capabilities,
                      &n_profile_levels, &error);
      if (error) {
          GST_ERROR ("Failed to get profile/levels: %s", error->message);
          goto next_supported_type;
      }

      func (caps, mime, profile_levels, n_profile_levels);

next_supported_type:
      if (capabilities)
          gst_amc_codec_capabilities_handle_free (capabilities);
      capabilities = NULL;
      g_clear_error (&error);
    }

    /* Clean up of all local references we got */
next_codec:
    if (name_str)
      g_free (name_str);
    name_str = NULL;
    if (supported_types)
      g_strfreev (supported_types);
    supported_types = NULL;
    if (codec_info)
      gst_amc_codec_info_handle_free (codec_info);
    codec_info = NULL;
    g_clear_error (&error);
  }

done:
  return caps;
}

void
gst_amc_codec_info_handle_free (GstAmcCodecInfoHandle * handle)
{
  JNIEnv *env;

  g_return_if_fail (handle != NULL);

  env = gst_amc_jni_get_env ();

  if (handle->object)
    gst_amc_jni_object_local_unref (env, handle->object);
  g_free (handle);
}

gchar *
gst_amc_codec_info_handle_get_name (GstAmcCodecInfoHandle * handle,
    GError ** err)
{
  JNIEnv *env;
  jstring v_str = NULL;

  g_return_val_if_fail (handle != NULL, NULL);

  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_call_object_method (env, err, handle->object,
          media_codecinfo.get_name, &v_str))
    return NULL;

  return gst_amc_jni_string_to_gchar (env, v_str, TRUE);
}

gboolean
gst_amc_codec_info_handle_is_encoder (GstAmcCodecInfoHandle * handle,
    gboolean * is_encoder, GError ** err)
{
  JNIEnv *env;

  g_return_val_if_fail (handle != NULL, FALSE);
  g_return_val_if_fail (is_encoder != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_call_boolean_method (env, err, handle->object,
          media_codecinfo.is_encoder, is_encoder))
    return FALSE;

  return TRUE;
}

gchar **
gst_amc_codec_info_handle_get_supported_types (GstAmcCodecInfoHandle * handle,
    gsize * length, GError ** err)
{
  JNIEnv *env;
  jarray array = NULL;
  jsize len;
  jsize i;
  gchar **strv = NULL;

  g_return_val_if_fail (handle != NULL, NULL);

  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_call_object_method (env, err, handle->object,
          media_codecinfo.get_supported_types, &array))
    goto done;

  len = (*env)->GetArrayLength (env, array);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
        GST_LIBRARY_ERROR_FAILED, "Failed to get array length");
    goto done;
  }

  strv = g_new0 (gchar *, len + 1);
  *length = len;

  for (i = 0; i < len; i++) {
    jstring string;

    string = (*env)->GetObjectArrayElement (env, array, i);
    if ((*env)->ExceptionCheck (env)) {
      gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
          GST_LIBRARY_ERROR_FAILED, "Failed to get array element");
      g_strfreev (strv);
      strv = NULL;
      goto done;
    }

    strv[i] = gst_amc_jni_string_to_gchar (env, string, TRUE);
    if (!strv[i]) {
      gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
          GST_LIBRARY_ERROR_FAILED, "Failed create string");
      g_strfreev (strv);
      strv = NULL;
      goto done;
    }
  }

done:
  if (array)
    (*env)->DeleteLocalRef (env, array);

  return strv;
}

GstAmcCodecCapabilitiesHandle *
gst_amc_codec_info_handle_get_capabilities_for_type (GstAmcCodecInfoHandle *
    handle, const gchar * type, GError ** err)
{
  GstAmcCodecCapabilitiesHandle *ret = NULL;
  jstring type_str;
  jobject object;
  JNIEnv *env;

  env = gst_amc_jni_get_env ();

  type_str = gst_amc_jni_string_from_gchar (env, err, FALSE, type);
  if (!type_str)
    goto done;

  if (!gst_amc_jni_call_object_method (env, err, handle->object,
          media_codecinfo.get_capabilities_for_type, &object, type_str))
    goto done;

  ret = g_new0 (GstAmcCodecCapabilitiesHandle, 1);
  ret->object = object;

done:
  if (type_str)
    gst_amc_jni_object_local_unref (env, type_str);

  return ret;
}

void
gst_amc_codec_capabilities_handle_free (GstAmcCodecCapabilitiesHandle * handle)
{
  JNIEnv *env;

  g_return_if_fail (handle != NULL);

  env = gst_amc_jni_get_env ();

  if (handle->object)
    gst_amc_jni_object_local_unref (env, handle->object);
  g_free (handle);
}

GstAmcCodecProfileLevel *
gst_amc_codec_capabilities_handle_get_profile_levels
    (GstAmcCodecCapabilitiesHandle * handle, gsize * length, GError ** err)
{
  GstAmcCodecProfileLevel *ret = NULL;
  JNIEnv *env;
  jobject array = NULL;
  jsize len;
  jsize i;

  env = gst_amc_jni_get_env ();

  if (!gst_amc_jni_get_object_field (env, err, handle->object,
          media_codeccapabilities.profile_levels, &array))
    goto done;

  len = (*env)->GetArrayLength (env, array);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
        GST_LIBRARY_ERROR_FAILED, "Failed to get array length");
    goto done;
  }

  ret = g_new0 (GstAmcCodecProfileLevel, len);
  *length = len;

  for (i = 0; i < len; i++) {
    jobject object = NULL;

    object = (*env)->GetObjectArrayElement (env, array, i);
    if ((*env)->ExceptionCheck (env)) {
      gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
          GST_LIBRARY_ERROR_FAILED, "Failed to get array element");
      g_free (ret);
      ret = NULL;
      goto done;
    }

    if (!gst_amc_jni_get_int_field (env, err, object,
            media_codecprofilelevel.level, &ret[i].level)) {
      gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
          GST_LIBRARY_ERROR_FAILED, "Failed to get level");
      (*env)->DeleteLocalRef (env, object);
      g_free (ret);
      ret = NULL;
      goto done;
    }

    if (!gst_amc_jni_get_int_field (env, err, object,
            media_codecprofilelevel.profile, &ret[i].profile)) {
      gst_amc_jni_set_error (env, err, GST_LIBRARY_ERROR,
          GST_LIBRARY_ERROR_FAILED, "Failed to get profile");
      (*env)->DeleteLocalRef (env, object);
      g_free (ret);
      ret = NULL;
      goto done;
    }

    (*env)->DeleteLocalRef (env, object);
  }

done:
  if (array)
    (*env)->DeleteLocalRef (env, array);

  return ret;
}

static gboolean
gst_amc_codec_static_init (void)
{
  gboolean ret = TRUE;
  JNIEnv *env;
  jclass tmp;

  env = gst_amc_jni_get_env ();

  tmp = (*env)->FindClass (env, "java/lang/System");
  if (!tmp) {
    ret = FALSE;
    GST_ERROR ("Failed to get system class");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }
  system.klass = (*env)->NewGlobalRef (env, tmp);
  if (!system.klass) {
    ret = FALSE;
    GST_ERROR ("Failed to get system class global reference");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  system.nano_time = (*env)->GetStaticMethodID (env, system.klass, "nanoTime", "()J");

  tmp = (*env)->FindClass (env, "android/media/MediaCodec");
  if (!tmp) {
    ret = FALSE;
    GST_ERROR ("Failed to get codec class");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }
  media_codec.klass = (*env)->NewGlobalRef (env, tmp);
  if (!media_codec.klass) {
    ret = FALSE;
    GST_ERROR ("Failed to get codec class global reference");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  media_codec.create_by_codec_name =
      (*env)->GetStaticMethodID (env, media_codec.klass, "createByCodecName",
      "(Ljava/lang/String;)Landroid/media/MediaCodec;");
  media_codec.create_decoder_by_type =
      (*env)->GetStaticMethodID (env, media_codec.klass, "createDecoderByType",
      "(Ljava/lang/String;)Landroid/media/MediaCodec;");
  media_codec.create_encoder_by_type =
      (*env)->GetStaticMethodID (env, media_codec.klass, "createEncoderByType",
      "(Ljava/lang/String;)Landroid/media/MediaCodec;");
  media_codec.configure =
      (*env)->GetMethodID (env, media_codec.klass, "configure",
      "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V");
  media_codec.dequeue_input_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "dequeueInputBuffer",
      "(J)I");
  media_codec.dequeue_output_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "dequeueOutputBuffer",
      "(Landroid/media/MediaCodec$BufferInfo;J)I");
  media_codec.flush =
      (*env)->GetMethodID (env, media_codec.klass, "flush", "()V");
  media_codec.get_input_buffers =
      (*env)->GetMethodID (env, media_codec.klass, "getInputBuffers",
      "()[Ljava/nio/ByteBuffer;");
  media_codec.get_output_buffers =
      (*env)->GetMethodID (env, media_codec.klass, "getOutputBuffers",
      "()[Ljava/nio/ByteBuffer;");
  media_codec.get_output_format =
      (*env)->GetMethodID (env, media_codec.klass, "getOutputFormat",
      "()Landroid/media/MediaFormat;");
  media_codec.queue_input_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "queueInputBuffer",
      "(IIIJI)V");
  media_codec.release =
      (*env)->GetMethodID (env, media_codec.klass, "release", "()V");
  media_codec.release_output_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "releaseOutputBuffer",
      "(IZ)V");
  media_codec.release_output_buffer_time =
      (*env)->GetMethodID (env, media_codec.klass, "releaseOutputBuffer",
      "(IJ)V");
  media_codec.start =
      (*env)->GetMethodID (env, media_codec.klass, "start", "()V");
  media_codec.stop =
      (*env)->GetMethodID (env, media_codec.klass, "stop", "()V");

  if (!media_codec.configure ||
      !media_codec.create_by_codec_name ||
      !media_codec.create_decoder_by_type ||
      !media_codec.create_encoder_by_type ||
      !media_codec.dequeue_input_buffer ||
      !media_codec.dequeue_output_buffer ||
      !media_codec.flush ||
      !media_codec.get_input_buffers ||
      !media_codec.get_output_buffers ||
      !media_codec.get_output_format ||
      !media_codec.queue_input_buffer ||
      !media_codec.release ||
      !media_codec.release_output_buffer ||
      !media_codec.release_output_buffer_time ||
      !media_codec.start || !media_codec.stop) {
    ret = FALSE;
    GST_ERROR ("Failed to get codec methods");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }

  tmp = (*env)->FindClass (env, "android/media/MediaCodec$BufferInfo");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec buffer info class");
    goto done;
  }
  media_codec_buffer_info.klass = (*env)->NewGlobalRef (env, tmp);
  if (!media_codec_buffer_info.klass) {
    ret = FALSE;
    GST_ERROR ("Failed to get codec buffer info class global reference");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  media_codec_buffer_info.constructor =
      (*env)->GetMethodID (env, media_codec_buffer_info.klass, "<init>", "()V");
  media_codec_buffer_info.flags =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "flags", "I");
  media_codec_buffer_info.offset =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "offset", "I");
  media_codec_buffer_info.presentation_time_us =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass,
      "presentationTimeUs", "J");
  media_codec_buffer_info.size =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "size", "I");
  if (!media_codec_buffer_info.constructor || !media_codec_buffer_info.flags
      || !media_codec_buffer_info.offset
      || !media_codec_buffer_info.presentation_time_us
      || !media_codec_buffer_info.size) {
    ret = FALSE;
    GST_ERROR ("Failed to get buffer info methods and fields");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }

done:
  if (tmp)
    (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  return ret;
}

static gboolean
gst_amc_format_static_init (void)
{
  gboolean ret = TRUE;
  JNIEnv *env;
  jclass tmp;

  env = gst_amc_jni_get_env ();

  tmp = (*env)->FindClass (env, "android/media/MediaFormat");
  if (!tmp) {
    ret = FALSE;
    GST_ERROR ("Failed to get format class");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }
  media_format.klass = (*env)->NewGlobalRef (env, tmp);
  if (!media_format.klass) {
    ret = FALSE;
    GST_ERROR ("Failed to get format class global reference");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  media_format.create_audio_format =
      (*env)->GetStaticMethodID (env, media_format.klass, "createAudioFormat",
      "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
  media_format.create_video_format =
      (*env)->GetStaticMethodID (env, media_format.klass, "createVideoFormat",
      "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
  media_format.to_string =
      (*env)->GetMethodID (env, media_format.klass, "toString",
      "()Ljava/lang/String;");
  media_format.contains_key =
      (*env)->GetMethodID (env, media_format.klass, "containsKey",
      "(Ljava/lang/String;)Z");
  media_format.get_float =
      (*env)->GetMethodID (env, media_format.klass, "getFloat",
      "(Ljava/lang/String;)F");
  media_format.set_float =
      (*env)->GetMethodID (env, media_format.klass, "setFloat",
      "(Ljava/lang/String;F)V");
  media_format.get_integer =
      (*env)->GetMethodID (env, media_format.klass, "getInteger",
      "(Ljava/lang/String;)I");
  media_format.set_integer =
      (*env)->GetMethodID (env, media_format.klass, "setInteger",
      "(Ljava/lang/String;I)V");
  media_format.get_string =
      (*env)->GetMethodID (env, media_format.klass, "getString",
      "(Ljava/lang/String;)Ljava/lang/String;");
  media_format.set_string =
      (*env)->GetMethodID (env, media_format.klass, "setString",
      "(Ljava/lang/String;Ljava/lang/String;)V");
  media_format.get_byte_buffer =
      (*env)->GetMethodID (env, media_format.klass, "getByteBuffer",
      "(Ljava/lang/String;)Ljava/nio/ByteBuffer;");
  media_format.set_byte_buffer =
      (*env)->GetMethodID (env, media_format.klass, "setByteBuffer",
      "(Ljava/lang/String;Ljava/nio/ByteBuffer;)V");
  if (!media_format.create_audio_format || !media_format.create_video_format
      || !media_format.contains_key || !media_format.get_float
      || !media_format.set_float || !media_format.get_integer
      || !media_format.set_integer || !media_format.get_string
      || !media_format.set_string || !media_format.get_byte_buffer
      || !media_format.set_byte_buffer) {
    ret = FALSE;
    GST_ERROR ("Failed to get format methods");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }

done:
  if (tmp)
    (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  return ret;
}

static gboolean
gst_amc_codeclist_static_init (void)
{
  JNIEnv *env;
  GError *err = NULL;

  env = gst_amc_jni_get_env ();

  media_codeclist.klass =
      gst_amc_jni_get_class (env, &err, "android/media/MediaCodecList");
  if (!media_codeclist.klass) {
    GST_ERROR ("Failed to get android.media.MediaCodecList class: %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codeclist.get_codec_count =
      gst_amc_jni_get_static_method_id (env, &err, media_codeclist.klass,
      "getCodecCount", "()I");
  if (!media_codeclist.get_codec_count) {
    GST_ERROR ("Failed to get android.media.MediaCodecList getCodecCount(): %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codeclist.get_codec_info_at =
      gst_amc_jni_get_static_method_id (env, &err, media_codeclist.klass,
      "getCodecInfoAt", "(I)Landroid/media/MediaCodecInfo;");
  if (!media_codeclist.get_codec_count) {
    GST_ERROR
        ("Failed to get android.media.MediaCodecList getCodecInfoAt(): %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codecinfo.klass =
      gst_amc_jni_get_class (env, &err, "android/media/MediaCodecInfo");
  if (!media_codecinfo.klass) {
    GST_ERROR ("Failed to get android.media.MediaCodecInfo class: %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codecinfo.get_capabilities_for_type =
      gst_amc_jni_get_method_id (env, &err, media_codecinfo.klass,
      "getCapabilitiesForType",
      "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");
  if (!media_codecinfo.get_capabilities_for_type) {
    GST_ERROR
        ("Failed to get android.media.MediaCodecInfo getCapabilitiesForType(): %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codecinfo.get_name =
      gst_amc_jni_get_method_id (env, &err, media_codecinfo.klass, "getName",
      "()Ljava/lang/String;");
  if (!media_codecinfo.get_name) {
    GST_ERROR ("Failed to get android.media.MediaCodecInfo getName(): %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codecinfo.get_supported_types =
      gst_amc_jni_get_method_id (env, &err, media_codecinfo.klass,
      "getSupportedTypes", "()[Ljava/lang/String;");
  if (!media_codecinfo.get_supported_types) {
    GST_ERROR
        ("Failed to get android.media.MediaCodecInfo getSupportedTypes(): %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codecinfo.is_encoder =
      gst_amc_jni_get_method_id (env, &err, media_codecinfo.klass, "isEncoder",
      "()Z");
  if (!media_codecinfo.is_encoder) {
    GST_ERROR ("Failed to get android.media.MediaCodecInfo isEncoder(): %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codeccapabilities.klass =
      gst_amc_jni_get_class (env, &err,
      "android/media/MediaCodecInfo$CodecCapabilities");
  if (!media_codeccapabilities.klass) {
    GST_ERROR
        ("Failed to get android.media.MediaCodecInfo.CodecCapabilities class: %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codeccapabilities.profile_levels =
      gst_amc_jni_get_field_id (env, &err, media_codeccapabilities.klass,
      "profileLevels", "[Landroid/media/MediaCodecInfo$CodecProfileLevel;");
  if (!media_codeccapabilities.profile_levels) {
    GST_ERROR
        ("Failed to get android.media.MediaCodecInfo.CodecCapabilities profileLevels: %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codecprofilelevel.klass =
      gst_amc_jni_get_class (env, &err,
      "android/media/MediaCodecInfo$CodecProfileLevel");
  if (!media_codecprofilelevel.klass) {
    GST_ERROR
        ("Failed to get android.media.MediaCodecInfo.CodecProfileLevel class: %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codecprofilelevel.level =
      gst_amc_jni_get_field_id (env, &err, media_codecprofilelevel.klass,
      "level", "I");
  if (!media_codecprofilelevel.level) {
    GST_ERROR
        ("Failed to get android.media.MediaCodecInfo.CodecProfileLevel level: %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  media_codecprofilelevel.profile =
      gst_amc_jni_get_field_id (env, &err, media_codecprofilelevel.klass,
      "profile", "I");
  if (!media_codecprofilelevel.profile) {
    GST_ERROR
        ("Failed to get android.media.MediaCodecInfo.CodecProfileLevel profile: %s",
        err->message);
    g_clear_error (&err);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_amc_init (void)
{
  if (!gst_amc_jni_initialize ())
    return FALSE;

  if (!gst_amc_codec_static_init ())
    return FALSE;

  if (!gst_amc_format_static_init ())
    return FALSE;

  if (!gst_amc_codeclist_static_init ())
    return FALSE;

  return TRUE;
}

/* vim:set tabstop=4 softtabstop=4 shiftwidth=4 expandtab: */

