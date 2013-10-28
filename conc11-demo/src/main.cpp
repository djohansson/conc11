#include "openglwindow.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLPaintDevice>
#include <QtGui/QScreen>
#include <QtGui/QPainter>
#include <QtGui/QPaintEngine>

#include <conc11/Task.h>
#include <conc11/TaskScheduler.h>
#include <conc11/TimeIntervalCollector.h>

#include <chrono>
#include <memory>
#include <numeric>
#include <thread>
#include <vector>

void mandel(unsigned xmin, unsigned xmax, unsigned xsize, unsigned ymin, unsigned ymax, unsigned ysize, unsigned* image)
{
	double MinRe = -2.0;
	double MaxRe = 1.0;
	double MinIm = -1.2;
	double MaxIm = MinIm+(MaxRe-MinRe)*ysize/xsize;
	double Re_factor = (MaxRe-MinRe)/(xsize-1);
	double Im_factor = (MaxIm-MinIm)/(ysize-1);
	unsigned MaxIterations = 30;
	
	for(unsigned y=ymin; y<ymax; ++y)
	{
		double c_im = MaxIm - y*Im_factor;
		for(unsigned x=xmin; x<xmax; ++x)
		{
			double c_re = MinRe + x*Re_factor;
			
			double Z_re = c_re, Z_im = c_im;
			unsigned n = 0;
			for(; n<MaxIterations; ++n)
			{
				double Z_re2 = Z_re*Z_re, Z_im2 = Z_im*Z_im;
				
				if(Z_re2 + Z_im2 > 4)
					break;
				
				Z_im = 2*Z_re*Z_im + c_im;
				Z_re = Z_re2 - Z_im2 + c_re;
			}
			
			image[(ymin + y)*xsize + xmin + x] = n;
		}
	}
}

void lerp(const float a[3], const float b[3], float t, float out[3])
{
	out[0] = (1.0f - t) * a[0] + t * b[0];
	out[1] = (1.0f - t) * a[1] + t * b[1];
	out[2] = (1.0f - t) * a[2] + t * b[2];
}

static const float red[3] = { 1.0f, 0.0f, 0.0f };
static const float green[3] = { 0.0f, 1.0f, 0.0f };
static const float blue[3] = { 0.0f, 0.0f, 1.0f };
static const float yellow[3] = { 1.0f, 1.0f, 0.0f };
static const float cyan[3] = { 0.0f, 1.0f, 1.0f };
static const float magenta[3] = { 1.0f, 0.0f, 1.0f };
static const float pink[3] = { 1.0f, 0.5f, 1.0f };
static const float rose[3] = { 1.0f, 0.5f, 0.5f };
static const float gray[3] = { 0.5f, 0.5f, 0.5f };

class MainWindow : public OpenGLWindow
{
public:
    MainWindow();
	
    virtual void initialize() final;
    virtual void render() final;
	
private:
    GLuint m_posAttr;
    GLuint m_colAttr;
	GLuint m_matrixUniform;
	
	conc11::TaskScheduler m_scheduler;
	std::vector<std::thread::id> m_threadIds;
	std::vector<std::unique_ptr<unsigned>> m_images;
	
    std::shared_ptr<conc11::TaskBase> m_createWork;
	std::shared_ptr<conc11::TaskBase> m_work;
	std::shared_ptr<conc11::TaskBase> m_clear;
	std::shared_ptr<conc11::TaskBase> m_updateFps;
	std::shared_ptr<conc11::TaskBase> m_drawIntervals;
	std::shared_ptr<conc11::TaskBase> m_swap;
	
	std::shared_ptr<conc11::TimeIntervalCollector> m_collectors[2];
	std::shared_ptr<conc11::TimeIntervalCollector> m_collector;
	std::shared_ptr<conc11::TimeIntervalCollector> m_lastFrameCollector;
	
	std::chrono::high_resolution_clock::time_point m_frameStart;
	std::chrono::high_resolution_clock::time_point m_lastFrameStart;
	
    QOpenGLShaderProgram* m_program;
	std::vector<float> m_vertexBuffer;
	std::vector<float> m_colorBuffer;

    QPainter m_painter;
	
    unsigned int m_frameIndex;
};

MainWindow::MainWindow()
: m_program(nullptr)
, m_frameIndex(0)
{
	auto& threads = m_scheduler.getThreads();
	m_threadIds.reserve(threads.size() + 1);
	m_threadIds.push_back(std::this_thread::get_id());
    for (const auto& t : threads)
        m_threadIds.push_back(t.get().get_id());
	
    const unsigned int imageSize = 128;
    const unsigned int imageCnt = 64;
	for (unsigned int i = 0; i < imageCnt; i++)
		m_images.push_back(std::unique_ptr<unsigned>(new unsigned[imageSize*imageSize]));
	
	m_collectors[0] = std::make_shared<conc11::TimeIntervalCollector>();
	m_collectors[1] = std::make_shared<conc11::TimeIntervalCollector>();
	m_collector = m_collectors[0];
	
	m_frameStart = std::chrono::high_resolution_clock::now();
	
	m_clear = m_scheduler.createTask([this]
	{
		glClearColor(0, 0, 0.3f, 0);
		glClear(GL_COLOR_BUFFER_BIT);
        m_painter.begin(m_device);
	}, "clear", pink);
	
	m_updateFps = m_scheduler.createTask([this]
	{
		auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(m_frameStart - m_lastFrameStart).count();
		auto fps = 1e9 / double(dt);

        m_painter.setWindow(0, 0, width(), height());

        //m_painter.paintEngine()->setSystemClip(QRegion(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio()));
        //m_painter.paintEngine()->setSystemRect(QRect(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio()));

        m_painter.setPen(Qt::blue);
        m_painter.drawRect(0, 0, width(), height());
        m_painter.setPen(Qt::red);
        m_painter.setFont(QFont("Arial", 30));
        m_painter.drawText(0, 0, 150, 30, Qt::AlignCenter, std::to_string(fps).c_str());

        /*
        QRectF rect = QRectF(0, 0, width(), height());
		painter.setPen(Qt::darkRed);
		painter.drawRect(rect);
		painter.setPen(Qt::white);
		painter.drawText(rect, QString(std::to_string(fps).c_str()), QTextOption(Qt::AlignLeft|Qt::AlignTop));
        */
	}, "updateFps", cyan);
	
	m_drawIntervals = m_scheduler.createTask([this]
	{
		std::vector<float>& vertices = m_vertexBuffer;
		std::vector<float>& colors = m_colorBuffer;
		
		vertices.clear();
		colors.clear();
		
		{
			vertices.push_back(-1);
			vertices.push_back(-1);
			colors.push_back(0);
			colors.push_back(0.3f);
			colors.push_back(0);
			
			vertices.push_back(-1);
			vertices.push_back(1);
			colors.push_back(0);
			colors.push_back(0.3f);
			colors.push_back(0);
			
			vertices.push_back(1);
			vertices.push_back(-1);
			colors.push_back(0);
			colors.push_back(0.3f);
			colors.push_back(0);
			
			vertices.push_back(-1);
			vertices.push_back(1);
			colors.push_back(0);
			colors.push_back(0.3f);
			colors.push_back(0);
			
			vertices.push_back(1);
			vertices.push_back(-1);
			colors.push_back(0);
			colors.push_back(0.3f);
			colors.push_back(0);
			
			vertices.push_back(1);
			vertices.push_back(1);
			colors.push_back(0);
			colors.push_back(0.3f);
			colors.push_back(0);
		}
		
		float dy = 2.0f / m_threadIds.size();
		float sy = 0.95f * dy;
		const conc11::TimeIntervalCollector::ContainerType& intervals = m_lastFrameCollector->getIntervals();
		unsigned int threadIndex = 0;
		for (auto& threadId : m_threadIds)
		{
			auto ip = intervals.equal_range(threadId);
			for (auto it = ip.first; it != ip.second; it++)
			{
				auto& ti = (*it).second;
								
				auto start = std::chrono::duration_cast<std::chrono::nanoseconds>(ti.start - m_lastFrameStart).count();
				auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(ti.end - ti.start).count();
				
				static float scale = 64.0f;
				float x = -1 + scale * static_cast<float>(start) / 1e9f;
				float y = -1 + (threadIndex * dy);
				float sx = scale * static_cast<float>(duration) / 1e9f;
				
				vertices.push_back(x);
				vertices.push_back(y);
				colors.push_back(ti.debugColor[0]);
				colors.push_back(ti.debugColor[1]);
				colors.push_back(ti.debugColor[2]);
				
				vertices.push_back(x);
				vertices.push_back(y + sy);
				colors.push_back(ti.debugColor[0]);
				colors.push_back(ti.debugColor[1]);
				colors.push_back(ti.debugColor[2]);
				
				vertices.push_back(x + sx);
				vertices.push_back(y);
				colors.push_back(ti.debugColor[0]);
				colors.push_back(ti.debugColor[1]);
				colors.push_back(ti.debugColor[2]);
				
				vertices.push_back(x);
				vertices.push_back(y + sy);
				colors.push_back(ti.debugColor[0]);
				colors.push_back(ti.debugColor[1]);
				colors.push_back(ti.debugColor[2]);
				
				vertices.push_back(x + sx);
				vertices.push_back(y);
				colors.push_back(ti.debugColor[0]);
				colors.push_back(ti.debugColor[1]);
				colors.push_back(ti.debugColor[2]);
				
				vertices.push_back(x + sx);
				vertices.push_back(y + sy);
				colors.push_back(ti.debugColor[0]);
				colors.push_back(ti.debugColor[1]);
				colors.push_back(ti.debugColor[2]);
			}
			
			threadIndex++;
        }

        m_painter.beginNativePainting();

		glViewport(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio());
		
		m_program->bind();

		QMatrix4x4 matrix;
		matrix.perspective(60, float(width()) / float(height()), 0.1f, 100.0f);
		matrix.translate(0, 0, -2);
    //	matrix.rotate(100.0f * m_frameIndex / screen()->refreshRate(), 0, 1, 0);
		m_program->setUniformValue(m_matrixUniform, matrix);
		
		glVertexAttribPointer(m_posAttr, 2, GL_FLOAT, GL_FALSE, 0, vertices.data());
		glVertexAttribPointer(m_colAttr, 3, GL_FLOAT, GL_FALSE, 0, colors.data());
		
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size() / 2);
		
		glDisableVertexAttribArray(1);
		glDisableVertexAttribArray(0);
		
        m_program->release();

        m_painter.endNativePainting();

  }, "drawIntervals", blue);

	
	m_createWork = m_scheduler.createTask([this, imageSize, imageCnt]
    {
        std::shared_ptr<conc11::Task<unsigned int>> branches[2];

        for (unsigned int j = 0; j < 2; j++)
        {
            std::vector<std::shared_ptr<conc11::Task<unsigned int>>> tasks;
            tasks.reserve(imageCnt);
            for (unsigned int i = 0; i < imageCnt; i++)
            {
                float color[3];
                lerp(red, green, float(i) / imageCnt, color);

                tasks.push_back(m_scheduler.createTask([this, i, imageSize]
                {
                    mandel(0, imageSize, imageSize, 0, imageSize, imageSize, m_images[i].get());
                    return i;
                }, std::string("mandel") + std::to_string(i), color));
            }
		
            branches[j] = m_scheduler.join(tasks)->then([&](std::vector<unsigned int> vals) // wtf do we need capture by ref on vc 2012 ctp here!?
            {
                return std::accumulate(begin(vals), end(vals), 0U);
            }, std::string("accumulate") + std::to_string(j), magenta);
        }

        auto work = m_scheduler.join(branches[0], branches[1]);
		
        m_scheduler.dispatch(work, m_collector);
		
        m_work = work;
		
	}, "createWork", rose);
	
	m_swap = m_scheduler.createTask([this]
	{
        m_painter.end();
		m_context->swapBuffers(this);
	}, "swap", yellow);
}

void MainWindow::initialize()
{
    m_program = new QOpenGLShaderProgram(this);
	
	static const char* vertexShaderSource =
	R"(
		attribute highp vec4 posAttr;
		attribute lowp vec4 colAttr;
		varying lowp vec4 col;
		uniform highp mat4 matrix;
		void main()
		{
			col = colAttr;
			gl_Position = matrix * posAttr;
		}
	)";
	m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    
	static const char* fragmentShaderSource =
	R"(
		varying lowp vec4 col;
		void main()
		{
			gl_FragColor = col;
		}
	)";
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
	
    m_program->link();
    
	m_posAttr = m_program->attributeLocation("posAttr");
    m_colAttr = m_program->attributeLocation("colAttr");
    m_matrixUniform = m_program->uniformLocation("matrix");
}

void MainWindow::render()
{
    m_frameIndex++;
    m_lastFrameStart = m_frameStart;
    m_lastFrameCollector = m_collector;

    m_frameStart = std::chrono::high_resolution_clock::now();

    m_collector = m_collectors[m_frameIndex % 2];
    m_collector->clear();

    m_scheduler.dispatch(m_createWork, m_collector);
	m_scheduler.run(m_clear, m_collector);
	m_scheduler.run(m_drawIntervals, m_collector);
	m_scheduler.run(m_updateFps, m_collector);
    m_scheduler.waitJoin(m_work);
    m_scheduler.run(m_swap, m_collector);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
	QScreen* screen = app.primaryScreen();
	assert(screen != nullptr);
	
    MainWindow window;
	window.setScreen(screen);
	window.resize(screen->size());
	window.show();
    window.setAnimating(true);
	
    return app.exec();
}
