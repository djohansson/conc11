#include "openglwindow.h"

#include <QtCore/QtCore>
#include <QtCore/QCoreApplication>
#include <QtGui/QOpenGLContext>

OpenGLWindow::OpenGLWindow(QWindow* parent)
: QWindow(parent), QOpenGLFunctions()
, m_context(this)
, m_renderEnable(true)
, m_updatePending(false)
, m_animating(false)
{
	QSurfaceFormat format = requestedFormat();
	format.setRenderableType(QSurfaceFormat::OpenGL);
#ifdef _DEBUG
	format.setOption(QSurfaceFormat::DebugContext);
#endif
	format.setVersion(4, 1);
	format.setSwapBehavior(QSurfaceFormat::TripleBuffer);
	format.setSwapInterval(1);
	format.setProfile(QSurfaceFormat::CoreProfile);

	setSurfaceType(QWindow::OpenGLSurface);
	setFormat(format);
	create();
	
	m_context.setFormat(format);
	m_context.create();
	m_context.makeCurrent(this);
	
	initializeOpenGLFunctions();
}

OpenGLWindow::~OpenGLWindow()
{
}

void OpenGLWindow::renderLater()
{
	if (!m_updatePending)
	{
		m_updatePending = true;
		QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest));
	}
}

bool OpenGLWindow::event(QEvent* event)
{
	switch (event->type())
	{
	case QEvent::UpdateRequest:
		renderNow();
		return true;
	default:
		return QWindow::event(event);
	}
}

void OpenGLWindow::exposeEvent(QExposeEvent* /*event*/)
{
	if (isExposed())
		renderNow();
}

void OpenGLWindow::resizeEvent(QResizeEvent* /*event*/)
{
	if (isExposed())
		renderNow();
}

void OpenGLWindow::renderNow()
{
	if (!m_renderEnable || !isExposed())
		return;

	m_updatePending = false;

	m_context.makeCurrent(this);
	
	render();

	if (m_animating)
		renderLater();
}

void OpenGLWindow::setAnimating(bool animating)
{
	m_animating = animating;

	if (animating)
		renderLater();
}
