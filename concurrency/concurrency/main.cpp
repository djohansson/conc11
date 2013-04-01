#include "TaskScheduler.h"

#include <iostream>
#include <numeric>
#include <string>

unsigned int zeroFunc(void)
{
	return 0U;
}

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

	TaskScheduler scheduler;

	{
		auto print = [](string s)
		{
			unique_lock<mutex> lock(g_coutMutex);
			cout << s << endl;
		};

		auto nothing = scheduler.createTask([]
		{
		}, "nothing");

		auto zero = scheduler.createTask(&zeroFunc, nothing, "zero"); // global function ptr

		auto hello = scheduler.createTask([] // const member function ptr (lambda)
		{
			return string("hello");
		}, "hello");

		auto world = scheduler.createTask([]() mutable // mutable member function ptr (lambda)
		{
			return string(" world!\n");
		}, "world");

		auto helloWorld = scheduler.join(hello, world)->then([](tuple<string, string> t)
		{
			return get<0>(t) + get<1>(t);
		}, "hello world merge")->then(print, "hello world print");

		scheduler.run(helloWorld, TrmSyncJoin);

		vector<shared_ptr<Task<unsigned int>>> tasks;
		for (unsigned int i = 0; i < 1000; i++)
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

		tasks.push_back(scheduler.createTask([](unsigned int v){ return ++v; }, zero));
		tasks.push_back(scheduler.createTask([]{ return 10U; }, nothing));
		tasks.push_back(scheduler.createTask([]{ return 100U; }));

		auto t0 = scheduler.join(tasks)->then([](vector<unsigned int> vals)
		{
			return accumulate(begin(vals), end(vals), 0U);
		}, "t0");
		auto f = [](unsigned int v)
		{
			return v + 1000;
		};
		auto t1 = scheduler.createTask(f, t0, "f")->then([](unsigned int v)
		{
			return v + 10000;
		}, "t1");

		scheduler.run(t1, TrmSyncJoin);

		auto finalString = scheduler.join(t0, t1)->then([](tuple<unsigned int, unsigned int> t)
		{
			return to_string(get<0>(t)) + to_string(get<1>(t));
		}, "finalString merge")->then(print, "finalString print");
	}

	return 0;
}
