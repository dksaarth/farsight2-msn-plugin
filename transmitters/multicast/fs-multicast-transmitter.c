/*
 * Farsight2 - Farsight Multicast UDP Transmitter
 *
 * Copyright 2007-2008 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007-2008 Nokia Corp.
 *
 * fs-multicast-transmitter.h - A Farsight multicast UDP transmitter
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

/**
 * SECTION:fs-multicast-transmitter
 * @short_description: A transmitter for multicast UDP
 *
 * This transmitter provides multicast udp
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-multicast-transmitter.h"
#include "fs-multicast-stream-transmitter.h"

#include <gst/farsight/fs-conference-iface.h>
#include <gst/farsight/fs-plugin.h>

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

GST_DEBUG_CATEGORY (fs_multicast_transmitter_debug);
#define GST_CAT_DEFAULT fs_multicast_transmitter_debug

/* Signals */
enum
{
  LAST_SIGNAL
};

/* props */
enum
{
  PROP_0,
  PROP_GST_SINK,
  PROP_GST_SRC,
  PROP_COMPONENTS
};

struct _FsMulticastTransmitterPrivate
{
  /* We hold references to this element */
  GstElement *gst_sink;
  GstElement *gst_src;

  /* We don't hold a reference to these elements, they are owned
     by the bins */
  /* They are tables of pointers, one per component */
  GstElement **udpsrc_funnels;
  GstElement **udpsink_tees;

  GList **udpports;

  gboolean disposed;
};

#define FS_MULTICAST_TRANSMITTER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), FS_TYPE_MULTICAST_TRANSMITTER, \
    FsMulticastTransmitterPrivate))

static void fs_multicast_transmitter_class_init (
    FsMulticastTransmitterClass *klass);
static void fs_multicast_transmitter_init (FsMulticastTransmitter *self);
static void fs_multicast_transmitter_constructed (GObject *object);
static void fs_multicast_transmitter_dispose (GObject *object);
static void fs_multicast_transmitter_finalize (GObject *object);

static void fs_multicast_transmitter_get_property (GObject *object,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void fs_multicast_transmitter_set_property (GObject *object,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);

static FsStreamTransmitter *fs_multicast_transmitter_new_stream_transmitter (
    FsTransmitter *transmitter, FsParticipant *participant,
    guint n_parameters, GParameter *parameters, GError **error);
static GType fs_multicast_transmitter_get_stream_transmitter_type (
    FsTransmitter *transmitter,
    GError **error);


typedef struct _UdpPort UdpPort;

static UdpPort *fs_multicast_transmitter_get_udpport (
    FsMulticastTransmitter *trans,
    guint component_id,
    const gchar *local_ip,
    guint16 port,
    guint8 ttl,
    gboolean recv,
    GError **error);
static void fs_multicast_transmitter_put_udpport (FsMulticastTransmitter *trans,
    UdpPort *udpport);


static GObjectClass *parent_class = NULL;
//static guint signals[LAST_SIGNAL] = { 0 };


/*
 * Lets register the plugin
 */

static GType type = 0;

GType
fs_multicast_transmitter_get_type (void)
{
  g_assert (type);
  return type;
}

static GType
fs_multicast_transmitter_register_type (FsPlugin *module)
{
  static const GTypeInfo info = {
    sizeof (FsMulticastTransmitterClass),
    NULL,
    NULL,
    (GClassInitFunc) fs_multicast_transmitter_class_init,
    NULL,
    NULL,
    sizeof (FsMulticastTransmitter),
    0,
    (GInstanceInitFunc) fs_multicast_transmitter_init
  };

  if (fs_multicast_transmitter_debug == NULL)
    GST_DEBUG_CATEGORY_INIT (fs_multicast_transmitter_debug,
        "fsmulticasttransmitter", 0,
        "Farsight multicast UDP transmitter");

  fs_multicast_stream_transmitter_register_type (module);

  type = g_type_module_register_type (G_TYPE_MODULE (module),
    FS_TYPE_TRANSMITTER, "FsMulticastTransmitter", &info, 0);

  return type;
}

static void
fs_multicast_transmitter_unload (FsPlugin *plugin)
{
  if (fs_multicast_transmitter_debug)
  {
    gst_debug_category_free (fs_multicast_transmitter_debug);
    fs_multicast_transmitter_debug = NULL;
  }
}

FS_INIT_PLUGIN (fs_multicast_transmitter_register_type,
    fs_multicast_transmitter_unload)

static void
fs_multicast_transmitter_class_init (FsMulticastTransmitterClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  FsTransmitterClass *transmitter_class = FS_TRANSMITTER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = fs_multicast_transmitter_set_property;
  gobject_class->get_property = fs_multicast_transmitter_get_property;

  gobject_class->constructed = fs_multicast_transmitter_constructed;

  g_object_class_override_property (gobject_class, PROP_GST_SRC, "gst-src");
  g_object_class_override_property (gobject_class, PROP_GST_SINK, "gst-sink");
  g_object_class_override_property (gobject_class, PROP_COMPONENTS,
    "components");

  transmitter_class->new_stream_transmitter =
    fs_multicast_transmitter_new_stream_transmitter;
  transmitter_class->get_stream_transmitter_type =
    fs_multicast_transmitter_get_stream_transmitter_type;

  gobject_class->dispose = fs_multicast_transmitter_dispose;
  gobject_class->finalize = fs_multicast_transmitter_finalize;

  g_type_class_add_private (klass, sizeof (FsMulticastTransmitterPrivate));
}

static void
fs_multicast_transmitter_init (FsMulticastTransmitter *self)
{

  /* member init */
  self->priv = FS_MULTICAST_TRANSMITTER_GET_PRIVATE (self);
  self->priv->disposed = FALSE;

  self->components = 2;
}

static void
fs_multicast_transmitter_constructed (GObject *object)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER_CAST (object);
  FsTransmitter *trans = FS_TRANSMITTER_CAST (self);
  GstPad *pad = NULL, *pad2 = NULL;
  GstPad *ghostpad = NULL;
  gchar *padname;
  GstPadLinkReturn ret;
  int c; /* component_id */


  /* We waste one space in order to have the index be the component_id */
  self->priv->udpsrc_funnels = g_new0 (GstElement *, self->components+1);
  self->priv->udpsink_tees = g_new0 (GstElement *, self->components+1);
  self->priv->udpports = g_new0 (GList *, self->components+1);

  /* First we need the src elemnet */

  self->priv->gst_src = gst_bin_new (NULL);

  if (!self->priv->gst_src) {
    trans->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not build the transmitter src bin");
    return;
  }

  gst_object_ref (self->priv->gst_src);


  /* Second, we do the sink element */

  self->priv->gst_sink = gst_bin_new (NULL);

  if (!self->priv->gst_sink) {
    trans->construction_error = g_error_new (FS_ERROR,
      FS_ERROR_CONSTRUCTION,
      "Could not build the transmitter sink bin");
    return;
  }

  gst_object_ref (self->priv->gst_sink);

  for (c = 1; c <= self->components; c++) {
    GstElement *fakesink = NULL;

    /* Lets create the RTP source funnel */

    self->priv->udpsrc_funnels[c] = gst_element_factory_make ("fsfunnel", NULL);

    if (!self->priv->udpsrc_funnels[c]) {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make the fsfunnel element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_src),
        self->priv->udpsrc_funnels[c])) {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not add the fsfunnel element to the transmitter src bin");
    }

    pad = gst_element_get_static_pad (self->priv->udpsrc_funnels[c], "src");
    padname = g_strdup_printf ("src%d", c);
    ghostpad = gst_ghost_pad_new (padname, pad);
    g_free (padname);
    gst_object_unref (pad);

    gst_pad_set_active (ghostpad, TRUE);
    gst_element_add_pad (self->priv->gst_src, ghostpad);


    /* Lets create the RTP sink tee */

    self->priv->udpsink_tees[c] = gst_element_factory_make ("tee", NULL);

    if (!self->priv->udpsink_tees[c]) {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make the tee element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_sink),
        self->priv->udpsink_tees[c])) {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not add the tee element to the transmitter sink bin");
    }

    pad = gst_element_get_static_pad (self->priv->udpsink_tees[c], "sink");
    padname = g_strdup_printf ("sink%d", c);
    ghostpad = gst_ghost_pad_new (padname, pad);
    g_free (padname);
    gst_object_unref (pad);

    gst_pad_set_active (ghostpad, TRUE);
    gst_element_add_pad (self->priv->gst_sink, ghostpad);

    fakesink = gst_element_factory_make ("fakesink", NULL);

    if (!fakesink) {
      trans->construction_error = g_error_new (FS_ERROR,
        FS_ERROR_CONSTRUCTION,
        "Could not make the fakesink element");
      return;
    }

    if (!gst_bin_add (GST_BIN (self->priv->gst_sink), fakesink))
    {
      gst_object_unref (fakesink);
      trans->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not add the fakesink element to the transmitter sink bin");
      return;
    }

    g_object_set (fakesink,
        "async", FALSE,
        "sync" , FALSE,
        NULL);

    pad = gst_element_get_request_pad (self->priv->udpsink_tees[c], "src%d");
    pad2 = gst_element_get_static_pad (fakesink, "sink");

    ret = gst_pad_link (pad, pad2);

    gst_object_unref (pad2);
    gst_object_unref (pad);

    if (GST_PAD_LINK_FAILED(ret)) {
      trans->construction_error = g_error_new (FS_ERROR,
          FS_ERROR_CONSTRUCTION,
          "Could not link the tee to the fakesink");
      return;
    }
  }
}

static void
fs_multicast_transmitter_dispose (GObject *object)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER (object);

  if (self->priv->disposed) {
    /* If dispose did already run, return. */
    return;
  }

  if (self->priv->gst_src) {
    gst_object_unref (self->priv->gst_src);
    self->priv->gst_src = NULL;
  }

  if (self->priv->gst_sink) {
    gst_object_unref (self->priv->gst_sink);
    self->priv->gst_sink = NULL;
  }

  /* Make sure dispose does not run twice. */
  self->priv->disposed = TRUE;

  parent_class->dispose (object);
}

static void
fs_multicast_transmitter_finalize (GObject *object)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER (object);

  if (self->priv->udpsrc_funnels) {
    g_free (self->priv->udpsrc_funnels);
    self->priv->udpsrc_funnels = NULL;
  }

  if (self->priv->udpsink_tees) {
    g_free (self->priv->udpsink_tees);
    self->priv->udpsink_tees = NULL;
  }

  if (self->priv->udpports) {
    g_free (self->priv->udpports);
    self->priv->udpports = NULL;
  }

  parent_class->finalize (object);
}

static void
fs_multicast_transmitter_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_GST_SINK:
      g_value_set_object (value, self->priv->gst_sink);
      break;
    case PROP_GST_SRC:
      g_value_set_object (value, self->priv->gst_src);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fs_multicast_transmitter_set_property (GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER (object);

  switch (prop_id) {
    case PROP_COMPONENTS:
      self->components = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/**
 * fs_multicast_transmitter_new_stream_multicast_transmitter:
 * @transmitter: a #FsTranmitter
 * @participant: the #FsParticipant for which the #FsStream using this
 * new #FsStreamTransmitter is created
 *
 * This function will create a new #FsStreamTransmitter element for a
 * specific participant for this #FsMulticastTransmitter
 *
 * Returns: a new #FsStreamTransmitter
 */

static FsStreamTransmitter *
fs_multicast_transmitter_new_stream_transmitter (FsTransmitter *transmitter,
  FsParticipant *participant, guint n_parameters, GParameter *parameters,
  GError **error)
{
  FsMulticastTransmitter *self = FS_MULTICAST_TRANSMITTER (transmitter);

  return FS_STREAM_TRANSMITTER (fs_multicast_stream_transmitter_newv (
        self, n_parameters, parameters, error));
}


/*
 * The UdpPort structure is a ref-counted pseudo-object use to represent
 * one ip:port:ttl trio on which we listen and send, so it includes a udpsrc
 * and a multiudpsink. It represents one BSD socket.
 */

struct _UdpPort {
  gint refcount;

  GstElement *udpsrc;
  GstPad *udpsrc_requested_pad;

  GstElement *udpsink;
  GstPad *udpsink_requested_pad;

  gchar *local_ip;
  guint16 port;
  guint8 ttl;

  gint fd;

  /* These are just convenience pointers to our parent transmitter */
  GstElement *funnel;
  GstElement *tee;

  guint component_id;

  gboolean sendonly;

  GList *multicast_groups;
};

static gint
_bind_port (
    const gchar *ip,
    guint16 port,
    guchar ttl,
    GError **error)
{
  int sock = -1;
  struct sockaddr_in address;
  int retval;
  guchar loop = 1;
  int reuseaddr = 1;

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;

  if (ip) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;

    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_NUMERICHOST;
    retval = getaddrinfo (ip, NULL, &hints, &result);
    if (retval != 0) {
      g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Invalid IP address %s passed: %s", ip, gai_strerror (retval));
      return -1;
    }
    memcpy (&address, result->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo (result);
  }

  if ((sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) <= 0) {
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
      "Error creating socket: %s", g_strerror (errno));
    goto error;
  }

  if (setsockopt (sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
          sizeof (ttl)) < 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Error setting the multicast TTL: %s",
        g_strerror (errno));
    goto error;
  }

  if (setsockopt (sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop,
          sizeof (loop)) < 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Error setting the multicast loop to FALSE: %s",
        g_strerror (errno));
    goto error;
  }

  if (setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr,
          sizeof (reuseaddr)) < 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Error setting reuseaddr to TRUE: %s",
        g_strerror (errno));
    goto error;
  }

#ifdef SO_REUSEPORT
  if (setsockopt (sock, SOL_SOCKET, SO_REUSEPORT, &reuseaddr,
          sizeof (reuseaddr)) < 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Error setting reuseaddr to TRUE: %s",
        g_strerror (errno));
    goto error;
  }
#endif

  address.sin_port = htons (port);
  retval = bind (sock, (struct sockaddr *) &address, sizeof (address));
  if (retval != 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_NETWORK,
        "Could not bind to port %d", port);
    goto error;
  }

  return sock;

 error:
  if (sock >= 0)
    close (sock);
  return -1;
}

static GstElement *
_create_sinksource (gchar *elementname, GstBin *bin,
  GstElement *teefunnel, gint fd, GstPadDirection direction,
  GstPad **requested_pad, GError **error)
{
  GstElement *elem;
  GstPadLinkReturn ret;
  GstPad *elempad = NULL;
  GstStateChangeReturn state_ret;

  g_assert (direction == GST_PAD_SINK || direction == GST_PAD_SRC);

  elem = gst_element_factory_make (elementname, NULL);
  if (!elem) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not create the %s element", elementname);
    return NULL;
  }

  g_object_set (elem,
    "closefd", FALSE,
    "sockfd", fd,
    NULL);

  if (direction == GST_PAD_SINK)
    g_object_set (elem, "auto-multicast", FALSE, NULL);

  if (!gst_bin_add (bin, elem)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not add the %s element to the gst %s bin", elementname,
      (direction == GST_PAD_SINK) ? "sink" : "src");
    gst_object_unref (elem);
    return NULL;
  }

  if (direction == GST_PAD_SINK)
    *requested_pad = gst_element_get_request_pad (teefunnel, "src%d");
  else
    *requested_pad = gst_element_get_request_pad (teefunnel, "sink%d");

  if (!*requested_pad) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not get the %s request pad from the %s",
      (direction == GST_PAD_SINK) ? "src" : "sink",
      (direction == GST_PAD_SINK) ? "tee" : "funnel");
    goto error;
  }

  if (direction == GST_PAD_SINK)
    elempad = gst_element_get_static_pad (elem, "sink");
  else
    elempad = gst_element_get_static_pad (elem, "src");

  if (direction == GST_PAD_SINK)
    ret = gst_pad_link (*requested_pad, elempad);
  else
    ret = gst_pad_link (elempad, *requested_pad);

  gst_object_unref (elempad);

  if (GST_PAD_LINK_FAILED(ret)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not link the new element %s (%d)", elementname, ret);
    goto error;
  }

  if (!gst_element_sync_state_with_parent (elem)) {
    g_set_error (error, FS_ERROR, FS_ERROR_CONSTRUCTION,
      "Could not sync the state of the new %s with its parent",
      elementname);
    goto error;
  }

  return elem;

 error:

  gst_object_ref (elem);
  gst_element_set_state (elem, GST_STATE_NULL);
  gst_bin_remove (bin, elem);
  state_ret = gst_element_set_state (elem, GST_STATE_NULL);
  if (state_ret != GST_STATE_CHANGE_SUCCESS) {
    GST_ERROR ("On error, could not reset %s to state NULL (%s)", elementname,
      gst_element_state_change_return_get_name (state_ret));
  }
  gst_object_unref (elem);

  if (elempad)
    gst_object_unref (elempad);

  return NULL;
}


static UdpPort *
fs_multicast_transmitter_get_udpport (FsMulticastTransmitter *trans,
    guint component_id,
    const gchar *local_ip,
    guint16 port,
    guint8 ttl,
    gboolean recv,
    GError **error)
{
  UdpPort *udpport;
  GList *udpport_e;
  gboolean sendonly = FALSE;

  /* First lets check if we already have one */
  if (component_id > trans->components)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
      "Invalid component %d > %d", component_id, trans->components);
    return NULL;
  }

  for (udpport_e = g_list_first (trans->priv->udpports[component_id]);
       udpport_e;
       udpport_e = g_list_next (udpport_e))
  {
    udpport = udpport_e->data;

    if (port == udpport->port &&
        ((local_ip == NULL && udpport->local_ip == NULL) ||
          !strcmp (local_ip, udpport->local_ip)))
    {
      if (recv)
      {
        if (!udpport->sendonly)
        {
          udpport->refcount++;
          return udpport;
        }
      }
      else /* recv */
      {
        if (ttl == udpport->ttl)
        {
          udpport->refcount++;
          return udpport;
        }
        sendonly = TRUE;
      }
    }
  }

  udpport = g_new0 (UdpPort, 1);

  udpport->refcount = 1;
  udpport->local_ip = g_strdup (local_ip);
  udpport->fd = -1;
  udpport->component_id = component_id;
  udpport->port = port;
  udpport->ttl = ttl;
  udpport->sendonly = sendonly;

  /* Now lets bind both ports */

  udpport->fd = _bind_port (local_ip, port, ttl, error);
  if (udpport->fd < 0)
    goto error;

  /* Now lets create the elements */

  udpport->tee = trans->priv->udpsink_tees[component_id];
  if (!udpport->sendonly)
    udpport->funnel = trans->priv->udpsrc_funnels[component_id];

  if (!udpport->sendonly)
  {
    udpport->udpsrc = _create_sinksource ("udpsrc",
        GST_BIN (trans->priv->gst_src), udpport->funnel, udpport->fd, GST_PAD_SRC,
        &udpport->udpsrc_requested_pad, error);
    if (!udpport->udpsrc)
      goto error;
  }

  udpport->udpsink = _create_sinksource ("multiudpsink",
    GST_BIN (trans->priv->gst_sink), udpport->tee, udpport->fd, GST_PAD_SINK,
    &udpport->udpsink_requested_pad, error);
  if (!udpport->udpsink)
    goto error;

  g_object_set (udpport->udpsink, "async", FALSE, NULL);

  trans->priv->udpports[component_id] =
    g_list_prepend (trans->priv->udpports[component_id], udpport);

  return udpport;

 error:

  if (udpport)
    fs_multicast_transmitter_put_udpport (trans, udpport);

  return NULL;
}

static void
fs_multicast_transmitter_put_udpport (FsMulticastTransmitter *trans,
  UdpPort *udpport)
{
  if (udpport->refcount > 1) {
    udpport->refcount--;
    return;
  }

  g_assert (udpport->multicast_groups == NULL);

  trans->priv->udpports[udpport->component_id] =
    g_list_remove (trans->priv->udpports[udpport->component_id], udpport);

  if (udpport->udpsrc) {
    GstStateChangeReturn ret;
    gst_object_ref (udpport->udpsrc);
    gst_element_set_state (udpport->udpsrc, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (trans->priv->gst_src), udpport->udpsrc);
    ret = gst_element_set_state (udpport->udpsrc, GST_STATE_NULL);
    if (ret != GST_STATE_CHANGE_SUCCESS) {
      GST_ERROR ("Error changing state of udpsrc: %s",
        gst_element_state_change_return_get_name (ret));
    }
    gst_object_unref (udpport->udpsrc);
  }

  if (udpport->udpsrc_requested_pad) {
    gst_element_release_request_pad (udpport->funnel,
      udpport->udpsrc_requested_pad);
    gst_object_unref (udpport->udpsrc_requested_pad);
  }

  if (udpport->udpsink) {
    GstStateChangeReturn ret;
    gst_object_ref (udpport->udpsink);
    gst_element_set_state (udpport->udpsink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (trans->priv->gst_sink), udpport->udpsink);
    ret = gst_element_set_state (udpport->udpsink, GST_STATE_NULL);
    if (ret != GST_STATE_CHANGE_SUCCESS) {
      GST_ERROR ("Error changing state of udpsink: %s",
        gst_element_state_change_return_get_name (ret));
    }
    gst_object_unref (udpport->udpsink);
  }

  if (udpport->udpsink_requested_pad) {
    gst_element_release_request_pad (udpport->tee,
      udpport->udpsink_requested_pad);
    gst_object_unref (udpport->udpsink_requested_pad);
  }

  if (udpport->fd >= 0)
    close (udpport->fd);

  g_free (udpport->local_ip);
  g_free (udpport);
}

static void
fs_multicast_transmitter_udpport_add_dest (UdpPort *udpport,
  const gchar *ip, gint port)
{
  GST_DEBUG ("Adding dest %s:%d", ip, port);
  g_signal_emit_by_name (udpport->udpsink, "add", ip, port);
}


static void
fs_multicast_transmitter_udpport_remove_dest (UdpPort *udpport,
  const gchar *ip, gint port)
{
  g_signal_emit_by_name (udpport->udpsink, "remove", ip, port);
}


static GType
fs_multicast_transmitter_get_stream_transmitter_type (
    FsTransmitter *transmitter,
    GError **error)
{
  return FS_TYPE_MULTICAST_STREAM_TRANSMITTER;
}

/*
 * The following functions are for counting the use of Multicast
 * Each struct represents a quatuor of local_ip:port:ttl:multicast_ip
 */

struct _UdpMulticastGroup
{
  UdpPort *udpport;

  gint refcount;
  gint sendcount;

  gchar *multicast_ip;

  struct ip_mreqn mreqn;
};

UdpMulticastGroup *
fs_multicast_transmitter_get_group (FsMulticastTransmitter *trans,
    guint component_id,
    const gchar *multicast_ip,
    guint port,
    const gchar *local_ip,
    guint8 ttl,
    gboolean recv,
    GError **error)
{
  UdpPort *udpport;
  UdpMulticastGroup *mcast = NULL;
  GList *item = NULL;

  udpport = fs_multicast_transmitter_get_udpport (trans, component_id,
      local_ip, port, ttl, recv, error);
  if (!udpport)
    return NULL;

  for (item = g_list_first (udpport->multicast_groups);
       item;
       item = g_list_next (item))
  {
    mcast = item->data;

    if (!strcmp (mcast->multicast_ip, multicast_ip))
    {
      mcast->refcount++;
      return mcast;
    }
  }

  mcast = g_new0 (UdpMulticastGroup, 1);

  mcast->refcount = 1;
  mcast->sendcount = 0;
  mcast->udpport = udpport;
  mcast->multicast_ip = g_strdup (multicast_ip);

  if (!inet_aton (multicast_ip, &mcast->mreqn.imr_multiaddr))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Invalid multicast IP");
    goto error;
  }

  if (udpport->local_ip &&
      !inet_aton (udpport->local_ip, &mcast->mreqn.imr_address))
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "UdpPort address invalid");
    goto error;
  }
  else
  {
    mcast->mreqn.imr_address.s_addr = INADDR_ANY;
  }
  mcast->mreqn.imr_ifindex = 0;

  if (setsockopt (udpport->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
          &(mcast->mreqn), sizeof (mcast->mreqn)) < 0)
  {
    g_set_error (error, FS_ERROR, FS_ERROR_INVALID_ARGUMENTS,
        "Could not join the socket to the multicast group2: %s",
        g_strerror (errno));
    goto error;
  }

  udpport->multicast_groups = g_list_prepend (udpport->multicast_groups,
      mcast);

  return mcast;

 error:

  if (mcast)
  {
    g_free (mcast->multicast_ip);
    g_free (mcast);
  }

  return NULL;
}

void
fs_multicast_transmitter_put_group (FsMulticastTransmitter *trans,
    UdpMulticastGroup *mcast)
{

  mcast->refcount--;

  if (mcast->refcount > 0)
    return;

  if (setsockopt (mcast->udpport->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
          &(mcast->mreqn), sizeof (mcast->mreqn)) < 0)
    GST_ERROR ("Could not remove the socket from the multicast group: %s",
        g_strerror (errno));

  mcast->udpport->multicast_groups = g_list_remove (
      mcast->udpport->multicast_groups,
      mcast);

  fs_multicast_transmitter_put_udpport (trans, mcast->udpport);

  g_free (mcast->multicast_ip);
  g_free (mcast);
}

void
fs_multicast_transmitter_group_inc_sending (UdpMulticastGroup *mcast)
{
  if (mcast->sendcount == 0)
    fs_multicast_transmitter_udpport_add_dest (mcast->udpport,
        mcast->multicast_ip, mcast->udpport->port);

  mcast->sendcount++;
}


void
fs_multicast_transmitter_group_dec_sending (UdpMulticastGroup *mcast)
{
  mcast->sendcount--;

  if (mcast->sendcount == 0)
    fs_multicast_transmitter_udpport_remove_dest (mcast->udpport,
        mcast->multicast_ip, mcast->udpport->port);
}
