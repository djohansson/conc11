#pragma once

#include <QtGui/QWindow>
#include <QtGui/QOpenGLFunctions_4_3_Core>

class QOpenGLContext;
class QOpenGLPaintDevice;

class OpenGLWindow : public QWindow, public QOpenGLFunctions_4_3_Core
{
	Q_OBJECT

public:
	explicit OpenGLWindow(QWindow* parent = nullptr);
	~OpenGLWindow();

	virtual void render() = 0;

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
