/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/
/* osi/os/posix/osdMutex.c */

/* Author:  Marty Kraimer Date:    13AUG1999 */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include "epicsMutex.h"
#include "cantProceed.h"
#include "epicsTime.h"
#include "errlog.h"
#include "epicsAssert.h"

#define checkStatus(status,message) \
if((status)) { \
    errlogPrintf("%s failed: error %s\n",(message),strerror((status)));}

#define checkStatusQuit(status,message,method) \
if(status) { \
    errlogPrintf("%s failed: error %s\n",(message),strerror((status))); \
    cantProceed((method)); \
}

/* Until these can be demonstrated to work leave them undefined*/
/* On solaris 8 _POSIX_THREAD_PRIO_INHERIT fails*/
#undef _POSIX_THREAD_PROCESS_SHARED
#undef _POSIX_THREAD_PRIO_INHERIT

/* Three completely different implementations are provided below
 * If support is available for _POSIX_SPIN_LOCK is available
 *      then pthread_spin is used
 * If support is available for PTHREAD_MUTEX_RECURSIVE then
 *      only pthread_mutex is used.
 * If support is not available for PTHREAD_MUTEX_RECURSIVE then
 *      a much more complicated solution is required
 */

#if defined ( _POSIX_SPIN_LOCKS ) && ( _POSIX_SPIN_LOCKS ) >= 200112L && EPICS_TEST_SPINLOCKS

typedef struct epicsMutexOSD {
    pthread_spinlock_t lock;
    pthread_t owner;
    unsigned recursionCount;
    char owned;
} epicsMutexOSD;

epicsMutexOSD * epicsMutexOsdCreate ( void ) {
    epicsMutexOSD *pmutex;
    int           status;

    pmutex = callocMustSucceed ( 1, sizeof(*pmutex),"epicsMutexOsdCreate" );
    status = pthread_spin_init ( &pmutex->lock,PTHREAD_PROCESS_PRIVATE );
    checkStatusQuit ( status, "pthread_spin_init","epicsMutexOsdCreate" );
    pmutex->recursionCount = 0u;
    pmutex->owner = 0;
    pmutex->owned = 0;
    return ( pmutex );
}

void epicsMutexOsdDestroy ( struct epicsMutexOSD * pmutex )
{
    int   status;

    status = pthread_spin_destroy ( & pmutex->lock );
    checkStatus ( status, "pthread_mutex_destroy" );
    free ( pmutex );
}

void epicsMutexOsdUnlock ( struct epicsMutexOSD * pmutex )
{
    int status;

    pmutex->recursionCount--;
    pmutex->owner = 0;
    pmutex->owned = 0;
    status = pthread_spin_unlock(&pmutex->lock);
    checkStatusQuit ( status,"pthread_spin_unlock","epicsMutexOsdUnlock" );
}

epicsMutexLockStatus epicsMutexOsdLock ( struct epicsMutexOSD * pmutex )
{
    pthread_t self = pthread_self ();
    int status;

    if ( ! pmutex ) return ( epicsMutexLockError );
    if ( ! pmutex->owned || ! pthread_equal ( self, pmutex->owner ) ) {
        status = pthread_spin_lock ( &pmutex->lock );
        checkStatusQuit ( status, "pthread_spin_lock", "epicsMutexOsdLock" );
        pmutex->owned = 1;
        pmutex->owner = self;
    }
    pmutex->recursionCount++;
    return ( epicsMutexLockOK );
}

epicsMutexLockStatus epicsMutexOsdTryLock(struct epicsMutexOSD * pmutex)
{
    pthread_t self = pthread_self ();
    epicsMutexLockStatus status;
    int pthreadStatus;

    if ( ! pmutex ) return(epicsMutexLockError);
    if ( ! pmutex->owned || ! pthread_equal ( self, pmutex->owner ) ) {
        pthreadStatus = pthread_spin_trylock ( &pmutex->lock );
        if ( pthreadStatus != 0 ) {
           if ( pthreadStatus == EBUSY ) return ( epicsMutexLockTimeout );
           checkStatusQuit ( pthreadStatus, 
	       "pthread_spin_trylock", "epicsMutexOsdTryLock" );
	}
        pmutex->owned = 1;
        pmutex->owner = self;
    }
    pmutex->recursionCount++;
    return ( epicsMutexLockOK );
}

void epicsMutexOsdShow ( struct epicsMutexOSD * pmutex, unsigned int level )
{
}

#elif defined(_XOPEN_SOURCE) && (_XOPEN_SOURCE)>=500
typedef struct epicsMutexOSD {
    pthread_mutexattr_t mutexAttr;
    pthread_mutex_t	lock;
}epicsMutexOSD;

epicsMutexOSD * epicsMutexOsdCreate(void) {
    epicsMutexOSD *pmutex;
    int           status;

    pmutex = callocMustSucceed(1,sizeof(*pmutex),"epicsMutexOsdCreate");
    status = pthread_mutexattr_init(&pmutex->mutexAttr);
    checkStatusQuit(status,"pthread_mutexattr_init","epicsMutexOsdCreate");
#if defined _POSIX_THREAD_PRIO_INHERIT
    status = pthread_mutexattr_setprotocol(
        &pmutex->mutexAttr,PTHREAD_PRIO_INHERIT);
    if(errVerbose) checkStatus(status,"pthread_mutexattr_setprotocal");
#endif /*_POSIX_THREAD_PRIO_INHERIT*/
    status = pthread_mutexattr_settype(&pmutex->mutexAttr,PTHREAD_MUTEX_RECURSIVE);
    if(errVerbose) checkStatus(status,"pthread_mutexattr_settype");
    status = pthread_mutex_init(&pmutex->lock,&pmutex->mutexAttr);
    checkStatusQuit(status,"pthread_mutex_init","epicsMutexOsdCreate");
    return(pmutex);
}

void epicsMutexOsdDestroy(struct epicsMutexOSD * pmutex)
{
    int   status;

    status = pthread_mutex_destroy(&pmutex->lock);
    checkStatus(status,"pthread_mutex_destroy");
    status = pthread_mutexattr_destroy(&pmutex->mutexAttr);
    checkStatus(status,"pthread_mutexattr_destroy");
    free(pmutex);
}

void epicsMutexOsdUnlock(struct epicsMutexOSD * pmutex)
{
    int status;

    status = pthread_mutex_unlock(&pmutex->lock);
    checkStatusQuit(status,"pthread_mutex_unlock","epicsMutexOsdUnlock");
}

epicsMutexLockStatus epicsMutexOsdLock(struct epicsMutexOSD * pmutex)
{
    int status;

    if(!pmutex) return(epicsMutexLockError);
    status = pthread_mutex_lock(&pmutex->lock);
    checkStatusQuit(status,"pthread_mutex_lock","epicsMutexOsdLock");
    return(epicsMutexLockOK);
}

epicsMutexLockStatus epicsMutexOsdTryLock(struct epicsMutexOSD * pmutex)
{
    int pthreadStatus;

    if(!pmutex) return(epicsMutexLockError);
    pthreadStatus = pthread_mutex_trylock(&pmutex->lock);
    if(pthreadStatus!=0) {
        if(pthreadStatus==EBUSY) return(epicsMutexLockTimeout);
        checkStatusQuit(pthreadStatus,"pthread_mutex_lock","epicsMutexOsdTryLock");
    }
    return(epicsMutexLockOK);
}

void epicsMutexOsdShow(struct epicsMutexOSD * pmutex,unsigned int level)
{
}

#else /*defined(_XOPEN_SOURCE) && (_XOPEN_SOURCE)>=500 */

typedef struct epicsMutexOSD {
    pthread_mutexattr_t mutexAttr;
    pthread_mutex_t	lock;
    pthread_cond_t	waitToBeOwner;
#if defined _POSIX_THREAD_PROCESS_SHARED
    pthread_condattr_t  condAttr;
#endif /*_POSIX_THREAD_PROCESS_SHARED*/
    int			count;
    int			owned;  /* TRUE | FALSE */
    pthread_t		ownerTid;
}epicsMutexOSD;

epicsMutexOSD * epicsMutexOsdCreate(void) {
    epicsMutexOSD *pmutex;
    int           status;

    pmutex = callocMustSucceed(1,sizeof(*pmutex),"epicsMutexOsdCreate");
    status = pthread_mutexattr_init(&pmutex->mutexAttr);
    checkStatusQuit(status,"pthread_mutexattr_init","epicsMutexOsdCreate");
#if defined _POSIX_THREAD_PRIO_INHERIT
    status = pthread_mutexattr_setprotocol(
        &pmutex->mutexAttr,PTHREAD_PRIO_INHERIT);
    if(errVerbose) checkStatus(status,"pthread_mutexattr_setprotocal");
#endif /*_POSIX_THREAD_PRIO_INHERIT*/
    status = pthread_mutex_init(&pmutex->lock,&pmutex->mutexAttr);
    checkStatusQuit(status,"pthread_mutex_init","epicsMutexOsdCreate");
#if defined _POSIX_THREAD_PROCESS_SHARED
    status = pthread_condattr_init(&pmutex->condAttr);
    checkStatus(status,"pthread_condattr_init");
    status = pthread_condattr_setpshared(&pmutex->condAttr,
        PTHREAD_PROCESS_PRIVATE);
    checkStatus(status,"pthread_condattr_setpshared");
    status = pthread_cond_init(&pmutex->waitToBeOwner,&pmutex->condAttr);
#else
    status = pthread_cond_init(&pmutex->waitToBeOwner,0);
#endif /*_POSIX_THREAD_PROCESS_SHARED*/
    checkStatusQuit(status,"pthread_cond_init","epicsMutexOsdCreate");
    return(pmutex);
}

void epicsMutexOsdDestroy(struct epicsMutexOSD * pmutex)
{
    int   status;

    status = pthread_cond_destroy(&pmutex->waitToBeOwner);
    checkStatus(status,"pthread_cond_destroy");
#if defined _POSIX_THREAD_PROCESS_SHARED
    status = pthread_condattr_destroy(&pmutex->condAttr);
#endif /*_POSIX_THREAD_PROCESS_SHARED*/
    status = pthread_mutex_destroy(&pmutex->lock);
    checkStatus(status,"pthread_mutex_destroy");
    status = pthread_mutexattr_destroy(&pmutex->mutexAttr);
    checkStatus(status,"pthread_mutexattr_destroy");
    free(pmutex);
}

void epicsMutexOsdUnlock(struct epicsMutexOSD * pmutex)
{
    int status;

    status = pthread_mutex_lock(&pmutex->lock);
    checkStatusQuit(status,"pthread_mutex_lock","epicsMutexOsdUnlock");
    if((pmutex->count<=0) || (pmutex->ownerTid != pthread_self())) {
        errlogPrintf("epicsMutexOsdUnlock but caller is not owner\n");
        status = pthread_mutex_unlock(&pmutex->lock);
        checkStatusQuit(status,"pthread_mutex_unlock","epicsMutexOsdUnlock");
        return;
    }
    pmutex->count--;
    if(pmutex->count == 0) {
        pmutex->owned = 0;
        pmutex->ownerTid = 0;
        pthread_cond_signal(&pmutex->waitToBeOwner);
    }
    status = pthread_mutex_unlock(&pmutex->lock);
    checkStatusQuit(status,"pthread_mutex_unlock","epicsMutexOsdUnlock");
}

epicsMutexLockStatus epicsMutexOsdLock(struct epicsMutexOSD * pmutex)
{
    pthread_t tid = pthread_self();
    int status;

    if(!pmutex || !tid) return(epicsMutexLockError);
    status = pthread_mutex_lock(&pmutex->lock);
    checkStatusQuit(status,"pthread_mutex_lock","epicsMutexOsdLock");
    while(pmutex->owned && !pthread_equal(pmutex->ownerTid,tid))
        pthread_cond_wait(&pmutex->waitToBeOwner,&pmutex->lock);
    pmutex->ownerTid = tid;
    pmutex->owned = 1;
    pmutex->count++;
    status = pthread_mutex_unlock(&pmutex->lock);
    checkStatusQuit(status,"pthread_mutex_unlock","epicsMutexOsdLock");
    return(epicsMutexLockOK);
}

epicsMutexLockStatus epicsMutexOsdTryLock(struct epicsMutexOSD * pmutex)
{
    pthread_t tid = pthread_self();
    epicsMutexLockStatus status;
    int pthreadStatus;

    pthreadStatus = pthread_mutex_lock(&pmutex->lock);
    checkStatusQuit(pthreadStatus,"pthread_mutex_lock","epicsMutexOsdTryLock");
    if(!pmutex->owned || pthread_equal(pmutex->ownerTid,tid)) {
        pmutex->ownerTid = tid;
        pmutex->owned = 1;
        pmutex->count++;
        status = epicsMutexLockOK;
    }
    else {
        status = epicsMutexLockTimeout;
    }
    pthreadStatus = pthread_mutex_unlock(&pmutex->lock);
    checkStatusQuit(pthreadStatus,"pthread_mutex_unlock","epicsMutexOsdTryLock");
    return(status);
}

void epicsMutexOsdShow(struct epicsMutexOSD *pmutex,unsigned int level)
{
    printf("ownerTid %p count %d owned %d\n",
        (void *)pmutex->ownerTid,pmutex->count,pmutex->owned);
}
#endif /*defined(_XOPEN_SOURCE) && (_XOPEN_SOURCE)>=500 */
