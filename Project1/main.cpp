#include<iostream>
#include<thread>
#include<mutex>
#include<functional>
#include<vector>
#include<queue>
#include<condition_variable>
#include<future>
using namespace std;

int Func(int a) {
	this_thread::sleep_for(chrono::seconds(2));
	cout << "Executing payload" << endl;
	return a;
}

class ThreadPool {
private : 
	int m_threads;
	bool stop; //Signal to stop the threadpool
	vector<thread> threads; //all threads in the pool
	queue<function<void()>> tasks; //all tasks in the queue
	mutex mtx;
	condition_variable cv;
public :
	explicit ThreadPool(int num_threads) : m_threads(num_threads), stop(false) {
		
		//To init the thread pool need to create the threads in while(1) for 100% uptime
		for (int i = 0; i < m_threads; i++) {
			//[capture](params) {} is an anon func. [capture] is for a list of all vars to be 
			//made usable in the anon func. [this] allows all variables in the class to be used
			//emplace back is to create the thread in the vector itself. 
			//alt is to push_back(move(threadname))
			//note btw that ou could very well define another function outside instead of lambda
			threads.emplace_back([this] {
				function<void()> task; //takes task from queue.
				while (1) {

					//Need to lock before using shared threads vector.
					unique_lock<mutex> thread_lock(mtx);
					cv.wait(thread_lock, [this] {
						//if Predicate is true in the function the condition variable continues
						//if False it keeps waiting asleep. so solves busy waiting.
						return (stop or tasks.size());
						});
					
					if (stop) { 
						//exit (threadpool destroyed)
						return;
					}

					//Threads cannot be copied and you must use move to move them.
					task = move(tasks.front());
					tasks.pop();
					thread_lock.unlock();
					//Unlock after reading the task and perform it.
					task();
				}
				});
		}
	}

	~ThreadPool() {
		unique_lock<mutex> stop_lock(mtx);
		stop = true;
		stop_lock.unlock();
		//Notifies all the threads of change in cv predicate
		cv.notify_all();
		for (auto &thread : threads) {
			thread.join(); //All threads wait here until each thread is not here. wait() equivalent
		}
	}

	//Since function could return any type and accept any params make it a template.
	//Since async exec takes place it returns a future. This btw is called a trailing return
	//decltype works out the return type of f(args...) on it's  own. && is for perfect forwarding in bind.
	template<class F, class... Args> 
	auto ExecuteTask(F&& f, Args&&... args) -> future<decltype(f(args...))> {
		using return_type = decltype(f(args...));
		//he class template std::packaged_task wraps any Callable target 
		//(function, lambda expression, bind expression, or another function object) 
		//so that it can be invoked asynchronously. Its return value or exception thrown 
		//is stored in a shared state which can be accessed through std::future objects.
		//Since our end goal is to make a task as a function<void()> it takes in no inputs.
		//instead we bind everything. g = bind(f,args...) => g() = f(args...) forward preserves input type
		//call by value/ref remains the same.
		//Using forwarding references takes advantage of C++'s reference collapsing rules:
		//If the argument is an lvalue, T&& deduces as T& .
		//If the argument is an rvalue, T&& remains T&& .
		//This behavior allows the arguments to be forwarded with std::forward, 
		//preserving their original value category. this needs to be a shared pointer so any thread can
		//access the task packaged_task<return_type()> should be wrapped in make_shared<> 
		//make_shared<T>(args) = make a shared pointer out of T(args) hence task is a shared ptr of
		//packaged_task<return_type()>(bind(forward<F>(f), forward<Args>(args)...))
		auto task = make_shared<packaged_task<return_type()>>(bind(forward<F>(f), forward<Args>(args)...));
		future<return_type> res = task->get_future();
		unique_lock<mutex> tasks_lock(mtx);
		//Since the tasks is expecting a function<void()> we make it so that the void function calls the task
		tasks.emplace([task]() {
			(*task)();
			});
		tasks_lock.unlock();
		//any one waiting task is notified.
		cv.notify_one();
		return res;
	}
};

int main() {
	ThreadPool pool(8);
	future<int> result = pool.ExecuteTask(Func, 10);
	cout << result.get() << endl;
	/*while (1) {
		auto fut = pool.ExecuteTask(Func,10);
	}*/
}