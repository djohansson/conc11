#pragma once

#include <QtGui/QWindow>
#include <QtGui/QOpenGLFunctions>

#include <memory>

class QOpenGLContext;

class OpenGLWindow : public QWindow, public QOpenGLFunctions
{
	Q_OBJECT
	
public:
	
	explicit OpenGLWindow(QWindow* parent = nullptr);
	virtual ~OpenGLWindow();

	virtual void render() = 0;

	void setAnimating(bool animating);
	
	inline bool getRenderEnable() const { return m_renderEnable; }
	inline void setRenderEnable(bool enable) { m_renderEnable = enable; }

public slots:
	
	void renderLater();
	void renderNow();

protected:

	inline QOpenGLContext& glContext() { return m_context; }
		
	bool event(QEvent* event);

	void exposeEvent(QExposeEvent* event);
	void resizeEvent(QResizeEvent* event);
	
private:
	
	QOpenGLContext m_context;

	bool m_renderEnable;
	bool m_updatePending;
	bool m_animating;
};
