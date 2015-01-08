#include "openglwindow.h"

#include <QtCore/QtCore>
#include <QtCore/QCoreApplication>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLPaintDevice>

OpenGLWindow::OpenGLWindow(QWindow* parent)
: QWindow(parent), QOpenGLFunctions()
, m_renderEnable(true)
, m_updatePending(false)
, m_animating(false)
{
	QSurfaceFormat format = requestedFormat();
	format.setRenderableType(QSurfaceFormat::OpenGLES);
	format.setOption(QSurfaceFormat::DebugContext);
	format.setVersion(3, 0);
	format.setProfile(QSurfaceFormat::CoreProfile);

	setSurfaceType(QWindow::OpenGLSurface);
	setFormat(format);
	create();
	
	m_context.reset(new QOpenGLContext(this));
	m_context->setFormat(format);
	m_context->create();
	m_context->makeCurrent(this);
	
	Q_ASSERT(m_context);
	
	initializeOpenGLFunctions();
	
//	m_paintDevice.reset(new QOpenGLPaintDevice(size()));
//	m_paintDevice->setDevicePixelRatio(devicePixelRatio());
	
//	Q_ASSERT(m_paintDevice);
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

	m_context->makeCurrent(this);
	
//	m_paintDevice->setSize(size());

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
