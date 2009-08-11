/**
 * @brief Semafor
 * @author Piotr Truszkowski
 */

#ifndef __RS_SEMAPHORE_HH__
#define __RS_SEMAPHORE_HH__

#include <semaphore.h>
#include <rs/Exception.hh>

class Semaphore {
  public:
    Semaphore(void) throw() 
    {
      if (sem_init(&sem, 0, 0)) 
        throw EInternal("Semaphore::Semaphore(): System error: %d, %s", errno, strerror(errno));
    }

    ~Semaphore(void) throw()
    {
      if (sem_destroy(&sem))
        throw EInternal("Semaphore::~Semaphore(): System error: %d, %s", errno, strerror(errno));
    }

    void p(void) throw() 
    {
      while (sem_wait(&sem))
        if (EINTR != errno && EAGAIN != errno)
          throw EInternal("Semaphore::p(): System error: %d, %s", errno, strerror(errno));
    }

    void v(void) throw()
    {
      if (sem_post(&sem)) 
        throw EInternal("Semaphore::v(): System error: %d, %s", errno, strerror(errno));
    }

  private:
    Semaphore(const Semaphore &); /* non-copyable */
    sem_t sem;
};

#endif

