# Define the current source locations
SET(CURRENT_HEADER_DIR ${CMAKE_SOURCE_DIR}/include/grabber)
SET(CURRENT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/libsrc/grabber/audiograbber)

SET(AudioGrabber_QT_HEADERS
		${CURRENT_HEADER_DIR}/AudioGrabber.h
		${CURRENT_HEADER_DIR}/AudioGrabberWrapper.h
)

SET(AudioGrabber_HEADERS
		${CURRENT_HEADER_DIR}/VideoStandard.h
		${CURRENT_HEADER_DIR}/PixelFormat.h
)

SET(AudioGrabber_SOURCES
		${CURRENT_SOURCE_DIR}/AudioGrabber.cpp
		${CURRENT_SOURCE_DIR}/AudioGrabberWrapper.cpp
)

QT4_WRAP_CPP(AudioGrabber_HEADERS_MOC ${AudioGrabber_QT_HEADERS})

add_library(audio-grabber
		#${AudioGrabber_HEADERS}
		${AudioGrabber_SOURCES}
		${AudioGrabber_QT_HEADERS}
		${AudioGrabber_HEADERS_MOC}
)

target_link_libraries(audio-grabber
	hyperion
	${QT_LIBRARIES}
)
