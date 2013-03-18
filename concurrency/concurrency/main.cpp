#include "TaskScheduler.h"

#include <iostream>
#include <numeric>
#include <string>

int zeroFunc(void)
{
	return 0;
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
			bool isInside = true;
			unsigned n = 0;
			for(; n<MaxIterations; ++n)
			{
				double Z_re2 = Z_re*Z_re, Z_im2 = Z_im*Z_im;
				if(Z_re2 + Z_im2 > 4)
				{
					isInside = false;
					break;
				}
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

	auto print = [](string s)
	{
		unique_lock<mutex> lock(g_coutMutex);
		cout << s << endl;
	};

	auto hello = scheduler.createTask([]
	{
		return string("hello");
	}, "hello");

	auto world = scheduler.createTask([]()
	{
		return string(" world!");
	}, "world");

	auto helloWorld = scheduler.join(hello, world)->then([](tuple<string, string> t)
	{
		return get<0>(t) + get<1>(t);
	}, "hello world join")->then(print, "hello world print");
	
	vector<shared_ptr<Task<int>>> tasks;
	for (unsigned int i = 0; i < 200; i++)
	{
		tasks.push_back(scheduler.createTask([=]
		{
			{
				unique_lock<mutex> lock(g_coutMutex);
				cout << to_string(i) << ":[" << this_thread::get_id() << "]" << endl;
			}

			static const unsigned int imageSize = 1024;
			unique_ptr<unsigned> image(new unsigned[imageSize*imageSize]);
			mandel(0, imageSize, imageSize, 0, imageSize, imageSize, image.get());
			return i;
		}, string("mandel") + to_string(i))->then([=](int val)
		{
			unique_lock<mutex> lock(g_coutMutex);
			cout << to_string(i) << ":[" << this_thread::get_id() << "] c" << endl;
			return val;
		}, string("mandel progress") + to_string(i)));

		tasks.back()->getEnabler()->enable();
	}

	auto nothing = scheduler.createTask([]
	{
	}, "nothing");
	auto zero = scheduler.createTask(&zeroFunc, nothing, "zero");

	tasks.push_back(scheduler.createTask([](int v){ return ++v; }, zero));
	tasks.push_back(scheduler.createTask([]{ return 10; }, nothing));
	tasks.push_back(scheduler.createTask([]{ return 100; }));

	auto t0 = scheduler.join(tasks)->then([](vector<int> vals)
	{
		return accumulate(begin(vals), end(vals), 0);
	}, "t0");
	auto f = [](int v)
	{
		return v + 1000;
	};
	auto t1 = scheduler.createTask(f, t0, "f")->then([](int v)
	{
		return v + 10000;
	}, "t1");

	hello->getEnabler()->enable();
	world->getEnabler()->enable();
	tasks.back()->getEnabler()->enable();
	nothing->getEnabler()->enable();

	/*auto finalString = scheduler.join(hello, world, t1)->then([](tuple<string, string, int> t)
	{
		return get<0>(t) + get<1>(t) + to_string(get<2>(t));
	}, "finalString join")->then([](string s)
	{
		unique_lock<mutex> lock(g_coutMutex);
		cout << s << endl;
	}, "finalString print");*/

	return 0;
}
