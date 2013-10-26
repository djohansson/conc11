CONFIG += debug_and_release
CONFIG += c++11
TARGET = conc11-demo

CONFIG(debug, debug|release) {
    TARGET = $$join(TARGET,,,-debug)
#   LIBS += -L$$OUT_PWD/../conc11-core/release/ -lconc11-core
} else {
#   LIBS += -L$$OUT_PWD/../conc11-core/debug/ -lconc11-core
}

win32 {
}

macx {
}

INCLUDEPATH += $$PWD/../conc11-core/include
DEPENDPATH += $$PWD/../conc11-core/include

HEADERS += \
    include/openglwindow.h

SOURCES += \
    src/main.cpp \
    src/openglwindow.cpp
