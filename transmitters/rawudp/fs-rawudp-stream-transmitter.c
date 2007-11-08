/*
 * Farsight2 - Farsight RAW UDP with STUN Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-rawudp-transmitter.c - A Farsight UDP transmitter with STUN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


/**
 * SECTION:fs-stream-transmitter
 * @short_description: A stream transmitter object for UDP with STUN
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rawudp-stream-transmitter.h"

#include <gst/gst.h>

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_SENDING,
};

struct _FsRawUdpStreamTransmitterPrivate
{
  gboolean disposed;
};

#define FS_RAWUDP_STREAM_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_RAWUDP_STREAM_TRANSMITTER, \
                                FsRawUdpStreamTransmitterPrivate))

static void fs_rawudp_stream_transmitter_class_init (FsRawUdpStreamTransmitterClass *klass);
static void fs_rawudp_stream_transmitter_init (FsRawUdpStreamTransmitter *self);
static void fs_rawudp_stream_transmitter_dispose (GObject *object);
static void fs_rawudp_stream_transmitter_finalize (GObject *object);

static void fs_rawudp_stream_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_rawudp_stream_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static gboolean fs_rawudp_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error);


static GObjectClass *parent_class = NULL;
// static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_rawudp_stream_transmitter_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsRawUdpStreamTransmitterClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_rawudp_stream_transmitter_class_init,
      NULL,
      NULL,
      sizeof (FsRawUdpStreamTransmitter),
      0,
      (GInstanceInitFunc) fs_rawudp_stream_transmitter_init
    };

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsRawUdpStreamTransmitter", &info, G_TYPE_FLAG_ABSTRACT);
  }

  return type;
}

static void
fs_rawudp_stream_transmitter_class_init (FsRawUdpStreamTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsStreamTransmitterClass *streamtransmitterclass =
    FS_STREAM_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_rawudp_stream_transmitter_set_property;
  gobject_class->get_property = fs_rawudp_stream_transmitter_get_property;

  streamtransmitterclass->add_remote_candidate =
    fs_rawudp_stream_transmitter_add_remote_candidate;

  g_object_class_override_property (gobject_class, PROP_SENDING, "sending");

  gobject_class->dispose = fs_rawudp_stream_transmitter_dispose;
  gobject_class->finalize = fs_rawudp_stream_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsRawUdpStreamTransmitterPrivate));
}

static void
fs_rawudp_stream_transmitter_init (FsRawUdpStreamTransmitter *self)
{
  /* member init */
  self->priv = FS_RAWUDP_STREAM_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_rawudp_stream_transmitter_dispose (GObject *object)
{
  FsRawUdpStreamTransmitter *self = FS_RAWUDP_STREAM_TRANSMITTER (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_rawudp_stream_transmitter_finalize (GObject *object)
{
  parent_class->finalize (object);
}

static void
fs_rawudp_stream_transmitter_get_property (GObject *object,
                                           guint prop_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
}

static void
fs_rawudp_stream_transmitter_set_property (GObject *object,
                                           guint prop_id,
                                           const GValue *value,
                                           GParamSpec *pspec)
{
}


/**
 * fs_rawudp_stream_transmitter_add_remote_candidate
 * @streamtransmitter: a #FsStreamTransmitter
 * @candidate: a remote #FsCandidate to add
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function is used to add remote candidates to the transmitter
 *
 * Returns: TRUE of the candidate could be added, FALSE if it couldnt
 *   (and the #GError will be set)
 */

static gboolean
fs_rawudp_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error)
{
  return FALSE;
}
