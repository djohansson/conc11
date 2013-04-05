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

	if (!glfwInit() || !glfwOpenWindow(1280, 720, 8, 8, 8, 8, 24, 8, GLFW_WINDOW) || glewInit() != GLEW_OK)   
	{
		glfwTerminate();

		return -1;
	}

	stringstream str;
	str << "conc11 (OpenGL " << getGLVersion() << ")";

	TaskScheduler scheduler;
    
    auto& threads = scheduler.getThreads();
    vector<thread::id> threadIds;
    threadIds.reserve(threads.size() + 1);
    threadIds.push_back(this_thread::get_id());
    for (auto t : threads)
        threadIds.push_back(t->get_id());

	glfwSetWindowTitle(str.str().c_str());

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepth(0.0);
	glClearStencil(0);
    
    float color0[3] = { 0.8f, 0.2f, 0.2f };
    float color1[3] = { 0.2f, 0.8f, 0.2f };
    float color2[3] = { 0.2f, 0.2f, 0.8f };
    
	while (glfwGetWindowParam(GLFW_OPENED))
	{
        auto frameStart = chrono::high_resolution_clock::now();
        
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

		vector<shared_ptr<Task<unsigned int>>> tasks;
		for (unsigned int i = 0, cnt = 64; i < cnt; i++)
		{
            float color[3];
            lerp(color0, color1, float(i) / cnt, color);

			tasks.push_back(scheduler.createTask([i]
			{
				static const unsigned int imageSize = 1024;
				unique_ptr<unsigned> image(new unsigned[imageSize*imageSize]);
				mandel(0, imageSize, imageSize, 0, imageSize, imageSize, image.get());
				return i;
			}, string("mandel") + to_string(i)));
            tasks.back()->setDebugColor(color);
		}

		auto t0 = scheduler.join(tasks);
        t0->setDebugColor(color2);
        auto t1 = t0->then([](vector<unsigned int> vals)
        {
            return accumulate(begin(vals), end(vals), 0U);
        }, "t1");

		shared_ptr<TimeIntervalCollector> collector = make_shared<TimeIntervalCollector>();
		scheduler.dispatch(t1, collector);
		scheduler.waitJoin();

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
                
                auto start = chrono::duration_cast<chrono::nanoseconds>(ti.start - frameStart).count();
                auto duration = chrono::duration_cast<chrono::nanoseconds>(ti.end - ti.start).count();

                float x = -1.0f + 2.0f * static_cast<float>(start) / 1e9f;
				float y = 1.0f - (threadIndex * dy);
                float sx = 2.0f * static_cast<float>(duration) / 1e9f;
				
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

		glfwSwapBuffers();
	}

	glfwTerminate();

	return 0;
}
