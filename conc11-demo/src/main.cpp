#include "openglwindow.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLPaintDevice>
#include <QtGui/QScreen>
#include <QtGui/QPainter>
#include <QtGui/QPaintEngine>

#include <conc11/Task.h>
#include <conc11/TaskScheduler.h>
#include <conc11/TaskUtils.h>
#include <conc11/TimeIntervalCollector.h>
#include <framework/HighResClock.h>

#include <memory>
#include <numeric>
#include <thread>
#include <vector>

using namespace conc11;

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

class MainWindow : public OpenGLWindow
{
public:
	
    MainWindow();
	virtual ~MainWindow();
	
    virtual void initialize() final;
    virtual void render() final;
	
private:
    GLuint m_posAttr;
    GLuint m_colAttr;
	GLuint m_matrixUniform;
	
	std::unique_ptr<TaskScheduler> m_scheduler;
	std::vector<std::thread::id> m_threadIds;
	std::vector<std::unique_ptr<unsigned>> m_images;
	
	std::shared_ptr<TaskBase> m_renderDataPrepare;
    std::shared_ptr<TaskBase> m_createWork;
	std::shared_ptr<TaskBase> m_workDone;
	std::shared_ptr<TaskBase> m_render;
	std::shared_ptr<TaskBase> m_swap;
	
	std::shared_ptr<TimeIntervalCollector> m_collectors[2];
	std::shared_ptr<TimeIntervalCollector> m_lastFrameCollector;
	
	HighResTimePointType m_frameStart;
	HighResTimePointType m_lastFrameStart;
	
    QOpenGLShaderProgram* m_program;
	std::vector<float> m_vertexBuffer;
	std::vector<Color> m_colorBuffer;

    QPainter m_painter;
	
    unsigned int m_frameIndex;
};

MainWindow::MainWindow()
: m_scheduler(std::make_unique<TaskScheduler>())
, m_program(nullptr)
, m_frameIndex(0)
{
	auto& threads = m_scheduler->getThreads();
	m_threadIds.reserve(threads.size() + 1);
	m_threadIds.push_back(std::this_thread::get_id());
    for (const auto& t : threads)
        m_threadIds.push_back(t.get().get_id());
	
    const unsigned int imageSize = 128;
    const unsigned int imageCnt = 64;
	for (unsigned int i = 0; i < imageCnt; i++)
		m_images.push_back(std::unique_ptr<unsigned>(new unsigned[imageSize*imageSize]));
	
	m_collectors[0].reset(new TimeIntervalCollector);
	m_collectors[1].reset(new TimeIntervalCollector);
	m_lastFrameCollector = m_collectors[0];
	
	m_frameStart = HighResClock::now();
	
	m_renderDataPrepare = createTask([this]
	{
		std::vector<float>& vertices = m_vertexBuffer;
		std::vector<Color>& colors = m_colorBuffer;
		
		vertices.clear();
		colors.clear();
		
		{
			static const Color c0 = createColor(0, 64, 0, 255);
			
			vertices.push_back(-1);
			vertices.push_back(-1);
			colors.push_back(c0);
			
			vertices.push_back(-1);
			vertices.push_back(1);
			colors.push_back(c0);
			
			vertices.push_back(1);
			vertices.push_back(-1);
			colors.push_back(c0);
			
			vertices.push_back(-1);
			vertices.push_back(1);
			colors.push_back(c0);
			
			vertices.push_back(1);
			vertices.push_back(-1);
			colors.push_back(c0);
			
			vertices.push_back(1);
			vertices.push_back(1);
			colors.push_back(c0);
		}
		
		float dy = 2.0f / m_threadIds.size();
		float sy = 0.95f * dy;
		const TimeIntervalCollector::ContainerType& intervals = m_lastFrameCollector->getIntervals();
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
				colors.push_back(ti.color);
				
				vertices.push_back(x);
				vertices.push_back(y + sy);
				colors.push_back(ti.color);
				
				vertices.push_back(x + sx);
				vertices.push_back(y);
				colors.push_back(ti.color);
				
				vertices.push_back(x);
				vertices.push_back(y + sy);
				colors.push_back(ti.color);
				
				vertices.push_back(x + sx);
				vertices.push_back(y);
				colors.push_back(ti.color);
				
				vertices.push_back(x + sx);
				vertices.push_back(y + sy);
				colors.push_back(ti.color);
			}
			
			threadIndex++;
        }
	}, "renderDataPrepare", createColor(0, 128, 0, 255));
	
	m_render = createTask([this]
	{
		auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(m_frameStart - m_lastFrameStart).count();
		auto fps = 1e9 / double(dt);
		
		glClearColor(0, 0, 0.3f, 0);
		glClear(GL_COLOR_BUFFER_BIT);
        
		m_painter.begin(m_device);
        m_painter.setWindow(0, 0, width(), height());
		
        m_painter.setPen(Qt::white);
        m_painter.setFont(QFont("Arial", 30));
        m_painter.drawText(0, 0, 150, 30, Qt::AlignCenter, std::to_string(fps).c_str());

        m_painter.end();

		glViewport(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio());
		
		m_program->bind();

		QMatrix4x4 matrix;
		matrix.perspective(60, float(width()) / float(height()), 0.1f, 100.0f);
		matrix.translate(0, 0, -2);
    //	matrix.rotate(100.0f * m_frameIndex / screen()->refreshRate(), 0, 1, 0);
		m_program->setUniformValue(m_matrixUniform, matrix);
		
		glVertexAttribPointer(m_posAttr, 2, GL_FLOAT, GL_FALSE, 0, m_vertexBuffer.data());
		glVertexAttribPointer(m_colAttr, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, m_colorBuffer.data());
		
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_vertexBuffer.size() / 2);
		
		glDisableVertexAttribArray(1);
		glDisableVertexAttribArray(0);
		
        m_program->release();

	}, "render", createColor(255, 0, 0, 255));
	
	m_createWork = createTask([this, imageSize, imageCnt]
    {
		TaskGroup branches;
		auto launcher = createTask([]{}, "launcher", createColor(255, 255, 255, 255));
		
        for (unsigned int j = 0; j < 2; j++)
        {
            TypedTaskGroup<unsigned int> tasks;
            tasks.reserve(imageCnt);
            for (unsigned int i = 0; i < imageCnt; i++)
            {
                tasks.push_back(createTask([this, i, imageSize]
                {
                    mandel(0, imageSize, imageSize, 0, imageSize, imageSize, m_images[i].get());
                    return i;
                }, launcher, std::string("mandel") + std::to_string(i), createColor(0, j ? 0 : i*(256/imageCnt), j ? i*(256/imageCnt) : 0, 255)));
            }
		
            branches.push_back(join(tasks)->then([](std::vector<unsigned int> vals)
            {
                return std::accumulate(vals.begin(), vals.end(), 0U);
            }, std::string("accumulate") + std::to_string(j), createColor(0, 128, 128, 255)));
        }
		
        m_workDone = join(branches);
		
		m_scheduler->run(launcher);
		
	}, "createWork", createColor(255, 255, 255, 255));
		
	m_swap = createTask([this]
	{
		m_context->swapBuffers(this);
	}, "swap", createColor(255, 255, 0, 255));
}

MainWindow::~MainWindow()
{
	m_scheduler.reset();
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
    m_lastFrameStart = m_frameStart;
	m_frameStart = HighResClock::now();
	m_lastFrameCollector = m_collectors[m_frameIndex % 2];
	++m_frameIndex;
	m_collectors[m_frameIndex % 2]->clear();
	m_scheduler->setTimeIntervalCollector(m_collectors[m_frameIndex % 2]);

	m_scheduler->dispatch(m_renderDataPrepare);
	m_scheduler->dispatch(m_createWork);
	m_scheduler->waitJoin(m_renderDataPrepare);
	m_scheduler->run(m_render);
	m_scheduler->waitJoin(m_createWork);
	m_scheduler->waitJoin(m_workDone);
	m_scheduler->run(m_swap);
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
