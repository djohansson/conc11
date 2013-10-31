QT -= core \
	gui
TARGET = conc11-core
TEMPLATE = lib
DEFINES += CONC11_CORE_EXPORTS
HEADERS += include/conc11/FunctionTraits.h \
	include/conc11/MutexedQueue.h \
	include/conc11/Task.h \
	include/conc11/TaskScheduler.h \
	include/conc11/TaskTypes.h \
	include/conc11/TaskUtils.h \
	include/conc11/Thread.h \
	include/conc11/TimeIntervalCollector.h \
	include/conc11/TupleElement.h
