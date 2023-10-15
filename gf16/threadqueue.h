#ifndef __THREADQUEUE_H__
#define __THREADQUEUE_H__

#include <memory>
#ifdef USE_LIBUV
# include <uv.h>
# define thread_t uv_thread_t
# define thread_create(t, f, a) uv_thread_create(&(t), f, a)
# define thread_join(t) uv_thread_join(&(t))
# define mutex_t uv_mutex_t
# define mutex_init(m) uv_mutex_init(&(m))
# define mutex_destroy(m) uv_mutex_destroy(&(m))
# define mutex_lock(m) uv_mutex_lock(&(m))
# define mutex_unlock(m) uv_mutex_unlock(&(m))
# define condvar_t uv_cond_t
# define condvar_init(c) uv_cond_init(&(c))
# define condvar_destroy(c) uv_cond_destroy(&(c))
# define condvar_signal(c) uv_cond_signal(&(c))
#else
# include <thread>
# define thread_t std::thread
# define thread_create(t, f, a) t = std::thread(f, a)
# define thread_join(t) t.join()
# include <mutex>
# define mutex_t std::unique_ptr<std::mutex>
# define mutex_init(m) m = std::unique_ptr<std::mutex>(new std::mutex())
# define mutex_destroy(m)
# define mutex_lock(m) m->lock()
# define mutex_unlock(m) m->unlock()
# include <condition_variable>
# define condvar_t std::unique_ptr<std::condition_variable>
# define condvar_init(c) c = std::unique_ptr<std::condition_variable>(new std::condition_variable())
# define condvar_destroy(c)
# define condvar_signal(c) c->notify_one()
#endif
#include <queue>

template<typename T>
class ThreadMessageQueue {
	std::queue<T> q;
	mutable mutex_t mutex;
	condvar_t cond;
	bool skipDestructor;
	
	// disable copy constructor
	ThreadMessageQueue(const ThreadMessageQueue&);
	ThreadMessageQueue& operator=(const ThreadMessageQueue&);
	// ...but allow moves
	void move(ThreadMessageQueue& other) {
		q = std::move(other.q);
#ifdef USE_LIBUV
		mutex = other.mutex;
		cond = other.cond;
#else
		mutex = std::move(other.mutex);
		cond = std::move(other.cond);
#endif
		skipDestructor = false;
		
		other.skipDestructor = true;
	}
	
	
public:
	ThreadMessageQueue() {
		mutex_init(mutex);
		condvar_init(cond);
		skipDestructor = false;
	}
	~ThreadMessageQueue() {
		if(skipDestructor) return;
		mutex_destroy(mutex);
		condvar_destroy(cond);
	}
	
	ThreadMessageQueue(ThreadMessageQueue&& other) noexcept {
		move(other);
	}
	ThreadMessageQueue& operator=(ThreadMessageQueue&& other) noexcept {
		move(other);
		return *this;
	}
	void push(T item) {
		mutex_lock(mutex);
		q.push(item);
		condvar_signal(cond);
		mutex_unlock(mutex);
	}
	template<class Iterable>
	void push_multi(const Iterable& list) {
		mutex_lock(mutex);
		for(auto it = list.cbegin(); it != list.cend(); ++it) {
			q.push(*it);
		}
		condvar_signal(cond);
		mutex_unlock(mutex);
	}
	T pop() {
#ifdef USE_LIBUV
		mutex_lock(mutex);
		while(q.empty()) {
			uv_cond_wait(&cond, &mutex);
		}
		T item = q.front();
		q.pop();
		mutex_unlock(mutex);
#else
		std::unique_lock<std::mutex> lk(*mutex);
		cond->wait(lk, [this]{ return !q.empty(); });
		T item = q.front();
		q.pop();
#endif
		return item;
	}
	
	bool trypop(T* item) {
		bool notEmpty;
		mutex_lock(mutex);
		notEmpty = !q.empty();
		if(notEmpty) {
			*item = q.front();
			q.pop();
		}
		mutex_unlock(mutex);
		return notEmpty;
	}
	
	size_t size() const {
		mutex_lock(mutex);
		size_t s = q.size();
		mutex_unlock(mutex);
		return s;
	}
	bool empty() const {
		mutex_lock(mutex);
		bool e = q.empty();
		mutex_unlock(mutex);
		return e;
	}
};


#ifdef USE_LIBUV
struct tnqCloseWrap {
	void(*cb)(void*);
	void* data;
};

template<class P>
class ThreadNotifyQueue {
	ThreadMessageQueue<void*> q;
	std::unique_ptr<uv_async_t> a;
	P* o;
	void (P::*cb)(void*);
	
	static void notified(uv_async_t *handle
#if UV_VERSION_MAJOR < 1
		, int
#endif
	) {
		auto self = static_cast<ThreadNotifyQueue*>(handle->data);
		void* notification;
		while(self->q.trypop(&notification))
			(self->o->*(self->cb))(notification);
	}
public:
	explicit ThreadNotifyQueue(uv_loop_t* loop, P* object, void (P::*callback)(void*)) {
		a.reset(new uv_async_t());
		uv_async_init(loop, a.get(), notified);
		a->data = static_cast<void*>(this);
		cb = callback;
		o = object;
	}
	
	void notify(void* item) {
		q.push(item);
		uv_async_send(a.get());
	}
	
	void close(void* data, void(*closeCb)(void*)) {
		auto* d = new tnqCloseWrap;
		d->cb = closeCb;
		d->data = data;
		a->data = d;
		uv_close(reinterpret_cast<uv_handle_t*>(a.release()), [](uv_handle_t* handle) {
			auto* d = static_cast<tnqCloseWrap*>(handle->data);
			d->cb(d->data);
			delete d;
			delete handle;
		});
	}
	void close() {
		uv_close(reinterpret_cast<uv_handle_t*>(a.release()), [](uv_handle_t* handle) {
			delete handle;
		});
	}
};
#endif

#ifdef USE_LIBUV
typedef void(*thread_cb_t)(ThreadMessageQueue<void*>&);
#else
typedef std::function<void(ThreadMessageQueue<void*>&)> thread_cb_t;
#endif


#if defined(_WINDOWS) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
#else
# include <pthread.h>
#endif
#if defined(__linux) || defined(__linux__)
# include <unistd.h>
# include <sys/prctl.h>
#endif
class MessageThread {
	ThreadMessageQueue<void*> q;
	thread_t thread;
	bool threadActive;
	bool threadCreated;
	thread_cb_t cb;
	
	static void thread_func(void* parent) {
		MessageThread* self = static_cast<MessageThread*>(parent);
		
		if(self->lowPrio) {
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
					#ifndef SCHED_BATCH
					// MacOS doesn't support SCHED_BATCH, but does seem to permit priorities on SCHED_OTHER
					int min = sched_get_priority_min(policy);
					if(min < param.sched_priority) {
						param.sched_priority -= 1;
						pthread_setschedparam(self, policy, &param);
					}
					#else
					pthread_setschedparam(self, SCHED_BATCH, &param);
					#endif
				}
			}
			
			# if defined(__linux) || defined(__linux__)
			// ...but Linux allows per-thread priority
			(void)!nice(1); // we don't care if this fails
			# endif
			#endif
		}
		
		if(self->name) {
			#if defined(_WINDOWS) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
			HMODULE h = GetModuleHandleA("kernelbase.dll");
			if(h) {
				HRESULT(__stdcall *fnSetTD)(HANDLE, PCWSTR) = (HRESULT(__stdcall *)(HANDLE, PCWSTR))((void*)GetProcAddress(h, "SetThreadDescription"));
				if(fnSetTD) {
					wchar_t nameUCS2[17];
					//assert(strlen(self->name) <= 16); // always hard-coded string, plus Linux limits it to 16 chars, so shouldn't ever overflow
					MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, self->name, -1, nameUCS2, sizeof(nameUCS2)/sizeof(wchar_t) -1);
					fnSetTD(GetCurrentThread(), nameUCS2);
				}
			}
			#elif defined(__linux) || defined(__linux__)
			prctl(PR_SET_NAME, self->name, 0, 0, 0);
			#elif defined(__MACH__)
			pthread_setname_np(self->name);
			#elif defined(__FreeBSD__) || defined(__DragonFly__)
			pthread_setname_np(pthread_self(), self->name);
			#elif defined(__NetBSD__)
			pthread_setname_np(pthread_self(), self->name, NULL);
			#elif defined(__OpenBSD__)
			pthread_set_name_np(pthread_self(), self->name);
			#endif
		}
		
		self->cb(self->q);
	}
	
	// disable copy constructor
	MessageThread(const MessageThread&);
	MessageThread& operator=(const MessageThread&);
	// ...but allow moves
	void move(MessageThread& other) {
		q = std::move(other.q);
#ifdef USE_LIBUV
		thread = other.thread;
#else
		thread = std::move(other.thread);
#endif
		threadActive = other.threadActive;
		threadCreated = other.threadCreated;
		cb = other.cb;
		name = other.name;
		lowPrio = other.lowPrio;
		
		other.threadActive = false;
		other.threadCreated = false;
	}
	
public:
	bool lowPrio;
	const char* name;
	MessageThread() {
		cb = NULL;
		threadActive = false;
		threadCreated = false;
		lowPrio = false;
		name = NULL;
	}
	MessageThread(thread_cb_t callback) {
		cb = callback;
		threadActive = false;
		threadCreated = false;
		lowPrio = false;
		name = NULL;
	}
	void setCallback(thread_cb_t callback) {
		cb = callback;
	}
	~MessageThread() {
		if(threadActive)
			q.push(NULL);
		if(threadCreated)
			thread_join(thread);
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
			thread_join(thread);
		threadCreated = true;
		thread_create(thread, thread_func, this);
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

static inline int hardware_concurrency() {
	int threads;
#ifdef USE_LIBUV
#if UV_VERSION_HEX >= 0x12c00  // 1.44.0
	threads = uv_available_parallelism();
#else
	uv_cpu_info_t *info;
	uv_cpu_info(&info, &threads);
	uv_free_cpu_info(info, threads);
#endif
#else
	threads = (int)std::thread::hardware_concurrency();
#endif
	if(threads < 1) threads = 1;
	return threads;
}


#undef thread_t
#undef thread_create
#undef thread_join
#undef mutex_t
#undef mutex_init
#undef mutex_destroy
#undef mutex_lock
#undef mutex_unlock
#undef condvar_t
#undef condvar_init
#undef condvar_destroy
#undef condvar_signal

#endif // defined(__THREADQUEUE_H__)
