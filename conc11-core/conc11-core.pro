QT -= core \
	gui

TARGET = conc11-core
TEMPLATE = lib
DEFINES += CONC11_CORE_EXPORTS
INCLUDEPATH += src
HEADERS += \
	src/conc11/Task.h \
	src/conc11/TaskScheduler.h \
	src/conc11/TaskTypes.h \
	src/conc11/TaskUtils.h \
	src/conc11/TimeIntervalCollector.h \
	src/framework/Bitfields.h \
	src/framework/FunctionTraits.h \
	src/framework/MutexedQueue.h \
	src/framework/Thread.h \
	src/framework/TupleElement.h
