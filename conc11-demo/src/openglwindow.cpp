#include "openglwindow.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLPaintDevice>

#include <cassert>

OpenGLWindow::OpenGLWindow(QWindow* parent)
: QWindow(parent)
, m_context(nullptr)
, m_device(nullptr)
, m_ogl43Context(nullptr)
, m_ogl43Functions(nullptr)
, m_updatePending(false)
, m_animating(false)
{
	QSurfaceFormat format = requestedFormat();
	format.setRenderableType(QSurfaceFormat::OpenGL);
	//format.setOption(QSurfaceFormat::DebugContext);

	setSurfaceType(QWindow::OpenGLSurface);
	setFormat(format);
	create();

	/*
	m_context = new QOpenGLContext(this);
	m_context->setFormat(format);
	m_context->create();
	m_context->makeCurrent(this);
	initializeOpenGLFunctions();
	m_context->doneCurrent();
	*/
	
	format.setVersion(4, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);

	m_ogl43Context = new QOpenGLContext(this);
	m_ogl43Context->setFormat(format);
	m_ogl43Context->create();
	m_ogl43Context->setShareContext(m_context);
	m_ogl43Context->makeCurrent(this);
	m_ogl43Functions = m_ogl43Context->versionFunctions<QOpenGLFunctions_4_3_Core>();
	m_ogl43Context->doneCurrent();
	if (m_ogl43Functions)
		m_context = m_ogl43Context;
	
	m_context->makeCurrent(this);
	if (!m_device)
	{
		m_device = new QOpenGLPaintDevice(size());
		m_device->setDevicePixelRatio(devicePixelRatio());
	}
	m_context->doneCurrent();
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
	if (!isExposed())
		return;

	m_updatePending = false;

	m_context->makeCurrent(this);
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
