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

#if !defined(TEST_H)
#define TEST_H

#include "../pro_net/pro_net.h"
#include "../pro_util/pro_config_file.h"
#include "../pro_util/pro_config_stream.h"
#include "../pro_util/pro_memory_pool.h"
#include "../pro_util/pro_ref_count.h"
#include "../pro_util/pro_stl.h"
#include "../pro_util/pro_thread_mutex.h"

/////////////////////////////////////////////////////////////////////////////
////

struct TCP_SERVER_CONFIG_INFO
{
    TCP_SERVER_CONFIG_INFO()
    {
        tcps_thread_count        = 40;
        tcps_using_hub           = false;
        tcps_port                = 3000;
        tcps_handshake_timeout   = 20;
        tcps_heartbeat_interval  = 200;
        tcps_heartbeat_bytes     = 0;
        tcps_sockbuf_size_recv   = 2048;
        tcps_sockbuf_size_send   = 2048;
        tcps_recvpool_size       = 2048;

        tcps_enable_ssl          = true;
        tcps_ssl_enable_sha1cert = true;
        tcps_ssl_keyfile         = "./server.key";

        tcps_ssl_cafile.push_back("./ca.crt");
        tcps_ssl_cafile.push_back("");
        tcps_ssl_crlfile.push_back("");
        tcps_ssl_crlfile.push_back("");
        tcps_ssl_certfile.push_back("./server.crt");
        tcps_ssl_certfile.push_back("");
    }

    void ToConfigs(CProStlVector<PRO_CONFIG_ITEM>& configs) const
    {
        CProConfigStream configStream;

        configStream.AddUint("tcps_thread_count"       , tcps_thread_count);
        configStream.AddInt ("tcps_using_hub"          , tcps_using_hub);
        configStream.AddUint("tcps_port"               , tcps_port);
        configStream.AddUint("tcps_handshake_timeout"  , tcps_handshake_timeout);
        configStream.AddUint("tcps_heartbeat_interval" , tcps_heartbeat_interval);
        configStream.AddUint("tcps_heartbeat_bytes"    , tcps_heartbeat_bytes);
        configStream.AddUint("tcps_sockbuf_size_recv"  , tcps_sockbuf_size_recv);
        configStream.AddUint("tcps_sockbuf_size_send"  , tcps_sockbuf_size_send);
        configStream.AddUint("tcps_recvpool_size"      , tcps_recvpool_size);

        configStream.AddInt ("tcps_enable_ssl"         , tcps_enable_ssl);
        configStream.AddInt ("tcps_ssl_enable_sha1cert", tcps_ssl_enable_sha1cert);
        configStream.Add    ("tcps_ssl_cafile"         , tcps_ssl_cafile);
        configStream.Add    ("tcps_ssl_crlfile"        , tcps_ssl_crlfile);
        configStream.Add    ("tcps_ssl_certfile"       , tcps_ssl_certfile);
        configStream.Add    ("tcps_ssl_keyfile"        , tcps_ssl_keyfile);

        configStream.Get(configs);
    }

    unsigned int                 tcps_thread_count;      /* 1 ~ 100 */
    bool                         tcps_using_hub;
    unsigned short               tcps_port;
    unsigned int                 tcps_handshake_timeout;
    unsigned int                 tcps_heartbeat_interval;
    unsigned int                 tcps_heartbeat_bytes;   /* 0 ~ 1024 */
    unsigned int                 tcps_sockbuf_size_recv; /* >= 1024 */
    unsigned int                 tcps_sockbuf_size_send; /* >= 1024 */
    unsigned int                 tcps_recvpool_size;     /* >= 1024 */

    bool                         tcps_enable_ssl;
    bool                         tcps_ssl_enable_sha1cert;
    CProStlVector<CProStlString> tcps_ssl_cafile;
    CProStlVector<CProStlString> tcps_ssl_crlfile;
    CProStlVector<CProStlString> tcps_ssl_certfile;
    CProStlString                tcps_ssl_keyfile;

    DECLARE_SGI_POOL(0);
};

/////////////////////////////////////////////////////////////////////////////
////

class CTest
:
public IProAcceptorObserver,
public IProServiceHostObserver,
public IProTcpHandshakerObserver,
public IProSslHandshakerObserver,
public IProTransportObserver,
public CProRefCount
{
public:

    static CTest* CreateInstance();

    bool Init(
        IProReactor*                  reactor,
        const TCP_SERVER_CONFIG_INFO& configInfo
        );

    void Fini();

    virtual unsigned long PRO_CALLTYPE AddRef();

    virtual unsigned long PRO_CALLTYPE Release();

    void SetHeartbeatDataSize(unsigned long size); /* 0 ~ 1024 */

    unsigned long GetHeartbeatDataSize() const;

    unsigned long GetTransportCount() const;

private:

    CTest();

    virtual ~CTest();

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

    virtual void PRO_CALLTYPE OnServiceAccept(
        IProServiceHost* serviceHost,
        PRO_INT64        sockId,
        bool             unixSocket,
        const char*      remoteIp,
        unsigned short   remotePort,
        unsigned char    serviceId,
        unsigned char    serviceOpt,
        PRO_UINT64       nonce
        );

    virtual void PRO_CALLTYPE OnHandshakeOk(
        IProTcpHandshaker* handshaker,
        PRO_INT64          sockId,
        bool               unixSocket,
        const void*        buf,
        unsigned long      size
        );

    virtual void PRO_CALLTYPE OnHandshakeError(
        IProTcpHandshaker* handshaker,
        long               errorCode
        );

    virtual void PRO_CALLTYPE OnHandshakeOk(
        IProSslHandshaker* handshaker,
        PRO_SSL_CTX*       ctx,
        PRO_INT64          sockId,
        bool               unixSocket,
        const void*        buf,
        unsigned long      size
        );

    virtual void PRO_CALLTYPE OnHandshakeError(
        IProSslHandshaker* handshaker,
        long               errorCode,
        long               sslCode
        );

    virtual void PRO_CALLTYPE OnRecv(
        IProTransport*          trans,
        const pbsd_sockaddr_in* remoteAddr
        );

    virtual void PRO_CALLTYPE OnSend(
        IProTransport* trans,
        PRO_UINT64     actionId
        )
    {
    }

    virtual void PRO_CALLTYPE OnClose(
        IProTransport* trans,
        long           errorCode,
        long           sslCode
        );

    virtual void PRO_CALLTYPE OnHeartbeat(IProTransport* trans);

private:

    IProReactor*                               m_reactor;
    TCP_SERVER_CONFIG_INFO                     m_configInfo;
    PRO_SSL_SERVER_CONFIG*                     m_sslConfig;
    IProAcceptor*                              m_acceptor;
    IProServiceHost*                           m_service;

    CProStlMap<IProTcpHandshaker*, PRO_UINT64> m_tcpHandshaker2Nonce;
    CProStlMap<IProSslHandshaker*, PRO_UINT64> m_sslHandshaker2Nonce;
    CProStlSet<IProTransport*>                 m_transports;
    PRO_UINT16                                 m_heartbeatData[512];
    unsigned long                              m_heartbeatSize; /* 0 ~ 1024 */

    mutable CProThreadMutex                    m_lock;

    DECLARE_SGI_POOL(0);
};

/////////////////////////////////////////////////////////////////////////////
////

#endif /* TEST_H */
