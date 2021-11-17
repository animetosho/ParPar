#ifndef __THREADQUEUE_H__
#define __THREADQUEUE_H__

#include <uv.h>
#include <queue>

template<typename T>
class ThreadMessageQueue {
	std::queue<T> q;
	uv_mutex_t mutex;
	uv_cond_t cond;
	bool skipDestructor;
	
	// disable copy constructor
	ThreadMessageQueue(const ThreadMessageQueue&);
	ThreadMessageQueue& operator=(const ThreadMessageQueue&);
	// ...but allow moves
	void move(ThreadMessageQueue& other) {
		q = std::move(other.q);
		mutex = other.mutex;
		cond = other.cond;
		skipDestructor = false;
		
		other.skipDestructor = true;
	}
	
	
public:
	ThreadMessageQueue() {
		uv_mutex_init(&mutex);
		uv_cond_init(&cond);
		skipDestructor = false;
	}
	~ThreadMessageQueue() {
		if(skipDestructor) return;
		uv_mutex_destroy(&mutex);
		uv_cond_destroy(&cond);
	}
	
	ThreadMessageQueue(ThreadMessageQueue&& other) noexcept {
		move(other);
	}
	ThreadMessageQueue& operator=(ThreadMessageQueue&& other) noexcept {
		move(other);
		return *this;
	}	
	void push(T item) {
		uv_mutex_lock(&mutex);
		q.push(item);
		uv_cond_signal(&cond);
		uv_mutex_unlock(&mutex);
	}
	template<class Iterable>
	void push_multi(const Iterable& list) {
		uv_mutex_lock(&mutex);
		for(auto it = list.cbegin(); it != list.cend(); ++it) {
			q.push(*it);
		}
		uv_cond_signal(&cond);
		uv_mutex_unlock(&mutex);
	}
	T pop() {
		uv_mutex_lock(&mutex);
		while(q.empty()) {
			uv_cond_wait(&cond, &mutex);
		}
		T item = q.front();
		q.pop();
		uv_mutex_unlock(&mutex);
		return item;
	}
	
	bool trypop(T* item) {
		bool notEmpty;
		uv_mutex_lock(&mutex);
		notEmpty = !q.empty();
		if(notEmpty) {
			*item = q.front();
			q.pop();
		}
		uv_mutex_unlock(&mutex);
		return notEmpty;
	}
	
	size_t size() {
		uv_mutex_lock(&mutex);
		size_t s = q.size();
		uv_mutex_unlock(&mutex);
		return s;
	}
	bool empty() {
		uv_mutex_lock(&mutex);
		bool e = q.empty();
		uv_mutex_unlock(&mutex);
		return e;
	}
};


#if defined(_WINDOWS) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
# define NOMINMAX
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
#else
# include <pthread.h>
#endif
#if defined(__linux) || defined(__linux__)
# include <unistd.h>
#endif
class MessageThread {
	ThreadMessageQueue<void*> q;
	uv_thread_t thread;
	bool threadActive;
	bool threadCreated;
	uv_thread_cb cb;
	
	static void thread_func(void* parent) {
		MessageThread* self = static_cast<MessageThread*>(parent);
		ThreadMessageQueue<void*>& q = self->q;
		uv_thread_cb cb = self->cb;
		
		void* item;
		while((item = q.pop()) != NULL) {
			cb(item);
		}
	}
	
	static void thread_func_low_prio(void* parent) {
		#if defined(_WINDOWS) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
		HANDLE hThread = GetCurrentThread();
		switch(GetThreadPriority(hThread)) {
			case THREAD_PRIORITY_TIME_CRITICAL:
				SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);
				break;
			case THREAD_PRIORITY_HIGHEST:
				SetThreadPriority(hThread, THREAD_PRIORITY_ABOVE_NORMAL);
				break;
			case THREAD_PRIORITY_ABOVE_NORMAL:
				SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);
				break;
			case THREAD_PRIORITY_NORMAL:
				SetThreadPriority(hThread, THREAD_PRIORITY_BELOW_NORMAL);
				break;
			case THREAD_PRIORITY_BELOW_NORMAL:
				SetThreadPriority(hThread, THREAD_PRIORITY_LOWEST);
				break;
			case THREAD_PRIORITY_LOWEST:
				SetThreadPriority(hThread, THREAD_PRIORITY_IDLE);
				break;
			case THREAD_PRIORITY_IDLE: // can't go lower
			default: // do nothing
				break;
		}
		#else
		// it seems that threads cannot have lower priority on POSIX, unless it's scheduled realtime, however we can declare it to be CPU intensive
		int policy;
		struct sched_param param;
		pthread_t self = pthread_self();
		if(!pthread_getschedparam(self, &policy, &param)) {
			if(policy == SCHED_OTHER) {
				#ifdef __MACH__
				// MacOS doesn't support SCHED_BATCH, but does seem to permit priorities on SCHED_OTHER
				int min = sched_get_priority_min(policy);
				if(min < param.sched_priority) {
					param.sched_priority -= 1;
					if(param.sched_priority < min) param.sched_priority = min;
					pthread_setschedparam(self, policy, &param);
				}
				#else
				pthread_setschedparam(self, SCHED_BATCH, &param);
				#endif
			}
		}
		
		# if defined(__linux) || defined(__linux__)
		// ...but Linux allows per-thread priority
		nice(1);
		# endif
		#endif
		thread_func(parent);
	}
	
	// disable copy constructor
	MessageThread(const MessageThread&);
	MessageThread& operator=(const MessageThread&);
	// ...but allow moves
	void move(MessageThread& other) {
		q = std::move(other.q);
		thread = other.thread;
		threadActive = other.threadActive;
		threadCreated = other.threadCreated;
		cb = other.cb;
		
		other.threadActive = false;
		other.threadCreated = false;
	}
	
public:
	bool lowPrio;
	MessageThread() {
		cb = NULL;
		threadActive = false;
		threadCreated = false;
		lowPrio = false;
	}
	MessageThread(uv_thread_cb callback) {
		cb = callback;
		threadActive = false;
		threadCreated = false;
		lowPrio = false;
	}
	void setCallback(uv_thread_cb callback) {
		cb = callback;
	}
	~MessageThread() {
		if(threadActive)
			q.push(NULL);
		if(threadCreated)
			uv_thread_join(&thread);
	}
	
	MessageThread(MessageThread&& other) noexcept {
		move(other);
	}
	MessageThread& operator=(MessageThread&& other) noexcept {
		move(other);
		return *this;
	}
	
	void start() {
		if(threadActive) return;
		threadActive = true;
		if(threadCreated) // previously created, but end fired, so need to wait for this thread to close before starting another
			uv_thread_join(&thread);
		threadCreated = true;
		uv_thread_create(&thread, lowPrio ? thread_func_low_prio : thread_func, this);
	}
	// item cannot be NULL
	void send(void* item) {
		start();
		q.push(item);
	}
	template<class Iterable>
	void send_multi(Iterable items) {
		start();
		q.push_multi(items);
	}
	void end() {
		if(threadActive) {
			q.push(NULL);
			threadActive = false;
		}
	}
	
	size_t size() {
		return q.size();
	}
	bool empty() {
		return q.empty();
	}
};

#endif // defined(__THREADQUEUE_H__)
