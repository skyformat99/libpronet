/*
 * Copyright (C) 2018 Eric Tung <libpronet@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of LibProNet (http://www.libpro.org)
 */

/*
 * reduce the stack size, for ProSleep_s(...)
 */
#if defined(WIN32) || defined(_WIN32_WCE)
#if defined(PRO_FD_SETSIZE)
#undef  PRO_FD_SETSIZE
#endif
#define PRO_FD_SETSIZE 64
#endif

#include "pro_shared.h"
#include "sgi_pool/stl_alloc.h"
#include "../pro_util/pro_bsd_wrapper.h"
#include "../pro_util/pro_version.h"
#include "../pro_util/pro_z.h"

#if defined(WIN32) || defined(_WIN32_WCE)
#include <windows.h>
#include <mmsystem.h>
#else
#include <pthread.h>
#if defined(PRO_MACH_ABSOLUTE_TIME)
#include <mach/mach_time.h>
#endif
#endif

#include <cassert>
#include <cstddef>

#if defined(_MSC_VER)
#if defined(_WIN32_WCE)
#pragma comment(lib, "mmtimer.lib")
#elif defined(WIN32)
#pragma comment(lib, "winmm.lib")
#endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////
////

#if defined(WIN32) || defined(_WIN32_WCE)

class CProThreadMutex_s
{
public:

    CProThreadMutex_s()
    {
        ::InitializeCriticalSection(&m_cs);
    }

    ~CProThreadMutex_s()
    {
        ::DeleteCriticalSection(&m_cs);
    }

    void Lock()
    {
        ::EnterCriticalSection(&m_cs);
    }

    void Unlock()
    {
        ::LeaveCriticalSection(&m_cs);
    }

private:

    CRITICAL_SECTION m_cs;
};

#else  /* WIN32, _WIN32_WCE */

class CProThreadMutex_s
{
public:

    CProThreadMutex_s()
    {
        pthread_mutex_init(&m_mutext, NULL);
    }

    ~CProThreadMutex_s()
    {
        pthread_mutex_destroy(&m_mutext);
    }

    void Lock()
    {
        pthread_mutex_lock(&m_mutext);
    }

    void Unlock()
    {
        pthread_mutex_unlock(&m_mutext);
    }

private:

    pthread_mutex_t m_mutext;
};

#endif /* WIN32, _WIN32_WCE */

/////////////////////////////////////////////////////////////////////////////
////

#if defined(WIN32) || defined(_WIN32_WCE)
#define PBSD_EINTR     WSAEINTR    /* 10004 */
#define PBSD_EINVAL    WSAEINVAL   /* 10022 */
#define PBSD_ENOTSOCK  WSAENOTSOCK /* 10038 */
#else
#define PBSD_EINTR     EINTR       /*  4 */
#define PBSD_EINVAL    EINVAL      /* 22 */
#define PBSD_ENOTSOCK  ENOTSOCK    /*  8 */
#endif

#define SMALL_OBJ_SIZE 4096        /* sizeof(obj) <= 4096 */

#if defined(WIN32) || defined(_WIN32_WCE)
static volatile bool                     g_s_tlsFlag       = false;
static unsigned long                     g_s_tlsKey0       = (unsigned long)-1;
static unsigned long                     g_s_tlsKey1       = (unsigned long)-1;
static PRO_INT64                         g_s_globalTick    = 0;
#elif defined(PRO_MACH_ABSOLUTE_TIME)
static volatile bool                     g_s_timebaseFlag  = false;
static mach_timebase_info_data_t         g_s_timebaseInfo  = { 0, 0 };
#endif
static volatile bool                     g_s_socketFlag    = false;
static PRO_INT64                         g_s_sockId        = -1;
static unsigned long                     g_s_nextTimerId   = 1;
static unsigned long                     g_s_nextMmTimerId = 2;
static CProThreadMutex_s                 g_s_lock;

/*
 * pool-0
 */
static std::__default_alloc_template<0>  g_s_allocator0a; /* small, (   1 <= sizeof(obj) <= 4096      ) */
static std::__default_alloc_template<1>  g_s_allocator0b; /* big  , (4097 <= sizeof(obj) <= 1024 * 128) */
static CProThreadMutex_s                 g_s_lock0;

/*
 * pool-1
 */
static std::__default_alloc_template<10> g_s_allocator1a; /* small, (   1 <= sizeof(obj) <= 4096      ) */
static std::__default_alloc_template<11> g_s_allocator1b; /* big  , (4097 <= sizeof(obj) <= 1024 * 128) */
static CProThreadMutex_s                 g_s_lock1;

/*
 * pool-2
 */
static std::__default_alloc_template<20> g_s_allocator2a; /* small, (   1 <= sizeof(obj) <= 4096      ) */
static std::__default_alloc_template<21> g_s_allocator2b; /* big  , (4097 <= sizeof(obj) <= 1024 * 128) */
static CProThreadMutex_s                 g_s_lock2;

/*
 * pool-3
 */
static std::__default_alloc_template<30> g_s_allocator3a; /* small, (   1 <= sizeof(obj) <= 4096      ) */
static std::__default_alloc_template<31> g_s_allocator3b; /* big  , (4097 <= sizeof(obj) <= 1024 * 128) */
static CProThreadMutex_s                 g_s_lock3;

/*
 * pool-4
 */
static std::__default_alloc_template<40> g_s_allocator4a; /* small, (   1 <= sizeof(obj) <= 4096      ) */
static std::__default_alloc_template<41> g_s_allocator4b; /* big  , (4097 <= sizeof(obj) <= 1024 * 128) */
static CProThreadMutex_s                 g_s_lock4;

/*
 * pool-5
 */
static std::__default_alloc_template<50> g_s_allocator5a; /* small, (   1 <= sizeof(obj) <= 4096      ) */
static std::__default_alloc_template<51> g_s_allocator5b; /* big  , (4097 <= sizeof(obj) <= 1024 * 128) */
static CProThreadMutex_s                 g_s_lock5;

/*
 * pool-6
 */
static std::__default_alloc_template<60> g_s_allocator6a; /* small, (   1 <= sizeof(obj) <= 4096      ) */
static std::__default_alloc_template<61> g_s_allocator6b; /* big  , (4097 <= sizeof(obj) <= 1024 * 128) */
static CProThreadMutex_s                 g_s_lock6;

/*
 * pool-7
 */
static std::__default_alloc_template<70> g_s_allocator7a; /* small, (   1 <= sizeof(obj) <= 4096      ) */
static std::__default_alloc_template<71> g_s_allocator7b; /* big  , (4097 <= sizeof(obj) <= 1024 * 128) */
static CProThreadMutex_s                 g_s_lock7;

/*
 * pool-8
 */
static std::__default_alloc_template<80> g_s_allocator8a; /* small, (   1 <= sizeof(obj) <= 4096      ) */
static std::__default_alloc_template<81> g_s_allocator8b; /* big  , (4097 <= sizeof(obj) <= 1024 * 128) */
static CProThreadMutex_s                 g_s_lock8;

/*
 * pool-9
 */
static std::__default_alloc_template<90> g_s_allocator9a; /* small, (   1 <= sizeof(obj) <= 4096      ) */
static std::__default_alloc_template<91> g_s_allocator9b; /* big  , (4097 <= sizeof(obj) <= 1024 * 128) */
static CProThreadMutex_s                 g_s_lock9;

/////////////////////////////////////////////////////////////////////////////
////

static
void
PRO_CALLTYPE
pbsd_startup_i()
{
    static bool s_flag = false;
    if (s_flag)
    {
        return;
    }
    s_flag = true;

#if defined(WIN32) || defined(_WIN32_WCE)
    WSADATA wsaData;
    ::WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

static
int
PRO_CALLTYPE
pbsd_errno_i()
{
    int errcode = 0;

#if defined(WIN32) || defined(_WIN32_WCE)
    errcode = ::WSAGetLastError();
#else
    errcode = errno;
#endif

    if (errcode == PBSD_ENOTSOCK)
    {
        errcode = PBSD_EBADF;
    }

    return (errcode);
}

static
int
PRO_CALLTYPE
pbsd_ioctl_closexec_i(PRO_INT64 fd,
                      long      on = true)
{
    if (on != 0)
    {
        on = (long)((unsigned long)-1 >> 1);
    }

    int retc = -1;

#if !defined(WIN32) && !defined(_WIN32_WCE)

    do
    {
        retc = fcntl((int)fd, F_GETFD);
    }
    while (retc < 0 && pbsd_errno_i() == PBSD_EINTR);

    if (retc >= 0)
    {
        int flags = retc;
        if (on != 0)
        {
            flags |= FD_CLOEXEC;
        }
        else
        {
            flags &= ~FD_CLOEXEC;
        }

        do
        {
            retc = fcntl((int)fd, F_SETFD, flags);
        }
        while (retc < 0 && pbsd_errno_i() == PBSD_EINTR);
    }

#endif /* WIN32, _WIN32_WCE */

    return (retc);
}

static
PRO_INT64
PRO_CALLTYPE
pbsd_socket_i(int af,
              int type,
              int protocol)
{
    PRO_INT64 fd = -1;

#if defined(WIN32) || defined(_WIN32_WCE)

    if (sizeof(SOCKET) == 8)
    {
        fd = (PRO_INT64)socket(af, type, protocol);
    }
    else
    {
        fd = (PRO_INT32)socket(af, type, protocol);
    }

#else  /* WIN32, _WIN32_WCE */

#if defined(SOCK_CLOEXEC)
    static bool s_hasclose = true;
    int         errorcode  = 0;
    if (s_hasclose)
    {
        fd        = (PRO_INT32)socket(af, type | SOCK_CLOEXEC, protocol);
        errorcode = pbsd_errno_i();
    }
#endif

    if (fd < 0)
    {
        fd = (PRO_INT32)socket(af, type, protocol);

#if defined(SOCK_CLOEXEC)
        if (fd >= 0 && errorcode == PBSD_EINVAL)
        {
            s_hasclose = false;
        }
#endif
    }

#endif /* WIN32, _WIN32_WCE */

    if (fd >= 0)
    {
        pbsd_ioctl_closexec_i(fd);
    }
    else
    {
        fd = -1;
    }

    return (fd);
}

static
int
PRO_CALLTYPE
pbsd_select_i(PRO_INT64       nfds,
              pbsd_fd_set*    readfds,
              pbsd_fd_set*    writefds,
              pbsd_fd_set*    exceptfds,
              struct timeval* timeout)
{
    int retc = -1;

#if defined(WIN32) || defined(_WIN32_WCE)
    do
    {
        retc = select(0, readfds, writefds, exceptfds, timeout);
    }
    while (0);
#else
    do
    {
        retc = select((int)nfds, readfds, writefds, exceptfds, timeout);
    }
    while (retc < 0 && pbsd_errno_i() == PBSD_EINTR);
#endif

    return (retc);
}

static
void
PRO_CALLTYPE
pbsd_closesocket_i(PRO_INT64 fd)
{
    if (fd == -1)
    {
        return;
    }

#if defined(WIN32) || defined(_WIN32_WCE)
    closesocket((SOCKET)fd);
#else
    close((int)fd);
#endif
}

static
void
PRO_CALLTYPE
InitSocket_i()
{
    g_s_sockId = pbsd_socket_i(AF_INET, SOCK_DGRAM, 0);
}

static
void
PRO_CALLTYPE
FiniSocket_i()
{
    pbsd_closesocket_i(g_s_sockId);
    g_s_sockId = -1;
}

static
void
PRO_CALLTYPE
Delay_i(unsigned long milliseconds)
{
    const PRO_INT64 te = ProGetTickCount64_s() + milliseconds;

    while (1)
    {
#if defined(WIN32) || defined(_WIN32_WCE)
        ::Sleep(1);
#else
        usleep(500);
#endif

        if (ProGetTickCount64_s() >= te)
        {
            break;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
////

PRO_SHARED_API
void
PRO_CALLTYPE
ProSharedVersion(unsigned char* major, /* = NULL */
                 unsigned char* minor, /* = NULL */
                 unsigned char* patch) /* = NULL */
{
    if (major != NULL)
    {
        *major = PRO_VER_MAJOR;
    }
    if (minor != NULL)
    {
        *minor = PRO_VER_MINOR;
    }
    if (patch != NULL)
    {
        *patch = PRO_VER_PATCH;
    }
}

PRO_SHARED_API
void
PRO_CALLTYPE
ProSrand()
{
    unsigned int seed = 0;

#if defined(WIN32) || defined(_WIN32_WCE)
    seed = (unsigned int)time(NULL) + (unsigned int)(PRO_UINT64)&seed +
        (unsigned int)::GetCurrentThreadId();
#else
    seed = (unsigned int)time(NULL) + (unsigned int)(PRO_UINT64)&seed +
        (unsigned int)pthread_self();
#endif
    if (seed == 0)
    {
        seed = (unsigned int)time(NULL);
    }

    srand(seed);
}

PRO_SHARED_API
double
PRO_CALLTYPE
ProRand_0_1()
{
    return ((double)rand() / RAND_MAX);
}

PRO_SHARED_API
PRO_INT64
PRO_CALLTYPE
ProGetTickCount64_s()
{
#if defined(WIN32) || defined(_WIN32_WCE)

    if (!g_s_tlsFlag)
    {
        g_s_lock.Lock();
        if (!g_s_tlsFlag) /* double check */
        {
            g_s_tlsKey0    = ::TlsAlloc(); /* dynamic TLS */
            g_s_tlsKey1    = ::TlsAlloc(); /* dynamic TLS */
            g_s_globalTick = ::timeGetTime();

            g_s_tlsFlag = true;
        }
        g_s_lock.Unlock();
    }

    bool updateGlobalTick = false;

    PRO_UINT32 tick0 = (PRO_UINT32)::TlsGetValue(g_s_tlsKey0);
    PRO_UINT32 tick1 = (PRO_UINT32)::TlsGetValue(g_s_tlsKey1);
    if (tick0 == 0 && tick1 == 0)
    {
        g_s_lock.Lock();
        tick0 = (PRO_UINT32)g_s_globalTick;
        tick1 = (PRO_UINT32)(g_s_globalTick >> 32);
        g_s_lock.Unlock();

        ::TlsSetValue(g_s_tlsKey0, (void*)tick0);
        ::TlsSetValue(g_s_tlsKey1, (void*)tick1);

        updateGlobalTick = true;
    }

    const PRO_UINT32 tick = ::timeGetTime();
    if (tick > tick0)
    {
        tick0 = tick;
        ::TlsSetValue(g_s_tlsKey0, (void*)tick0);
    }
    else if (tick < tick0)
    {
        tick0 = tick;
        ++tick1;
        ::TlsSetValue(g_s_tlsKey0, (void*)tick0);
        ::TlsSetValue(g_s_tlsKey1, (void*)tick1);

        updateGlobalTick = true;
    }
    else
    {
    }

    PRO_INT64 ret = tick1;
    ret <<= 32;
    ret |=  tick0;

    if (updateGlobalTick)
    {
        g_s_lock.Lock();
        if (ret > g_s_globalTick)
        {
            g_s_globalTick = ret;
        }
        g_s_lock.Unlock();
    }

    return (ret);

#elif !defined(PRO_LACKS_CLOCK_GETTIME) /* for non-MacOS */

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    PRO_INT64 ret = ts.tv_sec;
    ret *= 1000;
    ret += ts.tv_nsec / 1000000;

    return (ret);

#elif defined(PRO_MACH_ABSOLUTE_TIME)   /* for MacOS */

    if (!g_s_timebaseFlag)
    {
        g_s_lock.Lock();
        if (!g_s_timebaseFlag) /* double check */
        {
            mach_timebase_info(&g_s_timebaseInfo);

            g_s_timebaseFlag = true;
        }
        g_s_lock.Unlock();
    }

    PRO_INT64 ret = mach_absolute_time();
    ret =  ret * g_s_timebaseInfo.numer / g_s_timebaseInfo.denom; /* ns_ticks ---> ns */
    ret /= 1000000;                                               /* ns       ---> ms */

    return (ret);

#else

    struct timeval tv;
    gettimeofday(&tv, NULL);

    PRO_INT64 ret = tv.tv_sec;
    ret *= 1000;
    ret += tv.tv_usec / 1000;

    return (ret);

#endif
}

PRO_SHARED_API
void
PRO_CALLTYPE
ProSleep_s(PRO_UINT32 milliseconds)
{
    if (milliseconds == 0)
    {
#if defined(WIN32) || defined(_WIN32_WCE)
        ::Sleep(0);
#else
        usleep(0);
#endif

        return;
    }

    if (milliseconds == (PRO_UINT32)-1)
    {
        while (1)
        {
#if defined(WIN32) || defined(_WIN32_WCE)
            ::Sleep(-1);
#else
            usleep(999999);
#endif
        }
    }

    if (!g_s_socketFlag)
    {
        g_s_lock.Lock();
        if (!g_s_socketFlag) /* double check */
        {
            /*
             * initialize the socket library
             */
            pbsd_startup_i();

            InitSocket_i();
            g_s_socketFlag = true;
        }
        g_s_lock.Unlock();
    }

    const PRO_INT64 te = ProGetTickCount64_s() + milliseconds;

    while (1)
    {
        const PRO_INT64 t = ProGetTickCount64_s();
        if (t >= te)
        {
            break;
        }

        PRO_INT64 sockId = g_s_sockId;
        if (sockId == -1)
        {
            g_s_lock.Lock();
            FiniSocket_i();
            InitSocket_i();
            g_s_lock.Unlock();

            Delay_i(1);
            continue;
        }

#if !defined(WIN32) && !defined(_WIN32_WCE)
        /*
         * the descriptor value of the STDIN is less than FD_SETSIZE
         */
        if (sockId >= FD_SETSIZE)
        {
            sockId = 0;
        }
#endif

        pbsd_fd_set fds;
        PBSD_FD_ZERO(&fds);
        PBSD_FD_SET(sockId, &fds);

        struct timeval tv;
        if (te - t <= 5) /* 5ms */
        {
            tv.tv_sec  = 0;
            tv.tv_usec = 500;
        }
        else
        {
            tv.tv_sec  = (long)((te - t - 5) / 1000);        /* assert(0 <= tv_sec  <= 0x7FFFFFFF) */
            tv.tv_usec = (long)((te - t - 5) % 1000 * 1000); /* assert(0 <= tv_usec <= 0x7FFFFFFF) */
        }

        const int retc = pbsd_select_i(sockId + 1, NULL, NULL, &fds, &tv);

        assert(retc == 0);
        if (retc != 0)
        {
            g_s_lock.Lock();
            FiniSocket_i();
            InitSocket_i();
            g_s_lock.Unlock();

            Delay_i(1);
        }
    } /* end of while (...) */
}

PRO_SHARED_API
unsigned long
PRO_CALLTYPE
ProMakeTimerId()
{
    g_s_lock.Lock();

    const unsigned long timerId = g_s_nextTimerId;
    g_s_nextTimerId += 2;

    g_s_lock.Unlock();

    return (timerId);
}

PRO_SHARED_API
unsigned long
PRO_CALLTYPE
ProMakeMmTimerId()
{
    g_s_lock.Lock();

    const unsigned long timerId = g_s_nextMmTimerId;
    g_s_nextMmTimerId += 2;
    if (g_s_nextMmTimerId == 0)
    {
        g_s_nextMmTimerId += 2;
    }

    g_s_lock.Unlock();

    return (timerId);
}

PRO_SHARED_API
void*
PRO_CALLTYPE
ProAllocateSgiPoolBuffer(size_t        size,
                         unsigned long poolIndex) /* 0 ~ 9 */
{
    assert(poolIndex <= 9);
    if (poolIndex > 9)
    {
        return (NULL);
    }

    if (size == 0)
    {
        size = sizeof(PRO_UINT32) + sizeof(PRO_UINT32) + 1;
    }
    else
    {
        size = sizeof(PRO_UINT32) + sizeof(PRO_UINT32) + size;
    }

    PRO_UINT32* p = NULL;

    if (size <= SMALL_OBJ_SIZE)
    {
        switch (poolIndex)
        {
        case 0:
            {
                g_s_lock0.Lock();
                p = (PRO_UINT32*)g_s_allocator0a.allocate(size);
                g_s_lock0.Unlock();
                break;
            }
        case 1:
            {
                g_s_lock1.Lock();
                p = (PRO_UINT32*)g_s_allocator1a.allocate(size);
                g_s_lock1.Unlock();
                break;
            }
        case 2:
            {
                g_s_lock2.Lock();
                p = (PRO_UINT32*)g_s_allocator2a.allocate(size);
                g_s_lock2.Unlock();
                break;
            }
        case 3:
            {
                g_s_lock3.Lock();
                p = (PRO_UINT32*)g_s_allocator3a.allocate(size);
                g_s_lock3.Unlock();
                break;
            }
        case 4:
            {
                g_s_lock4.Lock();
                p = (PRO_UINT32*)g_s_allocator4a.allocate(size);
                g_s_lock4.Unlock();
                break;
            }
        case 5:
            {
                g_s_lock5.Lock();
                p = (PRO_UINT32*)g_s_allocator5a.allocate(size);
                g_s_lock5.Unlock();
                break;
            }
        case 6:
            {
                g_s_lock6.Lock();
                p = (PRO_UINT32*)g_s_allocator6a.allocate(size);
                g_s_lock6.Unlock();
                break;
            }
        case 7:
            {
                g_s_lock7.Lock();
                p = (PRO_UINT32*)g_s_allocator7a.allocate(size);
                g_s_lock7.Unlock();
                break;
            }
        case 8:
            {
                g_s_lock8.Lock();
                p = (PRO_UINT32*)g_s_allocator8a.allocate(size);
                g_s_lock8.Unlock();
                break;
            }
        case 9:
            {
                g_s_lock9.Lock();
                p = (PRO_UINT32*)g_s_allocator9a.allocate(size);
                g_s_lock9.Unlock();
                break;
            }
        } /* end of switch (...) */
    }
    else
    {
        switch (poolIndex)
        {
        case 0:
            {
                g_s_lock0.Lock();
                p = (PRO_UINT32*)g_s_allocator0b.allocate(size);
                g_s_lock0.Unlock();
                break;
            }
        case 1:
            {
                g_s_lock1.Lock();
                p = (PRO_UINT32*)g_s_allocator1b.allocate(size);
                g_s_lock1.Unlock();
                break;
            }
        case 2:
            {
                g_s_lock2.Lock();
                p = (PRO_UINT32*)g_s_allocator2b.allocate(size);
                g_s_lock2.Unlock();
                break;
            }
        case 3:
            {
                g_s_lock3.Lock();
                p = (PRO_UINT32*)g_s_allocator3b.allocate(size);
                g_s_lock3.Unlock();
                break;
            }
        case 4:
            {
                g_s_lock4.Lock();
                p = (PRO_UINT32*)g_s_allocator4b.allocate(size);
                g_s_lock4.Unlock();
                break;
            }
        case 5:
            {
                g_s_lock5.Lock();
                p = (PRO_UINT32*)g_s_allocator5b.allocate(size);
                g_s_lock5.Unlock();
                break;
            }
        case 6:
            {
                g_s_lock6.Lock();
                p = (PRO_UINT32*)g_s_allocator6b.allocate(size);
                g_s_lock6.Unlock();
                break;
            }
        case 7:
            {
                g_s_lock7.Lock();
                p = (PRO_UINT32*)g_s_allocator7b.allocate(size);
                g_s_lock7.Unlock();
                break;
            }
        case 8:
            {
                g_s_lock8.Lock();
                p = (PRO_UINT32*)g_s_allocator8b.allocate(size);
                g_s_lock8.Unlock();
                break;
            }
        case 9:
            {
                g_s_lock9.Lock();
                p = (PRO_UINT32*)g_s_allocator9b.allocate(size);
                g_s_lock9.Unlock();
                break;
            }
        } /* end of switch (...) */
    }

    if (p == NULL)
    {
        return (NULL);
    }

    *p = (PRO_UINT32)size;

    return (p + 2);
}

PRO_SHARED_API
void*
PRO_CALLTYPE
ProReallocateSgiPoolBuffer(void*         buf,
                           size_t        newSize,
                           unsigned long poolIndex) /* 0 ~ 9 */
{
    assert(poolIndex <= 9);
    if (poolIndex > 9)
    {
        return (NULL);
    }

    if (buf == NULL)
    {
        return (ProAllocateSgiPoolBuffer(newSize, poolIndex));
    }

    if (newSize == 0)
    {
        ProDeallocateSgiPoolBuffer(buf, poolIndex);

        return (NULL);
    }

    PRO_UINT32* const p = (PRO_UINT32*)buf - 2;
    if (*p < sizeof(PRO_UINT32) + sizeof(PRO_UINT32) + 1)
    {
        return (NULL);
    }

    newSize = sizeof(PRO_UINT32) + sizeof(PRO_UINT32) + newSize;

    PRO_UINT32* q = NULL;

    if (*p <= SMALL_OBJ_SIZE)
    {
        switch (poolIndex)
        {
        case 0:
            {
                g_s_lock0.Lock();
                q = (PRO_UINT32*)g_s_allocator0a.reallocate(p, *p, newSize);
                g_s_lock0.Unlock();
                break;
            }
        case 1:
            {
                g_s_lock1.Lock();
                q = (PRO_UINT32*)g_s_allocator1a.reallocate(p, *p, newSize);
                g_s_lock1.Unlock();
                break;
            }
        case 2:
            {
                g_s_lock2.Lock();
                q = (PRO_UINT32*)g_s_allocator2a.reallocate(p, *p, newSize);
                g_s_lock2.Unlock();
                break;
            }
        case 3:
            {
                g_s_lock3.Lock();
                q = (PRO_UINT32*)g_s_allocator3a.reallocate(p, *p, newSize);
                g_s_lock3.Unlock();
                break;
            }
        case 4:
            {
                g_s_lock4.Lock();
                q = (PRO_UINT32*)g_s_allocator4a.reallocate(p, *p, newSize);
                g_s_lock4.Unlock();
                break;
            }
        case 5:
            {
                g_s_lock5.Lock();
                q = (PRO_UINT32*)g_s_allocator5a.reallocate(p, *p, newSize);
                g_s_lock5.Unlock();
                break;
            }
        case 6:
            {
                g_s_lock6.Lock();
                q = (PRO_UINT32*)g_s_allocator6a.reallocate(p, *p, newSize);
                g_s_lock6.Unlock();
                break;
            }
        case 7:
            {
                g_s_lock7.Lock();
                q = (PRO_UINT32*)g_s_allocator7a.reallocate(p, *p, newSize);
                g_s_lock7.Unlock();
                break;
            }
        case 8:
            {
                g_s_lock8.Lock();
                q = (PRO_UINT32*)g_s_allocator8a.reallocate(p, *p, newSize);
                g_s_lock8.Unlock();
                break;
            }
        case 9:
            {
                g_s_lock9.Lock();
                q = (PRO_UINT32*)g_s_allocator9a.reallocate(p, *p, newSize);
                g_s_lock9.Unlock();
                break;
            }
        } /* end of switch (...) */
    }
    else
    {
        switch (poolIndex)
        {
        case 0:
            {
                g_s_lock0.Lock();
                q = (PRO_UINT32*)g_s_allocator0b.reallocate(p, *p, newSize);
                g_s_lock0.Unlock();
                break;
            }
        case 1:
            {
                g_s_lock1.Lock();
                q = (PRO_UINT32*)g_s_allocator1b.reallocate(p, *p, newSize);
                g_s_lock1.Unlock();
                break;
            }
        case 2:
            {
                g_s_lock2.Lock();
                q = (PRO_UINT32*)g_s_allocator2b.reallocate(p, *p, newSize);
                g_s_lock2.Unlock();
                break;
            }
        case 3:
            {
                g_s_lock3.Lock();
                q = (PRO_UINT32*)g_s_allocator3b.reallocate(p, *p, newSize);
                g_s_lock3.Unlock();
                break;
            }
        case 4:
            {
                g_s_lock4.Lock();
                q = (PRO_UINT32*)g_s_allocator4b.reallocate(p, *p, newSize);
                g_s_lock4.Unlock();
                break;
            }
        case 5:
            {
                g_s_lock5.Lock();
                q = (PRO_UINT32*)g_s_allocator5b.reallocate(p, *p, newSize);
                g_s_lock5.Unlock();
                break;
            }
        case 6:
            {
                g_s_lock6.Lock();
                q = (PRO_UINT32*)g_s_allocator6b.reallocate(p, *p, newSize);
                g_s_lock6.Unlock();
                break;
            }
        case 7:
            {
                g_s_lock7.Lock();
                q = (PRO_UINT32*)g_s_allocator7b.reallocate(p, *p, newSize);
                g_s_lock7.Unlock();
                break;
            }
        case 8:
            {
                g_s_lock8.Lock();
                q = (PRO_UINT32*)g_s_allocator8b.reallocate(p, *p, newSize);
                g_s_lock8.Unlock();
                break;
            }
        case 9:
            {
                g_s_lock9.Lock();
                q = (PRO_UINT32*)g_s_allocator9b.reallocate(p, *p, newSize);
                g_s_lock9.Unlock();
                break;
            }
        } /* end of switch (...) */
    }

    if (q == NULL)
    {
        return (NULL);
    }

    *q = (PRO_UINT32)newSize;

    return (q + 2);
}

PRO_SHARED_API
void
PRO_CALLTYPE
ProDeallocateSgiPoolBuffer(void*         buf,
                           unsigned long poolIndex) /* 0 ~ 9 */
{
    assert(poolIndex <= 9);
    if (buf == NULL || poolIndex > 9)
    {
        return;
    }

    PRO_UINT32* const p = (PRO_UINT32*)buf - 2;
    if (*p < sizeof(PRO_UINT32) + sizeof(PRO_UINT32) + 1)
    {
        return;
    }

    if (*p <= SMALL_OBJ_SIZE)
    {
        switch (poolIndex)
        {
        case 0:
            {
                g_s_lock0.Lock();
                g_s_allocator0a.deallocate(p, *p);
                g_s_lock0.Unlock();
                break;
            }
        case 1:
            {
                g_s_lock1.Lock();
                g_s_allocator1a.deallocate(p, *p);
                g_s_lock1.Unlock();
                break;
            }
        case 2:
            {
                g_s_lock2.Lock();
                g_s_allocator2a.deallocate(p, *p);
                g_s_lock2.Unlock();
                break;
            }
        case 3:
            {
                g_s_lock3.Lock();
                g_s_allocator3a.deallocate(p, *p);
                g_s_lock3.Unlock();
                break;
            }
        case 4:
            {
                g_s_lock4.Lock();
                g_s_allocator4a.deallocate(p, *p);
                g_s_lock4.Unlock();
                break;
            }
        case 5:
            {
                g_s_lock5.Lock();
                g_s_allocator5a.deallocate(p, *p);
                g_s_lock5.Unlock();
                break;
            }
        case 6:
            {
                g_s_lock6.Lock();
                g_s_allocator6a.deallocate(p, *p);
                g_s_lock6.Unlock();
                break;
            }
        case 7:
            {
                g_s_lock7.Lock();
                g_s_allocator7a.deallocate(p, *p);
                g_s_lock7.Unlock();
                break;
            }
        case 8:
            {
                g_s_lock8.Lock();
                g_s_allocator8a.deallocate(p, *p);
                g_s_lock8.Unlock();
                break;
            }
        case 9:
            {
                g_s_lock9.Lock();
                g_s_allocator9a.deallocate(p, *p);
                g_s_lock9.Unlock();
                break;
            }
        } /* end of switch (...) */
    }
    else
    {
        switch (poolIndex)
        {
        case 0:
            {
                g_s_lock0.Lock();
                g_s_allocator0b.deallocate(p, *p);
                g_s_lock0.Unlock();
                break;
            }
        case 1:
            {
                g_s_lock1.Lock();
                g_s_allocator1b.deallocate(p, *p);
                g_s_lock1.Unlock();
                break;
            }
        case 2:
            {
                g_s_lock2.Lock();
                g_s_allocator2b.deallocate(p, *p);
                g_s_lock2.Unlock();
                break;
            }
        case 3:
            {
                g_s_lock3.Lock();
                g_s_allocator3b.deallocate(p, *p);
                g_s_lock3.Unlock();
                break;
            }
        case 4:
            {
                g_s_lock4.Lock();
                g_s_allocator4b.deallocate(p, *p);
                g_s_lock4.Unlock();
                break;
            }
        case 5:
            {
                g_s_lock5.Lock();
                g_s_allocator5b.deallocate(p, *p);
                g_s_lock5.Unlock();
                break;
            }
        case 6:
            {
                g_s_lock6.Lock();
                g_s_allocator6b.deallocate(p, *p);
                g_s_lock6.Unlock();
                break;
            }
        case 7:
            {
                g_s_lock7.Lock();
                g_s_allocator7b.deallocate(p, *p);
                g_s_lock7.Unlock();
                break;
            }
        case 8:
            {
                g_s_lock8.Lock();
                g_s_allocator8b.deallocate(p, *p);
                g_s_lock8.Unlock();
                break;
            }
        case 9:
            {
                g_s_lock9.Lock();
                g_s_allocator9b.deallocate(p, *p);
                g_s_lock9.Unlock();
                break;
            }
        } /* end of switch (...) */
    }
}

PRO_SHARED_API
void
PRO_CALLTYPE
ProGetSgiSmallPoolInfo(void*         freeList[60],
                       size_t        objSize[60],
                       size_t*       heapSize,  /* = NULL */
                       unsigned long poolIndex) /* 0 ~ 9 */
{
    assert(poolIndex <= 9);
    if (poolIndex > 9)
    {
        return;
    }

    size_t heapSize2 = 0;

    switch (poolIndex)
    {
    case 0:
        {
            g_s_lock0.Lock();
            g_s_allocator0a.get_info(freeList, objSize, heapSize2);
            g_s_lock0.Unlock();
            break;
        }
    case 1:
        {
            g_s_lock1.Lock();
            g_s_allocator1a.get_info(freeList, objSize, heapSize2);
            g_s_lock1.Unlock();
            break;
        }
    case 2:
        {
            g_s_lock2.Lock();
            g_s_allocator2a.get_info(freeList, objSize, heapSize2);
            g_s_lock2.Unlock();
            break;
        }
    case 3:
        {
            g_s_lock3.Lock();
            g_s_allocator3a.get_info(freeList, objSize, heapSize2);
            g_s_lock3.Unlock();
            break;
        }
    case 4:
        {
            g_s_lock4.Lock();
            g_s_allocator4a.get_info(freeList, objSize, heapSize2);
            g_s_lock4.Unlock();
            break;
        }
    case 5:
        {
            g_s_lock5.Lock();
            g_s_allocator5a.get_info(freeList, objSize, heapSize2);
            g_s_lock5.Unlock();
            break;
        }
    case 6:
        {
            g_s_lock6.Lock();
            g_s_allocator6a.get_info(freeList, objSize, heapSize2);
            g_s_lock6.Unlock();
            break;
        }
    case 7:
        {
            g_s_lock7.Lock();
            g_s_allocator7a.get_info(freeList, objSize, heapSize2);
            g_s_lock7.Unlock();
            break;
        }
    case 8:
        {
            g_s_lock8.Lock();
            g_s_allocator8a.get_info(freeList, objSize, heapSize2);
            g_s_lock8.Unlock();
            break;
        }
    case 9:
        {
            g_s_lock9.Lock();
            g_s_allocator9a.get_info(freeList, objSize, heapSize2);
            g_s_lock9.Unlock();
            break;
        }
    } /* end of switch (...) */

    if (heapSize != NULL)
    {
        *heapSize = heapSize2;
    }
}

PRO_SHARED_API
void
PRO_CALLTYPE
ProGetSgiBigPoolInfo(void*         freeList[60],
                     size_t        objSize[60],
                     size_t*       heapSize,  /* = NULL */
                     unsigned long poolIndex) /* 0 ~ 9 */
{
    assert(poolIndex <= 9);
    if (poolIndex > 9)
    {
        return;
    }

    size_t heapSize2 = 0;

    switch (poolIndex)
    {
    case 0:
        {
            g_s_lock0.Lock();
            g_s_allocator0b.get_info(freeList, objSize, heapSize2);
            g_s_lock0.Unlock();
            break;
        }
    case 1:
        {
            g_s_lock1.Lock();
            g_s_allocator1b.get_info(freeList, objSize, heapSize2);
            g_s_lock1.Unlock();
            break;
        }
    case 2:
        {
            g_s_lock2.Lock();
            g_s_allocator2b.get_info(freeList, objSize, heapSize2);
            g_s_lock2.Unlock();
            break;
        }
    case 3:
        {
            g_s_lock3.Lock();
            g_s_allocator3b.get_info(freeList, objSize, heapSize2);
            g_s_lock3.Unlock();
            break;
        }
    case 4:
        {
            g_s_lock4.Lock();
            g_s_allocator4b.get_info(freeList, objSize, heapSize2);
            g_s_lock4.Unlock();
            break;
        }
    case 5:
        {
            g_s_lock5.Lock();
            g_s_allocator5b.get_info(freeList, objSize, heapSize2);
            g_s_lock5.Unlock();
            break;
        }
    case 6:
        {
            g_s_lock6.Lock();
            g_s_allocator6b.get_info(freeList, objSize, heapSize2);
            g_s_lock6.Unlock();
            break;
        }
    case 7:
        {
            g_s_lock7.Lock();
            g_s_allocator7b.get_info(freeList, objSize, heapSize2);
            g_s_lock7.Unlock();
            break;
        }
    case 8:
        {
            g_s_lock8.Lock();
            g_s_allocator8b.get_info(freeList, objSize, heapSize2);
            g_s_lock8.Unlock();
            break;
        }
    case 9:
        {
            g_s_lock9.Lock();
            g_s_allocator9b.get_info(freeList, objSize, heapSize2);
            g_s_lock9.Unlock();
            break;
        }
    } /* end of switch (...) */

    if (heapSize != NULL)
    {
        *heapSize = heapSize2;
    }
}

/////////////////////////////////////////////////////////////////////////////
////

#if defined(__cplusplus)
}
#endif
