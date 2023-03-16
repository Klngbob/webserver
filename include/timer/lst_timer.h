#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include <netinet/in.h>
#include <stdio.h>
#include "http/http_conn.h"
#include "log/log.h"

#define BUFFER_SIZE 64

/* 定时器类 */
template<typename T>
class util_timer
{
public:
    util_timer() : prev( NULL ), next( NULL ){}
    util_timer(time_t expire_, void (*cb_func_)( T* ), T* user_data_)
        :expire(expire_), cb_func(cb_func_), user_data(user_data_) {}
public:
   time_t expire; /* 任务的超时时间，绝对时间 */
   void (*cb_func)( T* ); /* 任务回调函数 */
   T* user_data; /* 回调函数处理的客户数据，由定时器的执行者传递给回调函数 */
   util_timer* prev;
   util_timer* next;
};

/* 定时器链表，升序、双向且带有头、尾节点指针 */
template<typename T>
class sort_timer_lst
{
public:
    sort_timer_lst() : head( NULL ), tail( NULL ) {}
    ~sort_timer_lst()
    {
        util_timer<T>* tmp = head;
        while( tmp )
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    void add_timer( util_timer<T>* timer )
    {
        if( !timer )
        {
            return;
        }
        if( !head )
        {
            head = tail = timer;
            return; 
        }
        if( timer->expire < head->expire )
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer( timer, head );
    }
    /* 当某个定时器任务发生变化时，调整对应的定时器在链表中位置，该函数只考虑被调整的定时器超时时间延长的情况 */
    void adjust_timer( util_timer<T>* timer )
    {
        if( !timer )
        {
            return;
        }
        util_timer<T>* tmp = timer->next;
        if( !tmp || ( timer->expire < tmp->expire ) )
        {
            return;
        }
        if( timer == head )
        {
            head = head->next; /* head不会为空 */
            head->prev = NULL;
            timer->next = NULL;
            add_timer( timer, head );
        }
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer( timer, timer->next );
        }
    }
    void del_timer( util_timer<T>* timer )
    {
        if( !timer )
        {
            return;
        }
        if( ( timer == head ) && ( timer == tail ) )
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        if( timer == head )
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        if( timer == tail )
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
    void tick()
    {
        if( !head )
        {
            return;
        }
        LOG_INFO( "timer tick" );
        time_t cur = time( NULL ); /* 获得系统当前时间 */
        util_timer<T>* tmp = head;
        while( tmp )
        {
            if( cur < tmp->expire )
            {
                break;
            }
            tmp->cb_func( tmp->user_data );
            printf("close one unactive connetion\n");
            head = tmp->next;
            if( head )
            {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    /* 插入定时器 */
    void add_timer( util_timer<T>* timer, util_timer<T>* lst_head )
    {
        util_timer<T>* prev = lst_head;
        util_timer<T>* tmp = prev->next;
        while( tmp )
        {
            if( timer->expire < tmp->expire )
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if( !tmp )
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
        
    }

private:
    util_timer<T>* head;
    util_timer<T>* tail;
};

#endif