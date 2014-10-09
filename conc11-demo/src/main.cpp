#include "openglwindow.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QOpenGLBuffer>
#include <QtGui/QOpenGLDebugLogger>
#include <QtGui/QOpenGLPaintDevice>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLVertexArrayObject>
#include <QtGui/QPainter>
#include <QtGui/QPaintEngine>
#include <QtGui/QScreen>

#include <conc11/TaskScheduler.h>
#include <conc11/TaskUtils.h>
#include <conc11/TimeIntervalCollector.h>
#include <framework/HighResClock.h>

#include <memory>
#include <numeric>
#include <thread>
#include <vector>

using namespace conc11;

std::atomic<uint32_t> TaskBase::s_instanceCount(0);

static const char* g_vertexShaderSource =
R"(#version 150
in highp vec4 posAttr;
in lowp vec4 colAttr;
out lowp vec4 col;
uniform mat4 matrix;
void main()
{
	col = colAttr;
	gl_Position = matrix * posAttr;
})";

static const char* g_fragmentShaderSource =
R"(#version 150
in lowp vec4 col;
out lowp vec4 fragColor;
void main()
{
	fragColor = col;
})";

void mandel(unsigned xmin, unsigned xmax, unsigned xsize, unsigned ymin, unsigned ymax, unsigned ysize, unsigned* image)
{
	double MinRe = -2.0;
	double MaxRe = 1.0;
	double MinIm = -1.2;
	double MaxIm = MinIm + (MaxRe - MinRe)*ysize / xsize;
	double Re_factor = (MaxRe - MinRe) / (xsize - 1);
	double Im_factor = (MaxIm - MinIm) / (ysize - 1);
	unsigned MaxIterations = 30;

	for (unsigned y = ymin; y < ymax; ++y)
	{
		double c_im = MaxIm - y*Im_factor;
		for (unsigned x = xmin; x < xmax; ++x)
		{
			double c_re = MinRe + x*Re_factor;

			double Z_re = c_re, Z_im = c_im;
			unsigned n = 0;
			for (; n<MaxIterations; ++n)
			{
				double Z_re2 = Z_re*Z_re, Z_im2 = Z_im*Z_im;

				if (Z_re2 + Z_im2 > 4)
					break;

				Z_im = 2 * Z_re*Z_im + c_im;
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

	virtual void render() final;

private:
	GLuint m_posAttr;
	GLuint m_colAttr;
	GLuint m_matrixUniform;

	TaskScheduler m_scheduler;
	std::vector<std::thread::id> m_threadIds;
	std::vector<std::unique_ptr<unsigned>> m_images;

	std::shared_ptr<TaskBase> m_renderDataUpdate;
	std::shared_ptr<TaskBase> m_createWork;
	std::shared_ptr<TaskBase> m_workDone;
	std::shared_ptr<TaskBase> m_render;
	std::shared_ptr<TaskBase> m_swap;

	std::vector<std::shared_ptr<TimeIntervalCollector>> m_collectors;

	HighResTimePointType m_frameStart;
	HighResTimePointType m_lastFrameStart;

	QOpenGLShaderProgram* m_program;
	QOpenGLVertexArrayObject* m_vao;
	QOpenGLBuffer m_positionBuffer;
	QOpenGLBuffer m_colorBuffer;
	std::unique_ptr<QOpenGLDebugLogger> m_logger;
	
	QPainter m_painter;

    std::atomic<uint32_t> m_frameIndex;
};

MainWindow::MainWindow()
: m_program(nullptr)
, m_vao(nullptr)
, m_positionBuffer(QOpenGLBuffer::VertexBuffer)
, m_colorBuffer(QOpenGLBuffer::VertexBuffer)
, m_logger(nullptr)
, m_frameIndex(0)
{
	m_context->makeCurrent(this);

	m_program = new QOpenGLShaderProgram(this);
	m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, g_vertexShaderSource);
	m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, g_fragmentShaderSource);
	m_program->link();
	
	auto gl = getGl();
	Q_ASSERT(gl);
	gl->glReleaseShaderCompiler();
	
	m_posAttr = m_program->attributeLocation("posAttr");
	m_colAttr = m_program->attributeLocation("colAttr");
	m_matrixUniform = m_program->uniformLocation("matrix");
	
	m_vao = new QOpenGLVertexArrayObject(this);
	m_vao->create();
	m_vao->bind();
	
	m_positionBuffer.create();
	m_positionBuffer.setUsagePattern(QOpenGLBuffer::StreamDraw);
	m_positionBuffer.bind();
	m_positionBuffer.allocate(16*1024 * 2 * sizeof(float));
	m_program->enableAttributeArray(m_posAttr);
	m_program->setAttributeBuffer(m_posAttr, GL_FLOAT, 0, 2);
	m_positionBuffer.release();
	
	m_colorBuffer.create();
	m_colorBuffer.setUsagePattern(QOpenGLBuffer::StreamDraw);
	m_colorBuffer.bind();
	m_colorBuffer.allocate(16*1024 * sizeof(Color::StoreType));
	m_program->enableAttributeArray(m_colAttr);
	m_program->setAttributeBuffer(m_colAttr, GL_UNSIGNED_BYTE, 0, 4);
	m_colorBuffer.release();
	
	m_vao->release();
	
	m_logger.reset(new QOpenGLDebugLogger(this));
	
	auto glDebugHandler = [this](QOpenGLDebugMessage message)
	{
		setRenderEnable(false);

		qDebug() << message;

		Q_ASSERT(message.severity() >= QOpenGLDebugMessage::LowSeverity);

		setRenderEnable(true);
	};

    connect(m_logger.get(), &QOpenGLDebugLogger::messageLogged, this, glDebugHandler, Qt::DirectConnection);
	
    if (m_logger->initialize())
	{
        m_logger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
        m_logger->enableMessages();
    }

	auto& threads = m_scheduler.getThreads();
	m_threadIds.reserve(threads.size() + 1);
	m_threadIds.push_back(std::this_thread::get_id());
	for (const auto& t : threads)
		m_threadIds.push_back(t.get().get_id());

	const unsigned int imageSize = 64;
	const unsigned int imageCnt = 32;
    const unsigned int branchCnt = 16;
	for (unsigned int i = 0; i < imageCnt; i++)
		m_images.emplace_back(std::unique_ptr<unsigned>(new unsigned[imageSize*imageSize]));

	const unsigned int collectorCount = 2;
	for (unsigned int i = 0; i < collectorCount; i++)
		m_collectors.emplace_back(std::make_unique<TimeIntervalCollector>());
	
	m_frameStart = HighResClock::now();

	m_renderDataUpdate = createTask([this]
	{
		m_context->makeCurrent(this);

		m_positionBuffer.bind();
		float* positions = static_cast<float*>(m_positionBuffer.map(QOpenGLBuffer::WriteOnly));
		m_colorBuffer.bind();
		Color::StoreType* colors = static_cast<Color::StoreType*>(m_colorBuffer.map(QOpenGLBuffer::WriteOnly));
		
		Q_ASSERT(positions);
		Q_ASSERT(colors);
        
        unsigned int pi = 0;
        unsigned int ci = 0;
        
        auto writeData = createTask([this, &positions, &colors, &pi, &ci]
        {
            {
                static const Color c0 = createColor(0, 64, 0, 64);
                
                positions[pi++] = -1;
                positions[pi++] = -1;
                colors[ci++] = c0.getStore();
                
                positions[pi++] = -1;
                positions[pi++] = 1;
                colors[ci++] = c0.getStore();
                
                positions[pi++] = 1;
                positions[pi++] = -1;
                colors[ci++] = c0.getStore();
                
                positions[pi++] = -1;
                positions[pi++] = 1;
                colors[ci++] = c0.getStore();
                
                positions[pi++] = 1;
                positions[pi++] = -1;
                colors[ci++] = c0.getStore();
                
                positions[pi++] = 1;
                positions[pi++] = 1;
                colors[ci++] = c0.getStore();
            }
            
            float fx = 2.0f / (m_collectors.size() - 1);
            float dy = 2.0f / m_threadIds.size();
            float sy = 0.95f * dy;
            for (unsigned int i = 1; i < static_cast<unsigned int>(m_collectors.size()); ++i)
            {
                const TimeIntervalCollector::ContainerType& intervals = m_collectors[(m_frameIndex - i) % m_collectors.size()]->getIntervals();
                unsigned int threadIndex = 0;
                for (auto& threadId : m_threadIds)
                {
                    auto ip = intervals.equal_range(threadId);
                    for (auto it = ip.first; it != ip.second; it++)
                    {
                        auto& ti = (*it).second;
                        const Color& c = ti.color;

                        auto start = std::chrono::duration_cast<std::chrono::nanoseconds>(ti.start - m_lastFrameStart).count();
                        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(ti.end - ti.start).count();

                        static float apa = 64.0f;
                        float scaleX = (apa / (m_collectors.size() - 1)) / 1e9f;
                        float x = -1 + fx * (i - 1) + scaleX * static_cast<float>(start);
                        float y = -1 + (threadIndex * dy);
                        float sx = scaleX * static_cast<float>(duration);

                        positions[pi++] = x;
                        positions[pi++] = y;
                        colors[ci++] = c.getStore();

                        positions[pi++] = x;
                        positions[pi++] = y + sy;
                        colors[ci++] = c.getStore();

                        positions[pi++] = x + sx;
                        positions[pi++] = y;
                        colors[ci++] = c.getStore();

                        positions[pi++] = x;
                        positions[pi++] = y + sy;
                        colors[ci++] = c.getStore();

                        positions[pi++] = x + sx;
                        positions[pi++] = y;
                        colors[ci++] = c.getStore();

                        positions[pi++] = x + sx;
                        positions[pi++] = y + sy;
                        colors[ci++] = c.getStore();
                    }

                    threadIndex++;
                }
            }
        }, "writeData", createColor(128, 255, 128, 255));
        
        m_scheduler.run(writeData);
        
        assert(pi < m_positionBuffer.size() / (2 * sizeof(float)));
        assert(ci < m_colorBuffer.size() / sizeof(Color::StoreType));
		
		m_positionBuffer.bind();
		m_positionBuffer.unmap();
		m_colorBuffer.bind();
		m_colorBuffer.unmap();
		
	}, "renderDataMap", createColor(255, 128, 128, 255));

	m_render = createTask([this]
	{
		//auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(m_frameStart - m_lastFrameStart).count();
		//auto fps = 1e9 / double(dt);

		{
			m_context->makeCurrent(this);
			auto gl = getGl();
			Q_ASSERT(gl);

			gl->glClearColor(0, 0, 0.3f, 0);
			gl->glClear(GL_COLOR_BUFFER_BIT);
			gl->glViewport(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio());

			m_program->bind();
			m_vao->bind();

			QMatrix4x4 matrix;
			matrix.ortho(-1, 1, -1, 1, -1, 1);
			m_program->setUniformValue(m_matrixUniform, matrix);

			gl->glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_positionBuffer.size() / 2);

			m_vao->release();
			m_program->release();
		}

//		{
//			m_paintContext->makeCurrent(this);
//
//			m_painter.begin(m_paintDevice.get());
//			m_painter.setWindow(0, 0, width(), height());
//
//			m_painter.setPen(Qt::white);
//			m_painter.setFont(QFont("Arial", 30));
//			m_painter.drawText(0, 0, 300, 60, Qt::AlignCenter, std::to_string(fps).c_str());
//
//			m_painter.end();
//		}

	}, "render", createColor(255, 0, 0, 255));

	m_createWork = createTask([this, imageSize, imageCnt, branchCnt]
	{
        auto branches = UntypedTaskGroup::create();
        branches->reserve(branchCnt);
		auto launcher = createTask([]{}, "launcher", createColor(255, 255, 255, 255));

		for (unsigned int j = 0; j < branchCnt; j++)
		{
            auto tasks = UntypedTaskGroup::create();
			tasks->reserve(imageCnt);
			for (unsigned int i = 0; i < imageCnt; i++)
			{
				tasks->emplace_back(createTask([this, i, imageSize]
				{
					mandel(0, imageSize, imageSize, 0, imageSize, imageSize, m_images[i].get());
					return i;
				}, launcher, std::string("mandel") + std::to_string(i), createColor(0, j ? 0 : i*(256 / imageCnt), j ? i*(256 / imageCnt) : 0, 255)));
			}

            branches->emplace_back(join(tasks)->then([]
			{
                std::this_thread::sleep_for(std::chrono::microseconds(3000));
			}, std::string("sleep") + std::to_string(j), createColor(0, 128, 128, 255)));
		}

		m_workDone = join(branches);
		m_scheduler.dispatch(launcher);
        
	}, "createWork", createColor(255, 255, 255, 255));

	m_swap = createTask([this]
	{
		m_context->makeCurrent(this);
		m_context->swapBuffers(this);
        
	}, "swap", createColor(255, 255, 0, 255));
}

MainWindow::~MainWindow()
{
}

void MainWindow::render()
{
	m_lastFrameStart = m_frameStart;
	m_frameStart = HighResClock::now();
	
	++m_frameIndex;

	auto& collector = m_collectors[m_frameIndex % m_collectors.size()];
	collector->clear();
	m_scheduler.setTimeIntervalCollector(collector);

	m_scheduler.dispatch(m_createWork);
	m_scheduler.run(m_renderDataUpdate);
	m_scheduler.run(m_render);
	m_scheduler.run(m_swap);
    m_scheduler.processQueueUntil(m_workDone);
    m_scheduler.processQueueUntil(m_createWork);
}

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	QScreen* screen = app.primaryScreen();
	Q_ASSERT(screen != nullptr);

	MainWindow window;
	window.setScreen(screen);
	window.resize(screen->size());
	window.show();
	window.setAnimating(true);

	return app.exec();
}
