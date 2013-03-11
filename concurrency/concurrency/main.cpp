#include "TaskFuture.h"
#include "TaskScheduler.h"

#include <iostream>
#include <numeric>
#include <string>

std::mutex g_coutMutex;

int zeroFunc(void)
{
	return 0;
}

int ackermann(int m, int n)
{
	if (m==0)
		return n+1;

	if (n==0)
		return ackermann(m-1, 1);

	return ackermann(m-1, ackermann(m, n-1));
}

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	using namespace std;
	using namespace conc11;

	TaskScheduler scheduler;

	auto nothing = scheduler.createTask([]
	{
	});
	auto zero = scheduler.createTask(&zeroFunc, nothing);
	auto hello = scheduler.createTask([]
	{
		return string("\nhello");
	});
	auto world = scheduler.createTask([]
	{
		return string(" world!\n");
	});
	auto helloWorld = scheduler.join(hello, world).then([](tuple<string, string> t)
	{
		return get<0>(t) + get<1>(t);
	});
	auto longTime = []
	{
		{
			unique_lock<mutex> lock(g_coutMutex);
			cout << "[" << std::this_thread::get_id() << ",";
		}
		return ackermann(3, 11);
	};
	auto progress = [](int val)
	{
		unique_lock<mutex> lock(g_coutMutex);
		cout << std::this_thread::get_id() << "]";
		return val;
	};
	
	vector<TaskFuture<int>> tasks;
	for (unsigned int i = 0; i < 20; i++)
		tasks.push_back(scheduler.createTask(longTime).then(progress));

	tasks.push_back(scheduler.createTask([](int v){ return ++v; }, zero));
	tasks.push_back(scheduler.createTask([]{ return 10; }, nothing));
	tasks.push_back(scheduler.createTask([]{ return 100; }));

	auto t0 = scheduler.join(tasks).then([](vector<int> vals)
	{
		return accumulate(begin(vals), end(vals), 0);
	});
	auto f = [](int v)
	{
		return v + 1000;
	};
	auto t1 = scheduler.createTask(f, t0).then([](int v)
	{
		return v + 10000;
	});
	auto finalString = scheduler.join(helloWorld, t1).then([](tuple<string, int> t)
	{
		return get<0>(t) + to_string(get<1>(t));
	}).then([](string s)
	{
		unique_lock<mutex> lock(g_coutMutex);
		cout << s << endl;
	});

	return 0;
}
