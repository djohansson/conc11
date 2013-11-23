#include "openglwindow.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLPaintDevice>

OpenGLWindow::OpenGLWindow(QWindow* parent)
: QWindow(parent)
, m_context(nullptr)
, m_defaultContext(nullptr)
, m_ogl43Context(nullptr)
, m_ogl43Functions(nullptr)
, m_device(nullptr)
, m_renderEnable(true)
, m_updatePending(false)
, m_animating(false)
{
	QSurfaceFormat format = requestedFormat();
	format.setRenderableType(QSurfaceFormat::OpenGL);
	format.setOption(QSurfaceFormat::DebugContext);

	setSurfaceType(QWindow::OpenGLSurface);
	setFormat(format);
	create();
	
	m_defaultContext = new QOpenGLContext(this);
	m_defaultContext->setFormat(format);
	m_defaultContext->create();
	m_defaultContext->makeCurrent(this);
	initializeOpenGLFunctions();

	Q_ASSERT(m_defaultContext);
	m_context = m_defaultContext;

	if (!m_device)
	{
		m_device = new QOpenGLPaintDevice(size());
		m_device->setDevicePixelRatio(devicePixelRatio());
	}
	
	format.setVersion(4, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);

	m_ogl43Context = new QOpenGLContext(this);
	m_ogl43Context->setFormat(format);
	m_ogl43Context->create();
	m_ogl43Context->setShareContext(m_defaultContext);
	m_ogl43Context->makeCurrent(this);
	m_ogl43Functions = m_ogl43Context->versionFunctions<QOpenGLFunctions_4_3_Core>();
	
	if (m_ogl43Functions)
		m_context = m_ogl43Context;
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
	
	if (m_device)
		m_device->setSize(size());

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
