QT -= gui
TARGET = conc11-core
TEMPLATE = lib
DEFINES += CONC11_CORE_EXPORTS
HEADERS += include/conc11/* \
    include/conc11/Thread.h \
    include/conc11/MutexedQueue.h \
    include/conc11/TupleElement.h
