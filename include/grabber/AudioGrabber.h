#pragma once

// stl includes
#include <string>
#include <vector>

// Qt includes
#include <QObject>
#include <QSocketNotifier>

// util includes
#include <utils/Image.h>
#include <utils/ColorRgb.h>
#include <utils/VideoMode.h>

// grabber includes
#include <grabber/VideoStandard.h>
#include <grabber/PixelFormat.h>

/// Capture class for V4L2 devices
///
/// @see http://linuxtv.org/downloads/v4l-dvb-apis/capture-example.html
class AudioGrabber : public QObject
{
	Q_OBJECT

public:
	AudioGrabber(const std::string & device,
			int input,
			VideoStandard videoStandard, PixelFormat pixelFormat,
			int width,
			int height,
			int frameDecimation,
			int horizontalPixelDecimation,
			int verticalPixelDecimation);
	virtual ~AudioGrabber();

public slots:
	void setCropping(int cropLeft,
					 int cropRight,
					 int cropTop,
					 int cropBottom);

	void set3D(VideoMode mode);

	void setSignalThreshold(double redSignalThreshold,
					double greenSignalThreshold,
					double blueSignalThreshold,
					int noSignalCounterThreshold);

	void start();

	void stop();

signals:
	void newFrame(const Image<ColorRgb> & image);

private slots:
	int read_frame();

private:
	void open_device();

	void close_device();

	void init_read(unsigned int buffer_size);

	void init_mmap();

	void init_userp(unsigned int buffer_size);

	void init_device(VideoStandard videoStandard, int input);

	void uninit_device();

	void start_capturing();

	void stop_capturing();

	bool process_image(const void *p, int size);

	void process_image(const uint8_t *p);

	int xioctl(int request, void *arg);

	void throw_exception(const std::string &error);

	void throw_errno_exception(const std::string &error);

private:
	enum io_method {
			IO_METHOD_READ,
			IO_METHOD_MMAP,
			IO_METHOD_USERPTR
	};

	struct buffer {
			void   *start;
			size_t  length;
	};

private:
	const std::string _deviceName;
	const io_method _ioMethod;
	int _fileDescriptor;
	std::vector<buffer> _buffers;

	PixelFormat _pixelFormat;
	int _width;
	int _height;
	int _frameByteSize;
	int _cropLeft;
	int _cropRight;
	int _cropTop;
	int _cropBottom;
	int _frameDecimation;
	int _horizontalPixelDecimation;
	int _verticalPixelDecimation;
	int _noSignalCounterThreshold;

	ColorRgb _noSignalThresholdColor;

	VideoMode _mode3D;

	int _currentFrame;
	int _noSignalCounter;

	QSocketNotifier * _streamNotifier;
};
