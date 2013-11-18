TARGET = conc11-demo

CONFIG += debug_and_release
CONFIG += c++11
CONFIG -= app_bundle

LIBS += -L$$OUT_PWD/../local -lconc11-core
DESTDIR = ../local

win32 {
}

macx {
}

INCLUDEPATH += $$PWD/../conc11-core/src
DEPENDPATH += $$PWD/../conc11-core/src

HEADERS += \
	src/openglwindow.h

SOURCES += \
	src/main.cpp \
	src/openglwindow.cpp
