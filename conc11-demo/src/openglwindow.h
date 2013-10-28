#pragma once

#include <QtGui/QWindow>
#include <QtGui/QOpenGLFunctions>

class QOpenGLContext;
class QOpenGLPaintDevice;

class OpenGLWindow : public QWindow, protected QOpenGLFunctions
{
    Q_OBJECT
	
public:
    explicit OpenGLWindow(QWindow* parent = nullptr);
    ~OpenGLWindow();
	
    virtual void render() = 0;
    virtual void initialize() = 0;
	
    void setAnimating(bool animating);
	
public slots:
    void renderLater();
    void renderNow();
	
protected:
    bool event(QEvent* event);
	
    void exposeEvent(QExposeEvent* event);
    void resizeEvent(QResizeEvent* event);
	
	QOpenGLContext* m_context;
	QOpenGLPaintDevice* m_device;
	
private:
    bool m_updatePending;
    bool m_animating;
};
