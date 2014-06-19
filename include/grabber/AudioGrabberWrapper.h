#pragma once

// Qt includes
#include <QTimer>

// Hyperion includes
#include <hyperion/Hyperion.h>
#include <hyperion/ImageProcessor.h>

// Grabber includes
#include <grabber/AudioGrabber.h>

class AudioGrabberWrapper : public QObject
{
	Q_OBJECT

public:
	AudioGrabberWrapper(const std::string & device,
			int freq,
			double volume_gain,
			int num_channels,
			int num_bands,
			int db_threshold,
			Hyperion * hyperion,
			int hyperionPriority);
	virtual ~AudioGrabberWrapper();

public slots:
	void start();

	void stop();

signals:
	void emitColors(int priority, const std::vector<ColorRgb> &ledColors, const int timeout_ms);

private slots:

void checkSources();

private:
	/// The timeout of the led colors [ms]
	const int _timeout_ms;

	/// The priority of the led colors
	const int _priority;

	/// The audio grabber
	AudioGrabber _grabber;

	/// The Hyperion instance
	Hyperion * _hyperion;

	/// The list with computed led colors
	std::vector<ColorRgb> _ledColors;

	/// Timer which tests if a higher priority source is active
	QTimer _timer;
};
