/* Farsigh2 generic unit tests for transmitters
 *
 * Copyright (C) 2007 Collabora, Nokia
 * @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#include <gst/gst.h>
#include <gst/farsight/fs-transmitter.h>

#ifndef __GENERIC_H__
#define  __GENERIC_H__

GstElement *setup_pipeline (FsTransmitter *trans, GCallback cb);

void setup_fakesrc (FsTransmitter *trans, GstElement *pipeline,
  guint component_id);

void _stream_transmitter_error (FsStreamTransmitter *streamtransmitter,
  gint errorno, gchar *error_msg, gchar *debug_msg, gpointer user_data);


#endif /* __GENERIC_H__ */