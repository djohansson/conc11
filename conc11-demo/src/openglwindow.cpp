#include "openglwindow.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLPaintDevice>

OpenGLWindow::OpenGLWindow(QWindow* parent)
: QWindow(parent)
, m_context(nullptr)
, m_device(nullptr)
, m_updatePending(false)
, m_animating(false)
{
    setSurfaceType(QWindow::OpenGLSurface);
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
	
    bool needsInitialize = false;
	
    if (!m_context)
	{
        m_context = new QOpenGLContext(this);
        m_context->setFormat(requestedFormat());
        m_context->create();
		
        needsInitialize = true;
    }
	
    m_context->makeCurrent(this);
	
	if (needsInitialize)
	{
        initializeOpenGLFunctions();
		
		if (!m_device)
		{
            m_device = new QOpenGLPaintDevice(size());
            m_device->setDevicePixelRatio(devicePixelRatio());
		}
		
        initialize();
    }
		
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
