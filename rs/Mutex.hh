/**
 * @brief Mutex
 * @author Piotr Truszkowski
 */

#ifndef __RS_MUTEX_HH__
#define __RS_MUTEX_HH__

#include <pthread.h>
#include <rs/Exception.hh>

class Mutex {
	public:
		Mutex(void) throw()
		{
			int p = pthread_mutex_init(&m, 0);
			if (p) throw EInternal("Mutex::Mutex(): Error: %d, %s", p, strerror(p));
		}

		~Mutex(void) throw()
		{
			int p = pthread_mutex_destroy(&m);
			if (p) throw EInternal("Mutex::~Mutex(): Error: %d, %s", p, strerror(p));
		}

		void lock(void) throw()
		{
			int p = pthread_mutex_lock(&m);
			if (p) throw EInternal("Mutex::lock(): Error: %d, %s", p, strerror(p));
		}

		void unlock(void) throw()
		{
			int p = pthread_mutex_unlock(&m);
			if (p) throw EInternal("Mutex::unlock(): Error: %d, %s", p, strerror(p));
		}

	private:
		Mutex(const Mutex&); /* non-copyable */
		pthread_mutex_t m;
};

class Lock {
	public:
		Lock(Mutex &m) throw() : m(m) { m.lock(); }
		~Lock(void) throw() { m.unlock(); }

	private:
		Lock(const Lock &); /* non-copyable */
		Mutex &m;
};

#endif

