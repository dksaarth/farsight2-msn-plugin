/*
 * fs-rtp-specific-nego.c - Per-codec SDP negociation
 *
 * Farsight RTP/AVP/SAVP/AVPF Module
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
 *   @author Olivier Crete <olivier.crete@collabora.co.uk>
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

#include <string.h>

#include <glib.h>
#include <gst/gst.h>

#include "fs-rtp-specific-nego.h"

struct SdpCompatCheck {
  FsMediaType media_type;
  const gchar *encoding_name;
  FsCodec * (* sdp_is_compat) (GstCaps *rtp_caps, FsCodec *local_codec,
      FsCodec *remote_codec);
};


static FsCodec *
sdp_is_compat_ilbc (GstCaps *rtp_caps, FsCodec *local_codec,
    FsCodec *remote_codec);
static FsCodec *
sdp_is_compat_h263_1998 (GstCaps *rtp_caps, FsCodec *local_codec,
    FsCodec *remote_codec);

struct SdpCompatCheck sdp_compat_checks[] = {
  {FS_MEDIA_TYPE_AUDIO, "iLBC", sdp_is_compat_ilbc},
  {FS_MEDIA_TYPE_VIDEO, "H263-1998", sdp_is_compat_h263_1998},
  {0, NULL, NULL}
};


static FsCodec *
sdp_is_compat_default (GstCaps *rtp_caps, FsCodec *local_codec,
    FsCodec *remote_codec);

FsCodec *
sdp_is_compat (GstCaps *rtp_caps, FsCodec *local_codec,
    FsCodec *remote_codec)
{
  gint i;

  g_assert (local_codec);
  g_assert (remote_codec);
  g_assert (rtp_caps);

  if (local_codec->media_type != remote_codec->media_type) {
    g_debug ("Wrong media type, local: %s, remote: %s",
        fs_media_type_to_string (local_codec->media_type),
        fs_media_type_to_string (remote_codec->media_type));
    return NULL;
  }
  if (g_ascii_strcasecmp (local_codec->encoding_name,
        remote_codec->encoding_name)) {
    g_debug ("Encoding names dont match, local: %s, remote: %s",
        local_codec->encoding_name, remote_codec->encoding_name);
    return NULL;
  }

  for (i = 0; sdp_compat_checks[i].sdp_is_compat; i++) {
    if (sdp_compat_checks[i].media_type == remote_codec->media_type &&
        !g_ascii_strcasecmp (sdp_compat_checks[i].encoding_name,
            remote_codec->encoding_name)) {
      return sdp_compat_checks[i].sdp_is_compat (rtp_caps, local_codec,
          remote_codec);
    }
  }

  return sdp_is_compat_default (rtp_caps, local_codec, remote_codec);
}

static FsCodec *
sdp_is_compat_default (GstCaps *rtp_caps, FsCodec *local_codec,
    FsCodec *remote_codec)
{
  FsCodec *negotiated_codec = NULL;
  GList *local_param_list = NULL, *negotiated_param_list = NULL;

  g_debug ("Using default codec negotiation function");

  if (remote_codec->clock_rate &&
      local_codec->clock_rate != remote_codec->clock_rate) {
    g_debug ("Clock rates differ local=%u remote=%u", local_codec->clock_rate,
        remote_codec->clock_rate);
    return NULL;
  }

  if (local_codec->channels && remote_codec->channels &&
      local_codec->channels != remote_codec->channels) {
    g_debug ("Channel counts differ local=%u remote=%u", local_codec->channels,
        remote_codec->channels);
    return NULL;
  }

  negotiated_codec = fs_codec_copy (remote_codec);

  /* Lets fix here missing clock rates and channels counts */
  if (negotiated_codec->channels == 0 && local_codec->channels)
    negotiated_codec->channels = local_codec->channels;
  if (negotiated_codec->clock_rate == 0)
    negotiated_codec->clock_rate = local_codec->clock_rate;

  for (local_param_list = local_codec->optional_params;
       local_param_list;
       local_param_list = g_list_next (local_param_list)) {
    FsCodecParameter *local_param = local_param_list->data;

    for (negotiated_param_list = negotiated_codec->optional_params;
         negotiated_param_list;
         negotiated_param_list = g_list_next (negotiated_param_list)) {
      FsCodecParameter *negotiated_param = negotiated_param_list->data;
      if (!g_ascii_strcasecmp (local_param->name, negotiated_param->name)) {
        if (!strcmp (local_param->value, negotiated_param->value)) {
          break;
        } else {
          g_debug ("Different values for %s, local=%s remote=%s",
              local_param->name, local_param->value, negotiated_param->value);
          fs_codec_destroy (negotiated_codec);
          return NULL;
        }
      }
    }

    /* Let's add the local param to the negotiated codec if it does not exist in
     * the remote codec */
    if (!negotiated_param_list) {
      FsCodecParameter *newparam = g_new (FsCodecParameter, 1);
      newparam->name = g_strdup (local_param->name);
      newparam->value = g_strdup (local_param->value);
      negotiated_codec->optional_params = g_list_append (
          negotiated_codec->optional_params, newparam);
    }
  }

  return negotiated_codec;
}

static FsCodec *
sdp_is_compat_ilbc (GstCaps *rtp_caps, FsCodec *local_codec,
    FsCodec *remote_codec)
{
  FsCodec *negotiated_codec = NULL;
  GList *mylistitem = NULL, *negotiated_param_list = NULL;
  gboolean has_mode = FALSE;

  g_debug ("Using ilbc negotiation function");

  if (remote_codec->clock_rate &&
      local_codec->clock_rate != remote_codec->clock_rate) {
    g_debug ("Clock rates differ local=%u remote=%u", local_codec->clock_rate,
        remote_codec->clock_rate);
    return NULL;
  }

  if (local_codec->channels && remote_codec->channels &&
      local_codec->channels != remote_codec->channels) {
    g_debug ("Channel counts differ local=%u remote=%u", local_codec->channels,
        remote_codec->channels);
    return NULL;
  }

  negotiated_codec = fs_codec_copy (remote_codec);

  /* Lets fix here missing clock rates and channels counts */
  if (negotiated_codec->channels == 0 && local_codec->channels)
    negotiated_codec->channels = local_codec->channels;
  if (negotiated_codec->clock_rate == 0)
    negotiated_codec->clock_rate = local_codec->clock_rate;

  for (mylistitem = local_codec->optional_params;
       mylistitem;
       mylistitem = g_list_next (mylistitem)) {
    FsCodecParameter *local_param = mylistitem->data;

    for (negotiated_param_list = negotiated_codec->optional_params;
         negotiated_param_list;
         negotiated_param_list = g_list_next (negotiated_param_list)) {
      FsCodecParameter *negotiated_param = negotiated_param_list->data;

      if (!g_ascii_strcasecmp (local_param->name, negotiated_param->name)) {
        if (!g_ascii_strcasecmp (local_param->name, "mode")) {
          gint local_mode = atoi (local_param->value);
          gint remote_mode = atoi (negotiated_param->value);

          has_mode = TRUE;

          if (remote_mode != 20 && remote_mode != 30) {
            g_debug ("Invalid mode on ilbc");
            goto failure;
          }
          if (local_mode != remote_mode) {
            g_free (negotiated_param->value);
            negotiated_param->value = g_strdup ("30");
            break;
          }
        } else {
          if (!strcmp (local_param->value, negotiated_param->value)) {
            break;
          } else {
            g_debug ("Different values for %s, local=%s remote=%s",
                local_param->name, local_param->value, negotiated_param->value);
            goto failure;
          }
        }
      }
    }

    /* Let's add the local param to the negotiated codec if it does not exist in
     * the remote codec */
    if (!negotiated_param_list) {
      FsCodecParameter *newparam = g_new (FsCodecParameter, 1);
      newparam->name = g_strdup (local_param->name);
      newparam->value = g_strdup (local_param->value);
      negotiated_codec->optional_params = g_list_append (
          negotiated_codec->optional_params, newparam);

      if (!g_ascii_strcasecmp (local_param->name, "mode")) {
        has_mode = TRUE;
      }
    }
  }

  /* If more has not be found in the local codec, let's check if it's in the
   * remote codec */
  if (!has_mode) {
    for (negotiated_param_list = negotiated_codec->optional_params;
         negotiated_param_list;
         negotiated_param_list = g_list_next (negotiated_param_list)) {
      FsCodecParameter *negotiated_param = negotiated_param_list->data;
      if (!g_ascii_strcasecmp (negotiated_param->name, "mode")) {
        has_mode = TRUE;
        break;
      }
    }
  }

  /* If we still can't find the mode anywhere, let's add it since it's mandatory
   * and use default value of 30 ms */
  if (!has_mode) {
    FsCodecParameter *newparam = g_new0 (FsCodecParameter, 1);
    newparam->name = g_strdup ("mode");
    newparam->value = g_strdup ("30");
    negotiated_codec->optional_params = g_list_append (
        negotiated_codec->optional_params, newparam);
  }

  return negotiated_codec;

 failure:
  if (negotiated_codec)
    fs_codec_destroy (negotiated_codec);
  return NULL;

}



static FsCodec *
sdp_is_compat_h263_1998 (GstCaps *rtp_caps, FsCodec *local_codec,
    FsCodec *remote_codec)
{
  FsCodec *negotiated_codec = NULL;
  GList *mylistitem = NULL, *remote_param_list = NULL;
  FsCodecParameter *profile = NULL;

  g_debug ("Using H263-1998 negotiation function");

  if (remote_codec->clock_rate != 90000) {
    g_debug ("Remote clock rate is %d which is not 90000",
        remote_codec->clock_rate);
    return NULL;
  }


  if (remote_codec->channels > 1) {
    g_debug ("Channel count  %d > 1", remote_codec->channels);
    return NULL;
  }

  /* First lets check if there is a profile */

  for (remote_param_list = remote_codec->optional_params;
       remote_param_list;
       remote_param_list = g_list_next (remote_param_list)) {
    FsCodecParameter *remote_param = remote_param_list->data;

    if (!g_ascii_strcasecmp (remote_param->name, "profile")) {

      if (profile) {
        g_debug ("The remote codecs contain the profile item more than once, ignoring");
        return NULL;
      } else {
        profile = remote_param;
      }

      for (mylistitem = local_codec->optional_params;
           mylistitem;
           mylistitem = g_list_next (mylistitem)) {
        FsCodecParameter *local_param = mylistitem->data;

        if (!g_ascii_strcasecmp (local_param->name, "profile")) {

          if (g_ascii_strcasecmp (local_param->value, remote_param->value)) {
            g_debug ("Local (%s) and remote (%s) profiles are different",
                local_param->value, remote_param->value);
            return NULL;
          } else {
            g_debug ("We have the same profile, lets return our local codec");

            negotiated_codec = fs_codec_copy (local_codec);

            negotiated_codec->id = remote_codec->id;

            return negotiated_codec;
          }
        }
      }
        g_debug ("Profile (%s) is unknown locally, rejecting",
            remote_param->value);
            return NULL;
    }
  }


  negotiated_codec = fs_codec_copy (local_codec);
  return negotiated_codec;
}