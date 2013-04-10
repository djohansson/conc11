#include "TaskScheduler.h"

#include <chrono>
#include <memory>
#include <numeric>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <GL/glew.h>
#include <GL/glfw.h>

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

static const char* getGLVersion()
{
	if (GLEW_VERSION_4_3)
		return "4.3";
	else if (GLEW_VERSION_4_2)
		return "4.2";
	else if (GLEW_VERSION_4_1)
		return "4.1";
	else if (GLEW_VERSION_4_0)
		return "4.0";
	else if (GLEW_VERSION_3_3)
		return "3.3";
	else if (GLEW_VERSION_3_2)
		return "3.2";
	else if (GLEW_VERSION_3_1)
		return "3.1";
	else if (GLEW_VERSION_3_0)
		return "3.0";
	else if (GLEW_VERSION_2_1)
		return "2.1";
	else if (GLEW_VERSION_2_0)
		return "2.0";
	else if (GLEW_VERSION_1_5)
		return "1.5";
	else if (GLEW_VERSION_1_4)
		return "1.4";
	else if (GLEW_VERSION_1_3)
		return "1.3";
	else if (GLEW_VERSION_1_2_1)
		return "1.2.1";
	else if (GLEW_VERSION_1_2)
		return "1.2";
	else if (GLEW_VERSION_1_1)
		return "1.1";

	assert(false);
	return "";
}

void lerp(const float a[3], const float b[3], float t, float out[3])
{
	out[0] = (1.0f - t) * a[0] + t * b[0];
	out[1] = (1.0f - t) * a[1] + t * b[1];
	out[2] = (1.0f - t) * a[2] + t * b[2];
}

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	using namespace std;
	using namespace conc11;

	glewExperimental = true;

	auto glfwInitResult = glfwInit();
	glfwSwapInterval(1);
	auto glfwOpenWindowResult = glfwOpenWindow(1280, 720, 8, 8, 8, 0, 0, 0, GLFW_WINDOW);
	auto glewInitResult = glewInit();

	if (!glfwInitResult || !glfwOpenWindowResult || glewInitResult != GLEW_OK)
	{
		glfwTerminate();

		return -1;
	}
	
	TaskScheduler scheduler;
	
	auto& threads = scheduler.getThreads();
	vector<thread::id> threadIds;
	threadIds.reserve(threads.size() + 1);
	threadIds.push_back(this_thread::get_id());
	for (auto t : threads)
		threadIds.push_back(t->get_id());

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepth(0.0);
	glClearStencil(0);

	float red[3] = { 0.8f, 0.2f, 0.2f };
	float green[3] = { 0.2f, 0.8f, 0.2f };
	float blue[3] = { 0.2f, 0.2f, 0.8f };
	float yellow[3] = { 1.0f, 1.0f, 0.0f };
	float cyan[3] = { 0.0f, 1.0f, 1.0f };
	float magenta[3] = { 1.0f, 0.0f, 1.0f };
	float pink[3] = { 1.0f, 0.5f, 1.0f };
	float gray[3] = { 0.5f, 0.5f, 0.5f };
	
	vector<unique_ptr<unsigned>> images;
	const unsigned int imageSize = 256;
	const unsigned int imageCnt = 32;
	for (unsigned int i = 0; i < imageCnt; i++)
		images.push_back(unique_ptr<unsigned>(new unsigned[imageSize*imageSize]));

	shared_ptr<TimeIntervalCollector> collector = make_shared<TimeIntervalCollector>();
	auto frameStart = chrono::high_resolution_clock::now();
	auto lastFrameStart = frameStart;
	auto drawIntervalsTimeStart = frameStart;
	
	auto updateWindowTitle = scheduler.createTask([&frameStart, &lastFrameStart]
	{
	  auto dt = chrono::duration_cast<chrono::nanoseconds>(frameStart - lastFrameStart).count();
	  auto fps = 1e9 / double(dt);
	  
	  stringstream str;
	  str << "conc11 (OpenGL " << getGLVersion() << ")" << " " << to_string(fps) << " fps";
	  glfwSetWindowTitle(str.str().c_str());
	  
	}, "updateWindowTitle", cyan);
	
	auto clear = scheduler.createTask([]
	{
		glClear(GL_COLOR_BUFFER_BIT);
	}, "clear", pink);
	
	auto swap = scheduler.createTask([]
	{
		glfwSwapBuffers();
	}, "swap", yellow);
	
	auto drawIntervals = scheduler.createTask([&threadIds, &drawIntervalsTimeStart, &collector]
	{
		float dy = 2.0f / threadIds.size();
		float sy = 0.95f * dy;
		const TimeIntervalCollector::ContainerType& intervals = collector->getIntervals();
		unsigned int threadIndex = 0;
		for (auto& threadId : threadIds)
		{
			auto ip = intervals.equal_range(threadId);
			for (auto it = ip.first; it != ip.second; it++)
			{
				auto& ti = (*it).second;
				
				glColor3f(ti.debugColor[0], ti.debugColor[1], ti.debugColor[2]);
				
				auto start = chrono::duration_cast<chrono::nanoseconds>(ti.start - drawIntervalsTimeStart).count();
				auto duration = chrono::duration_cast<chrono::nanoseconds>(ti.end - ti.start).count();
				
				const float scale = 24.0f;
				float x = -1.0f + scale * 2.0f * static_cast<float>(start) / 1e9f;
				float y = 1.0f - (threadIndex * dy);
				float sx = scale * 2.0f * static_cast<float>(duration) / 1e9f;
				
				glBegin(GL_TRIANGLES);
				{
					glVertex3f(x, y, 0.0f);
					glVertex3f(x, y - sy, 0.0f);
					glVertex3f(x + sx, y, 0.0f);
				}
				glEnd();
				glBegin(GL_TRIANGLES);
				{
					glVertex3f(x, y - sy, 0.0f);
					glVertex3f(x + sx, y, 0.0f);
					glVertex3f(x + sx, y - sy, 0.0f);
				}
				glEnd();
			}
			
			threadIndex++;
		}
		
		collector->clear();
		
	}, "drawIntervals", blue);
	
	auto createWork = scheduler.createTask([&scheduler, &collector, &red, &green, &blue, &magenta, &images, imageSize, imageCnt]
	{
		vector<shared_ptr<Task<unsigned int>>> tasks;
		
		for (unsigned int i = 0; i < imageCnt; i++)
		{
			float color[3];
			lerp(red, green, float(i) / imageCnt, color);
			
			tasks.push_back(scheduler.createTask([i, imageSize, &images]
			{
				mandel(0, imageSize, imageSize, 0, imageSize, imageSize, images[i].get());
				return i;
			}, string("mandel") + to_string(i), color));
		}
		
		auto t = scheduler.join(tasks)->then([](vector<unsigned int> vals)
		{
			return accumulate(begin(vals), end(vals), 0U);
		}, "t", magenta);
		
		scheduler.dispatch(t, collector);
		
	}, "createWork", gray);
	
	unsigned int frameIndex = 0;
	while (glfwGetWindowParam(GLFW_OPENED))
	{
		frameIndex++;
		lastFrameStart = frameStart;
		drawIntervalsTimeStart = frameStart;
		frameStart = chrono::high_resolution_clock::now();

		scheduler.run(clear, collector);
		scheduler.run(drawIntervals, collector);
		scheduler.dispatch(createWork, collector);
		scheduler.run(updateWindowTitle, collector);
		scheduler.waitJoin();
		scheduler.run(swap, collector);
	}

	glfwTerminate();

	return 0;
}
