
if (ENABLE_DISPMANX)
	add_subdirectory(dispmanx)
endif (ENABLE_DISPMANX)

if (ENABLE_V4L2)
	add_subdirectory(v4l2)
endif (ENABLE_V4L2)

if (ENABLE_AUDIOGRABBER)
	add_subdirectory(audiograbber)
endif (ENABLE_AUDIOGRABBER)
