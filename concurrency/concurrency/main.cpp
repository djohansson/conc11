#include "TaskScheduler.h"

#include <iostream>
#include <numeric>
#include <string>

#ifdef _MSC_VER
#include <strsafe.h>
#include <windows.h>
void ErrorExit() 
{ 
	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();
	LPTSTR lpszFunction = TEXT("GetProcessId");

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &lpMsgBuf,
		0, NULL );

	lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR)); 

	StringCchPrintf((LPTSTR)lpDisplayBuf, 
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("%s failed with error %d: %s"), 
		lpszFunction, dw, lpMsgBuf); 

	MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK); 

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);

	ExitProcess(dw); 
}
#else
void ErrorExit() 
{
	exit(EXIT_FAILURE);
}
#endif

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

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	using namespace std;
	using namespace conc11;

	if (!glfwInit() || !glfwOpenWindow(1280, 720, 8, 8, 8, 0, 24, 0, GLFW_WINDOW))   
	{
	//	glfwTerminate();
		ErrorExit();
	}

	TaskScheduler scheduler;

	glfwSetWindowTitle("conc11");

	while (glfwGetWindowParam(GLFW_OPENED))
	{
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glClearDepth(0.0);
		glClearStencil(0);
		glClear(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

		vector<shared_ptr<Task<unsigned int>>> tasks;
		for (unsigned int i = 0; i < 16; i++)
		{
			tasks.push_back(scheduler.createTask([i]
			{
				{
					unique_lock<mutex> lock(g_coutMutex);
					cout << to_string(i) << ":[" << this_thread::get_id() << "]" << endl;
				}

				static const unsigned int imageSize = 1024;
				unique_ptr<unsigned> image(new unsigned[imageSize*imageSize]);
				mandel(0, imageSize, imageSize, 0, imageSize, imageSize, image.get());
				return i;
			}, string("mandel") + to_string(i))->then([](unsigned int val)
			{
				unique_lock<mutex> lock(g_coutMutex);
				cout << to_string(val) << ":[" << this_thread::get_id() << "] c" << endl;
				return val;
			}, string("mandel progress") + to_string(i)));
		}

		auto t0 = scheduler.join(tasks)->then([](vector<unsigned int> vals)
		{
			return accumulate(begin(vals), end(vals), 0U);
		}, "t0");

		scheduler.run(t0, TrmSyncJoin);

		glColor3f(1.0f, 0.85f, 0.35f);
		glBegin(GL_TRIANGLES);
		{
			glVertex3f( 0.0f,  0.6f, 0.0f);
			glVertex3f(-0.2f, -0.3f, 0.0f);
			glVertex3f( 0.2f, -0.3f, 0.0f);
		}
		glEnd();

		glfwSwapBuffers();
	}

	glfwTerminate();

	return 0;
}
