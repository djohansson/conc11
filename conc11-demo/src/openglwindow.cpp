#include "openglwindow.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLPaintDevice>

#include <cassert>

OpenGLWindow::OpenGLWindow(QWindow* parent)
: QWindow(parent)
, m_context(nullptr)
, m_device(nullptr)
, m_updatePending(false)
, m_animating(false)
{
	QSurfaceFormat format = requestedFormat();
	//format.setMajorVersion(4);
	//format.setMinorVersion(3);
	format.setProfile(QSurfaceFormat::CoreProfile);

	setSurfaceType(QWindow::OpenGLSurface);
	setFormat(format);
	create();

	m_context = new QOpenGLContext(this);
	m_context->setFormat(format);
	m_context->create();

	m_context->makeCurrent(this);

	bool glInitResult = initializeOpenGLFunctions();
	assert(glInitResult);

	if (!m_device)
	{
		m_device = new QOpenGLPaintDevice(size());
		m_device->setDevicePixelRatio(devicePixelRatio());
	}
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
