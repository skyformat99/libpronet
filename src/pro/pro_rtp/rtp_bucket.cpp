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

#include "rtp_bucket.h"
#include "rtp_flow_stat.h"
#include "rtp_foundation.h"
#include "rtp_framework.h"
#include "../pro_util/pro_memory_pool.h"
#include "../pro_util/pro_stl.h"
#include "../pro_util/pro_z.h"
#include <cassert>

/////////////////////////////////////////////////////////////////////////////
////

#define BASE_REDLINE_BYTES   (1024 * 1024)
#define AUDIO_REDLINE_BYTES  (1024 * 8)
#define VIDEO_REDLINE_BYTES  (1024 * 1024)
#define MAX_FRAME_BYTES      (1920 * 1080 * 3 / 2)
#define VIDEO_REDLINE_FRAMES 10

struct RTP_VIDEO_FRAME
{
    RTP_VIDEO_FRAME()
    {
        keyFrame = false;
    }

    bool       keyFrame;
    CRtpBucket bucket;

    DECLARE_SGI_POOL(0);
};

/////////////////////////////////////////////////////////////////////////////
////

CRtpBucket::CRtpBucket()
{
    m_redlineBytes = BASE_REDLINE_BYTES;
    m_totalBytes   = 0;

    m_flowStat.SetTimeSpan(GetRtpFlowctrlTimeSpan());
}

CRtpBucket::~CRtpBucket()
{
    Reset();
}

void
PRO_CALLTYPE
CRtpBucket::Destroy()
{
    delete this;
}

unsigned long
PRO_CALLTYPE
CRtpBucket::GetTotalBytes() const
{
    return (m_totalBytes);
}

IRtpPacket*
PRO_CALLTYPE
CRtpBucket::GetFront()
{
    if (m_totalBytes == 0)
    {
        return (NULL);
    }

    IRtpPacket* const packet = m_packets.front();

    return (packet);
}

bool
PRO_CALLTYPE
CRtpBucket::PushBackAddRef(IRtpPacket* packet)
{
    assert(packet != NULL);
    if (packet == NULL)
    {
        return (false);
    }

    m_flowStat.PushData(1, packet->GetPayloadSize());

    if (m_totalBytes >= m_redlineBytes)
    {
        return (false);
    }

    packet->AddRef();
    m_packets.push_back(packet);
    m_totalBytes += packet->GetPayloadSize();

    return (true);
}

void
PRO_CALLTYPE
CRtpBucket::PopFrontRelease(IRtpPacket* packet)
{
    if (packet == NULL || m_totalBytes == 0 || packet != m_packets.front())
    {
        return;
    }

    m_packets.pop_front();

    m_flowStat.PopData(1, packet->GetPayloadSize());

    m_totalBytes -= packet->GetPayloadSize();
    packet->Release();
}

void
PRO_CALLTYPE
CRtpBucket::Reset()
{
    int       i = 0;
    const int c = (int)m_packets.size();

    for (; i < c; ++i)
    {
        m_packets[i]->Release();
    }

    m_totalBytes = 0;
    m_packets.clear();

    m_flowStat.Reset();
}

void
PRO_CALLTYPE
CRtpBucket::SetRedline(unsigned long redlineBytes,  /* = 0 */
                       unsigned long redlineFrames) /* = 0 */
{
    if (redlineBytes > 0)
    {
        m_redlineBytes = redlineBytes;
    }
    if (redlineFrames > 0)
    {
    }
}

void
PRO_CALLTYPE
CRtpBucket::GetRedline(unsigned long* redlineBytes,        /* = NULL */
                       unsigned long* redlineFrames) const /* = NULL */
{
    if (redlineBytes != NULL)
    {
        *redlineBytes  = m_redlineBytes;
    }
    if (redlineFrames != NULL)
    {
        *redlineFrames = 0;
    }
}

void
PRO_CALLTYPE
CRtpBucket::GetFlowctrlInfo(float*         inFrameRate,        /* = NULL */
                            float*         inBitRate,          /* = NULL */
                            float*         outFrameRate,       /* = NULL */
                            float*         outBitRate,         /* = NULL */
                            unsigned long* cachedBytes,        /* = NULL */
                            unsigned long* cachedFrames) const /* = NULL */
{
    m_flowStat.CalcInfo(inFrameRate, inBitRate, outFrameRate, outBitRate);

    if (cachedBytes != NULL)
    {
        *cachedBytes  = m_totalBytes;
    }
    if (cachedFrames != NULL)
    {
        *cachedFrames = (unsigned long)m_packets.size();
    }
}

void
PRO_CALLTYPE
CRtpBucket::ResetFlowctrlInfo()
{
    m_flowStat.Reset();
}

/////////////////////////////////////////////////////////////////////////////
////

CRtpAudioBucket::CRtpAudioBucket()
{
    m_redlineBytes = AUDIO_REDLINE_BYTES;
}

bool
PRO_CALLTYPE
CRtpAudioBucket::PushBackAddRef(IRtpPacket* packet)
{
    assert(packet != NULL);
    if (packet == NULL)
    {
        return (false);
    }

    m_flowStat.PushData(1, packet->GetPayloadSize());

    /*
     * remove old packets
     */
    while (m_totalBytes >= m_redlineBytes)
    {
        IRtpPacket* const packet2 = m_packets.front();
        m_packets.pop_front();
        m_totalBytes -= packet2->GetPayloadSize();
        packet2->Release();
    }

    packet->AddRef();
    m_packets.push_back(packet);
    m_totalBytes += packet->GetPayloadSize();

    return (true);
}

void
PRO_CALLTYPE
CRtpAudioBucket::PopFrontRelease(IRtpPacket* packet)
{
    if (packet == NULL || m_totalBytes == 0 || packet != m_packets.front())
    {
        return;
    }

    m_packets.pop_front();

    m_flowStat.PopData(1, packet->GetPayloadSize());

    m_totalBytes -= packet->GetPayloadSize();
    packet->Release();
}

/////////////////////////////////////////////////////////////////////////////
////

CRtpVideoBucket::CRtpVideoBucket()
{
    m_redlineBytes  = VIDEO_REDLINE_BYTES;
    m_redlineFrames = VIDEO_REDLINE_FRAMES;
    m_totalBytes    = 0;
    m_totalFrames   = 0;
    m_waitingFrame  = NULL;
    m_sendingFrame  = NULL;
    m_needKeyFrame  = true;
    m_nextSeq       = 0;
    m_ssrc          = 0;

    m_flowStat.SetTimeSpan(GetRtpFlowctrlTimeSpan());
}

CRtpVideoBucket::~CRtpVideoBucket()
{
    Reset();
}

void
PRO_CALLTYPE
CRtpVideoBucket::Destroy()
{
    delete this;
}

unsigned long
PRO_CALLTYPE
CRtpVideoBucket::GetTotalBytes() const
{
    return (m_totalBytes);
}

IRtpPacket*
PRO_CALLTYPE
CRtpVideoBucket::GetFront()
{
    if (m_sendingFrame != NULL)
    {
        IRtpPacket* const packet = m_sendingFrame->bucket.GetFront();
        if (packet != NULL)
        {
            return (packet);
        }

        delete m_sendingFrame;
        m_sendingFrame = NULL;
        --m_totalFrames;
    }

    if (m_frames.size() == 0)
    {
        return (NULL);
    }

    m_sendingFrame = m_frames.front();
    m_frames.pop_front();

    IRtpPacket* const packet = m_sendingFrame->bucket.GetFront();

    return (packet);
}

bool
PRO_CALLTYPE
CRtpVideoBucket::PushBackAddRef(IRtpPacket* packet)
{
    assert(packet != NULL);
    if (packet == NULL)
    {
        return (false);
    }

    const bool       marker             = packet->GetMarker();
    const PRO_UINT16 seq                = packet->GetSequence();
    const PRO_UINT32 ssrc               = packet->GetSsrc();
    const bool       keyFrame           = packet->GetKeyFrame();
    const bool       firstPacketOfFrame = packet->GetFirstPacketOfFrame();

    m_flowStat.PushData(marker ? 1 : 0, packet->GetPayloadSize());

    /*
     * 1. check synchronization point
     */
    {
        if (m_needKeyFrame)
        {
            if (!keyFrame || !firstPacketOfFrame)
            {
                return (false);
            }

            m_needKeyFrame = false;
        }

        if (keyFrame && firstPacketOfFrame)
        {
            m_nextSeq = seq;
            m_ssrc    = ssrc;
        }
    }

    /*
     * 2. check ssrc
     */
    if (0 && ssrc != m_ssrc)
    {
        m_needKeyFrame = true; /* ===resynchronize=== */

        return (false);
    }

    /*
     * 3. check sequence number
     */
    if (0)
    {
        if (seq != m_nextSeq)
        {
            m_needKeyFrame = true; /* ===resynchronize=== */

            return (false);
        }

        ++m_nextSeq;
    }

    /*
     * 4. check the first packet
     */
    {
        if (firstPacketOfFrame)
        {
            if (m_waitingFrame != NULL)
            {
                m_totalBytes -= m_waitingFrame->bucket.GetTotalBytes();
                delete m_waitingFrame;
                m_waitingFrame = NULL;
                --m_totalFrames;
            }

            m_waitingFrame = new RTP_VIDEO_FRAME;
            m_waitingFrame->keyFrame = keyFrame;
            ++m_totalFrames;
        }
        else
        {
            if (m_waitingFrame == NULL)
            {
                m_needKeyFrame = true; /* ===resynchronize=== */

                return (false);
            }
        }

        m_waitingFrame->bucket.PushBackAddRef(packet);
        m_totalBytes += packet->GetPayloadSize();
    }

    /*
     * 5. check the last packet
     */
    if (!marker)
    {
        if (m_waitingFrame->bucket.GetTotalBytes() >= MAX_FRAME_BYTES)
        {
            m_totalBytes -= m_waitingFrame->bucket.GetTotalBytes();
            delete m_waitingFrame;
            m_waitingFrame = NULL;
            --m_totalFrames;

            m_needKeyFrame = true; /* ===resynchronize=== */

            return (false);
        }
        else
        {
            return (true);
        }
    }

    /*
     * 6. check redline
     */
    if (!m_waitingFrame->keyFrame)
    {
        if (m_totalBytes <= m_redlineBytes && m_totalFrames <= m_redlineFrames)
        {
            m_frames.push_back(m_waitingFrame);
            m_waitingFrame = NULL;

            return (true);
        }
        else
        {
            m_totalBytes -= m_waitingFrame->bucket.GetTotalBytes();
            delete m_waitingFrame;
            m_waitingFrame = NULL;
            --m_totalFrames;

            m_needKeyFrame = true; /* ===resynchronize=== */

            return (false);
        }
    }

    assert(marker);
    assert(m_waitingFrame->keyFrame);

    /*
     * 7. remove old frames
     */
    while (m_frames.size() > 0)
    {
        RTP_VIDEO_FRAME* const frame = m_frames.front();
        m_frames.pop_front();
        m_totalBytes -= frame->bucket.GetTotalBytes();
        delete frame;
        --m_totalFrames;
    }

    /*
     * 8. add the new frame
     */
    m_frames.push_back(m_waitingFrame);
    m_waitingFrame = NULL;

    return (true);
}

void
PRO_CALLTYPE
CRtpVideoBucket::PopFrontRelease(IRtpPacket* packet)
{
    if (packet == NULL || m_sendingFrame == NULL || packet != m_sendingFrame->bucket.GetFront())
    {
        return;
    }

    m_flowStat.PopData(packet->GetMarker() ? 1 : 0, packet->GetPayloadSize());

    m_totalBytes -= packet->GetPayloadSize();
    m_sendingFrame->bucket.PopFrontRelease(packet);

    if (m_sendingFrame->bucket.GetFront() == NULL)
    {
        delete m_sendingFrame;
        m_sendingFrame = NULL;
        --m_totalFrames;
    }
}

void
PRO_CALLTYPE
CRtpVideoBucket::Reset()
{
    delete m_waitingFrame;

    int       i = 0;
    const int c = (int)m_frames.size();

    for (; i < c; ++i)
    {
        delete m_frames[i];
    }

    delete m_sendingFrame;

    m_totalBytes   = 0;
    m_totalFrames  = 0;
    m_waitingFrame = NULL;
    m_frames.clear();
    m_sendingFrame = NULL;
    m_needKeyFrame = true;
    m_nextSeq      = 0;
    m_ssrc         = 0;

    m_flowStat.Reset();
}

void
PRO_CALLTYPE
CRtpVideoBucket::SetRedline(unsigned long redlineBytes,  /* = 0 */
                            unsigned long redlineFrames) /* = 0 */
{
    if (redlineBytes > 0)
    {
        m_redlineBytes  = redlineBytes;
    }
    if (redlineFrames > 0)
    {
        m_redlineFrames = redlineFrames;
    }
}

void
PRO_CALLTYPE
CRtpVideoBucket::GetRedline(unsigned long* redlineBytes,        /* = NULL */
                            unsigned long* redlineFrames) const /* = NULL */
{
    if (redlineBytes != NULL)
    {
        *redlineBytes  = m_redlineBytes;
    }
    if (redlineFrames != NULL)
    {
        *redlineFrames = m_redlineFrames;
    }
}

void
PRO_CALLTYPE
CRtpVideoBucket::GetFlowctrlInfo(float*         inFrameRate,        /* = NULL */
                                 float*         inBitRate,          /* = NULL */
                                 float*         outFrameRate,       /* = NULL */
                                 float*         outBitRate,         /* = NULL */
                                 unsigned long* cachedBytes,        /* = NULL */
                                 unsigned long* cachedFrames) const /* = NULL */
{
    m_flowStat.CalcInfo(inFrameRate, inBitRate, outFrameRate, outBitRate);

    if (cachedBytes != NULL)
    {
        *cachedBytes  = m_totalBytes;
    }
    if (cachedFrames != NULL)
    {
        *cachedFrames = m_totalFrames;
    }
}

void
PRO_CALLTYPE
CRtpVideoBucket::ResetFlowctrlInfo()
{
    m_flowStat.Reset();
}

/////////////////////////////////////////////////////////////////////////////
////

IRtpBucket*
PRO_CALLTYPE
CreateRtpBucket(RTP_MM_TYPE      mmType,
                RTP_SESSION_TYPE sessionType)
{
    assert(mmType != 0);
    if (mmType == 0)
    {
        return (NULL);
    }

    IRtpBucket* bucket = NULL;

    if (mmType >= RTP_MMT_AUDIO_MIN && mmType <= RTP_MMT_AUDIO_MAX)
    {
        bucket = new CRtpAudioBucket;
    }
    else if (mmType >= RTP_MMT_VIDEO_MIN && mmType <= RTP_MMT_VIDEO_MAX)
    {
        switch (sessionType)
        {
        case RTP_ST_TCPCLIENT_EX:
        case RTP_ST_TCPSERVER_EX:
        case RTP_ST_SSLCLIENT_EX:
        case RTP_ST_SSLSERVER_EX:
            {
                bucket = new CRtpVideoBucket;
                break;
            }
        default:
            {
                bucket = new CRtpBucket;
                break;
            }
        }
    }
    else
    {
        bucket = new CRtpBucket;
    }

    return (bucket);
}
