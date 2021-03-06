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

#if !defined(PRO_SSL_TRANSPORT_H)
#define PRO_SSL_TRANSPORT_H

#include "pro_tcp_transport.h"

/////////////////////////////////////////////////////////////////////////////
////

struct PRO_SSL_CTX;

/////////////////////////////////////////////////////////////////////////////
////

class CProSslTransport : public CProTcpTransport
{
public:

    static CProSslTransport* CreateInstance(size_t recvPoolSize); /* = 0 */

    bool Init(
        IProTransportObserver* observer,
        CProTpReactorTask*     reactorTask,
        PRO_SSL_CTX*           ctx,
        PRO_INT64              sockId,
        bool                   unixSocket,
        size_t                 sockBufSizeRecv, /* = 0 */
        size_t                 sockBufSizeSend  /* = 0 */
        );

    void Fini();

    virtual PRO_TRANS_TYPE PRO_CALLTYPE GetType() const;

private:

    CProSslTransport(size_t recvPoolSize); /* = 0 */

    virtual ~CProSslTransport();

    virtual void PRO_CALLTYPE OnInput(PRO_INT64 sockId);

    virtual void PRO_CALLTYPE OnOutput(PRO_INT64 sockId);

    void DoRecv(PRO_INT64 sockId);

    void DoSend(PRO_INT64 sockId);

private:

    PRO_SSL_CTX* m_ctx;
};

/////////////////////////////////////////////////////////////////////////////
////

#endif /* PRO_SSL_TRANSPORT_H */
