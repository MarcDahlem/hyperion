#include <QMetaType>

#include <grabber/AudioGrabberWrapper.h>

AudioGrabberWrapper::AudioGrabberWrapper(const std::string & device,
				int freq,
				double volume_gain,
				int num_channels,
				int num_bands,
				int db_threshold,
		Hyperion *hyperion,
		int hyperionPriority) :
	_timeout_ms(1000),
	_priority(hyperionPriority),
	_grabber(device,
			freq,
			volume_gain,
			num_channels,
			num_bands,
			db_threshold),
	_hyperion(hyperion),
	_ledColors(hyperion->getLedCount(), ColorRgb{0,0,0}),
	_timer()
{

	// register the image type
	qRegisterMetaType<Image<ColorRgb>>("Image<ColorRgb>");
	qRegisterMetaType<std::vector<ColorRgb>>("std::vector<ColorRgb>");

	// Handle the image in the captured thread using a direct connection
	QObject::connect(
				&_grabber, SIGNAL(newFrame(Image<ColorRgb>)),
				this, SLOT(newFrame(Image<ColorRgb>)),
				Qt::DirectConnection);

	// send color data to Hyperion using a queued connection to handle the data over to the main event loop
	QObject::connect(
				this, SIGNAL(emitColors(int,std::vector<ColorRgb>,int)),
				_hyperion, SLOT(setColors(int,std::vector<ColorRgb>,int)),
				Qt::QueuedConnection);

	// setup the higher prio source checker
	// this will disable the audio grabber when a source with higher priority is active
	_timer.setInterval(500);
	_timer.setSingleShot(false);
	QObject::connect(&_timer, SIGNAL(timeout()), this, SLOT(checkSources()));
	_timer.start();
}

AudioGrabberWrapper::~AudioGrabberWrapper()
{
	//delete _processor;
}

void AudioGrabberWrapper::start()
{
	_grabber.start();
}

void AudioGrabberWrapper::stop()
{
	_grabber.stop();
}

void AudioGrabberWrapper::newFrame(const Image<ColorRgb> &image)
{
	// process the new image
	//_processor->process(image, _ledColors);

	// send colors to Hyperion
	emit emitColors(_priority, _ledColors, _timeout_ms);
}

void AudioGrabberWrapper::checkSources()
{
	QList<int> activePriorities = _hyperion->getActivePriorities();

	for (int x : activePriorities)
	{
		if (x < _priority)
		{
			// found a higher priority source: grabber should be disabled
			_grabber.stop();
			return;
		}
	}

	// no higher priority source was found: grabber should be enabled
	_grabber.start();
}
