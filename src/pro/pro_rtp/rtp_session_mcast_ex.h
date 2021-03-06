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

#if !defined(RTP_SESSION_MCAST_EX_H)
#define RTP_SESSION_MCAST_EX_H

#include "rtp_session_base.h"

/////////////////////////////////////////////////////////////////////////////
////

class CRtpSessionMcastEx : public CRtpSessionBase
{
public:

    static CRtpSessionMcastEx* CreateInstance(const RTP_SESSION_INFO* localInfo);

    bool Init(
        IRtpSessionObserver* observer,
        IProReactor*         reactor,
        const char*          mcastIp,
        unsigned short       mcastPort, /* = 0 */
        const char*          localIp    /* = NULL */
        );

    virtual void Fini();

private:

    CRtpSessionMcastEx(const RTP_SESSION_INFO& localInfo);

    virtual ~CRtpSessionMcastEx();

    virtual bool PRO_CALLTYPE AddMcastReceiver(const char* mcastIp);

    virtual void PRO_CALLTYPE RemoveMcastReceiver(const char* mcastIp);

private:

    virtual void PRO_CALLTYPE OnRecv(
        IProTransport*          trans,
        const pbsd_sockaddr_in* remoteAddr
        );
};

/////////////////////////////////////////////////////////////////////////////
////

#endif /* RTP_SESSION_MCAST_EX_H */
