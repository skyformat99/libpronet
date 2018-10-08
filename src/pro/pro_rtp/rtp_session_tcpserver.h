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
 * RFC-1889/1890, RFC-3550/3551, RFC-4571
 */

#if !defined(RTP_SESSION_TCPSERVER_H)
#define RTP_SESSION_TCPSERVER_H

#include "rtp_session_base.h"

/////////////////////////////////////////////////////////////////////////////
////

class CRtpSessionTcpserver : public IProAcceptorObserver, public CRtpSessionBase
{
public:

    static CRtpSessionTcpserver* CreateInstance(const RTP_SESSION_INFO* localInfo);

    bool Init(
        IRtpSessionObserver* observer,
        IProReactor*         reactor,
        const char*          localIp,         /* = NULL */
        unsigned short       localPort,       /* = 0 */
        unsigned long        timeoutInSeconds /* = 0 */
        );

    virtual void Fini();

    virtual unsigned long PRO_CALLTYPE AddRef();

    virtual unsigned long PRO_CALLTYPE Release();

private:

    CRtpSessionTcpserver(const RTP_SESSION_INFO& localInfo);

    virtual ~CRtpSessionTcpserver();

    virtual void PRO_CALLTYPE OnAccept(
        IProAcceptor*  acceptor,
        PRO_INT64      sockId,
        bool           unixSocket,
        const char*    remoteIp,
        unsigned short remotePort,
        unsigned char  serviceId,
        unsigned char  serviceOpt,
        PRO_UINT64     nonce
        );

    virtual void PRO_CALLTYPE OnRecv(
        IProTransport*          trans,
        const pbsd_sockaddr_in* remoteAddr
        );

private:

    IProAcceptor* m_acceptor;
};

/////////////////////////////////////////////////////////////////////////////
////

#endif /* RTP_SESSION_TCPSERVER_H */