/*
 * Farsight2 - Farsight RTP Discover Codecs
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-discover-codecs.c - A Farsight RTP Codec Discovery gobject
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



#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fs-rtp-discover-codecs.h"

#include "fs-rtp-codec-cache.h"

#include <string.h>


/*
 * Local TYPES
 */

typedef struct _CodecCap
{
  GstCaps *caps; /* media caps */
  GstCaps *rtp_caps; /* RTP caps of given media caps */
  /* 2 list approach so we can have a separation after an intersection is
   * calculted */
  GList *element_list1; /* elements for media, enc, dec, pay, depay */
  GList *element_list2; /* elements for media, enc, dec, pay, depay */
} CodecCap;


typedef gboolean (FilterFunc) (GstElementFactory *factory);

/* Static Functions */

static gboolean create_codec_lists (FsMediaType media_type,
  GList *recv_list, GList *send_list);
static GList *remove_dynamic_duplicates (GList *list);
static void parse_codec_cap_list (GList *list, FsMediaType media_type);
static GList *detect_send_codecs (GstCaps *caps);
static GList *detect_recv_codecs (GstCaps *caps);
static GList *codec_cap_list_intersect (GList *list1, GList *list2);
static GList *get_plugins_filtered_from_caps (FilterFunc filter,
  GstCaps *caps, GstPadDirection direction);
static gboolean extract_field_data (GQuark field_id,
                                    const GValue *value,
                                    gpointer user_data);

static gboolean check_for_sink (GList *pipeline);
static gboolean check_for_src (GList *pipeline);


/* GLOBAL variables */

static GList *list_codec_blueprints[FS_MEDIA_TYPE_LAST+1] = { NULL };
static gint codecs_lists_ref[FS_MEDIA_TYPE_LAST+1] = { 0 };


static void
debug_pipeline (GList *pipeline)
{
  GList *walk;

  g_debug ("pipeline: ");
  for (walk = pipeline; walk; walk = g_list_next (walk))
  {
    g_debug ("%p:%d:%s ", walk->data,
        GST_OBJECT_REFCOUNT_VALUE(GST_OBJECT (walk->data)),
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (walk->data)));
  }
  g_debug ("\n");
}

#if ENABLE_DEBUG_CAPS
static void
debug_codec_cap_list (GList *codec_cap_list)
{
  GList *walk;
  g_debug ("size of codec_cap list is %d", g_list_length (codec_cap_list));
  for (walk = codec_cap_list; walk; walk = g_list_next (walk))
  {
    debug_codec_cap ((CodecCap *)walk->data);
  }
}

static void
debug_codec_cap (CodecCap *codec_cap)
{
  gchar *caps;
  if (codec_cap->caps)
  {
    g_assert (gst_caps_get_size (codec_cap->caps) == 1);
    caps = gst_caps_to_string (codec_cap->caps);
    g_debug ("%p:%d:media_caps %s\n", codec_cap->caps,
        GST_CAPS_REFCOUNT_VALUE(codec_cap->caps),
        caps);
    g_free (caps);
  }

  if (codec_cap->rtp_caps)
  {
    g_assert (gst_caps_get_size (codec_cap->rtp_caps) == 1);
    caps = gst_caps_to_string (codec_cap->rtp_caps);
    g_debug ("%p:%d:rtp_caps %s\n", codec_cap->rtp_caps,
        GST_CAPS_REFCOUNT_VALUE(codec_cap->rtp_caps), caps);
    g_free (caps);
  }

  g_debug ("element_list1 -> ");
  debug_pipeline (codec_cap->element_list1);
  g_debug ("element_list2 -> ");
  debug_pipeline (codec_cap->element_list2);
}

#endif

static void
codec_cap_free (CodecCap *codec_cap)
{
  GList *walk;

  if (codec_cap->caps)
  {
    gst_caps_unref (codec_cap->caps);
  }
  if (codec_cap->rtp_caps)
  {
    gst_caps_unref (codec_cap->rtp_caps);
  }

  for (walk = codec_cap->element_list1; walk; walk = g_list_next (walk))
  {
    if (walk->data)
    {
      gst_object_unref ((GstElementFactory *)walk->data);
    }
  }
  for (walk = codec_cap->element_list2; walk; walk = g_list_next (walk))
  {
    if (walk->data)
    {
      gst_object_unref ((GstElementFactory *)walk->data);
    }
  }

  if (codec_cap->element_list1)
  {
    g_list_free (codec_cap->element_list1);
  }

  if (codec_cap->element_list2)
  {
    g_list_free (codec_cap->element_list2);
  }
  g_free (codec_cap);
}

static void
codec_cap_list_free (GList *list)
{
  GList *mwalk;
  for (mwalk = list; mwalk; mwalk = g_list_next (mwalk))
  {
    codec_cap_free ((CodecCap *)mwalk->data);
  }

  g_list_free (list);
}

/**
 * load_codecs:
 * @media_type: a #FsMediaType
 *
 * find all plugins that follow the pattern:
 * input (microphone) -> N* -> rtp payloader -> network
 * network  -> rtp depayloader -> N* -> output (soundcard)
 * media_type defines if we want audio or video codecs
 *
 * Returns : TRUE if load_codecs suceeded, FALSE otherwsie
 */
gboolean
load_codecs (FsMediaType media_type)
{
  GstCaps *caps;
  GList *recv_list = NULL;
  GList *send_list = NULL;
  gboolean ret;


  codecs_lists_ref[media_type]++;

  /* if already computed just return list */
  if (codecs_lists_ref[media_type] > 1)
    return TRUE;


#if 0
  /* let's not reload if already loaded */
  if (!elem_config)
  {
    elem_config = load_config_file();
  }
#endif

  if (load_codecs_cache(media_type)) {
    g_debug("Loaded codec blueprints from cache file");
    return TRUE;
  }

  /* caps used to find the payloaders and depayloaders based on media type */
  if (media_type == FS_MEDIA_TYPE_AUDIO)
  {
    caps = gst_caps_new_simple ("application/x-rtp",
            "media", G_TYPE_STRING, "audio", NULL);
  }
  else if (media_type == FS_MEDIA_TYPE_VIDEO)
  {
    caps = gst_caps_new_simple ("application/x-rtp",
            "media", G_TYPE_STRING, "video", NULL);
  }
  else
  {
    g_warning ("Invalid media type given to load_codecs");
    codecs_lists_ref[media_type]--;
    return FALSE;
  }

  recv_list = detect_recv_codecs (caps);
  send_list = detect_send_codecs (caps);

  gst_caps_unref (caps);
  /* if we can't send or recv let's just stop here */
  if (!recv_list && !send_list)
  {
    codecs_lists_ref[media_type]--;
    g_warning ("No codecs for media type %s detected",
        fs_media_type_to_string (media_type));

    ret = FALSE;
    goto out;
  }

  ret = create_codec_lists (media_type, recv_list, send_list);

 out:
  if (recv_list)
    codec_cap_list_free (recv_list);
  if (send_list)
    codec_cap_list_free (send_list);

  /* Save the codecs blueprint cache */
  save_codecs_cache(media_type, list_codec_blueprints[media_type]);

  return ret;
}

static gboolean
create_codec_lists (FsMediaType media_type,
    GList *recv_list, GList *send_list)
{
  GList *duplex_list = NULL;
  list_codec_blueprints[media_type] = NULL;

  /* TODO we should support non duplex as well, as in have some caps that are
   * only sendable or only receivable */
  duplex_list = codec_cap_list_intersect (recv_list, send_list);

  if(!duplex_list) {
    g_warning ("There are no send/recv codecs");
    return FALSE;
  }

#if ENABLE_DEBUG_CAPS
  g_debug ("*******Intersection of send_list and recv_list");
  debug_codec_cap_list(duplex_list);
#endif

  duplex_list = remove_dynamic_duplicates (duplex_list);

  if (!duplex_list) {
    g_warning ("Dynamic duplicate removal left us with nothing");
    return FALSE;
  }

  parse_codec_cap_list (duplex_list, media_type);

  codec_cap_list_free (duplex_list);

  return TRUE;
}

/* Check if any of the element factories in the pipeline that
   has the unique property set */
static gint
pipeline_has_unique (GList *list)
{
#if 0
  GList *elem;
  gint val;
  const gchar *name;

  if (!elem_config)
    return 0;
  for (elem = list;
       elem;
       elem = g_list_next (elem)) {
    GstElementFactory *elem_factory = elem->data;
    if (elem_factory == NULL)
      g_error ("NULL factory");
    name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (elem_factory));
    if (name && g_key_file_has_key (elem_config, name, "unique", NULL)) {
      GError *error = NULL;
      val = g_key_file_get_integer (elem_config, name, "unique", &error);

      if (error) {
        g_error_free (error);
        return 0;
      } else {
        return val;
      }
    }
  }
#endif
  return 0;
}


static gboolean
validate_h263_codecs (CodecCap *codec_cap)
{
  /* we assume we have just one structure per caps as it should be */
  GstStructure *media_struct = gst_caps_get_structure (codec_cap->caps, 0);
  const gchar *name = gst_structure_get_name (media_struct);
  const gchar *h263version;
  GstStructure *rtp_struct;
  const gchar *encoding_name;

  /* let's check if it's h263 */
  if (strcmp (name, "video/x-h263"))
  {
    return TRUE;
  }

  h263version = gst_structure_get_string (media_struct, "h263version");
  rtp_struct = gst_caps_get_structure (codec_cap->rtp_caps, 0);
  encoding_name = gst_structure_get_string (rtp_struct, "encoding-name");

  if( !strcmp (h263version, "h263"))
  {
    /* baseline H263 can only be encoding name H263 or H263-1998 */
    if (strcmp (encoding_name, "H263") &&
        strcmp (encoding_name, "H263-1998"))
    {
      return FALSE;
    }
  }
  else if (!strcmp (h263version, "h263p"))
  {
    /* has to be H263-1998 */
    if (strcmp (encoding_name, "H263-1998"))
    {
      return FALSE;
    }
  }
  else if (!strcmp (h263version, "h263pp"))
  {
    /* has to be H263-2000 */
    if (strcmp (encoding_name, "H263-2000"))
    {
      return FALSE;
    }
  }

  /* if no h263version specified, we assume it's all h263 versions */

  return TRUE;
}


static gboolean
validate_amr_codecs (CodecCap *codec_cap)
{
  /* we assume we have just one structure per caps as it should be */
  GstStructure *media_struct = gst_caps_get_structure (codec_cap->caps, 0);
  const gchar *name = gst_structure_get_name (media_struct);
  GstStructure *rtp_struct;
  const gchar *encoding_name;

  rtp_struct = gst_caps_get_structure (codec_cap->rtp_caps, 0);
  encoding_name = gst_structure_get_string (rtp_struct, "encoding-name");


  /* let's check if it's AMRWB */
  if (!strcmp (name, "audio/AMR-WB"))
  {
    if (!strcmp (encoding_name, "AMR-WB"))
      return TRUE;
    else
      return FALSE;
  }
  else if (!strcmp (name, "audio/AMR"))
  {
    if (!strcmp (encoding_name, "AMR"))
      return TRUE;
    else
      return FALSE;
  }

  /* Everything else is invalid */

  return TRUE;
}

/* Removes all dynamic pts that already have a static pt in the list */
static GList *
remove_dynamic_duplicates (GList *list)
{
  GList *walk1, *walk2;
  CodecCap *codec_cap, *cur_codec_cap;
  GstStructure *rtp_struct, *cur_rtp_struct;
  const gchar *encoding_name, *cur_encoding_name;

  for (walk1 = list; walk1; walk1 = g_list_next (walk1))
  {
    const GValue *value;
    GType type;

    codec_cap = (CodecCap *)(walk1->data);
    rtp_struct = gst_caps_get_structure (codec_cap->rtp_caps, 0);
    encoding_name = gst_structure_get_string (rtp_struct, "encoding-name");
    if (!encoding_name)
      continue;

    /* let's skip all non static payload types */
    value = gst_structure_get_value (rtp_struct, "payload");
    type = G_VALUE_TYPE (value);
    if (type != G_TYPE_INT)
    {
      continue;
    }
    else
    {
      gint payload_type;
      payload_type = g_value_get_int (value);
      if (payload_type >= 96)
      {
        continue;
      }
    }

    for (walk2 = list; walk2; walk2 = g_list_next (walk2))
    {
      cur_codec_cap = (CodecCap *)(walk2->data);
      cur_rtp_struct = gst_caps_get_structure (cur_codec_cap->rtp_caps, 0);
      cur_encoding_name =
        gst_structure_get_string (cur_rtp_struct, "encoding-name");
      if (!cur_encoding_name)
        continue;
      if (g_ascii_strcasecmp (encoding_name, cur_encoding_name) == 0)
      {
        const GValue *value = gst_structure_get_value (cur_rtp_struct, "payload");
        GType type = G_VALUE_TYPE (value);
        /* this is a dynamic pt that has a static one , let's remove it */
        if (type == GST_TYPE_INT_RANGE)
        {
          list = g_list_remove (list, cur_codec_cap);
          codec_cap_free (cur_codec_cap);
        }
      }
    }
  }

  return list;
}

/* insert given codec_cap list into list_codecs and list_codec_blueprints */
static void
parse_codec_cap_list (GList *list, FsMediaType media_type)
{
  GList *walk;
  CodecCap *codec_cap;
  FsCodec *codec;
  CodecBlueprint *codec_blueprint;
  gint i;
  GList *w;

  /* go thru all common caps */
  for (walk = list; walk; walk = g_list_next (walk))
  {
    codec_cap = (CodecCap *)(walk->data);

    codec = g_new0 (FsCodec, 1);
    codec->id = FS_CODEC_ID_ANY;
    codec->clock_rate = 0;

    for (i = 0; i < gst_caps_get_size (codec_cap->rtp_caps); i++)
    {
      GstStructure *structure = gst_caps_get_structure (codec_cap->rtp_caps, i);

      gst_structure_foreach (structure, extract_field_data,
            (gpointer) codec);
    }

    if (!codec->encoding_name)
    {
      GstStructure *caps = gst_caps_get_structure (codec_cap->rtp_caps, 0);
      const gchar *encoding_name = codec->encoding_name ? codec->encoding_name
        : gst_structure_get_string (caps, "encoding-name");

      g_debug ("skipping codec %s/%s, no encoding name specified"
          " (pt: %d clock_rate:%u",
          media_type == FS_MEDIA_TYPE_AUDIO ? "audio" : "video",
          encoding_name ? encoding_name : "unknown", codec->id,
          codec->clock_rate);

      encoding_name = NULL;
      fs_codec_destroy (codec);
      continue;
    }

    switch (codec->media_type) {
      case FS_MEDIA_TYPE_VIDEO:
        if (!validate_h263_codecs (codec_cap)) {
          fs_codec_destroy (codec);
          continue;
        }
        break;
      case FS_MEDIA_TYPE_AUDIO:
        if (!validate_amr_codecs (codec_cap)) {
          fs_codec_destroy (codec);
          continue;
        }
        break;
      default:
        break;
    }

  another:

    codec_blueprint = g_new0 (CodecBlueprint, 1);
    codec_blueprint->codec = codec;
    codec_blueprint->media_caps = gst_caps_copy (codec_cap->caps);
    codec_blueprint->rtp_caps = gst_caps_copy (codec_cap->rtp_caps);

    codec_blueprint->send_pipeline_factory =
      g_list_copy (codec_cap->element_list2);
    for (w = codec_blueprint->send_pipeline_factory; w; w = g_list_next (w))
    {
      gst_object_ref (GST_OBJECT (w->data));
    }
    codec_blueprint->receive_pipeline_factory =
      g_list_copy (codec_cap->element_list1);
    for (w = codec_blueprint->receive_pipeline_factory; w; w = g_list_next (w))
    {
      gst_object_ref (GST_OBJECT (w->data));
    }

    codec_blueprint->has_sink =
      check_for_sink (codec_blueprint->receive_pipeline_factory);
    codec_blueprint->has_src =
      check_for_src (codec_blueprint->send_pipeline_factory);

    codec_blueprint->send_has_unique = pipeline_has_unique (
        codec_blueprint->send_pipeline_factory);
    codec_blueprint->receive_has_unique = pipeline_has_unique (
        codec_blueprint->receive_pipeline_factory);

    /* insert new information into tables */
    list_codec_blueprints[media_type] = g_list_append (
        list_codec_blueprints[media_type], codec_blueprint);
    g_debug ("adding codec %s with pt %d, send_pipeline %p, receive_pipeline %p",
        codec->encoding_name, codec->id,
        codec_blueprint->send_pipeline_factory,
        codec_blueprint->receive_pipeline_factory);
    debug_pipeline (codec_blueprint->send_pipeline_factory);
    debug_pipeline (codec_blueprint->receive_pipeline_factory);

    if (!g_ascii_strcasecmp (codec->encoding_name, "H263-1998")) {
      codec = fs_codec_copy (codec);
      g_free (codec->encoding_name);
      codec->encoding_name = g_strdup ("H263-N800");
      goto another;
    }
  }
}


static gboolean
klass_contains (const gchar *klass, const gchar *needle)
{
  gchar *found = strstr (klass, needle);

  if(!found)
    return FALSE;
  if (found != klass && *(found-1) != '/')
    return FALSE;
  if (found[strlen (needle)] != 0 &&
      found[strlen (needle)] != '/')
    return FALSE;
  return TRUE;
}

static gboolean
is_payloader (GstElementFactory *factory)
{
  const gchar *klass = gst_element_factory_get_klass (factory);
  return (klass_contains (klass, "Payloader") &&
          klass_contains (klass, "Network"));
}

static gboolean
is_depayloader (GstElementFactory *factory)
{
  const gchar *klass = gst_element_factory_get_klass (factory);
  return (klass_contains (klass, "Network") &&
          (klass_contains (klass, "Depayloader") ||
           klass_contains (klass, "Depayr")));
}

static gboolean
is_encoder (GstElementFactory *factory)
{
  const gchar *klass = gst_element_factory_get_klass (factory);
  /* we might have some sources that provide a non raw stream */
  return (klass_contains (klass, "Encoder") ||
          klass_contains (klass, "Source"));
}

static gboolean
is_decoder (GstElementFactory *factory)
{
  const gchar *klass = gst_element_factory_get_klass (factory);
  /* we might have some sinks that provide decoding */
  return (klass_contains (klass, "Decoder") ||
          klass_contains (klass, "Sink"));
}


/* find all encoder/payloader combos and build list for them */
static GList *
detect_send_codecs (GstCaps *caps)
{
  GList *payloaders, *encoders;
  GList *send_list = NULL;

  /* find all payloader caps. All payloaders should be from klass
   * Codec/Payloader/Network and have as output a data of the mimetype
   * application/x-rtp */
  payloaders = get_plugins_filtered_from_caps (is_payloader, caps, GST_PAD_SINK);

  /* no payloader found. giving up */
  if (!payloaders)
  {
    g_warning ("No RTP Payloaders found");
    return NULL;
  }
#if ENABLE_DEBUG_CAPS
  else {
    g_debug ("**Payloaders");
    debug_codec_cap_list(payloaders);
  }
#endif

  /* find all encoders based on is_encoder filter */
  encoders = get_plugins_filtered_from_caps (is_encoder, NULL, GST_PAD_SRC);
  if (!encoders)
  {
    codec_cap_list_free (payloaders);
    g_warning ("No encoders found");
    return NULL;
  }
#if ENABLE_DEBUG_CAPS
  else {
    g_debug ("**Encoders");
    debug_codec_cap_list(encoders);
  }
#endif

  /* create intersection list of codecs common
   * to encoders and payloaders lists */
  send_list = codec_cap_list_intersect (payloaders, encoders);

  if (!send_list)
  {
    g_warning ("No compatible encoder/payloader pairs found");
  }
#if ENABLE_DEBUG_CAPS
  else {
    g_debug ("**intersection of payloaders and encoders");
    debug_codec_cap_list(send_list);
  }
#endif


  codec_cap_list_free (payloaders);
  codec_cap_list_free (encoders);

  return send_list;
}

/* find all decoder/depayloader combos and build list for them */
static GList *
detect_recv_codecs (GstCaps *caps)
{
  GList *depayloaders, *decoders;
  GList *recv_list = NULL;

  /* find all depayloader caps. All depayloaders should be from klass
   * Codec/Depayr/Network and have as input a data of the mimetype
   * application/x-rtp */
  depayloaders = get_plugins_filtered_from_caps (is_depayloader, caps,
      GST_PAD_SRC);

  /* no depayloader found. giving up */
  if (!depayloaders)
  {
    g_warning ("No RTP Depayloaders found");
    return NULL;
  }
#if ENABLE_DEBUG_CAPS
  else {
    g_debug ("**Depayloaders");
    debug_codec_cap_list(depayloaders);
  }
#endif

  /* find all decoders based on is_decoder filter */
  decoders = get_plugins_filtered_from_caps (is_decoder, NULL, GST_PAD_SINK);

  if (!decoders)
  {
    codec_cap_list_free (depayloaders);
    g_warning ("No decoders found");
    return NULL;
  }
#if ENABLE_DEBUG_CAPS
  else {
    g_debug ("**Decoders");
    debug_codec_cap_list(decoders);
  }
#endif

  /* create intersection list of codecs common
   * to decoders and depayloaders lists */
  recv_list = codec_cap_list_intersect (depayloaders, decoders);

  if (!recv_list)
  {
    g_warning ("No compatible decoder/depayloader pairs found");
  }
#if ENABLE_DEBUG_CAPS
  else {
    g_debug ("**intersection of depayloaders and decoders");
    debug_codec_cap_list(recv_list);
  }
#endif

  codec_cap_list_free (depayloaders);
  codec_cap_list_free (decoders);

  return recv_list;
}

/* returns the intersection of two lists */
static GList *
codec_cap_list_intersect (GList *list1, GList *list2)
{
  GList *walk1, *walk2;
  CodecCap *codec_cap1, *codec_cap2;
  GstCaps *caps1, *caps2;
  GstCaps *rtp_caps1, *rtp_caps2;
  GList *intersection_list = NULL;

  for (walk1 = list1; walk1; walk1 = g_list_next (walk1))
  {
    codec_cap1 = (CodecCap *)(walk1->data);
    caps1 = codec_cap1->caps;
    rtp_caps1 = codec_cap1->rtp_caps;
    for (walk2 = list2; walk2; walk2 = g_list_next (walk2))
    {
      GstCaps *intersection;
      GstCaps *rtp_intersection = NULL;

      codec_cap2 = (CodecCap *)(walk2->data);
      caps2 = codec_cap2->caps;
      rtp_caps2 = codec_cap2->rtp_caps;

      //g_debug ("intersecting %s AND %s", gst_caps_to_string (caps1), gst_caps_to_string (caps2));
      intersection = gst_caps_intersect (caps1, caps2);
      if (rtp_caps1 && rtp_caps2)
      {
        //g_debug ("RTP intersecting %s AND %s", gst_caps_to_string (rtp_caps1), gst_caps_to_string (rtp_caps2));
        rtp_intersection = gst_caps_intersect (rtp_caps1, rtp_caps2);
      }
      if (!gst_caps_is_empty (intersection) &&
          (rtp_intersection == NULL || !gst_caps_is_empty (rtp_intersection)))
      {
        CodecCap *item = g_new0 (CodecCap, 1);
        GList *swalk;
        item->caps = intersection;

        if (rtp_caps1 && rtp_caps2)
        {
          item->rtp_caps = rtp_intersection;
        }
        else if (rtp_caps1)
        {
          item->rtp_caps = rtp_caps1;
          gst_caps_ref (rtp_caps1);
        }
        else if (rtp_caps2)
        {
          item->rtp_caps = rtp_caps2;
          gst_caps_ref (rtp_caps2);
        }

        /* during an intersect, we concat/copy previous lists together and put them
         * into 1 and 2 */
        for (swalk = codec_cap1->element_list1; swalk;
            swalk = g_list_next(swalk))
        {
          item->element_list1 = g_list_prepend (item->element_list1,
              gst_object_ref(swalk->data));
        }
        for (swalk = codec_cap1->element_list2; swalk;
            swalk = g_list_next(swalk))
        {
          item->element_list1 = g_list_prepend (item->element_list1,
              gst_object_ref(swalk->data));
        }
        for (swalk = codec_cap2->element_list1; swalk;
            swalk = g_list_next(swalk))
        {
          item->element_list2 = g_list_prepend (item->element_list2,
              gst_object_ref(swalk->data));
        }
        for (swalk = codec_cap2->element_list2; swalk;
            swalk = g_list_next(swalk))
        {
          item->element_list2 = g_list_prepend (item->element_list2,
              gst_object_ref(swalk->data));
        }

        intersection_list = g_list_prepend (intersection_list, item);
        break;
      }
      gst_caps_unref (intersection);
      if (rtp_intersection)
        gst_caps_unref (rtp_intersection);
    }
  }

  return intersection_list;
}


void
codec_blueprint_destroy (CodecBlueprint *codec_blueprint)
{
  GList *walk;

  if (codec_blueprint->codec)
  {
    fs_codec_destroy (codec_blueprint->codec);
  }

  if (codec_blueprint->media_caps)
  {
    gst_caps_unref (codec_blueprint->media_caps);
  }

  if (codec_blueprint->rtp_caps)
  {
    gst_caps_unref (codec_blueprint->rtp_caps);
  }

  for (walk = codec_blueprint->send_pipeline_factory;
      walk; walk = g_list_next (walk))
  {
    if (walk->data)
    {
      gst_object_unref ((GstElementFactory *)walk->data);
    }
  }
  for (walk = codec_blueprint->receive_pipeline_factory;
      walk; walk = g_list_next (walk))
  {
    if (walk->data)
    {
      gst_object_unref ((GstElementFactory *)walk->data);
    }
  }
  g_list_free (codec_blueprint->send_pipeline_factory);
  g_list_free (codec_blueprint->receive_pipeline_factory);


  g_free (codec_blueprint);
}

void
unload_codecs (FsMediaType media_type)
{
  codecs_lists_ref[media_type]--;
  if (!codecs_lists_ref[media_type])
  {
    if (list_codec_blueprints[media_type])
    {
      GList *item;
      for (item = list_codec_blueprints[media_type];
           item;
           item = g_list_next (item)) {
        codec_blueprint_destroy (item->data);
      }
      g_list_free (list_codec_blueprints[media_type]);
      list_codec_blueprints[media_type] = NULL;
    }
  }
}


/* check if caps are found on given element */
static gboolean
check_caps_compatibility (GstElementFactory *factory,
                          GstCaps *caps, GstCaps **matched_caps)
{
  const GList *pads;
  GstStaticPadTemplate *padtemplate;
  GstCaps *padtemplate_caps = NULL;

  if (!factory->numpadtemplates)
  {
    return FALSE;
  }

  pads = factory->staticpadtemplates;
  while (pads)
  {
    padtemplate = (GstStaticPadTemplate *) (pads->data);
    pads = g_list_next (pads);

    padtemplate_caps = gst_static_caps_get (&padtemplate->static_caps);
    if (gst_caps_is_any (padtemplate_caps))
    {
      goto next;
    }

    if (caps)
    {
      GstCaps *intersection = gst_caps_intersect (padtemplate_caps, caps);
      gboolean have_intersection = !gst_caps_is_empty (intersection);

      if (have_intersection)
      {
        *matched_caps = intersection;
        gst_caps_unref (padtemplate_caps);
        return TRUE;
      }

      gst_caps_unref (intersection);
    }

next:
    if (padtemplate_caps)
    {
      gst_caps_unref (padtemplate_caps);
    }
  }

  *matched_caps = NULL;
  return FALSE;
}


/* GCompareFunc for list_find_custom */
/* compares caps and returns 0 if they intersect */
static gint
compare_media_caps (gconstpointer a, gconstpointer b)
{
  CodecCap *element = (CodecCap *)a;
  GstCaps *c_caps = (GstCaps *)b;
  GstCaps *intersect = gst_caps_intersect (element->caps, c_caps);
  if (!gst_caps_is_empty (intersect))
  {
    /* found */
    gst_caps_unref (intersect);
    return 0;
  }
  else
  {
    /* not found */
    gst_caps_unref (intersect);
    return 1;
  }
}

static gint
compare_rtp_caps (gconstpointer a, gconstpointer b)
{
  CodecCap *element = (CodecCap *)a;
  GstCaps *c_caps = (GstCaps *)b;
  GstCaps *intersect = gst_caps_intersect (element->rtp_caps, c_caps);
  if (!gst_caps_is_empty (intersect))
  {
    /* found */
    gst_caps_unref (intersect);
    return 0;
  }
  else
  {
    /* not found */
    gst_caps_unref (intersect);
    return 1;
  }
}


/* adds the given element to a list of CodecCap */
/* if element has several caps, several CodecCap elements will be added */
/* if element caps already in list, will make sure Transform elements have
 * priority and replace old ones */
static GList *
create_codec_cap_list (GstElementFactory *factory,
                       GstPadDirection direction,
                       GList *list,
                       GstCaps *rtp_caps)
{
  const GList *pads = factory->staticpadtemplates;
  gint i;


  /* Let us look at each pad for stuff to add*/
  while (pads)
  {
    GstCaps *caps = NULL;
    GstStaticPadTemplate *padtemplate = NULL;

    padtemplate = (GstStaticPadTemplate *) (pads->data);
    pads = g_list_next (pads);

    if (padtemplate->direction != direction)
      continue;

    if (GST_PAD_TEMPLATE_PRESENCE(padtemplate) != GST_PAD_ALWAYS) {
      continue;
    }

    caps = gst_static_caps_get (&padtemplate->static_caps);
    /*
      DEBUG ("%s caps are %s", gst_plugin_feature_get_name (GST_PLUGIN_FEATURE
      (factory)), gst_caps_to_string (caps));
    */

    /* skips caps ANY */
    if (!caps || gst_caps_is_any (caps))
    {
      goto done;
    }

    /* let us add one entry to the list per media type */
    for (i = 0; i < gst_caps_get_size (caps); i++)
    {
      CodecCap *entry = NULL;
      GList *found_item = NULL;
      GstStructure *structure = gst_caps_get_structure (caps, i);
      GstCaps *cur_caps =
          gst_caps_new_full (gst_structure_copy (structure), NULL);

      /* FIXME fix this in gstreamer! The rtpdepay element is bogus, it claims to
       * be a depayloader yet has application/x-rtp on both sides and does
       * absolutely nothing */
      /* Let's check if media caps are really media caps, this is to deal with
       * wierd elements such as rtpdepay that says it's a depayloader but has
       * application/x-rtp on src and sink pads */
      const gchar *name = gst_structure_get_name (structure);
      if (g_ascii_strcasecmp (name, "application/x-rtp") == 0)
      {
        g_debug ("skipping %s", gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
        continue;
      }

      /* let's check if this caps is already in the list, if so let's replace
       * that CodecCap list instead of creating a new one */
      /* we need to compare both media caps and rtp caps */
      found_item = g_list_find_custom (list, cur_caps,
          (GCompareFunc)compare_media_caps);
      if (found_item)
      {
        entry = (CodecCap *)found_item->data;
        /* if RTP caps exist and don't match nullify entry */
        if (rtp_caps && compare_rtp_caps (found_item->data, rtp_caps))
        {
          entry = NULL;
        }
      }

      if (!entry)
      {
        entry = g_new0 (CodecCap, 1);

        entry->caps = cur_caps;
        if (rtp_caps)
        {
          entry->rtp_caps = rtp_caps;
          gst_caps_ref (rtp_caps);
        }
        list = g_list_prepend (list, entry);
        entry->element_list1 = g_list_prepend (NULL, factory);
        gst_object_ref (factory);
      }
      else
      {
        const gchar *name;

        gst_caps_unref (cur_caps);
        /* FIXME Should we unionize RTP info from several elements even if they
         * will not be used? For now won't do it... */

        /* already exists, let's check if this element is in our config file, if
         * so give it priority */
        name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory));
#if 0
        if (elem_config &&
            g_key_file_get_boolean (elem_config, name, "prioritize", NULL))
        {
          /* should be only one element, let's free it */
          if (entry->element_list1)
          {
            gst_object_unref (entry->element_list1->data);
            g_list_free (entry->element_list1);
          }
          entry->element_list1 = g_list_prepend (NULL, factory);
          gst_object_ref (factory);
        }
        else
#endif
        {
          const gchar *prev_klass, *cur_klass, *prev_name;

          /* if previous element is a Source/Sink and current element is not,
           * and if the previous element has no priority, let's replace the
           * previous element with the current element */
          GstElementFactory *prev_factory = NULL;
          if (entry->element_list1)
          {
            prev_factory = entry->element_list1->data;
          }

          if (!prev_factory)
          {
            continue;
          }

          prev_klass = gst_element_factory_get_klass(prev_factory);
          if (g_strrstr (prev_klass, "Sink") == NULL &&
              g_strrstr (prev_klass, "Source") == NULL)
          {
            /* previous element is not a sink/source, don't do anything */
            continue;
          }

          cur_klass = gst_element_factory_get_klass (factory);
          if (g_strrstr (cur_klass, "Sink") != NULL ||
              g_strrstr (cur_klass, "Source") != NULL)
          {
            /* current element is a sink/source, don't do anything */
            continue;
          }

          prev_name = gst_plugin_feature_get_name
              (GST_PLUGIN_FEATURE (prev_factory));
#if 0
          if (elem_config &&
              !g_key_file_get_boolean (elem_config, prev_name, "prioritize",
                  NULL))
          {
            /* previous element is not prioritised, make the switch */
            gst_object_unref (prev_factory);
            g_list_free (entry->element_list1);
            entry->element_list1 = g_list_prepend (NULL, factory);
            gst_object_ref (factory);
          }
#endif
        }
      }
    }
  done:
    if (caps != NULL) {
      gst_caps_unref (caps);
    }

  }

  return list;
}


/* function used to sort element features */
/* Copy-pasted from decodebin */
static gint
compare_ranks (GstPluginFeature * f1, GstPluginFeature * f2)
{
  gint diff;
  const gchar *rname1, *rname2;

  diff =  gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;

  rname1 = gst_plugin_feature_get_name (f1);
  rname2 = gst_plugin_feature_get_name (f2);

  diff = strcmp (rname2, rname1);

  return diff;
}


/* creates/returns a list of CodecCap based on given filter function and caps */
static GList *
get_plugins_filtered_from_caps (FilterFunc filter,
                                GstCaps *caps,
                                GstPadDirection direction)
{
  GList *walk, *result;
  GstElementFactory *factory;
  GList *list = NULL;
  gboolean is_valid;
  GstCaps *matched_caps = NULL;

  result = gst_registry_get_feature_list (gst_registry_get_default (),
          GST_TYPE_ELEMENT_FACTORY);

  result = g_list_sort (result, (GCompareFunc) compare_ranks);

  walk = result;
  while (walk)
  {
    factory = GST_ELEMENT_FACTORY (walk->data);
    is_valid = FALSE;

    if (!filter (factory))
    {
      goto next;
    }

    if (caps)
    {
      if (check_caps_compatibility (factory, caps, &matched_caps))
      {
        is_valid = TRUE;
      }
    }

    if (is_valid || !caps)
    {
      if (!matched_caps)
      {
        list = create_codec_cap_list (factory, direction, list, NULL);
      }
      else
      {
        gint i;
        for (i = 0; i < gst_caps_get_size (matched_caps); i++)
        {
          GstStructure *structure = gst_caps_get_structure (matched_caps, i);
          GstCaps *cur_caps =
            gst_caps_new_full (gst_structure_copy (structure), NULL);

          list = create_codec_cap_list (factory, direction, list, cur_caps);
          gst_caps_unref (cur_caps);
        }
        gst_caps_unref (matched_caps);
      }
    }

next:
    walk = g_list_next (walk);
  }

  /*
  walk = result;
  while (walk)
  {
    factory = GST_ELEMENT_FACTORY (walk->data);
    DEBUG ("new refcnt is %d", GST_OBJECT_REFCOUNT_VALUE (GST_OBJECT (factory)));
    walk = g_list_next (walk);
  }
  */

  gst_plugin_feature_list_free (result);

  return list;
}

/**
 *  fill FarsightCodec fields based on payloader capabilities
 *  TODO: optimise using quarks
 */
static gboolean
extract_field_data (GQuark field_id,
                    const GValue *value,
                    gpointer user_data)
{
  /* TODO : This can be called several times from different rtp caps for the
   * same codec, it would be good to make sure any duplicate values are the
   * same, if not then we have several rtp elements that are giving different
   * caps information, therefore they need to be fixed */

  FsCodec *codec = (FsCodec *) user_data;
  GType type = G_VALUE_TYPE (value);
  const gchar *field_name = g_quark_to_string (field_id);
  const gchar *tmp;

  if (0 == strcmp (field_name, "media"))
  {
    if (type != G_TYPE_STRING)
    {
      return FALSE;
    }
    tmp = g_value_get_string (value);
    if (strcmp (tmp, "audio") == 0)
    {
      codec->media_type = FS_MEDIA_TYPE_AUDIO;
    }
    else if (strcmp (tmp, "video") == 0)
    {
      codec->media_type = FS_MEDIA_TYPE_VIDEO;
    }

  }
  else if (0 == strcmp (field_name, "payload"))
  {
    if (type == GST_TYPE_INT_RANGE)
    {
      if (gst_value_get_int_range_min (value) < 96 ||
          gst_value_get_int_range_max (value) > 255)
      {
        return FALSE;
      }
    }
    else if (type == G_TYPE_INT)
    {
      int id;
      id = g_value_get_int (value);
      if (id > 96)
      {
        /* Dynamic id that was explicitelly set ?? shouldn't happen */
        return FALSE;
      }
      codec->id = id;
    }
    else
    {
      return FALSE;
    }
  }
  else if (0 == strcmp (field_name, "clock-rate"))
  {
    if (type == GST_TYPE_INT_RANGE)
    {
      /* set to 0, this should be checked by the optional parameters code later
       * in Farsight */
      codec->clock_rate = 0;
      return TRUE;
    }
    else if (type != G_TYPE_INT)
    {
      return FALSE;
    }
    codec->clock_rate = g_value_get_int (value);
  }
  else if (0 == strcmp (field_name, "ssrc") ||
      0 == strcmp (field_name, "clock-base") ||
      0 == strcmp (field_name, "seqnum-base"))
  {
    // ignore these fields for now
    ;
  }
  else if (0 == strcmp (field_name, "encoding-name"))
  {
    if (type != G_TYPE_STRING)
    {
      return FALSE;
    }
    if (!codec->encoding_name)
    {
      codec->encoding_name = g_value_dup_string (value);
    }
  }
  else if (0 == strcmp (field_name, "encoding-params"))
  {
    if (type != G_TYPE_STRING)
    {
      return FALSE;
    }
    codec->channels = (guint) g_ascii_strtoull (
                                       g_value_get_string (value), NULL, 10);
  }
  else
  {
    if (type == G_TYPE_STRING)
    {
      FsCodecParameter *optional_param = g_new0 (FsCodecParameter, 1);

      optional_param->name = g_strdup (field_name);
      optional_param->value = g_strdup (g_value_get_string (value));
      codec->optional_params = g_list_append (codec->optional_params,
          optional_param);
    }
  }

  return TRUE;
}


/* checks if there is a Source element in the give 
 * list of GstElementFactories */
static gboolean
check_for_src (GList *pipeline)
{
  GList *walk;
  const gchar *class;

  for (walk = pipeline; walk; walk = g_list_next (walk))
  {
    class = gst_element_factory_get_klass (GST_ELEMENT_FACTORY (walk->data));
    if (g_strrstr (class, "Source") != NULL)
    {
      return TRUE;
    }
  }
  return FALSE;
}

/* checks if there is a Sink element in the give 
 * list of GstElementFactories */
static gboolean
check_for_sink (GList *pipeline)
{
  GList *walk;
  const gchar *class;

  for (walk = pipeline; walk; walk = g_list_next (walk))
  {
      class = gst_element_factory_get_klass (GST_ELEMENT_FACTORY (walk->data));
      if (g_strrstr (class, "Sink") != NULL)
      {
          return TRUE;
      }
  }
  return FALSE;
}