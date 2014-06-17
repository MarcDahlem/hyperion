/* GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2008 Jan Schmidt <jan.schmidt@sun.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <gst/gst.h>

#include "grabber/AudioGrabber.h"

/* receive spectral data from element message */
static gboolean
message_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  if (message->type == GST_MESSAGE_ELEMENT) {
    const GstStructure *s = gst_message_get_structure (message);
    const gchar *name = gst_structure_get_name (s);
    GstClockTime endtime;

    if (strcmp (name, "spectrum") == 0) {
      const GValue *magnitudes;
      const GValue *phases;
      const GValue *mag, *phase;
      gdouble freq;
      guint i;

      if (!gst_structure_get_clock_time (s, "endtime", &endtime))
        endtime = GST_CLOCK_TIME_NONE;

   //g_print ("New spectrum message, endtime %" GST_TIME_FORMAT "\n",
     //     GST_TIME_ARGS (endtime));

      magnitudes = gst_structure_get_value (s, "magnitude");
      phases = gst_structure_get_value (s, "phase");

      for (i = 0; i < spect_bands; ++i) {
        freq = (gdouble) ((AUDIOFREQ / 2) * i + AUDIOFREQ / 4) / spect_bands;
        mag = gst_value_list_get_value (magnitudes, i);
        phase = gst_value_list_get_value (phases, i);

        if (mag != NULL && phase != NULL && g_value_get_float (mag) >-50 && freq>16) {
          g_print ("band %d (freq %g): magnitude %f dB phase %f\n", i, freq,
              g_value_get_float (mag), g_value_get_float (phase));
          g_print ("\n");
        }
      }
    }
  }
  return TRUE;
}

AudioGrabber::AudioGrabber(const std::string & device,
		int freq,
		double volume_gain,
		int num_channels,
		int num_bands,
		int db_threshold
	) :
	_deviceName(device),
	_freq(freq),
	_num_channels(num_channels),
	_num_bands(num_bands),
	_spectrum_threshold(db_threshold)
{

	init_device();
}

AudioGrabber::~AudioGrabber()
{
	// stop if the grabber was not stopped
	stop();
	uninit_device();
}

void AudioGrabber::start()
{
	gst_element_set_state (bin, GST_STATE_PLAYING);

	/* we need to run a GLib main loop to get the messages */
	/* since this is not returning, run it in a new thread TODO */
	loop = g_main_loop_new (NULL, FALSE);

	g_main_loop_run (loop);

}

void AudioGrabber::stop()
{
	g_main_loop_quit (loop);
	gst_element_set_state (bin, GST_STATE_NULL);
}

void AudioGrabber::init_device()
{

	  GstElement *src, *volume, *audioconvert, *spectrum, *sink;
	  GstBus *bus;
	  GstCaps *caps;


	  gst_init (NULL, NULL);

	  bin = gst_pipeline_new ("bin");

	  src = gst_element_factory_make ("alsasrc", "src");
	  g_object_set (G_OBJECT (src), "device", _deviceName, NULL);
	  audioconvert = gst_element_factory_make ("audioconvert", NULL);
	  volume = gst_element_factory_make ("volume", "volume");
	  g_object_set (G_OBJECT (volume), "volume", _volume_gain , NULL);
	  g_assert (audioconvert);

	  spectrum = gst_element_factory_make ("spectrum", "spectrum");
	  g_object_set (G_OBJECT (spectrum), "bands", _num_bands, "threshold", _spectrum_threshold,
	      "post-messages", TRUE, "message-phase", FALSE, NULL);

	  sink = gst_element_factory_make ("fakesink", "sink");
	  g_object_set (G_OBJECT (sink), "sync", TRUE, NULL);

	  gst_bin_add_many (GST_BIN (bin), src, audioconvert, volume, spectrum, sink, NULL);

	  caps = gst_caps_new_simple ("audio/x-raw-int",
	      "rate", G_TYPE_INT, _freq, "channels", G_TYPE_INT, _num_channels,  "depth", G_TYPE_INT, 16,NULL);

	  if (!gst_element_link (src, audioconvert)) {
	    fprintf (stderr, "can't link elements 1\n");
	    exit (1);
	  }

	  if (!gst_element_link_filtered (audioconvert, volume, caps)) {
	    fprintf (stderr, "can't link elements 2\n");
	    exit (1);
	  }

	  if (!gst_element_link_filtered (volume, spectrum, caps)) {
	      fprintf (stderr, "can't link elements 2\n");
	      exit (1);
	    }

	  if (!gst_element_link (spectrum, sink)) {
	    fprintf (stderr, "can't link elements 3\n");
	    exit (1);
	  }

	  gst_caps_unref (caps);

	  bus = gst_element_get_bus (bin);
	  gst_bus_add_watch (bus, message_handler, NULL);
	  gst_object_unref (bus);
}

void AudioGrabber::uninit_device()
{
	gst_object_unref (bin);
}
