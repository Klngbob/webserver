#ifndef LOCKER_H_
#define LOCKER_H_

#include <exception>
#include <semaphore.h>
#include <pthread.h>

/* 封装信号量的类 */
class sem {
public:
    /* 创建并初始化信号量 */
    sem() {
        if(sem_init(&m_sem, 0, 0) != 0) {
            /* 构造函数没有返回值，可以通过抛出异常报告错误 */
            throw std::exception();
        }
    }
    sem(int num)
    {
        if(sem_init(&m_sem, 0, num) != 0) {
            /* 构造函数没有返回值，可以通过抛出异常报告错误 */
            throw std::exception();
        }
    }
    /* 销毁信号量 */
    ~sem() {
        sem_destroy(&m_sem);
    }
    /* 等待信号量P */
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    /* 增加信号量V */
    bool post() {
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

/* 封装互斥锁类 */
class locker {
public:
    /* 创建并初始化互斥锁 */
    locker() {
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock() {
        return pthread_mutex_lock(&m_mutex);
    }
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex);
    }
    pthread_mutex_t* get()
    {
        return &m_mutex;
    }
private:
    pthread_mutex_t m_mutex;
};

/* 封装条件变量类 */
class cond {
public:
    /* 创建并初始化条件变量 */
    cond() {
        if(pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t* m_mutex) {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal() {
        return pthread_cond_signal(&m_cond);
    }

    bool broadcast() {
        return pthread_cond_broadcast(&m_cond);
    }
private:
    // pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif