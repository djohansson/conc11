#pragma once

#include <QtGui/QWindow>
#include <QtGui/QOpenGLFunctions_4_3_Core>

#include <memory>

class QOpenGLContext;
class QOpenGLPaintDevice;

class OpenGLWindow : public QWindow
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
		
	bool event(QEvent* event);

	void exposeEvent(QExposeEvent* event);
	void resizeEvent(QResizeEvent* event);

	inline QOpenGLFunctions_4_3_Core* gl43() const { return static_cast<QOpenGLFunctions_4_3_Core*>(m_glFunctions); }
	
	std::unique_ptr<QOpenGLContext> m_context;
	QAbstractOpenGLFunctions* m_glFunctions;
	std::unique_ptr<QOpenGLPaintDevice> m_device;

private:
	
	bool m_renderEnable;
	bool m_updatePending;
	bool m_animating;
};
