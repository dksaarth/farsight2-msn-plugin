/*
 * fs-rtp-specific-nego.h - Per-codec SDP negociation
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

#ifndef __FS_RTP_SPECIFIC_NEGO_H__
#define __FS_RTP_SPECIFIC_NEGO_H__

#include <glib.h>
#include <gst/gst.h>

#include <gst/farsight/fs-codec.h>

G_BEGIN_DECLS

FsCodec *
sdp_is_compat (GstCaps *rtp_caps, FsCodec *local_codec,
    FsCodec *remote_codec);

G_END_DECLS

#endif