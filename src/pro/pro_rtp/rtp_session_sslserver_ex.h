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
 * 1) client ----->                connect()                -----> server
 * 2) client <-----                 accept()                <----- server
 * 3) client <-----                  nonce                  <----- server
 * 4) client ----->  serviceId + serviceOpt + (r) + (r+1)   -----> server
 * 5) client <-----              ssl handshake              -----> server
 * 6) client::[password hash]
 * 7) client ----->          rtp(RTP_SESSION_INFO)          -----> server
 * 8)                                             [password hash]::server
 * 9) client <-----               rtp(version)              <----- server
 *                   SSL_EX handshake protocol flow chart
 */

#if !defined(RTP_SESSION_SSLSERVER_EX_H)
#define RTP_SESSION_SSLSERVER_EX_H

#include "rtp_session_tcpserver_ex.h"

/////////////////////////////////////////////////////////////////////////////
////

class CRtpSessionSslserverEx : public CRtpSessionTcpserverEx
{
public:

    static CRtpSessionSslserverEx* CreateInstance(
        const RTP_SESSION_INFO* localInfo,
        PRO_SSL_CTX*            sslCtx
        );

private:

    CRtpSessionSslserverEx(
        const RTP_SESSION_INFO& localInfo,
        PRO_SSL_CTX*            sslCtx
        );

    virtual ~CRtpSessionSslserverEx()
    {
    }
};

/////////////////////////////////////////////////////////////////////////////
////

#endif /* RTP_SESSION_SSLSERVER_EX_H */
