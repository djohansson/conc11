#include "openglwindow.h"

#include <QtCore/QtCore>
#include <QtCore/QCoreApplication>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLPaintDevice>

OpenGLWindow::OpenGLWindow(QWindow* parent)
: QWindow(parent)
, m_glFunctions(nullptr)
, m_renderEnable(true)
, m_updatePending(false)
, m_animating(false)
{
	QSurfaceFormat format = requestedFormat();
	format.setRenderableType(QSurfaceFormat::OpenGL);
	format.setOption(QSurfaceFormat::DebugContext);
	format.setVersion(4, 1);
	format.setProfile(QSurfaceFormat::CoreProfile);

	setSurfaceType(QWindow::OpenGLSurface);
	setFormat(format);
	create();
	
	m_context.reset(new QOpenGLContext(this));
	m_context->setFormat(format);
	m_context->create();
	m_context->makeCurrent(this);
	
	Q_ASSERT(m_context);
	
	m_glFunctions = m_context->versionFunctions<QOpenGLFunctions_4_1_Core>();
	
	Q_ASSERT(m_glFunctions);

	m_glFunctions->initializeOpenGLFunctions();

	format.setVersion(2, 1);
	
	m_paintContext.reset(new QOpenGLContext(this));
	m_paintContext->setFormat(format);
	m_paintContext->create();
	m_paintContext->makeCurrent(this);

	m_paintDevice.reset(new QOpenGLPaintDevice(size()));
	m_paintDevice->setDevicePixelRatio(devicePixelRatio());
	
	Q_ASSERT(m_paintDevice);

	m_paintContext->doneCurrent();
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
	
	if (m_paintDevice)
		m_paintDevice->setSize(size());

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
