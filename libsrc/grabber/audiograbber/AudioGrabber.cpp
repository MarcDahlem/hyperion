#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "grabber/AudioGrabber.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))

static inline uint8_t clamp(int x)
{
	return (x<0) ? 0 : ((x>255) ? 255 : uint8_t(x));
}

static void yuv2rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t & r, uint8_t & g, uint8_t & b)
{
	// see: http://en.wikipedia.org/wiki/YUV#Y.27UV444_to_RGB888_conversion
	int c = y - 16;
	int d = u - 128;
	int e = v - 128;

	r = clamp((298 * c + 409 * e + 128) >> 8);
	g = clamp((298 * c - 100 * d - 208 * e + 128) >> 8);
	b = clamp((298 * c + 516 * d + 128) >> 8);
}


AudioGrabber::AudioGrabber(const std::string & device,
		int input,
		VideoStandard videoStandard,
		PixelFormat pixelFormat,
		int width,
		int height,
		int frameDecimation,
		int horizontalPixelDecimation,
		int verticalPixelDecimation) :
	_deviceName(device),
	_ioMethod(IO_METHOD_MMAP),
	_fileDescriptor(-1),
	_buffers(),
	_pixelFormat(pixelFormat),
	_width(width),
	_height(height),
	_frameByteSize(-1),
	_cropLeft(0),
	_cropRight(0),
	_cropTop(0),
	_cropBottom(0),
	_frameDecimation(std::max(1, frameDecimation)),
	_horizontalPixelDecimation(std::max(1, horizontalPixelDecimation)),
	_verticalPixelDecimation(std::max(1, verticalPixelDecimation)),
	_noSignalCounterThreshold(50),
	_noSignalThresholdColor(ColorRgb{0,0,0}),
	_mode3D(VIDEO_2D),
	_currentFrame(0),
	_noSignalCounter(0),
	_streamNotifier(nullptr)
{
	open_device();
	init_device(videoStandard, input);
}

AudioGrabber::~AudioGrabber()
{
	// stop if the grabber was not stopped
	stop();
	uninit_device();
	close_device();
}

void AudioGrabber::setCropping(int cropLeft, int cropRight, int cropTop, int cropBottom)
{
	_cropLeft = cropLeft;
	_cropRight = cropRight;
	_cropTop = cropTop;
	_cropBottom = cropBottom;
}

void AudioGrabber::set3D(VideoMode mode)
{
	_mode3D = mode;
}

void AudioGrabber::setSignalThreshold(double redSignalThreshold, double greenSignalThreshold, double blueSignalThreshold, int noSignalCounterThreshold)
{
	_noSignalThresholdColor.red = uint8_t(255*redSignalThreshold);
	_noSignalThresholdColor.green = uint8_t(255*greenSignalThreshold);
	_noSignalThresholdColor.blue = uint8_t(255*blueSignalThreshold);
	_noSignalCounterThreshold = std::max(1, noSignalCounterThreshold);

	std::cout << "Audio grabber signal threshold set to: " << _noSignalThresholdColor << std::endl;
}

void AudioGrabber::start()
{
	if (_streamNotifier != nullptr && !_streamNotifier->isEnabled())
	{
		_streamNotifier->setEnabled(true);
		start_capturing();
		std::cout << "Audio grabber started" << std::endl;
	}
}

void AudioGrabber::stop()
{
	if (_streamNotifier != nullptr && _streamNotifier->isEnabled())
	{
		stop_capturing();
		_streamNotifier->setEnabled(false);
		std::cout << "Audio grabber stopped" << std::endl;
	}
}

void AudioGrabber::open_device()
{
	struct stat st;

	if (-1 == stat(_deviceName.c_str(), &st))
	{
		std::ostringstream oss;
		oss << "Cannot identify '" << _deviceName << "'";
		throw_errno_exception(oss.str());
	}

	if (!S_ISCHR(st.st_mode))
	{
		std::ostringstream oss;
		oss << "'" << _deviceName << "' is no device";
		throw_exception(oss.str());
	}

	_fileDescriptor = open(_deviceName.c_str(), O_RDWR /* required */ | O_NONBLOCK, 0);

	if (-1 == _fileDescriptor)
	{
		std::ostringstream oss;
		oss << "Cannot open '" << _deviceName << "'";
		throw_errno_exception(oss.str());
	}

	// create the notifier for when a new frame is available
	_streamNotifier = new QSocketNotifier(_fileDescriptor, QSocketNotifier::Read);
	_streamNotifier->setEnabled(false);
	connect(_streamNotifier, SIGNAL(activated(int)), this, SLOT(read_frame()));
}

void AudioGrabber::close_device()
{
	if (-1 == close(_fileDescriptor))
		throw_errno_exception("close");

	_fileDescriptor = -1;

	if (_streamNotifier != nullptr)
	{
		delete _streamNotifier;
		_streamNotifier = nullptr;
	}
}

void AudioGrabber::init_read(unsigned int buffer_size)
{
	_buffers.resize(1);

	_buffers[0].length = buffer_size;
	_buffers[0].start = malloc(buffer_size);

	if (!_buffers[0].start) {
		throw_exception("Out of memory");
	}
}

void AudioGrabber::init_mmap()
{
	struct v4l2_requestbuffers req;

	CLEAR(req);

	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			std::ostringstream oss;
			oss << "'" << _deviceName << "' does not support memory mapping";
			throw_exception(oss.str());
		} else {
			throw_errno_exception("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		std::ostringstream oss;
		oss << "Insufficient buffer memory on " << _deviceName;
		throw_exception(oss.str());
	}

	_buffers.resize(req.count);

	for (size_t n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory      = V4L2_MEMORY_MMAP;
		buf.index       = n_buffers;

		if (-1 == xioctl(VIDIOC_QUERYBUF, &buf))
			throw_errno_exception("VIDIOC_QUERYBUF");

		_buffers[n_buffers].length = buf.length;
		_buffers[n_buffers].start =
				mmap(NULL /* start anywhere */,
					 buf.length,
					 PROT_READ | PROT_WRITE /* required */,
					 MAP_SHARED /* recommended */,
					 _fileDescriptor, buf.m.offset);

		if (MAP_FAILED == _buffers[n_buffers].start)
			throw_errno_exception("mmap");
	}
}

void AudioGrabber::init_userp(unsigned int buffer_size)
{
	struct v4l2_requestbuffers req;

	CLEAR(req);

	req.count  = 4;
	req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;

	if (-1 == xioctl(VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno)
		{
			std::ostringstream oss;
			oss << "'" << _deviceName << "' does not support user pointer";
			throw_exception(oss.str());
		} else {
			throw_errno_exception("VIDIOC_REQBUFS");
		}
	}

	_buffers.resize(4);

	for (size_t n_buffers = 0; n_buffers < 4; ++n_buffers) {
		_buffers[n_buffers].length = buffer_size;
		_buffers[n_buffers].start = malloc(buffer_size);

		if (!_buffers[n_buffers].start) {
			throw_exception("Out of memory");
		}
	}
}

void AudioGrabber::init_device(VideoStandard videoStandard, int input)
{
	struct v4l2_capability cap;
	if (-1 == xioctl(VIDIOC_QUERYCAP, &cap))
	{
		if (EINVAL == errno) {
			std::ostringstream oss;
			oss << "'" << _deviceName << "' is no audio device";
			throw_exception(oss.str());
		} else {
			throw_errno_exception("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
	{
		std::ostringstream oss;
		oss << "'" << _deviceName << "' is no video capture device";
		throw_exception(oss.str());
	}

	switch (_ioMethod) {
	case IO_METHOD_READ:
		if (!(cap.capabilities & V4L2_CAP_READWRITE))
		{
			std::ostringstream oss;
			oss << "'" << _deviceName << "' does not support read i/o";
			throw_exception(oss.str());
		}
		break;

	case IO_METHOD_MMAP:
	case IO_METHOD_USERPTR:
		if (!(cap.capabilities & V4L2_CAP_STREAMING))
		{
			std::ostringstream oss;
			oss << "'" << _deviceName << "' does not support streaming i/o";
			throw_exception(oss.str());
		}
		break;
	}


	/* Select video input, video standard and tune here. */

	struct v4l2_cropcap cropcap;
	CLEAR(cropcap);

	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (0 == xioctl(VIDIOC_CROPCAP, &cropcap)) {
		struct v4l2_crop crop;
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect; /* reset to default */

		if (-1 == xioctl(VIDIOC_S_CROP, &crop)) {
			switch (errno) {
			case EINVAL:
				/* Cropping not supported. */
				break;
			default:
				/* Errors ignored. */
				break;
			}
		}
	} else {
		/* Errors ignored. */
	}

	// set input if needed
	if (input >= 0)
	{
		if (-1 == xioctl(VIDIOC_S_INPUT, &input))
		{
			throw_errno_exception("VIDIOC_S_INPUT");
		}
	}

	// set the video standard if needed
	switch (videoStandard)
	{
	case VIDEOSTANDARD_PAL:
	{
		v4l2_std_id std_id = V4L2_STD_PAL;
		if (-1 == xioctl(VIDIOC_S_STD, &std_id))
		{
			throw_errno_exception("VIDIOC_S_STD");
		}
	}
		break;
	case VIDEOSTANDARD_NTSC:
	{
		v4l2_std_id std_id = V4L2_STD_NTSC;
		if (-1 == xioctl(VIDIOC_S_STD, &std_id))
		{
			throw_errno_exception("VIDIOC_S_STD");
		}
	}
		break;
	case VIDEOSTANDARD_NO_CHANGE:
	default:
		// No change to device settings
		break;
	}


	// get the current settings
	struct v4l2_format fmt;
	CLEAR(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl(VIDIOC_G_FMT, &fmt))
	{
		throw_errno_exception("VIDIOC_G_FMT");
	}

	// set the requested pixel format
	switch (_pixelFormat)
	{
	case PIXELFORMAT_UYVY:
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
		break;
	case PIXELFORMAT_YUYV:
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		break;
	case PIXELFORMAT_RGB32:
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32;
		break;
	case PIXELFORMAT_NO_CHANGE:
	default:
		// No change to device settings
		break;
	}

	// set the requested withd and height
	if (_width > 0 || _height > 0)
	{
		if (_width > 0)
		{
			fmt.fmt.pix.width = _width;
		}

		if (fmt.fmt.pix.height > 0)
		{
			fmt.fmt.pix.height = _height;
		}
	}

	// set the settings
	if (-1 == xioctl(VIDIOC_S_FMT, &fmt))
	{
		throw_errno_exception("VIDIOC_S_FMT");
	}

	// get the format settings again
	// (the size may not have been accepted without an error)
	if (-1 == xioctl(VIDIOC_G_FMT, &fmt))
	{
		throw_errno_exception("VIDIOC_G_FMT");
	}

	// store width & height
	_width = fmt.fmt.pix.width;
	_height = fmt.fmt.pix.height;

	// print the eventually used width and height
	std::cout << "audio width=" << _width << " height=" << _height << std::endl;

	// check pixel format and frame size
	switch (fmt.fmt.pix.pixelformat)
	{
	case V4L2_PIX_FMT_UYVY:
		_pixelFormat = PIXELFORMAT_UYVY;
		_frameByteSize = _width * _height * 2;
		std::cout << "audio pixel format=UYVY" << std::endl;
		break;
	case V4L2_PIX_FMT_YUYV:
		_pixelFormat = PIXELFORMAT_YUYV;
		_frameByteSize = _width * _height * 2;
		std::cout << "Audio pixel format=YUYV" << std::endl;
		break;
	case V4L2_PIX_FMT_RGB32:
		_pixelFormat = PIXELFORMAT_RGB32;
		_frameByteSize = _width * _height * 4;
		std::cout << "Audio pixel format=RGB32" << std::endl;
		break;
	default:
		throw_exception("Only pixel formats UYVY, YUYV, and RGB32 are supported");
	}

	switch (_ioMethod) {
	case IO_METHOD_READ:
		init_read(fmt.fmt.pix.sizeimage);
		break;

	case IO_METHOD_MMAP:
		init_mmap();
		break;

	case IO_METHOD_USERPTR:
		init_userp(fmt.fmt.pix.sizeimage);
		break;
	}
}

void AudioGrabber::uninit_device()
{
	switch (_ioMethod) {
	case IO_METHOD_READ:
		free(_buffers[0].start);
		break;

	case IO_METHOD_MMAP:
		for (size_t i = 0; i < _buffers.size(); ++i)
			if (-1 == munmap(_buffers[i].start, _buffers[i].length))
				throw_errno_exception("munmap");
		break;

	case IO_METHOD_USERPTR:
		for (size_t i = 0; i < _buffers.size(); ++i)
			free(_buffers[i].start);
		break;
	}

	_buffers.resize(0);
}

void AudioGrabber::start_capturing()
{
	switch (_ioMethod) {
	case IO_METHOD_READ:
		/* Nothing to do. */
		break;

	case IO_METHOD_MMAP:
	{
		for (size_t i = 0; i < _buffers.size(); ++i) {
			struct v4l2_buffer buf;

			CLEAR(buf);
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = i;

			if (-1 == xioctl(VIDIOC_QBUF, &buf))
				throw_errno_exception("VIDIOC_QBUF");
		}
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (-1 == xioctl(VIDIOC_STREAMON, &type))
			throw_errno_exception("VIDIOC_STREAMON");
		break;
	}
	case IO_METHOD_USERPTR:
	{
		for (size_t i = 0; i < _buffers.size(); ++i) {
			struct v4l2_buffer buf;

			CLEAR(buf);
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_USERPTR;
			buf.index = i;
			buf.m.userptr = (unsigned long)_buffers[i].start;
			buf.length = _buffers[i].length;

			if (-1 == xioctl(VIDIOC_QBUF, &buf))
				throw_errno_exception("VIDIOC_QBUF");
		}
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (-1 == xioctl(VIDIOC_STREAMON, &type))
			throw_errno_exception("VIDIOC_STREAMON");
		break;
	}
	}
}

void AudioGrabber::stop_capturing()
{
	enum v4l2_buf_type type;

	switch (_ioMethod) {
	case IO_METHOD_READ:
		/* Nothing to do. */
		break;

	case IO_METHOD_MMAP:
	case IO_METHOD_USERPTR:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (-1 == xioctl(VIDIOC_STREAMOFF, &type))
			throw_errno_exception("VIDIOC_STREAMOFF");
		break;
	}
}

int AudioGrabber::read_frame()
{
	bool rc = false;

	struct v4l2_buffer buf;

	switch (_ioMethod) {
	case IO_METHOD_READ:
		int size;
		if ((size = read(_fileDescriptor, _buffers[0].start, _buffers[0].length)) == -1)
		{
			switch (errno)
			{
			case EAGAIN:
				return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				throw_errno_exception("read");
			}
		}

		rc = process_image(_buffers[0].start, size);
		break;

	case IO_METHOD_MMAP:
		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl(VIDIOC_DQBUF, &buf))
		{
			switch (errno)
			{
			case EAGAIN:
				return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				throw_errno_exception("VIDIOC_DQBUF");
			}
		}

		assert(buf.index < _buffers.size());

		rc = process_image(_buffers[buf.index].start, buf.bytesused);

		if (-1 == xioctl(VIDIOC_QBUF, &buf))
		{
			throw_errno_exception("VIDIOC_QBUF");
		}

		break;

	case IO_METHOD_USERPTR:
		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_USERPTR;

		if (-1 == xioctl(VIDIOC_DQBUF, &buf))
		{
			switch (errno)
			{
			case EAGAIN:
				return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				throw_errno_exception("VIDIOC_DQBUF");
			}
		}

		for (size_t i = 0; i < _buffers.size(); ++i)
		{
			if (buf.m.userptr == (unsigned long)_buffers[i].start && buf.length == _buffers[i].length)
			{
				break;
			}
		}

		rc = process_image((void *)buf.m.userptr, buf.bytesused);

		if (-1 == xioctl(VIDIOC_QBUF, &buf))
		{
			throw_errno_exception("VIDIOC_QBUF");
		}
		break;
	}

	return rc ? 1 : 0;
}

bool AudioGrabber::process_image(const void *p, int size)
{
	if (++_currentFrame >= _frameDecimation)
	{
		// We do want a new frame...

		if (size != _frameByteSize)
		{
			std::cout << "Frame too small: " << size << " != " << _frameByteSize << std::endl;
		}
		else
		{
			process_image(reinterpret_cast<const uint8_t *>(p));
			_currentFrame = 0; // restart counting
			return true;
		}
	}

	return false;
}

void AudioGrabber::process_image(const uint8_t * data)
{
	int width = _width;
	int height = _height;

	switch (_mode3D)
	{
	case VIDEO_3DSBS:
		width = _width/2;
		break;
	case VIDEO_3DTAB:
		height = _height/2;
		break;
	default:
		break;
	}

	// create output structure
	int outputWidth = (width - _cropLeft - _cropRight + _horizontalPixelDecimation/2) / _horizontalPixelDecimation;
	int outputHeight = (height - _cropTop - _cropBottom + _verticalPixelDecimation/2) / _verticalPixelDecimation;
	Image<ColorRgb> image(outputWidth, outputHeight);

	for (int ySource = _cropTop + _verticalPixelDecimation/2, yDest = 0; ySource < height - _cropBottom; ySource += _verticalPixelDecimation, ++yDest)
	{
		for (int xSource = _cropLeft + _horizontalPixelDecimation/2, xDest = 0; xSource < width - _cropRight; xSource += _horizontalPixelDecimation, ++xDest)
		{
			ColorRgb & rgb = image(xDest, yDest);

			switch (_pixelFormat)
			{
			case PIXELFORMAT_UYVY:
				{
					int index = (_width * ySource + xSource) * 2;
					uint8_t y = data[index+1];
					uint8_t u = (xSource%2 == 0) ? data[index  ] : data[index-2];
					uint8_t v = (xSource%2 == 0) ? data[index+2] : data[index  ];
					yuv2rgb(y, u, v, rgb.red, rgb.green, rgb.blue);
				}
				break;
			case PIXELFORMAT_YUYV:
				{
					int index = (_width * ySource + xSource) * 2;
					uint8_t y = data[index];
					uint8_t u = (xSource%2 == 0) ? data[index+1] : data[index-1];
					uint8_t v = (xSource%2 == 0) ? data[index+3] : data[index+1];
					yuv2rgb(y, u, v, rgb.red, rgb.green, rgb.blue);
				}
				break;
			case PIXELFORMAT_RGB32:
				{
					int index = (_width * ySource + xSource) * 4;
					rgb.red   = data[index  ];
					rgb.green = data[index+1];
					rgb.blue  = data[index+2];
				}
				break;
			default:
				// this should not be possible
				break;
			}
		}
	}

	// check signal (only in center of the resulting image, because some grabbers have noise values along the borders)
	bool noSignal = true;
	for (unsigned x = 0; noSignal && x < (image.width()>>1); ++x)
	{
		int xImage = (image.width()>>2) + x;

		for (unsigned y = 0; noSignal && y < (image.height()>>1); ++y)
		{
			int yImage = (image.height()>>2) + y;

			ColorRgb & rgb = image(xImage, yImage);
			noSignal &= rgb <= _noSignalThresholdColor;
		}
	}

	if (noSignal)
	{
		++_noSignalCounter;
	}
	else
	{
		if (_noSignalCounter >= _noSignalCounterThreshold)
		{
			std::cout << "Audio Grabber: " << "Signal detected" << std::endl;
		}

		_noSignalCounter = 0;
	}

	if (_noSignalCounter < _noSignalCounterThreshold)
	{
		emit newFrame(image);
	}
	else if (_noSignalCounter == _noSignalCounterThreshold)
	{
		std::cout << "Audio Grabber: " << "Signal lost" << std::endl;
	}
}

int AudioGrabber::xioctl(int request, void *arg)
{
	int r;

	do
	{
		r = ioctl(_fileDescriptor, request, arg);
	}
	while (-1 == r && EINTR == errno);

	return r;
}

void AudioGrabber::throw_exception(const std::string & error)
{
	std::ostringstream oss;
	oss << error << " error";
	throw std::runtime_error(oss.str());
}

void AudioGrabber::throw_errno_exception(const std::string & error)
{
	std::ostringstream oss;
	oss << error << " error " << errno << ", " << strerror(errno);
	throw std::runtime_error(oss.str());
}
