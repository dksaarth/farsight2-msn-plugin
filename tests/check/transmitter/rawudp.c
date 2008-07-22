/* Farsight 2 unit tests for FsRawUdpTransmitter
 *
 * Copyright (C) 2007 Collabora, Nokia
 * @author: Olivier Crete <olivier.crete@collabora.co.uk>
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/farsight/fs-transmitter.h>
#include <gst/farsight/fs-conference-iface.h>

#include "check-threadsafe.h"
#include "generic.h"

gint buffer_count[2] = {0, 0};
GMainLoop *loop = NULL;
gint candidates[2] = {0, 0};
GstElement *pipeline = NULL;
gboolean src_setup[2] = {FALSE, FALSE};
volatile gint running = TRUE;

enum {
  FLAG_HAS_STUN = 1 << 0,
  FLAG_IS_LOCAL = 1 << 1
};

#define RTP_PORT 9828
#define RTCP_PORT 9829


GST_START_TEST (test_rawudptransmitter_new)
{
  GError *error = NULL;
  FsTransmitter *trans;
  GstElement *pipeline;
  GstElement *trans_sink, *trans_src;

  trans = fs_transmitter_new ("rawudp", 2, &error);

  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  }

  ts_fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  pipeline = setup_pipeline (trans, NULL);

  g_object_get (trans, "gst-sink", &trans_sink, "gst-src", &trans_src, NULL);

  ts_fail_if (trans_sink == NULL, "Sink is NULL");
  ts_fail_if (trans_src == NULL, "Src is NULL");

  gst_object_unref (trans_sink);
  gst_object_unref (trans_src);

  g_object_unref (trans);

  gst_object_unref (pipeline);

}
GST_END_TEST;

static void
_new_local_candidate (FsStreamTransmitter *st, FsCandidate *candidate,
  gpointer user_data)
{
  gboolean has_stun = GPOINTER_TO_INT (user_data) & FLAG_HAS_STUN;
  gboolean is_local = GPOINTER_TO_INT (user_data) & FLAG_IS_LOCAL;
  GError *error = NULL;
  GList *item = NULL;
  gboolean ret;

  g_debug ("Has local candidate %s:%u of type %d",
    candidate->ip, candidate->port, candidate->type);

  ts_fail_if (candidate == NULL, "Passed NULL candidate");
  ts_fail_unless (candidate->ip != NULL, "Null IP in candidate");
  ts_fail_if (candidate->port == 0, "Candidate has port 0");
  ts_fail_unless (candidate->proto == FS_NETWORK_PROTOCOL_UDP,
    "Protocol is not UDP");

  if (has_stun)
    ts_fail_unless (candidate->type == FS_CANDIDATE_TYPE_SRFLX,
      "Has stun, but candidate is not server reflexive,"
      " it is: %s:%u of type %d on component %u (IGNORE if you are not"
        " connected to the public internet",
      candidate->ip, candidate->port, candidate->type, candidate->component_id);
  else {
    ts_fail_unless (candidate->type == FS_CANDIDATE_TYPE_HOST,
      "Does not have stun, but candidate is not host");
    if (candidate->component_id == FS_COMPONENT_RTP) {
      ts_fail_unless (candidate->port % 2 == 0, "RTP port should be odd");
    } else if (candidate->component_id == FS_COMPONENT_RTCP) {
      ts_fail_unless (candidate->port % 2 == 1, "RTCP port should be event");
    }
  }

  if (is_local) {
    ts_fail_unless (!strcmp (candidate->ip, "127.0.0.1"),
      "IP is wrong, it is %s but should be 127.0.0.1 when local candidate set",
      candidate->ip);

    if (candidate->component_id == FS_COMPONENT_RTP) {
      ts_fail_unless (candidate->port >= RTP_PORT  , "RTP port invalid");
    } else if (candidate->component_id == FS_COMPONENT_RTCP) {
      ts_fail_unless (candidate->port >= RTCP_PORT, "RTCP port invalid");
    }
  }


  candidates[candidate->component_id-1] = 1;

  g_debug ("New local candidate %s:%d of type %d for component %d",
    candidate->ip, candidate->port, candidate->type, candidate->component_id);

  item = g_list_prepend (NULL, candidate);

  ret = fs_stream_transmitter_set_remote_candidates (st, item, &error);

  g_list_free (item);

  if (error)
    ts_fail ("Error while adding candidate: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);

  ts_fail_unless (ret == TRUE, "No detailed error from add_remote_candidate");

}

static void
_local_candidates_prepared (FsStreamTransmitter *st, gpointer user_data)
{
  gboolean has_stun = GPOINTER_TO_INT (user_data) & FLAG_HAS_STUN;

  ts_fail_if (candidates[0] == 0, "candidates-prepared with no RTP candidate");
  ts_fail_if (candidates[1] == 0, "candidates-prepared with no RTCP candidate");

  g_debug ("Local Candidates Prepared");

  /*
   * This doesn't work on my router
   */

  if (has_stun)
  {
    g_main_loop_quit (loop);
    g_atomic_int_set(&running, FALSE);
  }
}


static void
_new_active_candidate_pair (FsStreamTransmitter *st, FsCandidate *local,
  FsCandidate *remote, gpointer user_data)
{
  ts_fail_if (local == NULL, "Local candidate NULL");
  ts_fail_if (remote == NULL, "Remote candidate NULL");

  ts_fail_unless (local->component_id == remote->component_id,
    "Local and remote candidates dont have the same component id");

  g_debug ("New active candidate pair for component %d", local->component_id);

  if (!src_setup[local->component_id-1])
    setup_fakesrc (user_data, pipeline, local->component_id);
  src_setup[local->component_id-1] = TRUE;
}

static void
_handoff_handler (GstElement *element, GstBuffer *buffer, GstPad *pad,
  gpointer user_data)
{
  gint component_id = GPOINTER_TO_INT (user_data);

  ts_fail_unless (GST_BUFFER_SIZE (buffer) == component_id * 10,
    "Buffer is size %d but component_id is %d", GST_BUFFER_SIZE (buffer),
    component_id);

  buffer_count[component_id-1]++;

  /*
  g_debug ("Buffer %d component: %d size: %u", buffer_count[component_id-1],
    component_id, GST_BUFFER_SIZE (buffer));
  */

  ts_fail_if (buffer_count[component_id-1] > 20,
    "Too many buffers %d > 20 for component",
    buffer_count[component_id-1], component_id);

  if (buffer_count[0] == 20 && buffer_count[1] == 20) {
    /* TEST OVER */
    g_atomic_int_set(&running, FALSE);
    g_main_loop_quit (loop);
  }
}

static gboolean
check_running (gpointer data)
{
  if (g_atomic_int_get (&running) == FALSE)
    g_main_loop_quit (loop);

  return FALSE;
}


static void
run_rawudp_transmitter_test (gint n_parameters, GParameter *params,
  gint flags)
{
  GError *error = NULL;
  FsTransmitter *trans;
  FsStreamTransmitter *st;
  GstBus *bus = NULL;

  loop = g_main_loop_new (NULL, FALSE);
  trans = fs_transmitter_new ("rawudp", 2, &error);

  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  }

  ts_fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  pipeline = setup_pipeline (trans, G_CALLBACK (_handoff_handler));

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, bus_error_callback, NULL);
  gst_object_unref (bus);

  st = fs_transmitter_new_stream_transmitter (trans, NULL, n_parameters, params,
    &error);

  if (error) {
    if (flags & FLAG_HAS_STUN &&
        error->domain == FS_ERROR &&
        error->code == FS_ERROR_NETWORK &&
        error->message && strstr (error->message, "unreachable"))
    {
      g_debug ("Skipping stunserver test, we have no network");
      goto skip;
    }
    else
      ts_fail ("Error creating stream transmitter: (%s:%d) %s",
          g_quark_to_string (error->domain), error->code, error->message);
  }

  ts_fail_if (st == NULL, "No stream transmitter created, yet error is NULL");

  ts_fail_unless (g_signal_connect (st, "new-local-candidate",
      G_CALLBACK (_new_local_candidate), GINT_TO_POINTER (flags)),
    "Could not connect new-local-candidate signal");
  ts_fail_unless (g_signal_connect (st, "local-candidates-prepared",
      G_CALLBACK (_local_candidates_prepared), GINT_TO_POINTER (flags)),
    "Could not connect local-candidates-prepared signal");
  ts_fail_unless (g_signal_connect (st, "new-active-candidate-pair",
      G_CALLBACK (_new_active_candidate_pair), trans),
    "Could not connect new-active-candidate-pair signal");
  ts_fail_unless (g_signal_connect (st, "error",
      G_CALLBACK (stream_transmitter_error), NULL),
    "Could not connect error signal");

  ts_fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  if (!fs_stream_transmitter_gather_local_candidates (st, &error))
  {
    if (error)
      ts_fail ("Could not start gathering local candidates %s",
          error->message);
    else
      ts_fail ("Could not start gathering candidates"
          " (without a specified error)");
  }

  g_idle_add (check_running, NULL);

  g_main_run (loop);

 skip:

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (st)
    g_object_unref (st);

  g_object_unref (trans);

  gst_object_unref (pipeline);

  g_main_loop_unref (loop);

}

GST_START_TEST (test_rawudptransmitter_run_nostun)
{
  run_rawudp_transmitter_test (0, NULL, 0);
}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_run_invalid_stun)
{
  GParameter params[3];

  /*
   * Hopefully not one is runing a stun server on local port 7777
   */

  memset (params, 0, sizeof (GParameter) * 3);

  params[0].name = "stun-ip";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_static_string (&params[0].value, "127.0.0.1");

  params[1].name = "stun-port";
  g_value_init (&params[1].value, G_TYPE_UINT);
  g_value_set_uint (&params[1].value, 7777);

  params[2].name = "stun-timeout";
  g_value_init (&params[2].value, G_TYPE_UINT);
  g_value_set_uint (&params[2].value, 3);

  run_rawudp_transmitter_test (3, params, 0);

}
GST_END_TEST;

GST_START_TEST (test_rawudptransmitter_run_stunserver_dot_org)
{
  GParameter params[3];

  memset (params, 0, sizeof (GParameter) * 3);

  params[0].name = "stun-ip";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_static_string (&params[0].value, "192.245.12.229");

  params[1].name = "stun-port";
  g_value_init (&params[1].value, G_TYPE_UINT);
  g_value_set_uint (&params[1].value, 3478);

  params[2].name = "stun-timeout";
  g_value_init (&params[2].value, G_TYPE_UINT);
  g_value_set_uint (&params[2].value, 5);

  run_rawudp_transmitter_test (3, params, FLAG_HAS_STUN);
}
GST_END_TEST;


GST_START_TEST (test_rawudptransmitter_run_local_candidates)
{
  GParameter params[1];
  GList *list = NULL;
  FsCandidate *candidate;

  memset (params, 0, sizeof (GParameter) * 1);

  candidate = fs_candidate_new ("L1",
      FS_COMPONENT_RTP, FS_CANDIDATE_TYPE_HOST,
      FS_NETWORK_PROTOCOL_UDP, "127.0.0.1", RTP_PORT);
  list = g_list_prepend (list, candidate);

  candidate = fs_candidate_new ("L1",
      FS_COMPONENT_RTCP, FS_CANDIDATE_TYPE_HOST,
      FS_NETWORK_PROTOCOL_UDP, "127.0.0.1", RTCP_PORT);
  list = g_list_prepend (list, candidate);

  params[0].name = "preferred-local-candidates";
  g_value_init (&params[0].value, FS_TYPE_CANDIDATE_LIST);
  g_value_set_boxed (&params[0].value, list);

  run_rawudp_transmitter_test (1, params, FLAG_IS_LOCAL);

  g_value_reset (&params[0].value);

  fs_candidate_list_destroy (list);
}
GST_END_TEST;

static gboolean
_bus_stop_stream_cb (GstBus *bus, GstMessage *message, gpointer user_data)
{
  FsStreamTransmitter *st = user_data;
  GstState oldstate, newstate, pending;

  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_STATE_CHANGED ||
      G_OBJECT_TYPE (GST_MESSAGE_SRC (message)) != GST_TYPE_PIPELINE)
    return bus_error_callback (bus, message, user_data);

  gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);

  if (newstate != GST_STATE_PLAYING)
    return TRUE;

  if (pending != GST_STATE_VOID_PENDING)
    ts_fail ("New state playing, but pending is %d", pending);

  g_object_unref (st);

  g_atomic_int_set(&running, FALSE);
  g_main_loop_quit (loop);

  return TRUE;
}

/*
 * This test checks that starting a stream, getting it to playing
 * then stopping it works
 */

GST_START_TEST (test_rawudptransmitter_stop_stream)
{
  GError *error = NULL;
  FsTransmitter *trans;
  FsStreamTransmitter *st;
  GstBus *bus = NULL;

  loop = g_main_loop_new (NULL, FALSE);
  trans = fs_transmitter_new ("rawudp", 2, &error);

  if (error) {
    ts_fail ("Error creating transmitter: (%s:%d) %s",
      g_quark_to_string (error->domain), error->code, error->message);
  }

  ts_fail_if (trans == NULL, "No transmitter create, yet error is still NULL");

  pipeline = setup_pipeline (trans, G_CALLBACK (_handoff_handler));

  st = fs_transmitter_new_stream_transmitter (trans, NULL, 0, NULL, &error);

  if (error)
    ts_fail ("Error creating stream transmitter: (%s:%d) %s",
        g_quark_to_string (error->domain), error->code, error->message);

  ts_fail_if (st == NULL, "No stream transmitter created, yet error is NULL");

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, _bus_stop_stream_cb, st);
  gst_object_unref (bus);

  ts_fail_unless (g_signal_connect (st, "new-local-candidate",
          G_CALLBACK (_new_local_candidate), NULL),
      "Could not connect new-local-candidate signal");
  ts_fail_unless (g_signal_connect (st, "new-active-candidate-pair",
          G_CALLBACK (_new_active_candidate_pair), trans),
      "Could not connect new-active-candidate-pair signal");
  ts_fail_unless (g_signal_connect (st, "error",
          G_CALLBACK (stream_transmitter_error), NULL),
      "Could not connect error signal");

  ts_fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
    GST_STATE_CHANGE_FAILURE, "Could not set the pipeline to playing");

  if (!fs_stream_transmitter_gather_local_candidates (st, &error))
  {
    if (error)
      ts_fail ("Could not start gathering local candidates %s",
          error->message);
    else
      ts_fail ("Could not start gathering candidates"
          " (without a specified error)");
  }

  g_idle_add (check_running, NULL);

  g_main_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  g_object_unref (trans);

  gst_object_unref (pipeline);

  g_main_loop_unref (loop);
}
GST_END_TEST;


static Suite *
rawudptransmitter_suite (void)
{
  Suite *s = suite_create ("rawudptransmitter");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);

  tc_chain = tcase_create ("rawudptransmitter");
  tcase_set_timeout (tc_chain, 5);
  tcase_add_test (tc_chain, test_rawudptransmitter_new);
  tcase_add_test (tc_chain, test_rawudptransmitter_run_nostun);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-stun-timeout");
  tcase_set_timeout (tc_chain, 5);
  tcase_add_test (tc_chain, test_rawudptransmitter_run_invalid_stun);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-stunserver-org");
  tcase_set_timeout (tc_chain, 15);
  tcase_add_test (tc_chain, test_rawudptransmitter_run_stunserver_dot_org);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-local-candidates");
  tcase_add_test (tc_chain, test_rawudptransmitter_run_local_candidates);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rawudptransmitter-stop-stream");
  tcase_add_test (tc_chain, test_rawudptransmitter_stop_stream);
  suite_add_tcase (s, tc_chain);

  return s;
}


GST_CHECK_MAIN (rawudptransmitter);
