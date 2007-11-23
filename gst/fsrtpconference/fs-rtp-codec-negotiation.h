/*
 * Farsight2 - Farsight RTP Codec Negotiation
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-discover-codecs.h - A Farsight RTP Codec Negotiation
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

#ifndef __FS_RTP_CODEC_NEGOTIATION_H__
#define __FS_RTP_CODEC_NEGOTIATION_H__

#include "fs-rtp-discover-codecs.h"

G_BEGIN_DECLS

typedef struct _CodecAssociation {
  CodecBlueprint *blueprint;
  FsCodec *codec;
} CodecAssociation;


GList *validate_codecs_configuration (FsMediaType media_type, GList *blueprints,
  GList *codecs);

GHashTable *create_local_codec_associations (FsMediaType media_type,
  GList *blueprints, GList *codec_prefs, GHashTable *current_codec_associations,
  GList **local_codecs_list);

#if 0

GHashTable *negotiate_codecs (const GList *remote_codecs,
    GHashTable *current_negotiated_codec_associations,
    GHashTable *local_codec_associations, GList *local_codecs,
    GList **new_negotiated_codecs);

CodecAssociation *lookup_codec_association_by_pt (
    GHashTable *codec_associations, gint pt);

#endif


G_END_DECLS

#endif /* __FS_RTP_CODEC_NEGOTIATION_H__ */