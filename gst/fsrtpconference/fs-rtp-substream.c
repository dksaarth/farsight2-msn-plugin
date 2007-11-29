/*
 * Farsight2 - Farsight RTP Sub Stream
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rtp-substream.c - A Farsight RTP Substream gobject
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rtp-substream.h"


/* props */
enum
{
  PROP_0,
  PROP_CONFERENCE,
  PROP_RTPBIN_PAD,
  PROP_SSRC,
  PROP_PT,
};

struct _FsRtpSubStreamPrivate {
  gboolean disposed;

  FsRtpConference *conference;

  guint32 ssrc;
  guint pt;

  GstPad *rtpbin_pad;

  GstElement *valve;

  /* This only exists if the codec is valid,
   * otherwise the rtpbin_pad is blocked */
  /* Protected by the mutex */
  GstElement *codecbin;

  /* This is only created when the substream is associated with a FsRtpStream */
  GstPad *output_pad;

  GMutex *mutex;

  GError *construction_error;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE(FsRtpSubStream, fs_rtp_sub_stream, G_TYPE_OBJECT);

#define FS_RTP_SUB_STREAM_GET_PRIVATE(o)                                 \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RTP_SUB_STREAM,             \
   FsRtpSubStreamPrivate))

#define FS_RTP_SUB_STREAM_LOCK(self)    g_mutex_lock ((self)->priv->mutex)
#define FS_RTP_SUB_STREAM_UNLOCK(self)  g_mutex_unlock ((self)->priv->mutex)

static void fs_rtp_sub_stream_dispose (GObject *object);
static void fs_rtp_sub_stream_finalize (GObject *object);
static void fs_rtp_sub_stream_constructed (GObject *object);

static void fs_rtp_sub_stream_get_property (GObject *object, guint prop_id,
  GValue *value, GParamSpec *pspec);
static void fs_rtp_sub_stream_set_property (GObject *object, guint prop_id,
  const GValue *value, GParamSpec *pspec);

static void
fs_rtp_sub_stream_class_init (FsRtpSubStreamClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = fs_rtp_sub_stream_parent_class;

  gobject_class->constructed = fs_rtp_sub_stream_constructed;
  gobject_class->dispose = fs_rtp_sub_stream_dispose;
  gobject_class->finalize = fs_rtp_sub_stream_finalize;
  gobject_class->set_property = fs_rtp_sub_stream_set_property;
  gobject_class->get_property = fs_rtp_sub_stream_get_property;

  g_object_class_install_property (gobject_class,
    PROP_CONFERENCE,
    g_param_spec_object ("conference",
      "The Conference this substream stream refers to",
      "This is a convience pointer for the Conference",
      FS_TYPE_RTP_CONFERENCE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));


  g_object_class_install_property (gobject_class,
    PROP_RTPBIN_PAD,
    g_param_spec_object ("rtpbin-pad",
      "The GstPad this substrea is linked to",
      "This is the pad on which this substream will attach itself",
      GST_TYPE_PAD,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));


  g_object_class_install_property (gobject_class,
    PROP_SSRC,
    g_param_spec_uint ("ssrc",
      "The ssrc this stream is used for",
      "This is the SSRC from the pad",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
    PROP_PT,
    g_param_spec_uint ("pt",
      "The payload type this stream is used for",
      "This is the payload type from the pad",
      0, 128, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_type_class_add_private (klass, sizeof (FsRtpSubStreamPrivate));
}


static void
fs_rtp_sub_stream_init (FsRtpSubStream *self)
{
  self->priv = FS_RTP_SUB_STREAM_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  self->priv->mutex = g_mutex_new ();
}


static void
fs_rtp_sub_stream_constructed (GObject *object)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  if (!self->priv->conference) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_INVALID_ARGUMENTS, "A Substream needs a conference object");
    return;
  }

  self->priv->valve = gst_element_factory_make ("fsvalve", NULL);

  if (!self->priv->valve) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not create a fsvalve element for"
      " session substream with ssrc: %x and pt:%d", self->priv->ssrc,
      self->priv->pt);
    return;
  }


  if (!gst_bin_add (GST_BIN (self->priv->conference), self->priv->valve)) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not add the fsvalve element for session"
      " substream with ssrc: %x and pt:%d to the conference bin",
      self->priv->ssrc, self->priv->pt);
    return;
  }

  /* We set the valve to dropping, the stream will unblock it when its linked */
  g_object_set (self->priv->valve, "drop", TRUE, NULL);

  if (gst_element_set_state (self->priv->valve, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE) {
    self->priv->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION, "Could not set the fsvalve element for session"
      " substream with ssrc: %x and pt:%d to the playing state",
      self->priv->ssrc, self->priv->pt);
    return;
  }
}


static void
fs_rtp_sub_stream_dispose (GObject *object)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  if (self->priv->disposed)
    return;

  if (self->priv->output_pad) {
    gst_element_remove_pad (GST_ELEMENT (self->priv->conference),
      self->priv->output_pad);
    self->priv->output_pad = NULL;
  }

  if (self->priv->valve) {
    gst_object_ref (self->priv->valve);
    gst_element_set_state (self->priv->valve, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self->priv->conference), self->priv->valve);
    gst_element_set_state (self->priv->valve, GST_STATE_NULL);
    gst_object_unref (self->priv->valve);
    self->priv->valve = NULL;
  }


  if (self->priv->codecbin) {
    gst_object_ref (self->priv->codecbin);
    gst_element_set_state (self->priv->codecbin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self->priv->conference), self->priv->codecbin);
    gst_element_set_state (self->priv->codecbin, GST_STATE_NULL);
    gst_object_unref (self->priv->codecbin);
    self->priv->codecbin = NULL;
  }

  if (self->priv->rtpbin_pad) {
    gst_object_unref (self->priv->rtpbin_pad);
    self->priv->rtpbin_pad = NULL;
  }

  self->priv->disposed = TRUE;
  G_OBJECT_CLASS (fs_rtp_sub_stream_parent_class)->dispose (object);
}

static void
fs_rtp_sub_stream_finalize (GObject *object)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  g_mutex_free (self->priv->mutex);

  G_OBJECT_CLASS (fs_rtp_sub_stream_parent_class)->finalize (object);
}



static void
fs_rtp_sub_stream_set_property (GObject *object,
                                guint prop_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  switch (prop_id) {
    case PROP_CONFERENCE:
      self->priv->conference = g_value_get_object (value);
      break;
    case PROP_RTPBIN_PAD:
      self->priv->rtpbin_pad = g_value_dup_object (value);
      break;
    case PROP_SSRC:
      self->priv->ssrc = g_value_get_uint (value);
     break;
    case PROP_PT:
      self->priv->pt = g_value_get_uint (value);
      break;
  }
}


static void
fs_rtp_sub_stream_get_property (GObject *object,
                                guint prop_id,
                                GValue *value,
                                GParamSpec *pspec)
{
  FsRtpSubStream *self = FS_RTP_SUB_STREAM (object);

  switch (prop_id) {
    case PROP_CONFERENCE:
      g_value_set_object (value, self->priv->conference);
      break;
    case PROP_RTPBIN_PAD:
      g_value_set_object (value, self->priv->rtpbin_pad);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, self->priv->ssrc);
     break;
    case PROP_PT:
      g_value_set_uint (value, self->priv->pt);
      break;
  }
}

static void
_blocked_cb (GstPad *pad, gboolean blocked, gpointer user_data)
{
}

/**
 * fs_rtp_sub_stream_block:
 *
 * Blocks the src pad of this new substream until
 */

void
fs_rtp_sub_stream_block (FsRtpSubStream *substream,
  GstPadBlockCallback callback, gpointer user_data)
{
  if (!callback)
    callback = _blocked_cb;

  gst_pad_set_blocked_async (substream->priv->rtpbin_pad, TRUE, callback,
    user_data);
}

/**
 * fs_rtp_session_add_codecbin:
 * @codecbin: The codec bin
 *
 * Add and links the rtpbin for a given substream.
 * Eats a reference to the codec bin on success
 *
 * Returns: TRUE on success
 */

gboolean
fs_rtp_sub_stream_add_codecbin (FsRtpSubStream *substream,
  GstElement *codecbin, GError **error)
{
  GstPad *codec_bin_sink_pad;
  GstPadLinkReturn linkret;
  gboolean has_codecbin = FALSE;

  FS_RTP_SUB_STREAM_LOCK (substream);
  if (substream->priv->codecbin == NULL) {
    substream->priv->codecbin = codecbin;
    has_codecbin = TRUE;
  }
  FS_RTP_SUB_STREAM_UNLOCK (substream);

  if (has_codecbin) {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "There already is a codec bin for this substream");
    return FALSE;
  }


  if (!gst_bin_add (GST_BIN (substream->priv->conference), codecbin)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add the codec bin to the conference");
    goto error_no_remove;
  }

  if (gst_element_set_state (codecbin, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not set the codec bin to the playing state");
    goto error;
  }

  if (!gst_element_link_pads (codecbin, "src",
      substream->priv->valve, "sink")) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not link the codec bin to the valve");
    goto error;
  }

  codec_bin_sink_pad = gst_element_get_static_pad (codecbin, "sink");
  if (!codec_bin_sink_pad) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not get the codecbin's sink pad");
    goto error;
  }

  linkret = gst_pad_link (substream->priv->rtpbin_pad, codec_bin_sink_pad);

  gst_object_unref (codec_bin_sink_pad);

  if (GST_PAD_LINK_FAILED (linkret)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not link the rtpbin to the codec bin (%d)", linkret);
    goto error;
  }

  gst_pad_set_blocked_async (substream->priv->rtpbin_pad, FALSE, _blocked_cb,
    NULL);

  return TRUE;

 error:
    gst_element_set_state (codecbin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (substream->priv->conference), codecbin);

 error_no_remove:
    FS_RTP_SUB_STREAM_LOCK (substream);
    substream->priv->codecbin = NULL;
    FS_RTP_SUB_STREAM_UNLOCK (substream);

    return FALSE;

}

FsRtpSubStream *
fs_rtp_sub_stream_new (FsRtpConference *conference, GstPad *rtpbin_pad,
  guint32 ssrc, guint pt, GError **error)
{
  FsRtpSubStream *substream = g_object_new (FS_TYPE_RTP_SUB_STREAM,
    "conference", conference,
    "rtpbin-pad", rtpbin_pad,
    "ssrc", ssrc,
    "pt", pt,
    NULL);

  if (substream->priv->construction_error) {
    g_propagate_error (error, substream->priv->construction_error);
    g_object_unref (substream);
    return NULL;
  }

  return substream;
}


void
fs_rtp_sub_stream_stop (FsRtpSubStream *substream)
{
  if (substream->priv->output_pad)
    gst_pad_set_active (substream->priv->output_pad, FALSE);

  if (substream->priv->valve)
    gst_element_set_state (substream->priv->valve, GST_STATE_NULL);

  if (substream->priv->codecbin)
    gst_element_set_state (substream->priv->codecbin, GST_STATE_NULL);
}