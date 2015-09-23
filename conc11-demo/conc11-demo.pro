TARGET = conc11-demo

CONFIG += debug_and_release
CONFIG += c++11 \
	c++14
CONFIG -= app_bundle

win32 {
	CONFIG += embed_manifest_exe

	QMAKE_CXXFLAGS += /Zi
	QMAKE_LFLAGS += /Debug
}

CONFIG(debug, debug|release) {
    DESTDIR = ../build/debug
} else {
    DESTDIR = ../build/release
}

INCLUDEPATH += $$PWD/../conc11-core/src
DEPENDPATH += $$PWD/../conc11-core/src

HEADERS += \
	src/openglwindow.h

SOURCES += \
	src/main.cpp \
	src/openglwindow.cpp

win32 {
	TARGET_CUSTOM_EXT = .exe
	DEPLOY_COMMAND = windeployqt

	CONFIG(debug, debug|release) {
		DEPLOY_TARGET = $$shell_quote($$shell_path(../build/debug/$${TARGET}$${TARGET_CUSTOM_EXT}))
	} else {
		DEPLOY_TARGET = $$shell_quote($$shell_path(../build/release/$${TARGET}$${TARGET_CUSTOM_EXT}))
	}

	#warning($${DEPLOY_COMMAND} $${DEPLOY_TARGET})

	QMAKE_POST_LINK = $${DEPLOY_COMMAND} $${DEPLOY_TARGET}
}