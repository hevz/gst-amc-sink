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

#include "gst-amc.h"
#include "gst-jni-utils.h"

GST_DEBUG_CATEGORY (gst_amc_debug);
#define GST_CAT_DEFAULT gst_amc_debug

/* Global cached references */
static struct
{
  jclass klass;
  jmethodID constructor;
} java_string;
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

GstAmcCodec *
gst_amc_codec_new (const gchar * name, GError ** err)
{
  JNIEnv *env;
  GstAmcCodec *codec = NULL;
  jstring name_str;
  jobject object = NULL;

  g_return_val_if_fail (name != NULL, NULL);

  env = gst_amc_jni_get_env ();

  name_str = (*env)->NewStringUTF (env, name);
  if (name_str == NULL) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create Java String");
    goto error;
  }

  codec = g_slice_new0 (GstAmcCodec);

  object =
      (*env)->CallStaticObjectMethod (env, media_codec.klass,
      media_codec.create_by_codec_name, name_str);
  if ((*env)->ExceptionCheck (env) || !object) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create codec '%s'", name);
    goto error;
  }

  codec->object = (*env)->NewGlobalRef (env, object);
  if (!codec->object) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create global codec reference");
    goto error;
  }

done:
  if (object)
    (*env)->DeleteLocalRef (env, object);
  if (name_str)
    (*env)->DeleteLocalRef (env, name_str);
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
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create Java String");
    goto error;
  }

  codec = g_slice_new0 (GstAmcCodec);

  object =
      (*env)->CallStaticObjectMethod (env, media_codec.klass, method_id, type_str);
  if ((*env)->ExceptionCheck (env) || !object) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create decoder by type '%s'", type);
    goto error;
  }

  codec->object = (*env)->NewGlobalRef (env, object);
  if (!codec->object) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
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
  (*env)->DeleteGlobalRef (env, codec->object);
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
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.configure,
      format->object, surface, NULL, flags);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_SETTINGS,
        err, "Failed to configure codec");
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

GstAmcFormat *
gst_amc_codec_get_output_format (GstAmcCodec * codec, GError ** err)
{
  JNIEnv *env;
  GstAmcFormat *ret = NULL;
  jobject object = NULL;

  g_return_val_if_fail (codec != NULL, NULL);

  env = gst_amc_jni_get_env ();

  object =
      (*env)->CallObjectMethod (env, codec->object,
      media_codec.get_output_format);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_SETTINGS,
        err, "Failed to get output format");
    goto done;
  }

  ret = g_slice_new0 (GstAmcFormat);

  ret->object = (*env)->NewGlobalRef (env, object);
  if (!ret->object) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_SETTINGS,
        err, "Failed to create global format reference");
    g_slice_free (GstAmcFormat, ret);
    ret = NULL;
  }

  (*env)->DeleteLocalRef (env, object);

done:

  return ret;
}

gboolean
gst_amc_codec_start (GstAmcCodec * codec, GError ** err)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.start);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to start codec");
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

gboolean
gst_amc_codec_stop (GstAmcCodec * codec, GError ** err)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.stop);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to stop codec");
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

gboolean
gst_amc_codec_flush (GstAmcCodec * codec, GError ** err)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.flush);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to flush codec");
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

gboolean
gst_amc_codec_release (GstAmcCodec * codec, GError ** err)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.release);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to release codec");
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

void
gst_amc_codec_free_buffers (GstAmcBuffer * buffers, gsize n_buffers)
{
  JNIEnv *env;
  jsize i;

  g_return_if_fail (buffers != NULL);

  env = gst_amc_jni_get_env ();

  for (i = 0; i < n_buffers; i++) {
    if (buffers[i].object)
      (*env)->DeleteGlobalRef (env, buffers[i].object);
  }
  g_free (buffers);
}

GstAmcBuffer *
gst_amc_codec_get_output_buffers (GstAmcCodec * codec, gsize * n_buffers,
    GError ** err)
{
  JNIEnv *env;
  jobject output_buffers = NULL;
  jsize n_output_buffers;
  GstAmcBuffer *ret = NULL;
  jsize i;

  g_return_val_if_fail (codec != NULL, NULL);
  g_return_val_if_fail (n_buffers != NULL, NULL);

  *n_buffers = 0;
  env = gst_amc_jni_get_env ();

  output_buffers =
      (*env)->CallObjectMethod (env, codec->object,
      media_codec.get_output_buffers);
  if ((*env)->ExceptionCheck (env) || !output_buffers) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to get output buffers");
    goto done;
  }

  n_output_buffers = (*env)->GetArrayLength (env, output_buffers);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to get output buffers array length");
    goto done;
  }

  *n_buffers = n_output_buffers;
  ret = g_new0 (GstAmcBuffer, n_output_buffers);

  for (i = 0; i < n_output_buffers; i++) {
    jobject buffer = NULL;

    buffer = (*env)->GetObjectArrayElement (env, output_buffers, i);
    if ((*env)->ExceptionCheck (env) || !buffer) {
      gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
          err, "Failed to get output buffer %d", i);
      goto error;
    }

    ret[i].object = (*env)->NewGlobalRef (env, buffer);
    (*env)->DeleteLocalRef (env, buffer);
    if (!ret[i].object) {
      gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
          err, "Failed to create global output buffer reference %d", i);
      goto error;
    }

    ret[i].data = (*env)->GetDirectBufferAddress (env, ret[i].object);
    if (!ret[i].data) {
      gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
          err, "Failed to get output buffer address %d", i);
      goto error;
    }
    ret[i].size = (*env)->GetDirectBufferCapacity (env, ret[i].object);
  }

done:
  if (output_buffers)
    (*env)->DeleteLocalRef (env, output_buffers);
  output_buffers = NULL;

  return ret;
error:
  if (ret)
    gst_amc_codec_free_buffers (ret, n_output_buffers);
  ret = NULL;
  *n_buffers = 0;
  goto done;
}

GstAmcBuffer *
gst_amc_codec_get_input_buffers (GstAmcCodec * codec, gsize * n_buffers,
    GError ** err)
{
  JNIEnv *env;
  jobject input_buffers = NULL;
  jsize n_input_buffers;
  GstAmcBuffer *ret = NULL;
  jsize i;

  g_return_val_if_fail (codec != NULL, NULL);
  g_return_val_if_fail (n_buffers != NULL, NULL);

  *n_buffers = 0;
  env = gst_amc_jni_get_env ();

  input_buffers =
      (*env)->CallObjectMethod (env, codec->object,
      media_codec.get_input_buffers);
  if ((*env)->ExceptionCheck (env) || !input_buffers) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to get input buffers");
    goto done;
  }

  n_input_buffers = (*env)->GetArrayLength (env, input_buffers);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to get input buffers array length");
    goto done;
  }

  *n_buffers = n_input_buffers;
  ret = g_new0 (GstAmcBuffer, n_input_buffers);

  for (i = 0; i < n_input_buffers; i++) {
    jobject buffer = NULL;

    buffer = (*env)->GetObjectArrayElement (env, input_buffers, i);
    if ((*env)->ExceptionCheck (env) || !buffer) {
      gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
          err, "Failed to get input buffer %d", i);
      goto error;
    }

    ret[i].object = (*env)->NewGlobalRef (env, buffer);
    (*env)->DeleteLocalRef (env, buffer);
    if (!ret[i].object) {
      gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
          err, "Failed to create global input buffer reference %d", i);
      goto error;
    }

    ret[i].data = (*env)->GetDirectBufferAddress (env, ret[i].object);
    if (!ret[i].data) {
      gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
          err, "Failed to get input buffer address %d", i);
      goto error;
    }
    ret[i].size = (*env)->GetDirectBufferCapacity (env, ret[i].object);
  }

done:
  if (input_buffers)
    (*env)->DeleteLocalRef (env, input_buffers);
  input_buffers = NULL;

  return ret;
error:
  if (ret)
    gst_amc_codec_free_buffers (ret, n_input_buffers);
  ret = NULL;
  *n_buffers = 0;
  goto done;
}

gint
gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs,
    GError ** err)
{
  JNIEnv *env;
  gint ret = G_MININT;

  g_return_val_if_fail (codec != NULL, G_MININT);

  env = gst_amc_jni_get_env ();

  ret =
      (*env)->CallIntMethod (env, codec->object,
      media_codec.dequeue_input_buffer, timeoutUs);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to dequeue input buffer");
    ret = G_MININT;
    goto done;
  }

done:

  return ret;
}

static gboolean
gst_amc_codec_fill_buffer_info (JNIEnv * env, jobject buffer_info,
    GstAmcBufferInfo * info, GError ** err)
{
  g_return_val_if_fail (buffer_info != NULL, FALSE);

  info->flags =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.flags);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to get buffer info flags");
    return FALSE;
  }

  info->offset =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.offset);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to get buffer info offset");
    return FALSE;
  }

  info->presentation_time_us =
      (*env)->GetLongField (env, buffer_info,
      media_codec_buffer_info.presentation_time_us);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to get buffer info pts");
    return FALSE;
  }

  info->size =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.size);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to get buffer info size");
    return FALSE;
  }

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
      (*env)->NewObject (env, media_codec_buffer_info.klass,
      media_codec_buffer_info.constructor);
  if (!info_o) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create buffer info instance");
    goto done;
  }

  ret =
      (*env)->CallIntMethod (env, codec->object,
      media_codec.dequeue_output_buffer, info_o, timeoutUs);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to dequeue output buffer");
    ret = G_MININT;
    goto done;
  }

  if (!gst_amc_codec_fill_buffer_info (env, info_o, info, err)) {
    ret = G_MININT;
    goto done;
  }

done:
  if (info_o)
    (*env)->DeleteLocalRef (env, info_o);
  info_o = NULL;

  return ret;
}

gboolean
gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info, GError ** err)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.queue_input_buffer,
      index, info->offset, info->size, info->presentation_time_us, info->flags);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to queue input buffer");
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

gboolean
gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index,
    GError ** err)
{
  JNIEnv *env;
  gboolean ret = TRUE;

  g_return_val_if_fail (codec != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  (*env)->CallVoidMethod (env, codec->object, media_codec.release_output_buffer,
      index, JNI_TRUE);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to release output buffer");
    ret = FALSE;
    goto done;
  }

done:

  return ret;
}

GstAmcFormat *
gst_amc_format_new_audio (const gchar * mime, gint sample_rate, gint channels,
    GError ** err)
{
  JNIEnv *env;
  GstAmcFormat *format = NULL;
  jstring mime_str;
  jobject object = NULL;

  g_return_val_if_fail (mime != NULL, NULL);

  env = gst_amc_jni_get_env ();

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create Java string");
    goto error;
  }

  format = g_slice_new0 (GstAmcFormat);

  object =
      (*env)->CallStaticObjectMethod (env, media_format.klass,
      media_format.create_audio_format, mime_str, sample_rate, channels);
  if ((*env)->ExceptionCheck (env) || !object) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create format instance '%s'", mime);
    goto error;
  }

  format->object = (*env)->NewGlobalRef (env, object);
  if (!format->object) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create global format reference");
    goto error;
  }

done:
  if (object)
    (*env)->DeleteLocalRef (env, object);
  if (mime_str)
    (*env)->DeleteLocalRef (env, mime_str);
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
  jobject object = NULL;

  g_return_val_if_fail (mime != NULL, NULL);

  env = gst_amc_jni_get_env ();

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create Java string");
    goto error;
  }

  format = g_slice_new0 (GstAmcFormat);

  object =
      (*env)->CallStaticObjectMethod (env, media_format.klass,
      media_format.create_video_format, mime_str, width, height);
  if ((*env)->ExceptionCheck (env) || !object) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create format instance '%s'", mime);
    goto error;
  }

  format->object = (*env)->NewGlobalRef (env, object);
  if (!format->object) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT, err,
        "Failed to create global format reference");
    goto error;
  }

done:
  if (object)
    (*env)->DeleteLocalRef (env, object);
  if (mime_str)
    (*env)->DeleteLocalRef (env, mime_str);
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
  (*env)->DeleteGlobalRef (env, format->object);
  g_slice_free (GstAmcFormat, format);
}

gchar *
gst_amc_format_to_string (GstAmcFormat * format, GError ** err)
{
  JNIEnv *env;
  jstring v_str = NULL;
  const gchar *v = NULL;
  gchar *ret = NULL;

  g_return_val_if_fail (format != NULL, FALSE);

  env = gst_amc_jni_get_env ();

  v_str =
      (*env)->CallObjectMethod (env, format->object, media_format.to_string);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to convert format to string");
    goto done;
  }

  v = (*env)->GetStringUTFChars (env, v_str, NULL);
  if (!v) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to get UTF8 string");
    goto done;
  }

  ret = g_strdup (v);

done:
  if (v)
    (*env)->ReleaseStringUTFChars (env, v_str, v);
  if (v_str)
    (*env)->DeleteLocalRef (env, v_str);

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

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create Java string");
    goto done;
  }

  ret =
      (*env)->CallBooleanMethod (env, format->object, media_format.contains_key,
      key_str);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to check if format contains key '%s'", key);
    goto done;
  }

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

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

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create Java string");
    goto done;
  }

  *value =
      (*env)->CallFloatMethod (env, format->object, media_format.get_float,
      key_str);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed get float key '%s'", key);
    goto done;
  }
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;
}

void
gst_amc_format_set_float (GstAmcFormat * format, const gchar * key,
    gfloat value, GError ** err)
{
  JNIEnv *env;
  jstring key_str = NULL;

  g_return_if_fail (format != NULL);
  g_return_if_fail (key != NULL);

  env = gst_amc_jni_get_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create Java string");
    goto done;
  }

  (*env)->CallVoidMethod (env, format->object, media_format.set_float, key_str,
      value);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed set float key '%s'", key);
    goto done;
  }

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
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

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create Java string");
    goto done;
  }

  *value =
      (*env)->CallIntMethod (env, format->object, media_format.get_integer,
      key_str);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed get integer key '%s'", key);
    goto done;
  }
  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);

  return ret;

}

void
gst_amc_format_set_int (GstAmcFormat * format, const gchar * key, gint value,
    GError ** err)
{
  JNIEnv *env;
  jstring key_str = NULL;

  g_return_if_fail (format != NULL);
  g_return_if_fail (key != NULL);

  env = gst_amc_jni_get_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create Java string");
    goto done;
  }

  (*env)->CallVoidMethod (env, format->object, media_format.set_integer,
      key_str, value);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed set integer key '%s'", key);
    goto done;
  }

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
}

gboolean
gst_amc_format_get_string (GstAmcFormat * format, const gchar * key,
    gchar ** value, GError ** err)
{
  JNIEnv *env;
  gboolean ret = FALSE;
  jstring key_str = NULL;
  jstring v_str = NULL;
  const gchar *v = NULL;

  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  *value = 0;
  env = gst_amc_jni_get_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create Java string");
    goto done;
  }

  v_str =
      (*env)->CallObjectMethod (env, format->object, media_format.get_string,
      key_str);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed get string key '%s'", key);
    goto done;
  }

  v = (*env)->GetStringUTFChars (env, v_str, NULL);
  if (!v) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed get string UTF8 characters");
    goto done;
  }

  *value = g_strdup (v);

  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v)
    (*env)->ReleaseStringUTFChars (env, v_str, v);
  if (v_str)
    (*env)->DeleteLocalRef (env, v_str);

  return ret;
}

void
gst_amc_format_set_string (GstAmcFormat * format, const gchar * key,
    const gchar * value, GError ** err)
{
  JNIEnv *env;
  jstring key_str = NULL;
  jstring v_str = NULL;

  g_return_if_fail (format != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (value != NULL);

  env = gst_amc_jni_get_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create Java string");
    goto done;
  }

  v_str = (*env)->NewStringUTF (env, value);
  if (!v_str) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create Java string");
    goto done;
  }

  (*env)->CallVoidMethod (env, format->object, media_format.set_string, key_str,
      v_str);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed set string key '%s'", key);
    goto done;
  }

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v_str)
    (*env)->DeleteLocalRef (env, v_str);
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

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create Java string");
    goto done;
  }

  v = (*env)->CallObjectMethod (env, format->object,
      media_format.get_byte_buffer, key_str);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed get buffer key '%s'", key);
    goto done;
  }

  *data = (*env)->GetDirectBufferAddress (env, v);
  if (!data) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed get buffer address");
    goto done;
  }
  *size = (*env)->GetDirectBufferCapacity (env, v);
  *data = g_memdup (*data, *size);

  ret = TRUE;

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v)
    (*env)->DeleteLocalRef (env, v);

  return ret;
}

void
gst_amc_format_set_buffer (GstAmcFormat * format, const gchar * key,
    guint8 * data, gsize size, GError ** err)
{
  JNIEnv *env;
  jstring key_str = NULL;
  jobject v = NULL;

  g_return_if_fail (format != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (data != NULL);

  env = gst_amc_jni_get_env ();

  key_str = (*env)->NewStringUTF (env, key);
  if (!key_str) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed to create Java string");
    goto done;
  }

  /* FIXME: The memory must remain valid until the codec is stopped */
  v = (*env)->NewDirectByteBuffer (env, data, size);
  if (!v) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed create Java byte buffer");
    goto done;
  }

  (*env)->CallVoidMethod (env, format->object, media_format.set_byte_buffer,
      key_str, v);
  if ((*env)->ExceptionCheck (env)) {
    gst_amc_jni_set_error (env, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        err, "Failed set buffer key '%s'", key);
    goto done;
  }

done:
  if (key_str)
    (*env)->DeleteLocalRef (env, key_str);
  if (v)
    (*env)->DeleteLocalRef (env, v);
}

static gboolean
get_java_classes (void)
{
  gboolean ret = TRUE;
  JNIEnv *env;
  jclass tmp;

  GST_DEBUG ("Retrieving Java classes");

  env = gst_amc_jni_get_env ();

  tmp = (*env)->FindClass (env, "java/lang/String");
  if (!tmp) {
    ret = FALSE;
    GST_ERROR ("Failed to get string class");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }
  java_string.klass = (*env)->NewGlobalRef (env, tmp);
  if (!java_string.klass) {
    ret = FALSE;
    GST_ERROR ("Failed to get string class global reference");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  java_string.constructor =
      (*env)->GetMethodID (env, java_string.klass, "<init>", "([C)V");
  if (!java_string.constructor) {
    ret = FALSE;
    GST_ERROR ("Failed to get string methods");
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
    }
    goto done;
  }

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

gboolean
gst_amc_init (void)
{
  if (!gst_amc_jni_initialize ())
    return FALSE;

  if (!get_java_classes ())
    return FALSE;

  return TRUE;
}

/* vim:set tabstop=4 softtabstop=4 shiftwidth=4 expandtab: */

