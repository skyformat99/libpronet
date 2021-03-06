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

#if !defined(MSG_SERVER_H)
#define MSG_SERVER_H

#include "../pro_rtp/rtp_foundation.h"
#include "../pro_rtp/rtp_framework.h"
#include "../pro_util/pro_config_file.h"
#include "../pro_util/pro_config_stream.h"
#include "../pro_util/pro_memory_pool.h"
#include "../pro_util/pro_ref_count.h"
#include "../pro_util/pro_stl.h"
#include "../pro_util/pro_thread_mutex.h"

/////////////////////////////////////////////////////////////////////////////
////

class CDbConnection;
class CProLogFile;

struct MSG_SERVER_CONFIG_INFO
{
    MSG_SERVER_CONFIG_INFO()
    {
        msgs_thread_count        = 40;
        msgs_hub_port            = 3000;
        msgs_handshake_timeout   = 20;

        msgs_enable_ssl          = true;
        msgs_ssl_forced          = false;
        msgs_ssl_enable_sha1cert = true;
        msgs_ssl_keyfile         = "./server.key";

        msgs_log_loop_bytes      = 10 * 1000 * 1000;
        msgs_log_level_green     = 0;
        msgs_log_level_userchk   = -1;
        msgs_log_level_userin    = 0;
        msgs_log_level_userout   = 0;

        msgs_ssl_cafile.push_back("./ca.crt");
        msgs_ssl_cafile.push_back("");
        msgs_ssl_crlfile.push_back("");
        msgs_ssl_crlfile.push_back("");
        msgs_ssl_certfile.push_back("./server.crt");
        msgs_ssl_certfile.push_back("");
    }

    void ToConfigs(CProStlVector<PRO_CONFIG_ITEM>& configs) const
    {
        CProConfigStream configStream;

        configStream.AddUint("msgs_thread_count"       , msgs_thread_count);
        configStream.AddUint("msgs_hub_port"           , msgs_hub_port);
        configStream.AddUint("msgs_handshake_timeout"  , msgs_handshake_timeout);

        configStream.AddInt ("msgs_enable_ssl"         , msgs_enable_ssl);
        configStream.AddInt ("msgs_ssl_forced"         , msgs_ssl_forced);
        configStream.AddInt ("msgs_ssl_enable_sha1cert", msgs_ssl_enable_sha1cert);
        configStream.Add    ("msgs_ssl_cafile"         , msgs_ssl_cafile);
        configStream.Add    ("msgs_ssl_crlfile"        , msgs_ssl_crlfile);
        configStream.Add    ("msgs_ssl_certfile"       , msgs_ssl_certfile);
        configStream.Add    ("msgs_ssl_keyfile"        , msgs_ssl_keyfile);

        configStream.AddUint("msgs_log_loop_bytes"     , msgs_log_loop_bytes);
        configStream.AddInt ("msgs_log_level_green"    , msgs_log_level_green);
        configStream.AddInt ("msgs_log_level_userchk"  , msgs_log_level_userchk);
        configStream.AddInt ("msgs_log_level_userin"   , msgs_log_level_userin);
        configStream.AddInt ("msgs_log_level_userout"  , msgs_log_level_userout);

        configStream.Get(configs);
    }

    unsigned int                 msgs_thread_count; /* 1 ~ 100 */
    unsigned short               msgs_hub_port;
    unsigned int                 msgs_handshake_timeout;

    bool                         msgs_enable_ssl;
    bool                         msgs_ssl_forced;
    bool                         msgs_ssl_enable_sha1cert;
    CProStlVector<CProStlString> msgs_ssl_cafile;
    CProStlVector<CProStlString> msgs_ssl_crlfile;
    CProStlVector<CProStlString> msgs_ssl_certfile;
    CProStlString                msgs_ssl_keyfile;

    unsigned int                 msgs_log_loop_bytes;
    int                          msgs_log_level_green;
    int                          msgs_log_level_userchk;
    int                          msgs_log_level_userin;
    int                          msgs_log_level_userout;

    DECLARE_SGI_POOL(0);
};

struct MSG_USER_CTX
{
    CProStlSet<PRO_UINT16> iids;

    DECLARE_SGI_POOL(0);
};

/////////////////////////////////////////////////////////////////////////////
////

class CMsgServer : public IRtpMsgServerObserver, public CProRefCount
{
public:

    static CMsgServer* CreateInstance(
        CProLogFile&   logFile,
        CDbConnection& db
        );

    bool Init(
        IProReactor*                  reactor,
        const MSG_SERVER_CONFIG_INFO& configInfo
        );

    void Fini();

    virtual unsigned long PRO_CALLTYPE AddRef();

    virtual unsigned long PRO_CALLTYPE Release();

    void KickoutUsers(const CProStlSet<RTP_MSG_USER>& users);

    void Reconfig(const MSG_SERVER_CONFIG_INFO& configInfo);

private:

    CMsgServer(
        CProLogFile&   logFile,
        CDbConnection& db
        );

    virtual ~CMsgServer();

    virtual bool PRO_CALLTYPE OnCheckUser(
        IRtpMsgServer*      msgServer,
        const RTP_MSG_USER* user,
        const char*         userPublicIp,
        const RTP_MSG_USER* c2sUser, /* = NULL */
        const char          hash[32],
        PRO_UINT64          nonce,
        PRO_UINT64*         userId,
        PRO_UINT16*         instId,
        PRO_INT64*          appData,
        bool*               isC2s
        );

    virtual void PRO_CALLTYPE OnOkUser(
        IRtpMsgServer*      msgServer,
        const RTP_MSG_USER* user,
        const char*         userPublicIp,
        const RTP_MSG_USER* c2sUser, /* = NULL */
        PRO_INT64           appData
        );

    virtual void PRO_CALLTYPE OnCloseUser(
        IRtpMsgServer*      msgServer,
        const RTP_MSG_USER* user,
        long                errorCode,
        long                sslCode
        );

    virtual void PRO_CALLTYPE OnRecvMsg(
        IRtpMsgServer*      msgServer,
        const void*         buf,
        PRO_UINT16          size,
        PRO_UINT32          charset,
        const RTP_MSG_USER* srcUser
        )
    {
    }

private:

    CProLogFile&                         m_logFile;
    CDbConnection&                       m_db;
    IProReactor*                         m_reactor;
    MSG_SERVER_CONFIG_INFO               m_configInfo;
    PRO_SSL_SERVER_CONFIG*               m_sslConfig;
    IRtpMsgServer*                       m_msgServer;

    CProStlMap<PRO_UINT64, MSG_USER_CTX> m_uid2Ctx[256]; /* cid[0]<> ~ cid[255]<> */

    CProThreadMutex                      m_lock;

    DECLARE_SGI_POOL(0);
};

/////////////////////////////////////////////////////////////////////////////
////

#endif /* MSG_SERVER_H */
