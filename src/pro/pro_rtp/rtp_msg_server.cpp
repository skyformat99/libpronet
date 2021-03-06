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

#include "rtp_msg_server.h"
#include "rtp_foundation.h"
#include "rtp_framework.h"
#include "rtp_msg_command.h"
#include "../pro_net/pro_net.h"
#include "../pro_util/pro_bsd_wrapper.h"
#include "../pro_util/pro_config_file.h"
#include "../pro_util/pro_config_stream.h"
#include "../pro_util/pro_functor_command.h"
#include "../pro_util/pro_functor_command_task.h"
#include "../pro_util/pro_memory_pool.h"
#include "../pro_util/pro_ref_count.h"
#include "../pro_util/pro_stl.h"
#include "../pro_util/pro_thread_mutex.h"
#include "../pro_util/pro_time_util.h"
#include "../pro_util/pro_z.h"
#include <cassert>

/////////////////////////////////////////////////////////////////////////////
////

#define MAX_PENDING_COUNT     5000
#define DEFAULT_REDLINE_BYTES (1024 * 1024 * 8)
#define DEFAULT_TIMEOUT       20

static const RTP_MSG_USER  ROOT_ID    (1, 1, 0);                                  /* 1-1 */
static const RTP_MSG_USER  ROOT_ID_C2S(1, 1, 65535);                              /* 1-1-65535 */
static const unsigned char SERVER_CID    = 1;                                     /* 1-... */
static const PRO_UINT64    NODE_UID_MIN  = 1;                                     /* 1 ~ ... */
static const PRO_UINT64    NODE_UID_MAX  = ((PRO_UINT64)0xEF << 32) | 0xFFFFFFFF; /* 1 ~ 0xEFFFFFFFFF */
static const PRO_UINT64    NODE_UID_MAXX = ((PRO_UINT64)0xFF << 32) | 0xFFFFFFFF; /* 1 ~ 0xFFFFFFFFFF */
static const PRO_UINT16    NODE_IID_MIN  = 1;                                     /* 1 ~ ... */

typedef void (CRtpMsgServer::* ACTION)(PRO_INT64*);

static PRO_UINT64      g_s_nextServerId = (PRO_UINT64)0xF0 << 32; /* 0xF000000000 ~ 0xFFFFFFFFFF */
static PRO_UINT64      g_s_nextClientId = (PRO_UINT64)0xF0 << 32; /* 0xF000000000 ~ 0xFFFFFFFFFF */
static CProThreadMutex g_s_lock;

/////////////////////////////////////////////////////////////////////////////
////

static
PRO_UINT64
PRO_CALLTYPE
MakeServerId_i()
{
    g_s_lock.Lock();

    const PRO_UINT64 userId = g_s_nextServerId;
    ++g_s_nextServerId;
    if (g_s_nextServerId > (((PRO_UINT64)0xFF << 32) | 0xFFFFFFFF))
    {
        g_s_nextServerId = (PRO_UINT64)0xF0 << 32;
    }

    g_s_lock.Unlock();

    return (userId);
}

static
PRO_UINT64
PRO_CALLTYPE
MakeClientId_i()
{
    g_s_lock.Lock();

    const PRO_UINT64 userId = g_s_nextClientId;
    ++g_s_nextClientId;
    if (g_s_nextClientId > (((PRO_UINT64)0xFF << 32) | 0xFFFFFFFF))
    {
        g_s_nextClientId = (PRO_UINT64)0xF0 << 32;
    }

    g_s_lock.Unlock();

    return (userId);
}

/////////////////////////////////////////////////////////////////////////////
////

CRtpMsgServer*
CRtpMsgServer::CreateInstance(RTP_MM_TYPE                  mmType,
                              const PRO_SSL_SERVER_CONFIG* sslConfig, /* = NULL */
                              bool                         sslForced) /* = false */
{
    assert(mmType != 0);
    if (mmType == 0)
    {
        return (NULL);
    }

    CRtpMsgServer* const msgServer = new CRtpMsgServer(mmType, sslConfig, sslForced);

    return (msgServer);
}

CRtpMsgServer::CRtpMsgServer(RTP_MM_TYPE                  mmType,
                             const PRO_SSL_SERVER_CONFIG* sslConfig, /* = NULL */
                             bool                         sslForced) /* = false */
                             :
m_mmType(mmType),
m_sslConfig(sslConfig),
m_sslForced(sslForced)
{
    m_observer         = NULL;
    m_reactor          = NULL;
    m_service          = NULL;
    m_task             = NULL;
    m_timeoutInSeconds = DEFAULT_TIMEOUT;
    m_redlineBytes     = DEFAULT_REDLINE_BYTES;
}

CRtpMsgServer::~CRtpMsgServer()
{
    Fini();
}

bool
CRtpMsgServer::Init(IRtpMsgServerObserver* observer,
                    IProReactor*           reactor,
                    unsigned short         serviceHubPort,
                    unsigned long          timeoutInSeconds) /* = 0 */
{
    assert(observer != NULL);
    assert(reactor != NULL);
    assert(serviceHubPort > 0);
    if (observer == NULL || reactor == NULL || serviceHubPort == 0)
    {
        return (false);
    }

    if (timeoutInSeconds == 0)
    {
        timeoutInSeconds = DEFAULT_TIMEOUT;
    }

    IRtpService*            service = NULL;
    CProFunctorCommandTask* task    = NULL;

    {
        CProThreadMutexGuard mon(m_lock);

        assert(m_observer == NULL);
        assert(m_reactor == NULL);
        assert(m_service == NULL);
        assert(m_task == NULL);
        if (m_observer != NULL || m_reactor != NULL || m_service != NULL || m_task != NULL)
        {
            return (false);
        }

        task = new CProFunctorCommandTask;
        if (!task->Start())
        {
            goto EXIT;
        }

        service = CreateRtpService(
            m_sslConfig, this, reactor, m_mmType, serviceHubPort, timeoutInSeconds);
        if (service == NULL)
        {
            goto EXIT;
        }

        observer->AddRef();
        m_observer         = observer;
        m_reactor          = reactor;
        m_service          = service;
        m_task             = task;
        m_timeoutInSeconds = timeoutInSeconds;
    }

    return (true);

EXIT:

    if (task != NULL)
    {
        task->Stop();
        delete task;
    }

    DeleteRtpService(service);

    return (false);
}

void
CRtpMsgServer::Fini()
{
    IRtpMsgServerObserver*                      observer = NULL;
    IRtpService*                                service  = NULL;
    CProFunctorCommandTask*                     task     = NULL;
    CProStlMap<IRtpSession*, RTP_MSG_LINK_CTX*> session2Ctx;

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return;
        }

        session2Ctx = m_session2Ctx;
        m_session2Ctx.clear();
        m_user2Ctx.clear();

        task = m_task;
        m_task = NULL;
        service = m_service;
        m_service = NULL;
        m_reactor = NULL;
        observer = m_observer;
        m_observer = NULL;
    }

    task->Stop();
    delete task;

    CProStlMap<IRtpSession*, RTP_MSG_LINK_CTX*>::const_iterator       itr = session2Ctx.begin();
    CProStlMap<IRtpSession*, RTP_MSG_LINK_CTX*>::const_iterator const end = session2Ctx.end();

    for (; itr != end; ++itr)
    {
        DeleteRtpSessionWrapper(itr->first);
        delete itr->second;
    }

    DeleteRtpService(service);
    observer->Release();
}

unsigned long
PRO_CALLTYPE
CRtpMsgServer::AddRef()
{
    const unsigned long refCount = CProRefCount::AddRef();

    return (refCount);
}

unsigned long
PRO_CALLTYPE
CRtpMsgServer::Release()
{
    const unsigned long refCount = CProRefCount::Release();

    return (refCount);
}

void
PRO_CALLTYPE
CRtpMsgServer::GetUserCount(unsigned long* pendingUserCount,   /* = NULL */
                            unsigned long* baseUserCount,      /* = NULL */
                            unsigned long* subUserCount) const /* = NULL */
{
    {
        CProThreadMutexGuard mon(m_lock);

        if (pendingUserCount != NULL)
        {
            *pendingUserCount = m_task != NULL ? m_task->GetSize() : 0;
        }
        if (baseUserCount != NULL)
        {
            *baseUserCount    = (unsigned long)m_session2Ctx.size();
        }
        if (subUserCount != NULL)
        {
            *subUserCount     = (unsigned long)(m_user2Ctx.size() - m_session2Ctx.size());
        }
    }
}

void
PRO_CALLTYPE
CRtpMsgServer::KickoutUser(const RTP_MSG_USER* user)
{
    assert(user != NULL);
    if (user == NULL || user->classId == 0 || user->UserId() == 0)
    {
        return;
    }

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return;
        }

        IProFunctorCommand* const command =
            CProFunctorCommand_cpp<CRtpMsgServer, ACTION>::CreateInstance(
            *this,
            &CRtpMsgServer::AsyncKickoutUser,
            (PRO_INT64)user->classId,
            (PRO_INT64)user->UserId(),
            (PRO_INT64)user->instId
            );
        m_task->Put(command);
    }
}

void
CRtpMsgServer::AsyncKickoutUser(PRO_INT64* args)
{
    const RTP_MSG_USER user((unsigned char)args[0], args[1], (PRO_UINT16)args[2]);
    assert(user.classId > 0);
    assert(user.UserId() > 0);

    IRtpMsgServerObserver*   observer   = NULL;
    IRtpSession*             oldSession = NULL;
    CProStlSet<RTP_MSG_USER> oldUsers;

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return;
        }

        CProStlMap<RTP_MSG_USER, RTP_MSG_LINK_CTX*>::iterator const itr = m_user2Ctx.find(user);
        if (itr == m_user2Ctx.end())
        {
            return;
        }

        RTP_MSG_LINK_CTX* const ctx = itr->second;

        if (user == ctx->baseUser)
        {
            oldSession = ctx->session;
            oldUsers   = ctx->subUsers;

            CProStlSet<RTP_MSG_USER>::const_iterator       itr2 = oldUsers.begin();
            CProStlSet<RTP_MSG_USER>::const_iterator const end2 = oldUsers.end();

            for (; itr2 != end2; ++itr2)
            {
                m_user2Ctx.erase(*itr2);
            }

            m_user2Ctx.erase(itr);
            m_session2Ctx.erase(oldSession);
            delete ctx;

            oldUsers.insert(user);
        }
        else
        {
            oldUsers.insert(user);

            ctx->subUsers.erase(user);
            m_user2Ctx.erase(itr);

            NotifyKickout(ctx->session, ctx->baseUser, user);
        }

        m_observer->AddRef();
        observer = m_observer;
    }

    CProStlSet<RTP_MSG_USER>::const_iterator       itr = oldUsers.begin();
    CProStlSet<RTP_MSG_USER>::const_iterator const end = oldUsers.end();

    for (; itr != end; ++itr)
    {
        const RTP_MSG_USER user2 = *itr;
        observer->OnCloseUser(this, &user2, -1, 0);
    }

    observer->Release();
    DeleteRtpSessionWrapper(oldSession);
}

bool
PRO_CALLTYPE
CRtpMsgServer::SendMsg(const void*         buf,
                       PRO_UINT16          size,
                       PRO_UINT32          charset,
                       const RTP_MSG_USER* dstUsers,
                       unsigned char       dstUserCount)
{
    assert(buf != NULL);
    assert(size > 0);
    assert(dstUsers != NULL);
    assert(dstUserCount > 0);
    if (buf == NULL || size == 0 || dstUsers == NULL || dstUserCount == 0)
    {
        return (false);
    }

    bool ret = true;

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return (false);
        }

        unsigned char                                          sessionCount = 0;
        IRtpSession*                                           sessions[255];
        CProStlMap<IRtpSession*, CProStlVector<RTP_MSG_USER> > session2Users;

        for (int i = 0; i < (int)dstUserCount; ++i)
        {
            if (dstUsers[i].classId == 0 || dstUsers[i].UserId() == 0 || dstUsers[i].IsRoot())
            {
                ret = false;
                continue;
            }

            CProStlMap<RTP_MSG_USER, RTP_MSG_LINK_CTX*>::const_iterator const itr =
                m_user2Ctx.find(dstUsers[i]);
            if (itr == m_user2Ctx.end())
            {
                ret = false;
                continue;
            }

            RTP_MSG_LINK_CTX* const ctx = itr->second;

            if (dstUsers[i] == ctx->baseUser)
            {
                sessions[sessionCount] = ctx->session;
                ++sessionCount;
            }
            else
            {
                session2Users[ctx->session].push_back(dstUsers[i]);
            }
        }

        /*
         * to baseUsers
         */
        if (sessionCount > 0)
        {
            const bool ret2 = SendMsgToDownlink(sessions, sessionCount, buf, size,
                charset, &ROOT_ID, NULL, 0, NULL);
            if (!ret2)
            {
                ret = false;
            }
        }

        /*
         * to subUsers
         */
        {
            CProStlMap<IRtpSession*, CProStlVector<RTP_MSG_USER> >::const_iterator       itr = session2Users.begin();
            CProStlMap<IRtpSession*, CProStlVector<RTP_MSG_USER> >::const_iterator const end = session2Users.end();

            for (; itr != end; ++itr)
            {
                IRtpSession*                       session = itr->first;
                const CProStlVector<RTP_MSG_USER>& users   = itr->second;

                const bool ret2 = SendMsgToDownlink(&session, 1, buf, size,
                    charset, &ROOT_ID, &users[0], (unsigned char)users.size(), NULL);
                if (!ret2)
                {
                    ret = false;
                }
            }
        }
    }

    return (ret);
}

void
PRO_CALLTYPE
CRtpMsgServer::SetOutputRedline(unsigned long redlineBytes)
{
    assert(redlineBytes > 0);
    if (redlineBytes == 0)
    {
        return;
    }

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return;
        }

        m_redlineBytes = redlineBytes;
    }
}

unsigned long
PRO_CALLTYPE
CRtpMsgServer::GetOutputRedline() const
{
    unsigned long redlineBytes = 0;

    {
        CProThreadMutexGuard mon(m_lock);

        redlineBytes = m_redlineBytes;
    }

    return (redlineBytes);
}

void
PRO_CALLTYPE
CRtpMsgServer::OnAcceptSession(IRtpService*            service,
                               PRO_INT64               sockId,
                               bool                    unixSocket,
                               const char*             remoteIp,
                               unsigned short          remotePort,
                               const RTP_SESSION_INFO* remoteInfo,
                               PRO_UINT64              nonce)
{
    assert(service != NULL);
    assert(sockId != -1);
    assert(remoteIp != NULL);
    assert(remoteInfo != NULL);
    if (service == NULL || sockId == -1 || remoteIp == NULL || remoteInfo == NULL)
    {
        return;
    }

    assert(remoteInfo->sessionType == RTP_ST_TCPCLIENT_EX);
    assert(remoteInfo->mmType == m_mmType);
    if (remoteInfo->sessionType != RTP_ST_TCPCLIENT_EX || remoteInfo->mmType != m_mmType)
    {
        goto EXIT;
    }

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            goto EXIT;
        }

        if (service != m_service)
        {
            goto EXIT;
        }

        if (m_task->GetSize() >= MAX_PENDING_COUNT)
        {
            goto EXIT;
        }

        RTP_MSG_AsyncOnAcceptSession* const arg = new RTP_MSG_AsyncOnAcceptSession;
        arg->sockId     = sockId;
        arg->unixSocket = unixSocket;
        arg->remoteIp   = remoteIp;
        arg->remotePort = remotePort;
        arg->nonce      = nonce;
        arg->remoteInfo = *remoteInfo;

        IProFunctorCommand* const command =
            CProFunctorCommand_cpp<CRtpMsgServer, ACTION>::CreateInstance(
            *this,
            &CRtpMsgServer::AsyncOnAcceptSession,
            (PRO_INT64)arg
            );
        m_task->Put(command);
    }

    return;

EXIT:

    ProCloseSockId(sockId);
}

void
PRO_CALLTYPE
CRtpMsgServer::OnAcceptSession(IRtpService*            service,
                               PRO_SSL_CTX*            sslCtx,
                               PRO_INT64               sockId,
                               bool                    unixSocket,
                               const char*             remoteIp,
                               unsigned short          remotePort,
                               const RTP_SESSION_INFO* remoteInfo,
                               PRO_UINT64              nonce)
{
    assert(service != NULL);
    assert(sslCtx != NULL);
    assert(sockId != -1);
    assert(remoteIp != NULL);
    assert(remoteInfo != NULL);
    if (service == NULL || sslCtx == NULL || sockId == -1 ||
        remoteIp == NULL || remoteInfo == NULL)
    {
        return;
    }

    assert(remoteInfo->sessionType == RTP_ST_SSLCLIENT_EX);
    assert(remoteInfo->mmType == m_mmType);
    if (remoteInfo->sessionType != RTP_ST_SSLCLIENT_EX || remoteInfo->mmType != m_mmType)
    {
        goto EXIT;
    }

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            goto EXIT;
        }

        if (service != m_service)
        {
            goto EXIT;
        }

        if (m_task->GetSize() >= MAX_PENDING_COUNT)
        {
            goto EXIT;
        }

        RTP_MSG_AsyncOnAcceptSession* const arg = new RTP_MSG_AsyncOnAcceptSession;
        arg->sslCtx     = sslCtx;
        arg->sockId     = sockId;
        arg->unixSocket = unixSocket;
        arg->remoteIp   = remoteIp;
        arg->remotePort = remotePort;
        arg->nonce      = nonce;
        arg->remoteInfo = *remoteInfo;

        IProFunctorCommand* const command =
            CProFunctorCommand_cpp<CRtpMsgServer, ACTION>::CreateInstance(
            *this,
            &CRtpMsgServer::AsyncOnAcceptSession,
            (PRO_INT64)arg
            );
        m_task->Put(command);
    }

    return;

EXIT:

    ProSslCtx_Delete(sslCtx);
    ProCloseSockId(sockId);
}

void
CRtpMsgServer::AsyncOnAcceptSession(PRO_INT64* args)
{
    RTP_MSG_AsyncOnAcceptSession* const arg = (RTP_MSG_AsyncOnAcceptSession*)args[0];

    assert(arg->sockId != -1);
    assert(
        arg->remoteInfo.sessionType == RTP_ST_TCPCLIENT_EX ||
        arg->remoteInfo.sessionType == RTP_ST_SSLCLIENT_EX
        );
    assert(arg->remoteInfo.mmType == m_mmType);

    bool                   ret      = false;
    IRtpMsgServerObserver* observer = NULL;
    RTP_MSG_HEADER         msgHeader;
    RTP_MSG_USER           baseUser;
    PRO_UINT64             userId   = 0;
    PRO_UINT16             instId   = 0;
    PRO_INT64              appData  = 0;
    bool                   isC2s    = false;

    if (m_sslConfig == NULL)
    {
        if (arg->remoteInfo.sessionType != RTP_ST_TCPCLIENT_EX)
        {
            goto EXIT;
        }
    }
    else if (m_sslForced)
    {
        if (arg->remoteInfo.sessionType != RTP_ST_SSLCLIENT_EX)
        {
            goto EXIT;
        }
    }
    else
    {
    }

    memcpy(&msgHeader, arg->remoteInfo.userData, sizeof(RTP_MSG_HEADER));
    baseUser        = msgHeader.srcUser;
    baseUser.instId = pbsd_ntoh16(msgHeader.srcUser.instId);

    assert(baseUser.classId > 0);
    assert(
        baseUser.UserId() == 0 ||
        baseUser.UserId() >= NODE_UID_MIN && baseUser.UserId() <= NODE_UID_MAX
        );
    assert(!baseUser.IsRoot());
    if (baseUser.classId == 0
        ||
        baseUser.UserId() != 0 &&
        (baseUser.UserId() < NODE_UID_MIN || baseUser.UserId() > NODE_UID_MAX)
        ||
        baseUser.IsRoot())
    {
        goto EXIT;
    }

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            goto EXIT;
        }

        m_observer->AddRef();
        observer = m_observer;
    }

    if (baseUser.UserId() == 0)
    {
        if (baseUser.classId == SERVER_CID)
        {
            baseUser.UserId(MakeServerId_i());
        }
        else
        {
            baseUser.UserId(MakeClientId_i());
        }
        baseUser.instId = NODE_IID_MIN;
    }

    ret = observer->OnCheckUser(
        this,
        &baseUser,
        arg->remoteIp.c_str(),
        NULL,
        arg->remoteInfo.passwordHash,
        arg->nonce,
        &userId,
        &instId,
        &appData,
        &isC2s
        );
    observer->Release();

    if (!ret)
    {
        goto EXIT;
    }

    baseUser.UserId(userId);
    baseUser.instId = instId;

    ret = false; /* reset */

    assert(userId >= NODE_UID_MIN);
    assert(userId <= NODE_UID_MAXX);
    if (userId < NODE_UID_MIN || userId > NODE_UID_MAXX)
    {
        goto EXIT;
    }

    RTP_SESSION_INFO localInfo;
    memset(&localInfo, 0, sizeof(RTP_SESSION_INFO));
    localInfo.remoteVersion = arg->remoteInfo.localVersion;
    localInfo.mmType        = m_mmType;

    RTP_INIT_ARGS initArgs;
    memset(&initArgs, 0, sizeof(RTP_INIT_ARGS));

    if (arg->remoteInfo.sessionType == RTP_ST_SSLCLIENT_EX)
    {
        initArgs.sslserverEx.observer   = this;
        initArgs.sslserverEx.reactor    = m_reactor;
        initArgs.sslserverEx.sslCtx     = arg->sslCtx;
        initArgs.sslserverEx.sockId     = arg->sockId;
        initArgs.sslserverEx.unixSocket = arg->unixSocket;

        ret = AddBaseUser(RTP_ST_SSLSERVER_EX, initArgs, localInfo, baseUser, arg->remoteIp,
            appData, isC2s);
    }
    else
    {
        initArgs.tcpserverEx.observer   = this;
        initArgs.tcpserverEx.reactor    = m_reactor;
        initArgs.tcpserverEx.sockId     = arg->sockId;
        initArgs.tcpserverEx.unixSocket = arg->unixSocket;

        ret = AddBaseUser(RTP_ST_TCPSERVER_EX, initArgs, localInfo, baseUser, arg->remoteIp,
            appData, isC2s);
    }

EXIT:

    if (!ret)
    {
        ProSslCtx_Delete(arg->sslCtx);
        ProCloseSockId(arg->sockId);
    }

    delete arg;
}

void
PRO_CALLTYPE
CRtpMsgServer::OnRecvSession(IRtpSession* session,
                             IRtpPacket*  packet)
{
    assert(session != NULL);
    assert(packet != NULL);
    assert(packet->GetPayloadSize() > sizeof(RTP_MSG_HEADER));
    if (session == NULL || packet == NULL || packet->GetPayloadSize() <= sizeof(RTP_MSG_HEADER))
    {
        return;
    }

    RTP_MSG_HEADER* const msgHeaderPtr = (RTP_MSG_HEADER*)packet->GetPayloadBuffer();
    if (msgHeaderPtr->dstUserCount == 0)
    {
        msgHeaderPtr->dstUserCount = 1;
    }

    const unsigned long msgHeaderSize =
        sizeof(RTP_MSG_HEADER) + sizeof(RTP_MSG_USER) * (msgHeaderPtr->dstUserCount - 1);

    const void* const   msgBodyPtr  = (char*)msgHeaderPtr + msgHeaderSize;
    const unsigned long msgBodySize = packet->GetPayloadSize() - msgHeaderSize;
    const PRO_UINT32    charset     = pbsd_ntoh32(msgHeaderPtr->charset);

    RTP_MSG_USER srcUser = msgHeaderPtr->srcUser;
    srcUser.instId       = pbsd_ntoh16(msgHeaderPtr->srcUser.instId);

    assert(msgBodySize > 0);
    assert(srcUser.classId > 0);
    assert(srcUser.UserId() > 0);
    if (msgBodySize == 0 || srcUser.classId == 0 || srcUser.UserId() == 0)
    {
        return;
    }

    IRtpMsgServerObserver* observer = NULL;

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return;
        }

        CProStlMap<IRtpSession*, RTP_MSG_LINK_CTX*>::const_iterator const itr =
            m_session2Ctx.find(session);
        if (itr == m_session2Ctx.end())
        {
            return;
        }

        RTP_MSG_LINK_CTX* const srcCtx = itr->second;
        if (srcUser != srcCtx->baseUser &&
            srcCtx->subUsers.find(srcUser) == srcCtx->subUsers.end())
        {
            return;
        }

        bool                                                   toRoot       = false;
        bool                                                   toC2sPort    = false;
        unsigned char                                          sessionCount = 0;
        IRtpSession*                                           sessions[255];
        CProStlMap<IRtpSession*, CProStlVector<RTP_MSG_USER> > session2Users;

        for (int i = 0; i < (int)msgHeaderPtr->dstUserCount; ++i)
        {
            RTP_MSG_USER dstUser = msgHeaderPtr->dstUsers[i];
            dstUser.instId       = pbsd_ntoh16(msgHeaderPtr->dstUsers[i].instId);

            if (dstUser.classId == 0 || dstUser.UserId() == 0)
            {
                continue;
            }

            if (dstUser.IsRoot())
            {
                if (dstUser.instId == ROOT_ID_C2S.instId)
                {
                    toC2sPort = true;
                }
                else
                {
                    toRoot    = true;
                }
                continue;
            }

            CProStlMap<RTP_MSG_USER, RTP_MSG_LINK_CTX*>::const_iterator const itr2 =
                m_user2Ctx.find(dstUser);
            if (itr2 == m_user2Ctx.end())
            {
                continue;
            }

            RTP_MSG_LINK_CTX* const dstCtx = itr2->second;

            if (dstUser == dstCtx->baseUser)
            {
                sessions[sessionCount] = dstCtx->session;
                ++sessionCount;
            }
            else
            {
                session2Users[dstCtx->session].push_back(dstUser);
            }
        }

        /*
         * to baseUsers
         */
        if (sessionCount > 0)
        {
            SendMsgToDownlink(sessions, sessionCount, msgBodyPtr, (PRO_UINT16)msgBodySize,
                charset, &srcUser, NULL, 0, NULL);
        }

        /*
         * to subUsers
         */
        {
            CProStlMap<IRtpSession*, CProStlVector<RTP_MSG_USER> >::const_iterator       itr2 = session2Users.begin();
            CProStlMap<IRtpSession*, CProStlVector<RTP_MSG_USER> >::const_iterator const end2 = session2Users.end();

            for (; itr2 != end2; ++itr2)
            {
                IRtpSession*                       session = itr2->first;
                const CProStlVector<RTP_MSG_USER>& users   = itr2->second;

                SendMsgToDownlink(&session, 1, msgBodyPtr, (PRO_UINT16)msgBodySize,
                    charset, &srcUser, &users[0], (unsigned char)users.size(), NULL);
            }
        }

        /*
         * from c2s
         */
        do
        {
            if (!toC2sPort || !srcCtx->isC2s ||
                srcUser.classId != SERVER_CID || srcUser != srcCtx->baseUser)
            {
                break;
            }

            const CProStlString            theString((char*)msgBodyPtr, msgBodySize);
            CProStlVector<PRO_CONFIG_ITEM> theConfigs;

            if (!CProConfigStream::StringToConfigs(theString, theConfigs))
            {
                break;
            }

            RTP_MSG_AsyncOnRecvSession* const arg = new RTP_MSG_AsyncOnRecvSession;
            arg->session = session;
            arg->c2sUser = srcUser;
            arg->msgStream.Add(theConfigs);

            CProStlString msgName = "";
            arg->msgStream.Get(TAG_msg_name, msgName);

            if (stricmp(msgName.c_str(), MSG_client_login) == 0)
            {
                if (m_task->GetSize() >= MAX_PENDING_COUNT)
                {
                    delete arg;
                    break;
                }
            }
            else if (stricmp(msgName.c_str(), MSG_client_logout) == 0)
            {
            }
            else
            {
                delete arg;
                break;
            }

            arg->session->AddRef();
            IProFunctorCommand* const command =
                CProFunctorCommand_cpp<CRtpMsgServer, ACTION>::CreateInstance(
                *this,
                &CRtpMsgServer::AsyncOnRecvSession,
                (PRO_INT64)arg
                );
            m_task->Put(command);
        }
        while (0);

        /*
         * from other
         */
        if (toRoot)
        {
            m_observer->AddRef();
            observer = m_observer;
        }
    }

    if (observer != NULL)
    {
        observer->OnRecvMsg(this, msgBodyPtr, (PRO_UINT16)msgBodySize, charset, &srcUser);
        observer->Release();
    }
}

void
CRtpMsgServer::AsyncOnRecvSession(PRO_INT64* args)
{
    RTP_MSG_AsyncOnRecvSession* const arg = (RTP_MSG_AsyncOnRecvSession*)args[0];

    CProStlString msgName = "";
    arg->msgStream.Get(TAG_msg_name, msgName);

    if (stricmp(msgName.c_str(), MSG_client_login) == 0)
    {
        ProcessMsg_client_login(arg->session, arg->msgStream, arg->c2sUser);
    }
    else if (stricmp(msgName.c_str(), MSG_client_logout) == 0)
    {
        ProcessMsg_client_logout(arg->session, arg->msgStream, arg->c2sUser);
    }
    else
    {
    }

    arg->session->Release();
    delete arg;
}

void
CRtpMsgServer::ProcessMsg_client_login(IRtpSession*            session,
                                       const CProConfigStream& msgStream,
                                       const RTP_MSG_USER&     c2sUser)
{
    assert(session != NULL);
    assert(c2sUser.classId == SERVER_CID);
    assert(c2sUser.UserId() > 0);
    if (session == NULL || c2sUser.classId != SERVER_CID || c2sUser.UserId() == 0)
    {
        return;
    }

    unsigned int  client_index       = 0;
    CProStlString client_id          = "";
    CProStlString client_public_ip   = "";
    CProStlString client_hash_string = "";
    PRO_UINT64    client_nonce       = 0;

    msgStream.GetUint  (TAG_client_index      , client_index);
    msgStream.Get      (TAG_client_id         , client_id);
    msgStream.Get      (TAG_client_public_ip  , client_public_ip);
    msgStream.Get      (TAG_client_hash_string, client_hash_string);
    msgStream.GetUint64(TAG_client_nonce      , client_nonce);

    RTP_MSG_USER subUser;
    RtpMsgString2User(client_id.c_str(), &subUser);

    assert(subUser.classId > 0);
    assert(
        subUser.UserId() == 0 ||
        subUser.UserId() >= NODE_UID_MIN && subUser.UserId() <= NODE_UID_MAX
        );
    assert(!subUser.IsRoot());
    assert(subUser != c2sUser);
    if (subUser.classId == 0
        ||
        subUser.UserId() != 0 &&
        (subUser.UserId() < NODE_UID_MIN || subUser.UserId() > NODE_UID_MAX)
        ||
        subUser.IsRoot() || subUser == c2sUser)
    {
        return;
    }

    assert(client_hash_string.length() == 64);
    if (client_hash_string.length() != 64)
    {
        return;
    }

    char hash[32];

    {
        const char* const p = client_hash_string.c_str();

        for (int i = 0; i < 32; ++i)
        {
            char tmpString[3] = { '\0', '\0', '\0' };
            tmpString[0] = p[i * 2];
            tmpString[1] = p[i * 2 + 1];

            unsigned int tmpInt = 0;
            sscanf(tmpString, "%02x", &tmpInt); /* unsigned */

            hash[i] = (char)tmpInt;
        }
    }

    bool                   ret      = false;
    IRtpMsgServerObserver* observer = NULL;
    PRO_UINT64             userId   = 0;
    PRO_UINT16             instId   = 0;
    PRO_INT64              appData  = 0;
    bool                   isC2s    = false;

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return;
        }

        m_observer->AddRef();
        observer = m_observer;
    }

    if (subUser.UserId() == 0)
    {
        if (subUser.classId == SERVER_CID)
        {
            subUser.UserId(MakeServerId_i());
        }
        else
        {
            subUser.UserId(MakeClientId_i());
        }
        subUser.instId = NODE_IID_MIN;
    }

    ret = observer->OnCheckUser(
        this,
        &subUser,
        client_public_ip.c_str(),
        &c2sUser,
        hash,
        client_nonce,
        &userId,
        &instId,
        &appData,
        &isC2s
        );
    observer->Release();

    if (ret)
    {
        subUser.UserId(userId);
        subUser.instId = instId;

        assert(userId >= NODE_UID_MIN);
        assert(userId <= NODE_UID_MAXX);
        assert(subUser != c2sUser);
        if (userId < NODE_UID_MIN || userId > NODE_UID_MAXX || subUser == c2sUser)
        {
            ret = false;
        }
    }

    if (ret)
    {
        char idString[64] = "";
        RtpMsgUser2String(&subUser, idString);

        CProConfigStream msgStream;
        msgStream.Add    (TAG_msg_name    , MSG_client_login_ok);
        msgStream.AddUint(TAG_client_index, client_index);
        msgStream.Add    (TAG_client_id   , idString);

        CProStlString theString = "";
        msgStream.ToString(theString);

        AddSubUser(c2sUser, subUser, client_public_ip, theString, appData);
    }
    else
    {
        CProConfigStream msgStream;
        msgStream.Add    (TAG_msg_name    , MSG_client_login_error);
        msgStream.AddUint(TAG_client_index, client_index);

        CProStlString theString = "";
        msgStream.ToString(theString);

        SendMsgToDownlink(&session, 1, theString.c_str(), (PRO_UINT16)theString.length(),
            0, &ROOT_ID_C2S, &c2sUser, 1, NULL);
    }
}

void
CRtpMsgServer::ProcessMsg_client_logout(IRtpSession*            session,
                                        const CProConfigStream& msgStream,
                                        const RTP_MSG_USER&     c2sUser)
{
    assert(session != NULL);
    assert(c2sUser.classId == SERVER_CID);
    assert(c2sUser.UserId() > 0);
    if (session == NULL || c2sUser.classId != SERVER_CID || c2sUser.UserId() == 0)
    {
        return;
    }

    CProStlString client_id = "";
    msgStream.Get(TAG_client_id, client_id);

    RTP_MSG_USER subUser;
    RtpMsgString2User(client_id.c_str(), &subUser);

    assert(subUser.classId > 0);
    assert(subUser.UserId() > 0);
    assert(subUser != c2sUser);
    if (subUser.classId == 0 || subUser.UserId() == 0 || subUser == c2sUser)
    {
        return;
    }

    IRtpMsgServerObserver* observer = NULL;

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return;
        }

        CProStlMap<RTP_MSG_USER, RTP_MSG_LINK_CTX*>::iterator const itr =
            m_user2Ctx.find(subUser);
        if (itr == m_user2Ctx.end())
        {
            return;
        }

        RTP_MSG_LINK_CTX* const ctx = itr->second;
        if (!ctx->isC2s || c2sUser != ctx->baseUser)
        {
            return;
        }

        ctx->subUsers.erase(subUser);
        m_user2Ctx.erase(itr);

        m_observer->AddRef();
        observer = m_observer;
    }

    observer->OnCloseUser(this, &subUser, -1, 0);
    observer->Release();
}

void
PRO_CALLTYPE
CRtpMsgServer::OnCloseSession(IRtpSession* session,
                              long         errorCode,
                              long         sslCode,
                              bool         tcpConnected)
{
    assert(session != NULL);
    if (session == NULL)
    {
        return;
    }

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return;
        }

        session->AddRef();
        IProFunctorCommand* const command =
            CProFunctorCommand_cpp<CRtpMsgServer, ACTION>::CreateInstance(
            *this,
            &CRtpMsgServer::AsyncOnCloseSession,
            (PRO_INT64)session,
            (PRO_INT64)errorCode,
            (PRO_INT64)sslCode
            );
        m_task->Put(command);
    }
}

void
CRtpMsgServer::AsyncOnCloseSession(PRO_INT64* args)
{
    IRtpSession* const session   = (IRtpSession*)args[0];
    const long         errorCode = (long)        args[1];
    const long         sslCode   = (long)        args[2];

    IRtpMsgServerObserver*   observer = NULL;
    CProStlSet<RTP_MSG_USER> oldUsers;

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            session->Release();

            return;
        }

        CProStlMap<IRtpSession*, RTP_MSG_LINK_CTX*>::iterator const itr =
            m_session2Ctx.find(session);
        if (itr == m_session2Ctx.end())
        {
            session->Release();

            return;
        }

        RTP_MSG_LINK_CTX* const ctx = itr->second;
        session->Release();

        oldUsers = ctx->subUsers;
        oldUsers.insert(ctx->baseUser);

        CProStlSet<RTP_MSG_USER>::const_iterator       itr2 = oldUsers.begin();
        CProStlSet<RTP_MSG_USER>::const_iterator const end2 = oldUsers.end();

        for (; itr2 != end2; ++itr2)
        {
            m_user2Ctx.erase(*itr2);
        }

        m_session2Ctx.erase(itr);
        delete ctx;

        m_observer->AddRef();
        observer = m_observer;
    }

    CProStlSet<RTP_MSG_USER>::const_iterator       itr = oldUsers.begin();
    CProStlSet<RTP_MSG_USER>::const_iterator const end = oldUsers.end();

    for (; itr != end; ++itr)
    {
        const RTP_MSG_USER user = *itr;
        observer->OnCloseUser(this, &user, errorCode, sslCode);
    }

    observer->Release();
    DeleteRtpSessionWrapper(session);
}

bool
CRtpMsgServer::AddBaseUser(RTP_SESSION_TYPE        sessionType,
                           const RTP_INIT_ARGS&    initArgs,
                           const RTP_SESSION_INFO& localInfo,
                           const RTP_MSG_USER&     baseUser,
                           const CProStlString&    publicIp,
                           PRO_INT64               appData,
                           bool                    isC2s)
{
    assert(sessionType == RTP_ST_TCPSERVER_EX || sessionType == RTP_ST_SSLSERVER_EX);
    assert(localInfo.mmType == m_mmType);

    assert(baseUser.classId > 0);
    assert(baseUser.UserId() > 0);
    if (baseUser.classId == 0 || baseUser.UserId() == 0)
    {
        return (false);
    }

    IRtpMsgServerObserver*   observer   = NULL;
    IRtpSession*             newSession = NULL;
    IRtpSession*             oldSession = NULL;
    CProStlSet<RTP_MSG_USER> oldUsers;

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return (false);
        }

        newSession = CreateRtpSessionWrapper(sessionType, &initArgs, &localInfo);
        if (newSession == NULL)
        {
            return (false);
        }

        /*
         * remove old
         */
        CProStlMap<RTP_MSG_USER, RTP_MSG_LINK_CTX*>::iterator const itr =
            m_user2Ctx.find(baseUser);
        if (itr != m_user2Ctx.end())
        {
            RTP_MSG_LINK_CTX* const ctx = itr->second;

            if (baseUser == ctx->baseUser)
            {
                oldSession = ctx->session;
                oldUsers   = ctx->subUsers;

                CProStlSet<RTP_MSG_USER>::const_iterator       itr2 = oldUsers.begin();
                CProStlSet<RTP_MSG_USER>::const_iterator const end2 = oldUsers.end();

                for (; itr2 != end2; ++itr2)
                {
                    m_user2Ctx.erase(*itr2);
                }

                m_user2Ctx.erase(itr);
                m_session2Ctx.erase(oldSession);
                delete ctx;

                oldUsers.insert(baseUser);
            }
            else
            {
                oldUsers.insert(baseUser);

                ctx->subUsers.erase(baseUser);
                m_user2Ctx.erase(itr);

                NotifyKickout(ctx->session, ctx->baseUser, baseUser);
            }
        }

        /*
         * add new
         */
        RTP_MSG_LINK_CTX* const ctx = new RTP_MSG_LINK_CTX;
        ctx->session  = newSession;
        ctx->baseUser = baseUser;
        ctx->isC2s    = isC2s;
        m_session2Ctx[newSession] = ctx;
        m_user2Ctx[baseUser]      = ctx;

        newSession->AddRef();
        m_observer->AddRef();
        observer = m_observer;
    }

    CProStlSet<RTP_MSG_USER>::const_iterator       itr = oldUsers.begin();
    CProStlSet<RTP_MSG_USER>::const_iterator const end = oldUsers.end();

    for (; itr != end; ++itr)
    {
        const RTP_MSG_USER user = *itr;
        observer->OnCloseUser(this, &user, -1, 0);
    }

    /*
     * 1. response
     */
    {
        newSession->SuspendRecv();

        const char buf[] = { 0 }; /* dummy data */
        SendMsgToDownlink(
            &newSession, 1, buf, sizeof(buf), 0, &baseUser, NULL, 0, publicIp.c_str());
    }

    /*
     * 2. make a callback
     */
    {
        observer->OnOkUser(this, &baseUser, publicIp.c_str(), NULL, appData);

        newSession->ResumeRecv();
        newSession->Release();
    }

    observer->Release();
    DeleteRtpSessionWrapper(oldSession);

    return (true);
}

void
CRtpMsgServer::AddSubUser(const RTP_MSG_USER&  c2sUser,
                          const RTP_MSG_USER&  subUser,
                          const CProStlString& publicIp,
                          const CProStlString& msgText,
                          PRO_INT64            appData)
{
    assert(c2sUser.classId == SERVER_CID);
    assert(c2sUser.UserId() > 0);
    assert(subUser.classId > 0);
    assert(subUser.UserId() > 0);
    assert(subUser != c2sUser);
    if (c2sUser.classId != SERVER_CID || c2sUser.UserId() == 0 ||
        subUser.classId == 0 || subUser.UserId() == 0 || subUser == c2sUser)
    {
        return;
    }

    IRtpMsgServerObserver*   observer   = NULL;
    IRtpSession*             newSession = NULL;
    IRtpSession*             oldSession = NULL;
    CProStlSet<RTP_MSG_USER> oldUsers;

    {
        CProThreadMutexGuard mon(m_lock);

        if (m_observer == NULL || m_reactor == NULL || m_service == NULL || m_task == NULL)
        {
            return;
        }

        CProStlMap<RTP_MSG_USER, RTP_MSG_LINK_CTX*>::iterator itr = m_user2Ctx.find(c2sUser);
        if (itr == m_user2Ctx.end())
        {
            return;
        }

        RTP_MSG_LINK_CTX* const newCtx = itr->second;
        if (!newCtx->isC2s || c2sUser != newCtx->baseUser)
        {
            return;
        }

        /*
         * remove old
         */
        itr = m_user2Ctx.find(subUser);
        if (itr != m_user2Ctx.end())
        {
            RTP_MSG_LINK_CTX* const oldCtx = itr->second;

            if (subUser == oldCtx->baseUser)
            {
                oldSession = oldCtx->session;
                oldUsers   = oldCtx->subUsers;

                CProStlSet<RTP_MSG_USER>::const_iterator       itr2 = oldUsers.begin();
                CProStlSet<RTP_MSG_USER>::const_iterator const end2 = oldUsers.end();

                for (; itr2 != end2; ++itr2)
                {
                    m_user2Ctx.erase(*itr2);
                }

                m_user2Ctx.erase(itr);
                m_session2Ctx.erase(oldSession);
                delete oldCtx;

                oldUsers.insert(subUser);
            }
            else
            {
                oldUsers.insert(subUser);

                oldCtx->subUsers.erase(subUser);
                m_user2Ctx.erase(itr);

                if (c2sUser != oldCtx->baseUser)
                {
                    NotifyKickout(oldCtx->session, oldCtx->baseUser, subUser);
                }
            }
        }

        newSession = newCtx->session;

        /*
         * add new
         */
        newCtx->subUsers.insert(subUser);
        m_user2Ctx[subUser] = newCtx;

        if (newCtx->subUsers.size() == 1)
        {
            newSession->SetOutputRedline(m_redlineBytes, 0);
        }

        newSession->AddRef();
        m_observer->AddRef();
        observer = m_observer;
    }

    CProStlSet<RTP_MSG_USER>::const_iterator       itr = oldUsers.begin();
    CProStlSet<RTP_MSG_USER>::const_iterator const end = oldUsers.end();

    for (; itr != end; ++itr)
    {
        const RTP_MSG_USER user = *itr;
        observer->OnCloseUser(this, &user, -1, 0);
    }

    /*
     * 1. response
     */
    {
        newSession->SuspendRecv();

        SendMsgToDownlink(&newSession, 1, msgText.c_str(), (PRO_UINT16)msgText.length(),
            0, &ROOT_ID_C2S, &c2sUser, 1, NULL);
    }

    /*
     * 2. make a callback
     */
    {
        observer->OnOkUser(this, &subUser, publicIp.c_str(), &c2sUser, appData);

        newSession->ResumeRecv();
        newSession->Release();
    }

    observer->Release();
    DeleteRtpSessionWrapper(oldSession);
}

bool
CRtpMsgServer::SendMsgToDownlink(IRtpSession**       sessions,
                                 unsigned char       sessionCount,
                                 const void*         buf,
                                 PRO_UINT16          size,
                                 PRO_UINT32          charset,
                                 const RTP_MSG_USER* srcUser,
                                 const RTP_MSG_USER* dstUsers,     /* = NULL */
                                 unsigned char       dstUserCount, /* = 0 */
                                 const char*         publicIp)     /* = NULL */
{
    assert(sessions != NULL);
    assert(sessionCount > 0);
    assert(buf != NULL);
    assert(size > 0);
    assert(srcUser != NULL);
    assert(srcUser->classId > 0);
    assert(srcUser->UserId() > 0);
    if (sessions == NULL || sessionCount == 0 || buf == NULL || size == 0 ||
        srcUser == NULL || srcUser->classId == 0 || srcUser->UserId() == 0)
    {
        return (false);
    }

    if (dstUsers == NULL || dstUserCount == 0)
    {
        dstUsers     = NULL;
        dstUserCount = 1;
    }

    const unsigned long msgHeaderSize =
        sizeof(RTP_MSG_HEADER) + sizeof(RTP_MSG_USER) * (dstUserCount - 1);

    IRtpPacket* const packet = CreateRtpPacketSpace(msgHeaderSize + size);
    if (packet == NULL)
    {
        return (false);
    }

    RTP_MSG_HEADER* const msgHeaderPtr = (RTP_MSG_HEADER*)packet->GetPayloadBuffer();
    memset(msgHeaderPtr, 0, msgHeaderSize);

    msgHeaderPtr->charset        = pbsd_hton32(charset);
    if (publicIp != NULL)
    {
        msgHeaderPtr->publicIp   = pbsd_inet_aton(publicIp);
    }
    msgHeaderPtr->srcUser        = *srcUser;
    msgHeaderPtr->srcUser.instId = pbsd_hton16(srcUser->instId);

    if (dstUsers != NULL && dstUserCount > 0)
    {
        msgHeaderPtr->dstUserCount = dstUserCount;

        for (int i = 0; i < (int)dstUserCount; ++i)
        {
            msgHeaderPtr->dstUsers[i]        = dstUsers[i];
            msgHeaderPtr->dstUsers[i].instId = pbsd_hton16(dstUsers[i].instId);
        }
    }

    memcpy((char*)msgHeaderPtr + msgHeaderSize, buf, size);

    packet->SetMmType(m_mmType);

    bool ret = true;

    for (int i = 0; i < (int)sessionCount; ++i)
    {
        assert(sessions[i] != NULL);
        if (!sessions[i]->SendPacket(packet))
        {
            ret = false;
        }
    }

    packet->Release();

    return (ret);
}

void
CRtpMsgServer::NotifyKickout(IRtpSession*        session,
                             const RTP_MSG_USER& c2sUser,
                             const RTP_MSG_USER& subUser)
{
    assert(session != NULL);
    assert(c2sUser.classId == SERVER_CID);
    assert(c2sUser.UserId() > 0);
    assert(subUser.classId > 0);
    assert(subUser.UserId() > 0);
    assert(subUser != c2sUser);
    if (session == NULL || c2sUser.classId != SERVER_CID || c2sUser.UserId() == 0 ||
        subUser.classId == 0 || subUser.UserId() == 0 || subUser == c2sUser)
    {
        return;
    }

    char idString[64] = "";
    RtpMsgUser2String(&subUser, idString);

    CProConfigStream msgStream;
    msgStream.Add(TAG_msg_name , MSG_client_kickout);
    msgStream.Add(TAG_client_id, idString);

    CProStlString theString = "";
    msgStream.ToString(theString);

    SendMsgToDownlink(&session, 1, theString.c_str(), (PRO_UINT16)theString.length(),
        0, &ROOT_ID_C2S, &c2sUser, 1, NULL);
}
