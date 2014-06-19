#pragma once

#include <string>
#include <cstdlib>
#include <gst/gst.h>

// Qt includes
#include <QObject>
#include <QSocketNotifier>

/// Capture class for V4L2 devices
///
/// @see http://linuxtv.org/downloads/v4l-dvb-apis/capture-example.html
class AudioGrabber : public QObject
{
	Q_OBJECT

	class MyQThread: public QThread
	{
	private:
		GMainLoop *loop;

	public:
		MyQThread(GMainLoop *loop)
	{
			this->loop = loop;
	}

	protected:
		virtual void run()
		{
			if(loop)
			{
				g_main_loop_run(loop);
			}
		}
	};

public:
	AudioGrabber(const std::string & device,
			int freq,
			double volume_gain,
			int num_channels,
			int num_bands,
			int db_threshold);
	virtual ~AudioGrabber();

public slots:

	void start();

	void stop();

private:

	void init_device();

	void uninit_device();

private:
	const std::string _deviceName;
	int _freq;
	int _num_channels;
	int _num_bands;
	int _spectrum_threshold;
	double _volume_gain;
	GMainLoop *loop;
	GstElement *bin;
	gboolean message_handler (GstBus * bus, GstMessage * message, gpointer data);
};
