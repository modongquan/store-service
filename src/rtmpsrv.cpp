/*  Simple RTMP Server
 *  Copyright (C) 2009 Andrej Stepanchuk
 *  Copyright (C) 2009-2011 Howard Chu
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RTMPDump; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

/* This is just a stub for an RTMP server. It doesn't do anything
 * beyond obtaining the connection parameters from the client.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <string>
#include <stdint.h>

#include <signal.h>
#include <getopt.h>

#include <assert.h>
#include <vector>

#include <librtmp/rtmp_sys.h>
#include <librtmp/log.h>

#include "thread.h"
#include "common.h"

#ifdef linux
#include <linux/netfilter_ipv4.h>
#endif

#ifndef WIN32
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include "rtmpsrv.h"
#include "common.h"
#include "store.h"

#define RD_SUCCESS 0
#define RD_FAILED 1
#define RD_INCOMPLETE 2

#define PACKET_SIZE 1024 * 1024

#ifdef WIN32
#define InitSockets()                  \
    {                                  \
        WORD version;                  \
        WSADATA wsaData;               \
                                       \
        version = MAKEWORD(1, 1);      \
        WSAStartup(version, &wsaData); \
    }

#define CleanupSockets() WSACleanup()
#else
#define InitSockets()
#define CleanupSockets()
#endif

#define DUPTIME 5000 /* interval we disallow duplicate requests, in msec */

enum
{
    STREAMING_ACCEPTING,
    STREAMING_IN_PROGRESS,
    STREAMING_STOPPING,
    STREAMING_STOPPED
};

typedef struct
{
    int socket;
    int state;
    int streamID;
    int arglen;
    int argc;
    uint32_t filetime; /* time of last download we started */
    AVal filename;     /* name of last download */
    char *connect;

} STREAMING_SERVER;

struct NaluInfo
{
    uint8_t type;
    uint8_t *nalu;
    uint32_t nalu_len;
};

std::vector<STREAMING_SERVER *> rtmpServerList;

void *sslCtx = NULL;

STREAMING_SERVER *startStreaming(const char *address, int port);
void stopStreaming(STREAMING_SERVER *server);
void AVreplace(AVal *src, const AVal *orig, const AVal *repl);

static const AVal av_dquote = AVC("\"");
static const AVal av_escdquote = AVC("\\\"");

#define SPS_DATA_BUF_SIZE 256
#define PPS_DATA_BUF_SIZE 256
#define VIDEO_DATA_BUF_SIZE (50 * 1024 * 1024)

extern uint8_t SpsBuf[SPS_DATA_BUF_SIZE];
extern volatile uint32_t SpsSize;

extern uint8_t PpsBuf[PPS_DATA_BUF_SIZE];
extern volatile uint32_t PpsSize;

static uint8_t spsBuf[SPS_DATA_BUF_SIZE];
static volatile uint32_t spsSize = 0;

static uint8_t ppsBuf[PPS_DATA_BUF_SIZE];
static volatile uint32_t ppsSize = 0;

static uint8_t VideoDataBody[VIDEO_DATA_BUF_SIZE];
static uint32_t VideoDataBodySize = 0;
static bool is_key_frame = false;
static bool is_get_first_key_frame = false;
static uint32_t key_frame_cnt = 0;
static std::vector<struct NaluInfo> naluList;
static int key_frame_idx = 0;
static int sps_idx = 0;
static int pps_idx = 0;

typedef struct
{
    char *hostname;
    int rtmpport;
    int protocol;
    int bLiveStream; // is it a live stream? then we can't seek/resume

    long int timeout; // timeout connection afte 300 seconds
    uint32_t bufferTime;

    char *rtmpurl;
    AVal playpath;
    AVal swfUrl;
    AVal tcUrl;
    AVal pageUrl;
    AVal app;
    AVal auth;
    AVal swfHash;
    AVal flashVer;
    AVal subscribepath;
    uint32_t swfSize;

    uint32_t dStartOffset;
    uint32_t dStopOffset;
    uint32_t nTimeStamp;
} RTMP_REQUEST;

#define STR2AVAL(av, str) \
    av.av_val = str;      \
    av.av_len = strlen(av.av_val)

/* this request is formed from the parameters and used to initialize a new request,
 * thus it is a default settings list. All settings can be overriden by specifying the
 * parameters in the GET request. */
RTMP_REQUEST defaultRTMPRequest;

#ifdef _DEBUG
uint32_t debugTS = 0;

int pnum = 0;

FILE *netstackdump = NULL;
FILE *netstackdump_read = NULL;
#endif

#define SAVC(x) static const AVal av_##x = AVC(#x)

SAVC(app);
SAVC(connect);
SAVC(flashVer);
SAVC(swfUrl);
SAVC(pageUrl);
SAVC(tcUrl);
SAVC(fpad);
SAVC(capabilities);
SAVC(audioCodecs);
SAVC(videoCodecs);
SAVC(videoFunction);
SAVC(objectEncoding);
SAVC(_result);
SAVC(createStream);
SAVC(getStreamLength);
SAVC(play);
SAVC(fmsVer);
SAVC(mode);
SAVC(level);
SAVC(code);
SAVC(description);
SAVC(secureToken);
SAVC(deleteStream);

static void ParseH264Param(uint8_t *pVideoData, uint32_t size)
{
    uint32_t idx = 0;
    struct NaluInfo naluInfo;
    uint8_t is_first_nalu = 0;

    naluList.clear();

    while (idx < size)
    {
        uint32_t divide1 = *(uint32_t *)(pVideoData + idx);
        uint32_t divide2 = (divide1 & 0x00ffffff);

        if (divide1 != 0x01000000 && divide2 != 0x00010000)
        {
            idx++;
            naluInfo.nalu_len++;
            continue;
        }
        if (is_first_nalu)
            naluList.push_back(naluInfo);

        if (divide1 == 0x01000000)
            idx += sizeof(uint32_t);
        else if (divide2 == 0x00010000)
            idx += 3;

        naluInfo.type = (pVideoData[idx] & 0x1f);
        naluInfo.nalu = (pVideoData + idx++);
        naluInfo.nalu_len = 1;
        is_first_nalu = 1;
    }
    naluList.push_back(naluInfo);
}

static void ParseVideoParam(uint8_t *pVideoData, uint32_t size)
{
    if (pVideoData == nullptr)
        return;

    if (*(uint32_t *)(pVideoData) == 0x01000000)
    {
        ParseH264Param(pVideoData, size);
    }
}

static void ParseVideoData(uint8_t *pData, uint32_t data_size)
{
    int is_key_frame_exist = 0;
    key_frame_idx = -1;
    sps_idx = -1;
    pps_idx = -1;

    ParseVideoParam(pData, data_size);
    for (int i = 0; i < naluList.size(); i++)
    {
        if (naluList[i].type == 0x5)
        {
            // myprintf("is key frame,gop cnt = %d\n", key_frame_cnt);
            key_frame_cnt = 1;
            is_key_frame = true;
            key_frame_idx = i;

            is_key_frame_exist = 1;
        }
        else if (naluList[i].type == 0x7)
        {
            sps_idx = i;
        }
        else if (naluList[i].type == 0x8)
        {
            pps_idx = i;
        }
    }
    if (is_key_frame_exist)
        return;

    key_frame_cnt++;
    is_key_frame = false;
}

static uint32_t PacketVideoParam(uint8_t *body)
{
    uint32_t i = 0;

    body[i++] = 0x17;

    // AVC Sequence Header
    body[i++] = 0x00;
    body[i++] = 0x00;
    body[i++] = 0x00;
    body[i++] = 0x00;

    // AVCDecoderConfigurationRecord
    body[i++] = 0x01;
    body[i++] = naluList[sps_idx].nalu[1];
    body[i++] = naluList[sps_idx].nalu[2];
    body[i++] = naluList[sps_idx].nalu[3];
    body[i++] = 0xff;
    body[i++] = 0xe1;
    body[i++] = (naluList[sps_idx].nalu_len >> 8) & 0xff;
    body[i++] = naluList[sps_idx].nalu_len & 0xff;
    for (size_t j = 0; j < naluList[sps_idx].nalu_len; j++)
    {
        body[i++] = naluList[sps_idx].nalu[j];
    }
    body[i++] = 0x01;
    uint32_t pps_size = naluList[pps_idx].nalu_len + 1;
    body[i++] = (pps_size >> 8) & 0xff;
    body[i++] = pps_size & 0xff;
    for (size_t j = 0; j < naluList[pps_idx].nalu_len; j++)
    {
        body[i++] = naluList[pps_idx].nalu[j];
    }
    body[i++] = 0;

    return i;
}

static uint32_t PacketVideoData(uint8_t *body)
{
    uint32_t i = 0;

    if (is_key_frame)
    {
        body[i++] = 0x17; // 1: IFrame, 7: AVC
    }
    else
    {
        body[i++] = 0x27; // 2: PFrame, 7: AVC
    }
    // AVCVIDEOPACKET
    body[i++] = 0x01;
    body[i++] = 0x00;
    body[i++] = 0x00;
    body[i++] = 0x00;

    // NALUs
    for (int j = 0; j < naluList.size(); j++)
    {
        body[i++] = naluList[j].nalu_len >> 24 & 0xff;
        body[i++] = naluList[j].nalu_len >> 16 & 0xff;
        body[i++] = naluList[j].nalu_len >> 8 & 0xff;
        body[i++] = naluList[j].nalu_len & 0xff;
        memcpy(VideoDataBody + i, naluList[j].nalu, naluList[j].nalu_len);
        i += naluList[j].nalu_len;
    }

    return i;
}

static int32_t SendVideoParam(RTMP *r, time_t time_stame)
{
    if (key_frame_idx < 0 || sps_idx < 0 || pps_idx < 0)
        return 0;

    uint32_t param_size = PacketVideoParam(VideoDataBody);
    if (!param_size)
        return 0;

    RTMPPacket packet;

    packet.m_nChannel = 0x07; // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_VIDEO;
    packet.m_nTimeStamp = time_stame;
    packet.m_nInfoField2 = 1;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = (char *)VideoDataBody;
    packet.m_nBodySize = param_size;

    int32_t ret = RTMP_SendPacket(r, &packet, FALSE);

    uint32_t video_size = PacketVideoData(VideoDataBody);

    packet.m_nChannel = 0x07; // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
    packet.m_packetType = RTMP_PACKET_TYPE_VIDEO;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 1;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = (char *)VideoDataBody;
    packet.m_nBodySize = video_size;

    ret = RTMP_SendPacket(r, &packet, FALSE);

    return ret;
}

static int32_t SendVideoData(RTMP *r, time_t time_stame)
{
    RTMPPacket packet;

    uint32_t video_size = PacketVideoData(VideoDataBody);

    packet.m_nChannel = 0x07; // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
    packet.m_packetType = RTMP_PACKET_TYPE_VIDEO;
    packet.m_nTimeStamp = time_stame;
    packet.m_nInfoField2 = 1;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = (char *)VideoDataBody;
    packet.m_nBodySize = video_size;

    int32_t ret = RTMP_SendPacket(r, &packet, FALSE);

    return ret;
}

static char SampleAccess[] = {"|RtmpSampleAccess"};

static int SendSampleAccess(RTMP *r)
{
    RTMPPacket packet;
    char pbuf[384], *pend = pbuf + sizeof(pbuf);
    AMFObject obj;
    AMFObjectProperty p, op;
    AVal av;

    packet.m_nChannel = 0x05; // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INFO;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 1;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    char *enc = packet.m_body;

    av.av_val = SampleAccess;
    av.av_len = strlen(SampleAccess);
    enc = AMF_EncodeString(enc, pend, &av);

    enc = AMF_EncodeBoolean(enc, pend, true);
    enc = AMF_EncodeBoolean(enc, pend, true);

    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(r, &packet, FALSE);
}

static char OnMetaData[] = {"OnMetaData"};
static char Server[] = {"Server"};
static char TeamWayRtmp[] = {"teamway rtmp"};
static char Width[] = {"width"};
static char Height[] = {"height"};
static char DisplayW[] = {"displaywidth"};
static char DisplayH[] = {"displayheight"};
static char Duration[] = {"duration"};
static char Framerate[] = {"framerate"};
static char FPS[] = {"fps"};
static char VideoDataRate[] = {"videodatarate"};
static char VideoCodecId[] = {"videocodecid"};
static char AudioDataRate[] = {"audiodatarate"};
static char AudioCodecId[] = {"audiodatarate"};
static char Profile[] = {"profile"};
static char Level[] = {"level"};

static int SendMetaData(RTMP *r)
{
    RTMPPacket packet;
    char pbuf[384], *pend = pbuf + sizeof(pbuf);
    AMFObject obj;
    AMFObjectProperty p, op;
    AVal av, av1;

    packet.m_nChannel = 0x05; // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INFO;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 1;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    char *enc = packet.m_body;

    av.av_val = OnMetaData;
    av.av_len = strlen(OnMetaData);
    enc = AMF_EncodeString(enc, pend, &av);

    *enc++ = AMF_OBJECT;

    av.av_val = Server;
    av.av_len = strlen(Server);
    av1.av_val = TeamWayRtmp;
    av1.av_len = strlen(TeamWayRtmp);
    enc = AMF_EncodeNamedString(enc, pend, &av, &av1);

    av.av_val = Width;
    av.av_len = strlen(Width);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 1280);

    av.av_val = Height;
    av.av_len = strlen(Height);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 720);

    av.av_val = DisplayW;
    av.av_len = strlen(DisplayW);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 1280);

    av.av_val = DisplayH;
    av.av_len = strlen(DisplayH);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 720);

    av.av_val = Duration;
    av.av_len = strlen(Duration);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 0);

    av.av_val = Framerate;
    av.av_len = strlen(Framerate);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 25);

    av.av_val = FPS;
    av.av_len = strlen(FPS);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 25);

    av.av_val = VideoDataRate;
    av.av_len = strlen(VideoDataRate);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 0);

    av.av_val = VideoCodecId;
    av.av_len = strlen(VideoCodecId);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 7);

    av.av_val = AudioDataRate;
    av.av_len = strlen(AudioDataRate);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 0);

    av.av_val = AudioCodecId;
    av.av_len = strlen(AudioCodecId);
    enc = AMF_EncodeNamedNumber(enc, pend, &av, 0);

    av.av_val = Profile;
    av.av_len = strlen(Profile);
    av1.av_val = "";
    av1.av_len = 0;
    enc = AMF_EncodeNamedString(enc, pend, &av, &av1);

    av.av_val = Level;
    av.av_len = strlen(Level);
    av1.av_val = "";
    av1.av_len = 0;
    enc = AMF_EncodeNamedString(enc, pend, &av, &av1);

    *enc++ = 0;
    *enc++ = 0;
    *enc++ = AMF_OBJECT_END;

    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(r, &packet, FALSE);
}

static int
SendConnectResult(RTMP *r, double txn)
{
    RTMPPacket packet;
    char pbuf[384], *pend = pbuf + sizeof(pbuf);
    AMFObject obj;
    AMFObjectProperty p, op;
    AVal av;

    packet.m_nChannel = 0x03; // control channel (invoke)
    packet.m_headerType = 1;  /* RTMP_PACKET_SIZE_MEDIUM; */
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    char *enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av__result);
    enc = AMF_EncodeNumber(enc, pend, txn);
    *enc++ = AMF_OBJECT;

    STR2AVAL(av, "FMS/3,5,1,525");
    enc = AMF_EncodeNamedString(enc, pend, &av_fmsVer, &av);
    enc = AMF_EncodeNamedNumber(enc, pend, &av_capabilities, 31.0);
    enc = AMF_EncodeNamedNumber(enc, pend, &av_mode, 1.0);
    *enc++ = 0;
    *enc++ = 0;
    *enc++ = AMF_OBJECT_END;

    *enc++ = AMF_OBJECT;

    STR2AVAL(av, "status");
    enc = AMF_EncodeNamedString(enc, pend, &av_level, &av);
    STR2AVAL(av, "NetConnection.Connect.Success");
    enc = AMF_EncodeNamedString(enc, pend, &av_code, &av);
    STR2AVAL(av, "Connection succeeded.");
    enc = AMF_EncodeNamedString(enc, pend, &av_description, &av);
    enc = AMF_EncodeNamedNumber(enc, pend, &av_objectEncoding, r->m_fEncoding);
#if 0
    STR2AVAL(av, "58656322c972d6cdf2d776167575045f8484ea888e31c086f7b5ffbd0baec55ce442c2fb");
    enc = AMF_EncodeNamedString(enc, pend, &av_secureToken, &av);
#endif
    STR2AVAL(p.p_name, "version");
    STR2AVAL(p.p_vu.p_aval, "3,5,1,525");
    p.p_type = AMF_STRING;
    obj.o_num = 1;
    obj.o_props = &p;
    op.p_type = AMF_OBJECT;
    STR2AVAL(op.p_name, "data");
    op.p_vu.p_object = obj;
    enc = AMFProp_Encode(&op, enc, pend);
    *enc++ = 0;
    *enc++ = 0;
    *enc++ = AMF_OBJECT_END;

    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(r, &packet, FALSE);
}

static int SendWinAcknowledg(RTMP *r, uint32_t win_acknowledg_size)
{
    RTMPPacket packet;
    uint32_t body;

    packet.m_nChannel = 0x02;                     // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE; /* RTMP_PACKET_SIZE_MEDIUM; */
    packet.m_packetType = RTMP_PACKET_TYPE_SERVER_BW;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = (char *)&body;

    packet.m_body[0] = (char)(win_acknowledg_size >> 24);
    packet.m_body[1] = (char)((win_acknowledg_size >> 16) & 0x000000ff);
    packet.m_body[2] = (char)((win_acknowledg_size >> 8) & 0x000000ff);
    packet.m_body[3] = (char)((win_acknowledg_size)&0x000000ff);
    packet.m_nBodySize = sizeof(win_acknowledg_size);

    return RTMP_SendPacket(r, &packet, FALSE);
}

static int SendPeerBandwidth(RTMP *r, uint32_t win_acknowledg_size, uint8_t dynamic)
{
    RTMPPacket packet;
    char body[5];

    packet.m_nChannel = 0x02;                     // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE; /* RTMP_PACKET_SIZE_MEDIUM; */
    packet.m_packetType = RTMP_PACKET_TYPE_CLIENT_BW;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = body;

    packet.m_body[0] = (char)(win_acknowledg_size >> 24);
    packet.m_body[1] = (char)((win_acknowledg_size >> 16) & 0x000000ff);
    packet.m_body[2] = (char)((win_acknowledg_size >> 8) & 0x000000ff);
    packet.m_body[3] = (char)((win_acknowledg_size)&0x000000ff);
    packet.m_body[4] = dynamic;
    packet.m_nBodySize = sizeof(win_acknowledg_size) + sizeof(dynamic);

    return RTMP_SendPacket(r, &packet, FALSE);
}

static int SendChunkSize(RTMP *r, uint32_t chunk_size)
{
    RTMPPacket packet;
    uint32_t body;

    packet.m_nChannel = 0x02;                     // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE; /* RTMP_PACKET_SIZE_MEDIUM; */
    packet.m_packetType = RTMP_PACKET_TYPE_CHUNK_SIZE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = (char *)&body;

    packet.m_body[0] = (char)(chunk_size >> 24);
    packet.m_body[1] = (char)((chunk_size >> 16) & 0x000000ff);
    packet.m_body[2] = (char)((chunk_size >> 8) & 0x000000ff);
    packet.m_body[3] = (char)((chunk_size)&0x000000ff);
    packet.m_nBodySize = sizeof(chunk_size);

    return RTMP_SendPacket(r, &packet, FALSE);
}

static int
SendResultNumber(RTMP *r, double txn, double ID)
{
    RTMPPacket packet;
    char pbuf[256], *pend = pbuf + sizeof(pbuf);

    packet.m_nChannel = 0x03; // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    char *enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av__result);
    enc = AMF_EncodeNumber(enc, pend, txn);
    *enc++ = AMF_NULL;
    enc = AMF_EncodeNumber(enc, pend, ID);

    packet.m_nBodySize = enc - packet.m_body;

    return RTMP_SendPacket(r, &packet, FALSE);
}

SAVC(onStatus);
SAVC(status);
static const AVal av_NetStream_Play_Start = AVC("NetStream.Play.Start");
static const AVal av_Started_playing = AVC("Started playing");
static const AVal av_NetStream_Play_Stop = AVC("NetStream.Play.Stop");
static const AVal av_Stopped_playing = AVC("Stopped playing");
SAVC(details);
SAVC(clientid);
static const AVal av_NetStream_Authenticate_UsherToken = AVC("NetStream.Authenticate.UsherToken");

static int SendStreamBegin(RTMP *r)
{
    RTMPPacket packet;
    char pbuf[512], *pend = pbuf + sizeof(pbuf);

    packet.m_nChannel = 0x02; // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_CONTROL;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf;

    char *enc = packet.m_body;

    *enc++ = 0;
    *enc++ = 0;
    *enc++ = 0;
    *enc++ = 0;
    *enc++ = 0;
    *enc++ = 1;

    packet.m_nBodySize = enc - packet.m_body;
    return RTMP_SendPacket(r, &packet, FALSE);
}

static int
SendPlayStart(RTMP *r)
{
    RTMPPacket packet;
    char pbuf[512], *pend = pbuf + sizeof(pbuf);

    packet.m_nChannel = 0x05; // control channel (invoke)
    packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 1;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    char *enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av_onStatus);
    enc = AMF_EncodeNumber(enc, pend, 0);
    *enc++ = AMF_NULL;
    *enc++ = AMF_OBJECT;

    enc = AMF_EncodeNamedString(enc, pend, &av_level, &av_status);
    enc = AMF_EncodeNamedString(enc, pend, &av_code, &av_NetStream_Play_Start);
    enc = AMF_EncodeNamedString(enc, pend, &av_description, &av_Started_playing);
    enc = AMF_EncodeNamedString(enc, pend, &av_details, &r->Link.playpath);
    enc = AMF_EncodeNamedString(enc, pend, &av_clientid, &av_clientid);
    *enc++ = 0;
    *enc++ = 0;
    *enc++ = AMF_OBJECT_END;

    packet.m_nBodySize = enc - packet.m_body;
    return RTMP_SendPacket(r, &packet, FALSE);
}

static int
SendPlayStop(RTMP *r)
{
    RTMPPacket packet;
    char pbuf[512], *pend = pbuf + sizeof(pbuf);

    packet.m_nChannel = 0x03; // control channel (invoke)
    packet.m_headerType = 1;  /* RTMP_PACKET_SIZE_MEDIUM; */
    packet.m_packetType = RTMP_PACKET_TYPE_INVOKE;
    packet.m_nTimeStamp = 0;
    packet.m_nInfoField2 = 0;
    packet.m_hasAbsTimestamp = 0;
    packet.m_body = pbuf + RTMP_MAX_HEADER_SIZE;

    char *enc = packet.m_body;
    enc = AMF_EncodeString(enc, pend, &av_onStatus);
    enc = AMF_EncodeNumber(enc, pend, 0);
    *enc++ = AMF_OBJECT;

    enc = AMF_EncodeNamedString(enc, pend, &av_level, &av_status);
    enc = AMF_EncodeNamedString(enc, pend, &av_code, &av_NetStream_Play_Stop);
    enc = AMF_EncodeNamedString(enc, pend, &av_description, &av_Stopped_playing);
    enc = AMF_EncodeNamedString(enc, pend, &av_details, &r->Link.playpath);
    enc = AMF_EncodeNamedString(enc, pend, &av_clientid, &av_clientid);
    *enc++ = 0;
    *enc++ = 0;
    *enc++ = AMF_OBJECT_END;

    packet.m_nBodySize = enc - packet.m_body;
    return RTMP_SendPacket(r, &packet, FALSE);
}

static void
spawn_dumper(int argc, AVal *av, char *cmd)
{
#ifdef WIN32
    STARTUPINFO si = {0};
    PROCESS_INFORMATION pi = {0};

    si.cb = sizeof(si);
    if (CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL,
                      &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
#else
    /* reap any dead children */
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    if (fork() == 0)
    {
        char **argv = (char **)malloc((argc + 1) * sizeof(char *));
        int i;

        for (i = 0; i < argc; i++)
        {
            argv[i] = av[i].av_val;
            argv[i][av[i].av_len] = '\0';
        }
        argv[i] = NULL;
        if ((i = execvp(argv[0], argv)))
            _exit(i);
    }
#endif
}

static int
countAMF(AMFObject *obj, int *argc)
{
    int i, len;

    for (i = 0, len = 0; i < obj->o_num; i++)
    {
        AMFObjectProperty *p = &obj->o_props[i];
        len += 4;
        (*argc) += 2;
        if (p->p_name.av_val)
            len += 1;
        len += 2;
        if (p->p_name.av_val)
            len += p->p_name.av_len + 1;
        switch (p->p_type)
        {
        case AMF_BOOLEAN:
            len += 1;
            break;
        case AMF_STRING:
            len += p->p_vu.p_aval.av_len;
            break;
        case AMF_NUMBER:
            len += 40;
            break;
        case AMF_OBJECT:
            len += 9;
            len += countAMF(&p->p_vu.p_object, argc);
            (*argc) += 2;
            break;
        case AMF_NULL:
        default:
            break;
        }
    }
    return len;
}

static char *
dumpAMF(AMFObject *obj, char *ptr, AVal *argv, int *argc)
{
    int i, ac = *argc;
    const char opt[] = "NBSO Z";

    for (i = 0; i < obj->o_num; i++)
    {
        AMFObjectProperty *p = &obj->o_props[i];
        argv[ac].av_val = ptr + 1;
        argv[ac++].av_len = 2;
        ptr += sprintf(ptr, " -C ");
        argv[ac].av_val = ptr;
        if (p->p_name.av_val)
            *ptr++ = 'N';
        *ptr++ = opt[p->p_type];
        *ptr++ = ':';
        if (p->p_name.av_val)
            ptr += sprintf(ptr, "%.*s:", p->p_name.av_len, p->p_name.av_val);
        switch (p->p_type)
        {
        case AMF_BOOLEAN:
            *ptr++ = p->p_vu.p_number != 0 ? '1' : '0';
            argv[ac].av_len = ptr - argv[ac].av_val;
            break;
        case AMF_STRING:
            memcpy(ptr, p->p_vu.p_aval.av_val, p->p_vu.p_aval.av_len);
            ptr += p->p_vu.p_aval.av_len;
            argv[ac].av_len = ptr - argv[ac].av_val;
            break;
        case AMF_NUMBER:
            ptr += sprintf(ptr, "%f", p->p_vu.p_number);
            argv[ac].av_len = ptr - argv[ac].av_val;
            break;
        case AMF_OBJECT:
            *ptr++ = '1';
            argv[ac].av_len = ptr - argv[ac].av_val;
            ac++;
            *argc = ac;
            ptr = dumpAMF(&p->p_vu.p_object, ptr, argv, argc);
            ac = *argc;
            argv[ac].av_val = ptr + 1;
            argv[ac++].av_len = 2;
            argv[ac].av_val = ptr + 4;
            argv[ac].av_len = 3;
            ptr += sprintf(ptr, " -C O:0");
            break;
        case AMF_NULL:
        default:
            argv[ac].av_len = ptr - argv[ac].av_val;
            break;
        }
        ac++;
    }
    *argc = ac;
    return ptr;
}
#include <sys/time.h>
int ServePacket(STREAMING_SERVER *server, RTMP *r, RTMPPacket *packet);
static int32_t StartPlaybackStoreFile(std::string &file_path, uint32_t frame_rate, STREAMING_SERVER *server, RTMP *r, RTMPPacket *packet)
{
    uint8_t *pFrameBuf, *pOneFrame;
    uint32_t FrameSize = 300 * 1024;
    uint32_t time_interval = 1000000 / frame_rate;

    if (!(pFrameBuf = (uint8_t *)malloc(STORE_FILE_MAX_SIZE)))
    {
        return -1;
    }

    if (!(pOneFrame = (uint8_t *)malloc(FrameSize)))
    {
        return -1;
    }
    struct timeval startTime, endTime;
    uint64_t start_ms, end_ms;

    gettimeofday(&startTime, nullptr);
    if (ReadStoreFileFrame(file_path, pFrameBuf, STORE_FILE_MAX_SIZE) < 0)
    {
        return -1;
    }
    gettimeofday(&endTime, nullptr);
    start_ms = startTime.tv_sec * 1000 + startTime.tv_usec / 1000;
    end_ms = endTime.tv_sec * 1000 + endTime.tv_usec / 1000;
    myprintf("read store file \"%s\" cast %ld ms\n", file_path.c_str(), end_ms - start_ms);

    uint32_t idx = 0;
    int32_t get_frame_size = 0;
    uint64_t first_sys_time = 0, sys_time, prev_sys_time;

    while ((get_frame_size = GetOneFrameByIndex(idx++, pFrameBuf, STORE_FILE_MAX_SIZE, pOneFrame, FrameSize, &sys_time)) > 0)
    {
        if (!first_sys_time)
            first_sys_time = sys_time;

        ParseVideoData(pOneFrame, get_frame_size);
        if (!is_get_first_key_frame)
        {
            if (is_key_frame)
            {
                if (!SendVideoParam(r, 0))
                    continue;
                prev_sys_time = sys_time;
                is_get_first_key_frame = true;
            }
            continue;
        }
        SendVideoData(r, sys_time - prev_sys_time);

        usleep(time_interval);

        if (!RTMP_IsConnected(r))
        {
            break;
        }
    }

    free(pOneFrame);
    free(pFrameBuf);
}

// Returns 0 for OK/Failed/error, 1 for 'Stop or Complete'
int ServeInvoke(STREAMING_SERVER *server, RTMP *r, RTMPPacket *packet, unsigned int offset)
{
    const char *body;
    unsigned int nBodySize;
    int ret = 0, nRes;

    body = packet->m_body + offset;
    nBodySize = packet->m_nBodySize - offset;

    if (body[0] != 0x02) // make sure it is a string method name we start with
    {
        RTMP_Log(RTMP_LOGWARNING, "%s, Sanity failed. no string method in invoke packet",
                 __FUNCTION__);
        return 0;
    }

    AMFObject obj;
    nRes = AMF_Decode(&obj, body, nBodySize, FALSE);
    if (nRes < 0)
    {
        RTMP_Log(RTMP_LOGERROR, "%s, error decoding invoke packet", __FUNCTION__);
        return 0;
    }

    AMF_Dump(&obj);
    AVal method;
    AMFProp_GetString(AMF_GetProp(&obj, NULL, 0), &method);
    double txn = AMFProp_GetNumber(AMF_GetProp(&obj, NULL, 1));
    RTMP_Log(RTMP_LOGDEBUG, "%s, client invoking <%s>", __FUNCTION__, method.av_val);
    myprintf("%s, client invoking <%s> \n", __FUNCTION__, method.av_val);

    if (AVMATCH(&method, &av_connect))
    {
        AMFObject cobj;
        AVal pname, pval;
        int i;

        server->connect = packet->m_body;
        packet->m_body = NULL;

        AMFProp_GetObject(AMF_GetProp(&obj, NULL, 2), &cobj);
        for (i = 0; i < cobj.o_num; i++)
        {
            pname = cobj.o_props[i].p_name;
            pval.av_val = NULL;
            pval.av_len = 0;
            if (cobj.o_props[i].p_type == AMF_STRING)
                pval = cobj.o_props[i].p_vu.p_aval;
            if (AVMATCH(&pname, &av_app))
            {
                r->Link.app = pval;
                pval.av_val = NULL;
                if (!r->Link.app.av_val)
                    r->Link.app.av_val = "";
                server->arglen += 6 + pval.av_len;
                server->argc += 2;
            }
            else if (AVMATCH(&pname, &av_flashVer))
            {
                r->Link.flashVer = pval;
                pval.av_val = NULL;
                server->arglen += 6 + pval.av_len;
                server->argc += 2;
            }
            else if (AVMATCH(&pname, &av_swfUrl))
            {
                r->Link.swfUrl = pval;
                pval.av_val = NULL;
                server->arglen += 6 + pval.av_len;
                server->argc += 2;
            }
            else if (AVMATCH(&pname, &av_tcUrl))
            {
                r->Link.tcUrl = pval;
                pval.av_val = NULL;
                server->arglen += 6 + pval.av_len;
                server->argc += 2;
            }
            else if (AVMATCH(&pname, &av_pageUrl))
            {
                r->Link.pageUrl = pval;
                pval.av_val = NULL;
                server->arglen += 6 + pval.av_len;
                server->argc += 2;
            }
            else if (AVMATCH(&pname, &av_audioCodecs))
            {
                r->m_fAudioCodecs = cobj.o_props[i].p_vu.p_number;
            }
            else if (AVMATCH(&pname, &av_videoCodecs))
            {
                r->m_fVideoCodecs = cobj.o_props[i].p_vu.p_number;
            }
            else if (AVMATCH(&pname, &av_objectEncoding))
            {
                r->m_fEncoding = cobj.o_props[i].p_vu.p_number;
            }
        }
        /* Still have more parameters? Copy them */
        if (obj.o_num > 3)
        {
            int i = obj.o_num - 3;
            r->Link.extras.o_num = i;
            r->Link.extras.o_props = (struct AMFObjectProperty *)malloc(i * sizeof(AMFObjectProperty));
            memcpy(r->Link.extras.o_props, obj.o_props + 3, i * sizeof(AMFObjectProperty));
            obj.o_num = 3;
            server->arglen += countAMF(&r->Link.extras, &server->argc);
        }
        SendConnectResult(r, txn);
        SendWinAcknowledg(r, 2500000);
        SendPeerBandwidth(r, 2500000, 2);
        SendChunkSize(r, RTMP_DEFAULT_CHUNKSIZE);
    }
    else if (AVMATCH(&method, &av_createStream))
    {
        SendResultNumber(r, txn, ++server->streamID);
    }
    else if (AVMATCH(&method, &av_getStreamLength))
    {
        SendResultNumber(r, txn, 10.0);
    }
    else if (AVMATCH(&method, &av_NetStream_Authenticate_UsherToken))
    {
        AVal usherToken;
        AMFProp_GetString(AMF_GetProp(&obj, NULL, 3), &usherToken);
        AVreplace(&usherToken, &av_dquote, &av_escdquote);
        server->arglen += 6 + usherToken.av_len;
        server->argc += 2;
        r->Link.usherToken = usherToken;
    }
    else if (AVMATCH(&method, &av_play))
    {
        char *file, *p, *q, *cmd, *ptr;
        AVal *argv, av;
        int len, argc;
        uint32_t now;
        RTMPPacket pc = {0};
        AMFProp_GetString(AMF_GetProp(&obj, NULL, 3), &r->Link.playpath);
        myprintf("play app = %s, play path = %s\n", r->Link.app.av_val, r->Link.playpath.av_val);
        if (!r->Link.playpath.av_len)
            return 0;
            /*
      r->Link.seekTime = AMFProp_GetNumber(AMF_GetProp(&obj, NULL, 4));
      if (obj.o_num > 5)
    r->Link.length = AMFProp_GetNumber(AMF_GetProp(&obj, NULL, 5));
      */
#if 0
        if (r->Link.tcUrl.av_len)
        {
            len = server->arglen + r->Link.playpath.av_len + 4 +
                  sizeof("rtmpdump") + r->Link.playpath.av_len + 12;
            server->argc += 5;

            cmd = (char *)malloc(len + server->argc * sizeof(AVal));
            ptr = cmd;
            argv = (AVal *)(cmd + len);
            argv[0].av_val = cmd;
            argv[0].av_len = sizeof("rtmpdump") - 1;
            ptr += sprintf(ptr, "rtmpdump");
            argc = 1;

            argv[argc].av_val = ptr + 1;
            argv[argc++].av_len = 2;
            argv[argc].av_val = ptr + 5;
            ptr += sprintf(ptr, " -r \"%s\"", r->Link.tcUrl.av_val);
            argv[argc++].av_len = r->Link.tcUrl.av_len;

            if (r->Link.app.av_val)
            {
                argv[argc].av_val = ptr + 1;
                argv[argc++].av_len = 2;
                argv[argc].av_val = ptr + 5;
                ptr += sprintf(ptr, " -a \"%s\"", r->Link.app.av_val);
                argv[argc++].av_len = r->Link.app.av_len;
            }
            if (r->Link.flashVer.av_val)
            {
                argv[argc].av_val = ptr + 1;
                argv[argc++].av_len = 2;
                argv[argc].av_val = ptr + 5;
                ptr += sprintf(ptr, " -f \"%s\"", r->Link.flashVer.av_val);
                argv[argc++].av_len = r->Link.flashVer.av_len;
            }
            if (r->Link.swfUrl.av_val)
            {
                argv[argc].av_val = ptr + 1;
                argv[argc++].av_len = 2;
                argv[argc].av_val = ptr + 5;
                ptr += sprintf(ptr, " -W \"%s\"", r->Link.swfUrl.av_val);
                argv[argc++].av_len = r->Link.swfUrl.av_len;
            }
            if (r->Link.pageUrl.av_val)
            {
                argv[argc].av_val = ptr + 1;
                argv[argc++].av_len = 2;
                argv[argc].av_val = ptr + 5;
                ptr += sprintf(ptr, " -p \"%s\"", r->Link.pageUrl.av_val);
                argv[argc++].av_len = r->Link.pageUrl.av_len;
            }
            if (r->Link.usherToken.av_val)
            {
                argv[argc].av_val = ptr + 1;
                argv[argc++].av_len = 2;
                argv[argc].av_val = ptr + 5;
                ptr += sprintf(ptr, " -j \"%s\"", r->Link.usherToken.av_val);
                argv[argc++].av_len = r->Link.usherToken.av_len;
                free(r->Link.usherToken.av_val);
                r->Link.usherToken.av_val = NULL;
                r->Link.usherToken.av_len = 0;
            }
            if (r->Link.extras.o_num)
            {
                ptr = dumpAMF(&r->Link.extras, ptr, argv, &argc);
                AMF_Reset(&r->Link.extras);
            }
            argv[argc].av_val = ptr + 1;
            argv[argc++].av_len = 2;
            argv[argc].av_val = ptr + 5;
            ptr += sprintf(ptr, " -y \"%.*s\"",
                           r->Link.playpath.av_len, r->Link.playpath.av_val);
            argv[argc++].av_len = r->Link.playpath.av_len;

            av = r->Link.playpath;
            /* strip trailing URL parameters */
            q = (char *)memchr(av.av_val, '?', av.av_len);
            if (q)
            {
                if (q == av.av_val)
                {
                    av.av_val++;
                    av.av_len--;
                }
                else
                {
                    av.av_len = q - av.av_val;
                }
            }
            /* strip leading slash components */
            for (p = av.av_val + av.av_len - 1; p >= av.av_val; p--)
                if (*p == '/')
                {
                    p++;
                    av.av_len -= p - av.av_val;
                    av.av_val = p;
                    break;
                }
            /* skip leading dot */
            if (av.av_val[0] == '.')
            {
                av.av_val++;
                av.av_len--;
            }
            file = (char *)malloc(av.av_len + 5);

            memcpy(file, av.av_val, av.av_len);
            file[av.av_len] = '\0';
            for (p = file; *p; p++)
                if (*p == ':')
                    *p = '_';

            /* Add extension if none present */
            if (file[av.av_len - 4] != '.')
            {
                av.av_len += 4;
            }
            /* Always use flv extension, regardless of original */
            if (strcmp(file + av.av_len - 4, ".flv"))
            {
                strcpy(file + av.av_len - 4, ".flv");
            }
            argv[argc].av_val = ptr + 1;
            argv[argc++].av_len = 2;
            argv[argc].av_val = file;
            argv[argc].av_len = av.av_len;
            ptr += sprintf(ptr, " -o %s", file);
            now = RTMP_GetTime();
            if (now - server->filetime < DUPTIME && AVMATCH(&argv[argc], &server->filename))
            {
                printf("Duplicate request, skipping.\n");
                free(file);
            }
            else
            {
                printf("\n%s\n\n", cmd);
                fflush(stdout);
                server->filetime = now;
                free(server->filename.av_val);
                server->filename = argv[argc++];
                spawn_dumper(argc, argv, cmd);
            }

            free(cmd);
        }
#endif
        std::string file_path = storePath;
        file_path += r->Link.app.av_val;
        file_path += "/";

        char *path_ptr = r->Link.playpath.av_val;
        while (path_ptr != r->Link.playpath.av_val + r->Link.playpath.av_len)
        {
            if (path_ptr[0] == '%' && path_ptr[1] == '2' && path_ptr[2] == '0')
            {
                file_path.insert(file_path.end(), ' ');
                path_ptr += 3;
            }
            else
            {
                file_path += (*path_ptr++);
            }
        }
        uint32_t frame_rate = 25;

        // RTMP_SendCtrl(r, 0, 1, 0);
        SendStreamBegin(r);
        SendPlayStart(r);

        SendSampleAccess(r);
        SendMetaData(r);

        myprintf("play file = %s\n", file_path.c_str());
        StartPlaybackStoreFile(file_path, frame_rate, server, r, packet);

        RTMP_SendCtrl(r, 1, 1, 0);
        SendPlayStop(r);

        pc.m_body = server->connect;
        server->connect = NULL;
        RTMPPacket_Free(&pc);
        ret = 1;
    }
    else
    {
        myprintf("other method %s\n", method.av_val);
    }
    AMF_Reset(&obj);
    return ret;
}

int ServePacket(STREAMING_SERVER *server, RTMP *r, RTMPPacket *packet)
{
    int ret = 0;

    RTMP_Log(RTMP_LOGDEBUG, "%s, received packet type %02X, size %u bytes", __FUNCTION__,
             packet->m_packetType, packet->m_nBodySize);
    myprintf("%s, received packet type %02X, size %u bytes \n", __FUNCTION__,
             packet->m_packetType, packet->m_nBodySize);
    switch (packet->m_packetType)
    {
    case RTMP_PACKET_TYPE_CHUNK_SIZE:
        //      HandleChangeChunkSize(r, packet);
        break;

    case RTMP_PACKET_TYPE_BYTES_READ_REPORT:
        break;

    case RTMP_PACKET_TYPE_CONTROL:
        //      HandleCtrl(r, packet);
        break;

    case RTMP_PACKET_TYPE_SERVER_BW:
        //      HandleServerBW(r, packet);
        break;

    case RTMP_PACKET_TYPE_CLIENT_BW:
        //     HandleClientBW(r, packet);
        break;

    case RTMP_PACKET_TYPE_AUDIO:
        //RTMP_Log(RTMP_LOGDEBUG, "%s, received: audio %lu bytes", __FUNCTION__, packet.m_nBodySize);
        break;

    case RTMP_PACKET_TYPE_VIDEO:
        //RTMP_Log(RTMP_LOGDEBUG, "%s, received: video %lu bytes", __FUNCTION__, packet.m_nBodySize);
        break;

    case RTMP_PACKET_TYPE_FLEX_STREAM_SEND:
        break;

    case RTMP_PACKET_TYPE_FLEX_SHARED_OBJECT:
        break;

    case RTMP_PACKET_TYPE_FLEX_MESSAGE:
    {
        RTMP_Log(RTMP_LOGDEBUG, "%s, flex message, size %u bytes, not fully supported",
                 __FUNCTION__, packet->m_nBodySize);
        //RTMP_LogHex(packet.m_body, packet.m_nBodySize);

        // some DEBUG code
        /*RTMP_LIB_AMFObject obj;
       int nRes = obj.Decode(packet.m_body+1, packet.m_nBodySize-1);
       if(nRes < 0) {
       RTMP_Log(RTMP_LOGERROR, "%s, error decoding AMF3 packet", __FUNCTION__);
       //return;
       }

       obj.Dump(); */

        if ((ret = ServeInvoke(server, r, packet, 1)))
            RTMP_Close(r);
        break;
    }
    case RTMP_PACKET_TYPE_INFO:
        break;

    case RTMP_PACKET_TYPE_SHARED_OBJECT:
        break;

    case RTMP_PACKET_TYPE_INVOKE:
        RTMP_Log(RTMP_LOGDEBUG, "%s, received: invoke %u bytes", __FUNCTION__,
                 packet->m_nBodySize);
        //RTMP_LogHex(packet.m_body, packet.m_nBodySize);

        if ((ret = ServeInvoke(server, r, packet, 0)))
            RTMP_Close(r);
        break;

    case RTMP_PACKET_TYPE_FLASH_VIDEO:
        break;
    default:
        RTMP_Log(RTMP_LOGDEBUG, "%s, unknown packet type received: 0x%02x", __FUNCTION__,
                 packet->m_packetType);
#ifdef _DEBUG
        RTMP_LogHex(RTMP_LOGDEBUG, (uint8_t *)packet->m_body, packet->m_nBodySize);
#endif
    }
    fflush(stdout);
    return ret;
}

void doServe(STREAMING_SERVER *server, // server socket and state (our listening socket)
             int sockfd                // client connection socket
)
{
    server->state = STREAMING_IN_PROGRESS;

    RTMP *rtmp = RTMP_Alloc(); /* our session with the real client */
    RTMPPacket packet = {0};

    // timeout for http requests
    fd_set fds;
    struct timeval tv;

    int ret = 0;

    memset(&tv, 0, sizeof(struct timeval));
    tv.tv_sec = 5;

    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    if (select(sockfd + 1, &fds, NULL, NULL, &tv) <= 0)
    {
        RTMP_Log(RTMP_LOGERROR, "Request timeout/select failed, ignoring request");
        goto quit;
    }
    else
    {
        RTMP_Init(rtmp);
        rtmp->m_sb.sb_socket = sockfd;
        if (sslCtx && !RTMP_TLS_Accept(rtmp, sslCtx))
        {
            RTMP_Log(RTMP_LOGERROR, "TLS handshake failed");
            goto cleanup;
        }
        if (!RTMP_Serve(rtmp))
        {
            RTMP_Log(RTMP_LOGERROR, "Handshake failed");
            goto cleanup;
        }
    }
    server->arglen = 0;
    while (RTMP_IsConnected(rtmp) && RTMP_ReadPacket(rtmp, &packet))
    {
        if (!RTMPPacket_IsReady(&packet))
            continue;
        ret = ServePacket(server, rtmp, &packet);
        RTMPPacket_Free(&packet);
        if (ret)
            break;
    }

cleanup:
    RTMP_LogPrintf("Closing connection... ");
    RTMP_Close(rtmp);
    /* Should probably be done by RTMP_Close() ... */
    rtmp->Link.playpath.av_val = NULL;
    rtmp->Link.tcUrl.av_val = NULL;
    rtmp->Link.swfUrl.av_val = NULL;
    rtmp->Link.pageUrl.av_val = NULL;
    rtmp->Link.app.av_val = NULL;
    rtmp->Link.flashVer.av_val = NULL;
    if (rtmp->Link.usherToken.av_val)
    {
        free(rtmp->Link.usherToken.av_val);
        rtmp->Link.usherToken.av_val = NULL;
    }
    RTMP_Free(rtmp);
    close(sockfd);
    RTMP_LogPrintf("done!\n\n");

    is_get_first_key_frame = false;
    key_frame_cnt = 0;
quit:
    if (server->state == STREAMING_IN_PROGRESS)
        server->state = STREAMING_STOPPING;

    return;
}

TFTYPE
serverThread(void *arg)
{
    STREAMING_SERVER *server = (STREAMING_SERVER *)arg;
    server->state = STREAMING_ACCEPTING;

    while (server->state == STREAMING_ACCEPTING)
    {
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(struct sockaddr_in);
        int sockfd = accept(server->socket, (struct sockaddr *)&addr, &addrlen);

        if (sockfd > 0)
        {
#ifdef linux
            struct sockaddr_in dest;
            char destch[16];
            socklen_t destlen = sizeof(struct sockaddr_in);
            getsockopt(sockfd, SOL_IP, SO_ORIGINAL_DST, &dest, &destlen);
            strcpy(destch, inet_ntoa(dest.sin_addr));
            RTMP_Log(RTMP_LOGDEBUG, "%s: accepted connection from %s to %s\n", __FUNCTION__,
                     inet_ntoa(addr.sin_addr), destch);
#else
            RTMP_Log(RTMP_LOGDEBUG, "%s: accepted connection from %s\n", __FUNCTION__,
                     inet_ntoa(addr.sin_addr));
#endif
            /* Create a new thread and transfer the control to that */
            // doServe(server, sockfd);
            // RTMP_Log(RTMP_LOGDEBUG, "%s: processed request\n", __FUNCTION__);
            //            RtmpServerWork(sockfd);
            int pid = fork();
            if (0 == pid)
            {
                char buf[32];
                snprintf(buf, 32, "%d", sockfd);
                execl("./rtmp-server", "./rtmp-server", buf, nullptr);
            }
            else
            {
                waitpid(pid, nullptr, 0);
            }
        }
        else
        {
            RTMP_Log(RTMP_LOGERROR, "%s: accept failed", __FUNCTION__);
        }
    }
    server->state = STREAMING_STOPPED;

    TFRET();
}

STREAMING_SERVER *startStreaming(const char *address, int port)
{
    struct sockaddr_in addr;
    int sockfd, tmp;
    STREAMING_SERVER *server;

    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1)
    {
        RTMP_Log(RTMP_LOGERROR, "%s, couldn't create socket", __FUNCTION__);
        return 0;
    }

    tmp = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&tmp, sizeof(tmp));
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&tmp, sizeof(tmp));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(address); //htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) ==
        -1)
    {
        RTMP_Log(RTMP_LOGERROR, "%s, TCP bind failed for port number: %d", __FUNCTION__,
                 port);
        perror("");
        return 0;
    }

    if (listen(sockfd, 0) == -1)
    {
        RTMP_Log(RTMP_LOGERROR, "%s, listen failed", __FUNCTION__);
        closesocket(sockfd);
        return 0;
    }

    server = (STREAMING_SERVER *)calloc(1, sizeof(STREAMING_SERVER));
    if (!server)
        return 0;

    server->socket = sockfd;
    ThreadCreate(serverThread, server);

    return server;
}

void stopStreaming(STREAMING_SERVER *server)
{
    assert(server);

    if (server->state != STREAMING_STOPPED)
    {
        if (server->state == STREAMING_IN_PROGRESS)
        {
            server->state = STREAMING_STOPPING;

            // wait for streaming threads to exit
            while (server->state != STREAMING_STOPPED)
                msleep(1);
        }

        if (closesocket(server->socket))
            RTMP_Log(RTMP_LOGERROR, "%s: Failed to close listening socket, error %d",
                     __FUNCTION__, GetSockError());

        server->state = STREAMING_STOPPED;
    }
}

void sigIntHandler(int sig)
{
    RTMP_ctrlC = TRUE;
    auto iter = rtmpServerList.begin();
    STREAMING_SERVER *rtmpServer = 0;

    RTMP_LogPrintf("Caught signal: %d, cleaning up, just a second...\n", sig);
    while (iter != rtmpServerList.end())
    {
        rtmpServer = *iter;
        if (rtmpServer)
            stopStreaming(rtmpServer);
    }

    signal(SIGINT, SIG_DFL);
}

int RtmpServerStart(void)
{
    signal(SIGINT, sigIntHandler);
#ifndef WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    // http streaming server
    char DEFAULT_HTTP_STREAMING_DEVICE[] = "0.0.0.0"; // 0.0.0.0 is any device

    char *rtmpStreamingDevice = DEFAULT_HTTP_STREAMING_DEVICE; // streaming device, default 0.0.0.0
    int nRtmpStreamingPort = 1935;                             // port
    char *cert = NULL, *key = NULL;

    STREAMING_SERVER *rtmpServer = 0;

    RTMP_LogPrintf("RTMP Server %s\n", "v2.4");
    RTMP_LogPrintf("(c) 2010 Andrej Stepanchuk, Howard Chu; license: GPL\n\n");

    if (cert && key)
        sslCtx = RTMP_TLS_AllocServerContext(cert, key);

    InitSockets();

    RTMP_LogPrintf("Streaming on rtmp://%s:%d\n", rtmpStreamingDevice,
                   nRtmpStreamingPort);

    // start http streaming
    if ((rtmpServer = startStreaming(rtmpStreamingDevice, nRtmpStreamingPort)) == 0)
    {
        RTMP_Log(RTMP_LOGERROR, "Failed to start RTMP server, exiting!");
        return -1;
    }

    RTMP_Log(RTMP_LOGDEBUG, "Done, exiting...");

    if (sslCtx)
        RTMP_TLS_FreeServerContext(sslCtx);

    CleanupSockets();

    return 0;
}

int RtmpServerWork(int sockfd)
{
    STREAMING_SERVER *server;

    server = (STREAMING_SERVER *)calloc(1, sizeof(STREAMING_SERVER));
    if (!server)
        return -1;

    doServe(server, sockfd);

    free(server);
}

void AVreplace(AVal *src, const AVal *orig, const AVal *repl)
{
    char *srcbeg = src->av_val;
    char *srcend = src->av_val + src->av_len;
    char *dest, *sptr, *dptr;
    int n = 0;

    /* count occurrences of orig in src */
    sptr = src->av_val;
    while (sptr < srcend && (sptr = strstr(sptr, orig->av_val)))
    {
        n++;
        sptr += orig->av_len;
    }
    if (!n)
        return;

    dest = (char *)malloc(src->av_len + 1 + (repl->av_len - orig->av_len) * n);

    sptr = src->av_val;
    dptr = dest;
    while (sptr < srcend && (sptr = strstr(sptr, orig->av_val)))
    {
        n = sptr - srcbeg;
        memcpy(dptr, srcbeg, n);
        dptr += n;
        memcpy(dptr, repl->av_val, repl->av_len);
        dptr += repl->av_len;
        sptr += orig->av_len;
        srcbeg = sptr;
    }
    n = srcend - srcbeg;
    memcpy(dptr, srcbeg, n);
    dptr += n;
    *dptr = '\0';
    src->av_val = dest;
    src->av_len = dptr - dest;
}
