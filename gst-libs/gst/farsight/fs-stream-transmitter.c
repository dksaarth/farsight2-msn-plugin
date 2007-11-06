/*
 * Farsight2 - Farsight Stream Transmitter
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-stream-transmitter.c - A Farsight Stream Transmitter gobject
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
 * @short_description: A stream transmitter object used to convey per-stream
 *   information to a transmitter.
 *
 * This object is the base implementation of a Farsight Stream Transmitter.
 * It needs to be derived and implement by a Farsight transmitter.
 * A Farsight Stream transmitter is used to convery per-stream information
 * to a transmitter, this is mostly local and remote candidates
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-marshal.h"
#include "fs-stream-transmitter.h"

#include <gst/gst.h>

/* Signals */
enum
{
  ERROR,
  NEW_NATIVE_CANDIDATE,
  NEW_ACTIVE_CANDIDATE_PAIR,
  NATIVE_CANDIDATES_PREPARED,
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_SENDING,
};

struct _FsStreamTransmitterPrivate
{
  gboolean disposed;
};

#define FS_STREAM_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_STREAM_TRANSMITTER, \
                                FsStreamTransmitterPrivate))

static void fs_stream_transmitter_class_init (FsStreamTransmitterClass *klass);
static void fs_stream_transmitter_init (FsStreamTransmitter *self);
static void fs_stream_transmitter_dispose (GObject *object);
static void fs_stream_transmitter_finalize (GObject *object);

static void fs_stream_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_stream_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static GObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

GType
fs_stream_transmitter_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (FsStreamTransmitterClass),
      NULL,
      NULL,
      (GClassInitFunc) fs_stream_transmitter_class_init,
      NULL,
      NULL,
      sizeof (FsStreamTransmitter),
      0,
      (GInstanceInitFunc) fs_stream_transmitter_init
    };

    type = g_type_register_static (G_TYPE_OBJECT,
        "FsStreamTransmitter", &info, G_TYPE_FLAG_ABSTRACT);
  }

  return type;
}

static void
fs_stream_transmitter_class_init (FsStreamTransmitterClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_stream_transmitter_set_property;
  gobject_class->get_property = fs_stream_transmitter_get_property;



  /**
   * FsStreamTransmitter:sending:
   *
   * A network source #GstElement to be used by the #FsSession
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_SENDING,
      g_param_spec_boolean ("sending",
        "Whether to send from this transmitter",
        "If set to FALSE, the transmitter will stop sending to this person",
        TRUE,
        G_PARAM_READWRITE));

  /**
   * FsStreamTransmitter::error:
   * @self: #FsStreamTransmitter that emitted the signal
   * @errorno: The number of the error
   * @error_msg: Error message to be displayed to user
   * @debug_msg: Debugging error message
   *
   * This signal is emitted in any error condition
   *
   */
  signals[ERROR] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      fs_marshal_VOID__OBJECT_INT_STRING_STRING,
      G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

    /**
   * FsStreamTransmitter::new-active-candidate-pair:
   * @self: #FsStream that emitted the signal
   * @native_candidate: #FsCandidate of the native candidate being used
   * @remote_candidate: #FsCandidate of the remote candidate being used
   *
   * This signal is emitted when there is a new active chandidate pair that has
   * been established. This is specially useful for ICE where the active
   * candidate pair can change automatically due to network conditions. The user
   * must not modify the candidates and must copy them if he wants to use them
   * outside the callback scope.
   *
   */
  signals[NEW_ACTIVE_CANDIDATE_PAIR] = g_signal_new
    ("new-active-candidate-pair",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      fs_marshal_VOID__BOXED_BOXED,
      G_TYPE_NONE, 2, FS_TYPE_CANDIDATE, FS_TYPE_CANDIDATE);

 /**
   * FsStreamTransmitter::new-native-candidate:
   * @self: #FsStream that emitted the signal
   * @native_candidate: #FsCandidate of the native candidate
   *
   * This signal is emitted when a new native candidate is discovered.
   *
   */
  signals[NEW_NATIVE_CANDIDATE] = g_signal_new
    ("new-native-candidate",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__BOXED,
      G_TYPE_NONE, 1, FS_TYPE_CANDIDATE);

 /**
   * FsStreamTransmitter::native-candidates-prepared:
   * @self: #FsStream that emitted the signal
   *
   * This signal is emitted when all native candidates have been
   * prepared, an ICE implementation would send its SDP offer or answer.
   *
   */
  signals[NATIVE_CANDIDATES_PREPARED] = g_signal_new
    ("native-candidates-prepared",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);



  gobject_class->dispose = fs_stream_transmitter_dispose;
  gobject_class->finalize = fs_stream_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsStreamTransmitterPrivate));
}

static void
fs_stream_transmitter_init (FsStreamTransmitter *self)
{
  /* member init */
  self->priv = FS_STREAM_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;
}

static void
fs_stream_transmitter_dispose (GObject *object)
{
  FsStreamTransmitter *self = FS_STREAM_TRANSMITTER (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_stream_transmitter_finalize (GObject *object)
{
  parent_class->finalize (object);
}

static void
fs_stream_transmitter_get_property (GObject *object,
                                    guint prop_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
}

static void
fs_stream_transmitter_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
}


/**
 * fs_stream_transmitter_add_remote_candidate
 * @streamtransmitter: a #FsStreamTranmitter
 * @candidate: a remote #FsCandidate to add
 * @error: location of a #GError, or NULL if no error occured
 *
 * This function is used to add remote candidates to the transmitter
 *
 * Returns: TRUE of the candidate could be added, FALSE if it couldnt
 *   (and the #GError will be set)
 */

gboolean
fs_stream_transmitter_add_remote_candidate (
    FsStreamTransmitter *streamtransmitter, FsCandidate *candidate,
    GError **error)
{
  FsStreamTransmitterClass *klass =
    FS_STREAM_TRANSMITTER_GET_CLASS (streamtransmitter);

  *error = NULL;

  if (klass->add_remote_candidate) {
    return klass->add_remote_candidate (streamtransmitter, candidate, error);
  } else {
    g_warning ("add_remote_candidate not defined in class");
  }

  return FALSE;
}
