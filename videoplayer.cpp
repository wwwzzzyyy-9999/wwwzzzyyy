#include "videoplayer.h"
#include <QDebug>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QMutexLocker>
#include <QRegularExpression>
#include <cstring>

QMutex VideoPlayer::s_sdlMutex;
int VideoPlayer::s_sdlRefCount = 0;

#define FLUSH_DATA "FLUSH"
static const int64_t HLS_BACKWARD_SEEK_THRESHOLD_US = 500000;
// 视频落后音频超过该阈值（微秒）则丢帧显示；约 2 帧 @25fps
static const int64_t AV_SYNC_DROP_THRESHOLD_US = 40000;
// 视频包 PTS 落后音频超过该阈值则跳过整包不解码，加快追赶
static const int64_t AV_SYNC_PACKET_DROP_THRESHOLD_US = 200000;

static void resolveAudioChannelInfo(AVCodecContext *ctx, AVStream *st,
                                    int *channels, uint64_t *channelLayout)
{
    int ch = ctx ? ctx->channels : 0;
    uint64_t layout = ctx ? ctx->channel_layout : 0;
    if (st && st->codecpar) {
        if (ch <= 0)
            ch = st->codecpar->channels;
        if (layout == 0)
            layout = st->codecpar->channel_layout;
    }
    if (layout == 0 && ch > 0)
        layout = av_get_default_channel_layout(ch);
    if (ch <= 0 && layout != 0)
        ch = av_get_channel_layout_nb_channels(layout);
    if (ch <= 0) {
        ch = 2;
        layout = AV_CH_LAYOUT_STEREO;
    }
    *channels = ch;
    *channelLayout = layout;
}

static void resetVideoState(VideoState *vs, VideoPlayer *player)
{
    vs->pFormatCtx = nullptr;
    vs->audio_st = nullptr;
    vs->audioq = nullptr;
    vs->pAudioCodecCtx = nullptr;
    vs->audioStream = -1;
    vs->audioID = 0;
    vs->audioFrame = nullptr;
    vs->swr_ctx = nullptr;
    vs->video_st = nullptr;
    vs->videoq = nullptr;
    vs->pCodecCtx = nullptr;
    vs->videoStream = -1;
    vs->video_tid = nullptr;
    vs->audio_buf_size = 0;
    vs->audio_buf_index = 0;
    vs->start_time = 0;
    vs->m_player = player;

    vs->audio_clock_us.store(0, std::memory_order_relaxed);
    vs->video_clock_us.store(0, std::memory_order_relaxed);
    vs->isPause.store(false, std::memory_order_relaxed);
    vs->quit.store(false, std::memory_order_relaxed);
    vs->readFinished.store(false, std::memory_order_relaxed);
    vs->readThreadFinished.store(false, std::memory_order_relaxed);
    vs->videoThreadFinished.store(true, std::memory_order_relaxed);
    vs->seek_req.store(0, std::memory_order_relaxed);
    vs->seek_pos_us.store(0, std::memory_order_relaxed);
    vs->seek_flag_audio.store(0, std::memory_order_relaxed);
    vs->seek_flag_video.store(0, std::memory_order_relaxed);
    vs->seek_time_us.store(0, std::memory_order_relaxed);
    vs->hls_is_seeking.store(false, std::memory_order_relaxed);
}

static void closeAudioDevice(VideoState *vs)
{
    if (!vs || vs->audioID == 0)
        return;
    SDL_LockAudio();
    SDL_PauseAudioDevice(vs->audioID, 1);
    SDL_CloseAudioDevice(vs->audioID);
    SDL_UnlockAudio();
    vs->audioID = 0;
}

static void waitVideoThread(VideoState *vs)
{
    if (!vs || !vs->video_tid)
        return;
    int threadStatus = 0;
    SDL_WaitThread(vs->video_tid, &threadStatus);
    vs->video_tid = nullptr;
    while (!vs->videoThreadFinished.load(std::memory_order_relaxed))
        SDL_Delay(5);
}

static int64_t framePtsUs(AVStream *st, AVFrame *frame);
static int64_t packetPtsUs(AVFormatContext *ctx, AVPacket *pkt);
static int64_t synchronize_video(VideoState *is, AVFrame *src_frame, int64_t pts_us);

// SDL 原生线程：只负责从 videoq 解码并与 audio_clock 同步，不调用任何 Qt API
static int video_thread(void *arg)
{
    VideoState *is = static_cast<VideoState *>(arg);
    AVPacket pkt1;
    AVPacket *packet = &pkt1;
    AVFrame *pFrame = av_frame_alloc();
    AVFrame *pFrameRGB = av_frame_alloc();
    SwsContext *img_convert_ctx = nullptr;
    uint8_t *out_buffer_rgb = nullptr;
    int lastW = 0;
    int lastH = 0;

    if (!pFrame || !pFrameRGB || !is || !is->pCodecCtx) {
        av_frame_free(&pFrame);
        av_frame_free(&pFrameRGB);
        if (is)
            is->videoThreadFinished.store(true, std::memory_order_relaxed);
        return 0;
    }

    AVCodecContext *pCodecCtx = is->pCodecCtx;

    while (!is->quit.load(std::memory_order_relaxed)) {
        if (is->isPause.load(std::memory_order_relaxed)) {
            SDL_Delay(10);
            continue;
        }

        if (packet_queue_get(is->videoq, packet, 0) <= 0) {
            if (is->seek_flag_video.load(std::memory_order_relaxed) != 1
                && is->readFinished.load(std::memory_order_relaxed)
                && (!is->audioq || is->audioq->nb_packets == 0)) {
                break;
            }
            SDL_Delay(5);
            continue;
        }

        if (packet->size >= 6 && memcmp(packet->data, FLUSH_DATA, 6) == 0) {
            avcodec_flush_buffers(pCodecCtx);
            av_packet_unref(packet);
            if (is->seek_flag_video.load(std::memory_order_relaxed))
                is->video_clock_us.store(is->seek_time_us.load(), std::memory_order_relaxed);
            else
                is->video_clock_us.store(0, std::memory_order_relaxed);
            continue;
        }

        // 落后追赶：整包 PTS 已远落后于音频，跳过不解码
        if (is->audioStream >= 0 && is->pFormatCtx) {
            const int64_t audio_pts = is->audio_clock_us.load(std::memory_order_relaxed);
            const int64_t pkt_pts = packetPtsUs(is->pFormatCtx, packet);
            if (pkt_pts >= 0 && audio_pts > 0
                && pkt_pts + AV_SYNC_PACKET_DROP_THRESHOLD_US < audio_pts) {
                is->video_clock_us.store(pkt_pts, std::memory_order_relaxed);
                av_packet_unref(packet);
                continue;
            }
        }

        // 音视频同步：视频超前时等待音频，仅在视频线程内阻塞
        while (!is->quit.load(std::memory_order_relaxed)) {
            if (is->audioStream < 0 || !is->audioq || is->audioq->nb_packets == 0)
                break;
            const int64_t audio_pts = is->audio_clock_us.load(std::memory_order_relaxed);
            const int64_t video_pts = is->video_clock_us.load(std::memory_order_relaxed);
            if (video_pts <= audio_pts || video_pts < 0)
                break;
            SDL_Delay(5);
        }

        if (avcodec_send_packet(pCodecCtx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
            if (pFrame->width <= 0 || pFrame->height <= 0)
                continue;

            int64_t video_pts = framePtsUs(is->video_st, pFrame);
            if (video_pts >= 0)
                video_pts = synchronize_video(is, pFrame, video_pts);

            if (is->seek_flag_video.load(std::memory_order_relaxed)) {
                if (video_pts >= 0
                    && video_pts < is->seek_time_us.load(std::memory_order_relaxed)) {
                    continue;
                }
                is->seek_flag_video.store(0, std::memory_order_relaxed);
            }

            // 落后追赶：帧 PTS 已过显示时间，丢弃不渲染（在 sws_scale 前省 CPU）
            if (is->audioStream >= 0) {
                const int64_t audio_pts = is->audio_clock_us.load(std::memory_order_relaxed);
                if (video_pts >= 0 && audio_pts > 0
                    && video_pts + AV_SYNC_DROP_THRESHOLD_US < audio_pts) {
                    continue;
                }
            }

            if (!img_convert_ctx || lastW != pFrame->width || lastH != pFrame->height) {
                lastW = pFrame->width;
                lastH = pFrame->height;
                if (img_convert_ctx)
                    sws_freeContext(img_convert_ctx);
                img_convert_ctx = sws_getContext(
                    pFrame->width, pFrame->height,
                    static_cast<AVPixelFormat>(pFrame->format),
                    pFrame->width, pFrame->height,
                    AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                if (!img_convert_ctx)
                    continue;
                av_freep(&out_buffer_rgb);
                const int numBytes = av_image_get_buffer_size(
                    AV_PIX_FMT_BGRA, pFrame->width, pFrame->height, 1);
                out_buffer_rgb = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(numBytes)));
                if (!out_buffer_rgb)
                    continue;
                av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, out_buffer_rgb,
                                     AV_PIX_FMT_BGRA, pFrame->width, pFrame->height, 1);
            }

            sws_scale(img_convert_ctx,
                      pFrame->data, pFrame->linesize, 0, pFrame->height,
                      pFrameRGB->data, pFrameRGB->linesize);
            if (!is->quit.load(std::memory_order_relaxed) && is->m_player) {
                QImage tmpImg(out_buffer_rgb, pFrame->width, pFrame->height,
                              pFrameRGB->linesize[0], QImage::Format_RGB32);
                is->m_player->submitFrame(tmpImg.copy());
            }
        }
    }

    if (img_convert_ctx)
        sws_freeContext(img_convert_ctx);
    av_frame_free(&pFrame);
    av_frame_free(&pFrameRGB);
    av_freep(&out_buffer_rgb);
    is->videoThreadFinished.store(true, std::memory_order_relaxed);
    return 0;
}

bool VideoPlayer::initSdl()
{
    QMutexLocker lock(&s_sdlMutex);
    if (s_sdlRefCount == 0) {
        if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
            qDebug() << "SDL_Init failed:" << SDL_GetError();
            return false;
        }
    }
    ++s_sdlRefCount;
    return true;
}

void VideoPlayer::quitSdl()
{
    QMutexLocker lock(&s_sdlMutex);
    if (s_sdlRefCount > 0 && --s_sdlRefCount == 0) {
        SDL_Quit();
    }
}

static void enqueueFlushPacket(PacketQueue *queue)
{
    if (!queue)
        return;
    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
        return;
    av_new_packet(pkt, 10);
    memcpy(pkt->data, FLUSH_DATA, strlen(FLUSH_DATA) + 1);
    packet_queue_flush(queue);
    if (packet_queue_put(queue, pkt) < 0) {
        av_packet_unref(pkt);
        av_packet_free(&pkt);
    }
}

//回调函数
void audio_callback(void *userdata, Uint8 *stream, int len);
//解码函数
int audio_decode_frame(VideoState *pcodec_ctx, uint8_t *audio_buf, int buf_size);
//找 auto_stream
int find_stream_index(AVFormatContext *pformat_ctx, int *video_stream, int* audio_stream);
// 从 AVIOContext 读取一行（替代不存在的 avio_get_line）
static int read_line_from_avio(AVIOContext *ioCtx, unsigned char *buf, int buf_size)
{
    int i = 0;
    unsigned char c;
    while (i < buf_size - 1) {
        if (avio_read(ioCtx, &c, 1) <= 0)
            break;
        buf[i++] = c;
        if (c == '\n')
            break;
    }
    buf[i] = '\0';
    return i;
}

static int64_t framePtsUs(AVStream *st, AVFrame *frame)
{
    if (!st || !frame)
        return -1;
    int64_t pts = frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
        pts = frame->pkt_dts;
    if (pts == AV_NOPTS_VALUE)
        return -1;
    return static_cast<int64_t>(pts * av_q2d(st->time_base) * 1000000.0);
}

// 将 AVPacket PTS 转为微秒；-1 表示无效
static int64_t packetPtsUs(AVFormatContext *ctx, AVPacket *pkt)
{
    if (!ctx || !pkt || pkt->pts == AV_NOPTS_VALUE)
        return -1;
    int idx = pkt->stream_index;
    if (idx < 0 || idx >= (int)ctx->nb_streams)
        return -1;
    return (int64_t)(pkt->pts * av_q2d(ctx->streams[idx]->time_base) * 1000000.0);
}

VideoPlayer::VideoPlayer()
{
    m_playerState = PlayerState::Stop;
    m_isHls = false;
    m_hlsSegList.clear();
    m_hlsSeekTarget = -1;
    m_hlsDurationUs = 0;
}

static int64_t synchronize_video(VideoState *is, AVFrame *src_frame, int64_t pts_us)
{
    double frame_delay_us = av_q2d(is->pCodecCtx->time_base) * 1000000.0;
    frame_delay_us += src_frame->repeat_pict * (frame_delay_us * 0.5);

    if (pts_us != 0) {
        is->video_clock_us.store(pts_us, std::memory_order_relaxed);
    } else {
        pts_us = is->video_clock_us.load(std::memory_order_relaxed);
    }
    const int64_t next = is->video_clock_us.load(std::memory_order_relaxed)
                         + static_cast<int64_t>(frame_delay_us);
    is->video_clock_us.store(next, std::memory_order_relaxed);
    return pts_us;
}



void VideoPlayer::submitFrame(QImage img)
{
    if (m_videoState.quit.load(std::memory_order_relaxed))
        return;
    QMutexLocker lock(&m_displayMutex);
    m_displayFrame = std::move(img);
}

bool VideoPlayer::takeDisplayFrame(QImage *out)
{
    if (!out)
        return false;
    QMutexLocker lock(&m_displayMutex);
    if (m_displayFrame.isNull())
        return false;
    *out = m_displayFrame;
    return true;
}

VideoPlayer::~VideoPlayer()
{
    stopInternal(true, false);
}

void VideoPlayer::play()
{
    m_videoState.isPause = false;
    if( m_playerState != Pause) return;
    m_playerState = Playing;
}

void VideoPlayer::pause()
{
    m_videoState.isPause = true;
    if( m_playerState != Playing ) return;
    m_playerState = Pause;
}
//跳转
void VideoPlayer::seek(int64_t pos) //精确到微秒
{
    if (pos < 0)
        return;

    m_videoState.isPause = false;

    if (m_isHls) {
        ensureHlsPlaylistParsed();
        int64_t cur = (int64_t)getCurrentTime();
        // 明显向后跳：重开 demuxer，读线程内 skip 到目标（不触发 UI Stop）
        if (cur > 0 && pos < cur - HLS_BACKWARD_SEEK_THRESHOLD_US) {
            qDebug() << "HLS seek reopen, target us =" << pos
                     << "seg =" << findHlsSegmentForTime(pos);
            rebuildHlsFromSegment(pos);
            if (isRunning())
                stopInternal(true, false);
            m_playerState = PlayerState::Seeking;
            Q_EMIT SIG_PlayerStateChanged(PlayerState::Seeking);
            start();
            return;
        }
    }

    m_videoState.seek_pos_us.store(pos, std::memory_order_relaxed);
    m_videoState.seek_time_us.store(pos, std::memory_order_relaxed);
    if (m_isHls) {
        m_videoState.hls_is_seeking = true;
        if (m_playerState != PlayerState::Seeking) {
            m_playerState = PlayerState::Seeking;
            Q_EMIT SIG_PlayerStateChanged(PlayerState::Seeking);
        }
    } else {
        m_playerState = Playing;
    }
    m_videoState.seek_req = 1;
}

void VideoPlayer::stopInternal(bool isWait, bool notifyStop)
{
    m_videoState.quit.store(true, std::memory_order_relaxed);
    if (isWait) {
        while (m_videoState.videoStream != -1 && !m_videoState.readThreadFinished.load()) {
            SDL_Delay(10);
        }
        if (QThread::isRunning())
            wait();
    }
    m_videoState.videoStream = -1;
    m_videoState.audioStream = -1;
    if (notifyStop) {
        m_playerState = PlayerState::Stop;
        Q_EMIT SIG_PlayerStateChanged(PlayerState::Stop);
    }
}

void VideoPlayer::stop(bool isWait)
{
    stopInternal(isWait, true);
}

void VideoPlayer::ensureHlsPlaylistParsed()
{
    if (!m_isHls || !m_hlsSegList.isEmpty())
        return;
    m_hlsDurationUs = parseHlsDuration(m_fileName);
}

int VideoPlayer::findHlsSegmentForTime(int64_t posUs) const
{
    for (int i = 0; i < m_hlsSegList.size(); ++i) {
        const HlsSegInfo &seg = m_hlsSegList.at(i);
        if (posUs >= seg.startTime && posUs < seg.startTime + seg.duration)
            return i;
    }
    if (!m_hlsSegList.isEmpty() && posUs >= m_hlsSegList.last().startTime)
        return m_hlsSegList.size() - 1;
    return -1;
}

void VideoPlayer::finishSeekingUi()
{
    if (m_playerState == PlayerState::Seeking) {
        m_playerState = PlayerState::Playing;
        Q_EMIT SIG_PlayerStateChanged(PlayerState::Playing);
    }
}

void VideoPlayer::rebuildHlsFromSegment(int64_t seekPosUs)
{
    m_hlsSeekTarget = seekPosUs;
}

PlayerState VideoPlayer::playerState() const
{
    return m_playerState;
}



#define MAX_AUDIO_SIZE (1024*16*25*10)
#define MAX_VIDEO_PACKETS 128

void VideoPlayer::run()
{
    AVFormatContext *pFormatCtx = nullptr;
    AVCodecContext *pCodecCtx = nullptr;
    AVCodecContext *pAudioCodecCtx = nullptr;
    AVPacket *packet = nullptr;
    int videoStream = -1;
    int audioStream = -1;
    bool sdlStarted = false;
    int64_t hls_skip_until_us = 0;
    int DelayCount = 0;
    std::string filePathUtf8;

    if (!initSdl()) {
        qDebug() << "Couldn't init SDL";
        return;
    }
    sdlStarted = true;

    resetVideoState(&m_videoState, this);

    pFormatCtx = avformat_alloc_context();
    if (!pFormatCtx) {
        qDebug() << "Could not allocate format context";
        goto cleanup;
    }

    filePathUtf8 = m_fileName.toStdString();
    {
        AVDictionary *opts = nullptr;
        if (m_isHls || m_fileName.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || m_fileName.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
            av_dict_set(&opts, "user_agent", "VideoRecorderPlayer/1.0", 0);
            av_dict_set(&opts, "protocol_whitelist", "file,http,https,tcp,tls,crypto", 0);
            av_dict_set(&opts, "reconnect", "1", 0);
            av_dict_set(&opts, "reconnect_streamed", "1", 0);
            av_dict_set(&opts, "reconnect_delay_max", "5", 0);
        }
        const int openRet = avformat_open_input(&pFormatCtx, filePathUtf8.c_str(), nullptr, &opts);
        av_dict_free(&opts);
        if (openRet != 0) {
            char errbuf[256];
            av_strerror(openRet, errbuf, sizeof(errbuf));
            qDebug() << "can't open file:" << m_fileName << "ret=" << openRet << errbuf;
            Q_EMIT SIG_playbackError(
                QStringLiteral("无法打开视频源，请确认 HLS 地址可访问：\n%1").arg(m_fileName));
            goto cleanup;
        }
    }

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        qDebug() << "Couldn't find stream information";
        Q_EMIT SIG_playbackError(QStringLiteral("无法解析视频流信息"));
        goto cleanup;
    }

    if (find_stream_index(pFormatCtx, &videoStream, &audioStream) < 0
        || videoStream < 0) {
        qDebug() << "Couldn't find stream index, video=" << videoStream
                 << "audio=" << audioStream;
        Q_EMIT SIG_playbackError(QStringLiteral("未找到可播放的视频流"));
        goto cleanup;
    }

    if (m_isHls && m_hlsSegList.isEmpty()) {
        if (pFormatCtx->duration > 0)
            m_hlsDurationUs = pFormatCtx->duration;
        else
            m_hlsDurationUs = parseHlsDuration(m_fileName);
        qDebug() << "HLS duration us =" << m_hlsDurationUs;
    }

    qDebug() << "streams ready, video=" << videoStream << "audio=" << audioStream;
    m_videoState.pFormatCtx = pFormatCtx;
    m_videoState.videoStream = videoStream;
    m_videoState.audioStream = audioStream;

    if (m_hlsSeekTarget >= 0) {
        hls_skip_until_us = m_hlsSeekTarget;
        m_videoState.audio_clock_us.store(m_hlsSeekTarget, std::memory_order_relaxed);
        m_videoState.video_clock_us.store(m_hlsSeekTarget, std::memory_order_relaxed);
        m_hlsSeekTarget = -1;
    }

    packet = av_packet_alloc();
    if (!packet)
        goto cleanup;

    if (videoStream != -1) {
        pCodecCtx = avcodec_alloc_context3(nullptr);
        if (!pCodecCtx)
            goto cleanup;
        if (avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar) < 0) {
            qDebug() << "Could not copy codec parameters";
            goto cleanup;
        }
        AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
        if (!pCodec) {
            qDebug() << "Codec not found";
            goto cleanup;
        }
        if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
            qDebug() << "Could not open codec";
            goto cleanup;
        }
        m_videoState.video_st = pFormatCtx->streams[videoStream];
        m_videoState.pCodecCtx = pCodecCtx;
        m_videoState.videoq = new PacketQueue;
        packet_queue_init(m_videoState.videoq);
        m_videoState.videoThreadFinished.store(false, std::memory_order_relaxed);
        m_videoState.video_tid = SDL_CreateThread(video_thread, "video_thread", &m_videoState);
        if (!m_videoState.video_tid) {
            qDebug() << "Failed to create video thread:" << SDL_GetError();
            goto cleanup;
        }
        qDebug() << "player: video thread started";
    }

    if (audioStream != -1) {
        SDL_AudioSpec wanted_spec;
        SDL_AudioSpec spec;

        pAudioCodecCtx = avcodec_alloc_context3(nullptr);
        if (!pAudioCodecCtx) {
            qDebug() << "Could not allocate audio codec context";
            goto cleanup;
        }
        if (avcodec_parameters_to_context(pAudioCodecCtx, pFormatCtx->streams[audioStream]->codecpar) < 0) {
            qDebug() << "Could not copy audio codec parameters";
            goto cleanup;
        }
        AVCodec *pAudioCodec = avcodec_find_decoder(pAudioCodecCtx->codec_id);
        if (!pAudioCodec) {
            qDebug() << "Couldn't find audio decoder";
            goto cleanup;
        }
        if (avcodec_open2(pAudioCodecCtx, pAudioCodec, nullptr) < 0) {
            qDebug() << "Could not open audio codec";
            goto cleanup;
        }

        m_videoState.audio_st = pFormatCtx->streams[audioStream];
        m_videoState.pAudioCodecCtx = pAudioCodecCtx;

        SDL_LockAudio();
        wanted_spec.freq = pAudioCodecCtx->sample_rate;
        if (wanted_spec.freq <= 0)
            wanted_spec.freq = 44100;
        switch (pAudioCodecCtx->sample_fmt) {
        case AV_SAMPLE_FMT_U8:
            wanted_spec.format = AUDIO_S8;
            m_videoState.out_frame.format = AV_SAMPLE_FMT_U8;
            break;
        case AV_SAMPLE_FMT_S16:
            wanted_spec.format = AUDIO_S16SYS;
            m_videoState.out_frame.format = AV_SAMPLE_FMT_S16;
            break;
        default:
            wanted_spec.format = AUDIO_S16SYS;
            m_videoState.out_frame.format = AV_SAMPLE_FMT_S16;
            break;
        }
        int audioChannels = 0;
        uint64_t inChannelLayout = 0;
        resolveAudioChannelInfo(pAudioCodecCtx, m_videoState.audio_st,
                                &audioChannels, &inChannelLayout);

        wanted_spec.channels = 2;
        wanted_spec.silence = 0;
        wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = &m_videoState;

        m_videoState.audioID = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, 0);
        if (m_videoState.audioID < 1) {
            SDL_UnlockAudio();
            qDebug() << "Couldn't open Audio:" << SDL_GetError();
            goto cleanup;
        }

        m_videoState.out_frame.sample_rate = spec.freq > 0 ? spec.freq : wanted_spec.freq;
        m_videoState.out_frame.channels = 2;
        m_videoState.out_frame.channel_layout = AV_CH_LAYOUT_STEREO;
        if (inChannelLayout == 0)
            inChannelLayout = AV_CH_LAYOUT_STEREO;
        m_videoState.audioq = new PacketQueue;
        packet_queue_init(m_videoState.audioq);
        m_videoState.audioFrame = av_frame_alloc();
        m_videoState.swr_ctx = swr_alloc_set_opts(nullptr,
            m_videoState.out_frame.channel_layout,
            static_cast<AVSampleFormat>(m_videoState.out_frame.format),
            m_videoState.out_frame.sample_rate,
            inChannelLayout,
            pAudioCodecCtx->sample_fmt,
            pAudioCodecCtx->sample_rate,
            0, nullptr);
        if (!m_videoState.swr_ctx || swr_init(m_videoState.swr_ctx) < 0) {
            qDebug() << "swr_init error at init";
            swr_free(&m_videoState.swr_ctx);
            m_videoState.swr_ctx = nullptr;
        }
        SDL_UnlockAudio();
        SDL_PauseAudioDevice(m_videoState.audioID, 0);
        qDebug() << "player: audio device ready";
    }

    m_playerState = PlayerState::Playing;
    Q_EMIT SIG_PlayerStateChanged(PlayerState::Playing);
    Q_EMIT SIG_TotalTime(getTotalTime());
    qDebug() << "player: entering read loop";

    while (!m_videoState.quit.load(std::memory_order_relaxed)) {
        const bool audioFull = m_videoState.audioStream != -1 && m_videoState.audioq
            && m_videoState.audioq->size > MAX_AUDIO_SIZE;
        const bool videoFull = m_videoState.videoStream != -1 && m_videoState.videoq
            && m_videoState.videoq->nb_packets >= MAX_VIDEO_PACKETS;
        if (audioFull || videoFull) {
            SDL_Delay(10);
            continue;
        }

        if (m_videoState.seek_req.load(std::memory_order_relaxed)) {
            const int64_t seekPosUs = m_videoState.seek_pos_us.load(std::memory_order_relaxed);

            if (m_videoState.hls_is_seeking.load(std::memory_order_relaxed)) {
                int stream_index = -1;
                if (m_videoState.videoStream >= 0)
                    stream_index = m_videoState.videoStream;
                else if (m_videoState.audioStream >= 0)
                    stream_index = m_videoState.audioStream;

                int seek_ret = avformat_seek_file(pFormatCtx, -1, INT64_MIN, seekPosUs, INT64_MAX, 0);
                if (seek_ret < 0 && stream_index >= 0) {
                    AVRational avTimeBase = {1, AV_TIME_BASE};
                    const int64_t seekTarget = av_rescale_q(
                        seekPosUs, avTimeBase, pFormatCtx->streams[stream_index]->time_base);
                    seek_ret = av_seek_frame(pFormatCtx, stream_index, seekTarget, AVSEEK_FLAG_BACKWARD);
                }
                if (seek_ret < 0)
                    hls_skip_until_us = seekPosUs;

                m_videoState.audio_clock_us.store(seekPosUs, std::memory_order_relaxed);
                m_videoState.video_clock_us.store(seekPosUs, std::memory_order_relaxed);

                if (m_videoState.audioStream >= 0)
                    enqueueFlushPacket(m_videoState.audioq);
                if (m_videoState.videoStream >= 0 && m_videoState.videoq)
                    enqueueFlushPacket(m_videoState.videoq);
                if (pCodecCtx)
                    m_videoState.video_clock_us.store(0, std::memory_order_relaxed);

                m_videoState.seek_flag_audio.store(1, std::memory_order_relaxed);
                m_videoState.seek_flag_video.store(1, std::memory_order_relaxed);
                m_videoState.seek_time_us.store(seekPosUs, std::memory_order_relaxed);
                m_videoState.hls_is_seeking.store(false, std::memory_order_relaxed);
                m_videoState.seek_req.store(0, std::memory_order_relaxed);
                finishSeekingUi();
                continue;
            }

            int stream_index = -1;
            int64_t seek_target = seekPosUs;
            if (m_videoState.videoStream >= 0)
                stream_index = m_videoState.videoStream;
            else if (m_videoState.audioStream >= 0)
                stream_index = m_videoState.audioStream;

            AVRational avTimeBase = {1, AV_TIME_BASE};
            if (stream_index >= 0) {
                seek_target = av_rescale_q(seek_target, avTimeBase,
                                           pFormatCtx->streams[stream_index]->time_base);
            }

            int seek_ret = av_seek_frame(pFormatCtx, stream_index, seek_target, AVSEEK_FLAG_BACKWARD);
            if (seek_ret < 0)
                seek_ret = av_seek_frame(pFormatCtx, stream_index, seek_target, 0);

            if (seek_ret >= 0) {
                m_videoState.seek_time_us.store(seekPosUs, std::memory_order_relaxed);
                m_videoState.audio_clock_us.store(seekPosUs, std::memory_order_relaxed);
                m_videoState.video_clock_us.store(seekPosUs, std::memory_order_relaxed);
                m_videoState.seek_flag_audio.store(1, std::memory_order_relaxed);
                m_videoState.seek_flag_video.store(1, std::memory_order_relaxed);

                if (m_videoState.audioStream >= 0)
                    enqueueFlushPacket(m_videoState.audioq);
                if (m_videoState.videoStream >= 0 && m_videoState.videoq)
                    enqueueFlushPacket(m_videoState.videoq);
                if (pCodecCtx)
                    m_videoState.video_clock_us.store(0, std::memory_order_relaxed);
            }
            m_videoState.seek_req.store(0, std::memory_order_relaxed);
        }

        if (av_read_frame(pFormatCtx, packet) < 0) {
            DelayCount++;
            if (DelayCount >= 300) {
                if (m_videoState.seek_flag_video.load() != 1
                    && m_videoState.seek_flag_audio.load() != 1) {
                    m_videoState.readFinished.store(true, std::memory_order_relaxed);
                    DelayCount = 0;
                }
            }
            if (m_videoState.quit.load(std::memory_order_relaxed))
                break;
            SDL_Delay(10);
            continue;
        }
        DelayCount = 0;

        if (hls_skip_until_us > 0) {
            const int64_t ptsUs = packetPtsUs(pFormatCtx, packet);
            if (ptsUs >= 0 && ptsUs < hls_skip_until_us) {
                av_packet_unref(packet);
                continue;
            }
            if (ptsUs >= hls_skip_until_us) {
                hls_skip_until_us = 0;
                m_videoState.audio_clock_us.store(ptsUs, std::memory_order_relaxed);
                m_videoState.video_clock_us.store(ptsUs, std::memory_order_relaxed);
                finishSeekingUi();
            }
            if (hls_skip_until_us > 0) {
                av_packet_unref(packet);
                continue;
            }
        }

        if (packet->stream_index == m_videoState.videoStream) {
            packet_queue_put(m_videoState.videoq, packet);
        } else if (packet->stream_index == m_videoState.audioStream) {
            packet_queue_put(m_videoState.audioq, packet);
        } else {
            av_packet_unref(packet);
        }
    }

cleanup:
    m_videoState.quit.store(true, std::memory_order_relaxed);
    closeAudioDevice(&m_videoState);
    waitVideoThread(&m_videoState);

    if (m_videoState.videoStream != -1 && m_videoState.videoq) {
        packet_queue_flush(m_videoState.videoq);
        delete m_videoState.videoq;
        m_videoState.videoq = nullptr;
    }
    if (m_videoState.audioStream != -1 && m_videoState.audioq) {
        packet_queue_flush(m_videoState.audioq);
        delete m_videoState.audioq;
        m_videoState.audioq = nullptr;
    }

    if (m_videoState.swr_ctx) {
        swr_free(&m_videoState.swr_ctx);
        m_videoState.swr_ctx = nullptr;
    }
    if (m_videoState.audioFrame) {
        av_frame_free(&m_videoState.audioFrame);
        m_videoState.audioFrame = nullptr;
    }
    if (pAudioCodecCtx)
        avcodec_free_context(&pAudioCodecCtx);
    if (pCodecCtx)
        avcodec_free_context(&pCodecCtx);
    if (packet)
        av_packet_free(&packet);
    if (pFormatCtx)
        avformat_close_input(&pFormatCtx);

    m_videoState.pFormatCtx = nullptr;
    m_videoState.pCodecCtx = nullptr;
    m_videoState.pAudioCodecCtx = nullptr;
    m_videoState.videoStream = -1;
    m_videoState.audioStream = -1;
    m_videoState.readThreadFinished.store(true, std::memory_order_relaxed);

    m_playerState = PlayerState::Stop;
    Q_EMIT SIG_PlayerStateChanged(PlayerState::Stop);

    if (sdlStarted)
        quitSdl();
}

double VideoPlayer::getCurrentTime()
{
    return static_cast<double>(m_videoState.audio_clock_us.load(std::memory_order_relaxed));
}

int64_t VideoPlayer::getTotalTime()
{
    if( !m_videoState.pFormatCtx ) return -1;
    int64_t dur = m_videoState.pFormatCtx->duration;
    // 尝试从视频流获取
    if( dur <= 0 && m_videoState.videoStream >= 0 )
    {
        AVStream* st = m_videoState.pFormatCtx->streams[m_videoState.videoStream];
        if( st && st->duration > 0 )
        {
            double tb = av_q2d(st->time_base);
            dur = (int64_t)(st->duration * tb * 1000000);
        }
    }
    if (m_isHls && m_hlsDurationUs > 0)
        return m_hlsDurationUs;
    if (dur <= 0 && m_isHls) {
        ensureHlsPlaylistParsed();
        dur = m_hlsDurationUs;
    }
    return dur;
}

// 解析 HLS m3u8 文件，计算总时长（微秒）
// 同时填充 m_hlsSegList，供 HLS 跳转使用
int64_t VideoPlayer::parseHlsDuration(const QString& m3u8Url)
{
    AVIOContext* ioCtx = nullptr;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "user_agent", "VideoRecorderPlayer/1.0", 0);
    av_dict_set(&opts, "protocol_whitelist", "file,http,https,tcp,tls,crypto", 0);
    if (avio_open2(&ioCtx, m3u8Url.toUtf8().constData(), AVIO_FLAG_READ, nullptr, &opts) < 0) {
        av_dict_free(&opts);
        qDebug() << "parseHlsDuration: cannot open" << m3u8Url;
        return 0;
    }
    av_dict_free(&opts);

    m_hlsSegList.clear();
    int64_t totalDur = 0;
    char line[4096];
    double segDur = 0.0;
    const QUrl playlistUrl(m3u8Url);

    while( 1 )
    {
        int ret = read_line_from_avio(ioCtx, (unsigned char*)line, sizeof(line));
        if( ret <= 0 ) break;
        QString s = QString::fromUtf8(line).trimmed();
        if (s.startsWith("#EXTINF", Qt::CaseInsensitive)) {
            const QRegularExpression rx(QStringLiteral("([0-9]+\\.?[0-9]*)"));
            const QRegularExpressionMatch match = rx.match(s);
            if (match.hasMatch())
                segDur = match.captured(1).toDouble();
        }
        else if( !s.startsWith("#") && !s.isEmpty() )
        {
            HlsSegInfo info;
            if (s.startsWith("http://", Qt::CaseInsensitive)
                || s.startsWith("https://", Qt::CaseInsensitive)) {
                info.url = s;
            } else {
                info.url = playlistUrl.resolved(QUrl(s)).toString();
            }
            info.duration = (int64_t)(segDur * 1000000);
            info.startTime = totalDur;
            m_hlsSegList.append(info);

            totalDur += info.duration;
            segDur = 0.0;
        }
    }
    avio_close(ioCtx);
    qDebug() << "parseHlsDuration:" << m_hlsSegList.size() << "segments, total"
             << totalDur/1000000.0 << "s";
    return totalDur;
}
void VideoPlayer::setFileName(const QString &newFileName)
{
    if (isRunning())
        stopInternal(true, false);

    m_playerState = PlayerState::Stop;
    m_fileName = newFileName;
    m_isHls = newFileName.contains(".m3u8", Qt::CaseInsensitive);
    m_hlsSegList.clear();
    m_hlsDurationUs = 0;
    m_hlsSeekTarget = -1;
    QMutexLocker lock(&m_displayMutex);
    m_displayFrame = QImage();
}

//13.回调函数中将从队列中取数据, 解码后填充到播放缓冲区.
void audio_callback(void *userdata, Uint8 *stream, int len)
{
    // AVCodecContext *pcodec_ctx = (AVCodecContext *) userdata;
    VideoState * is = (VideoState *) userdata;
    if (!is || is->quit.load(std::memory_order_relaxed)) {
            memset(stream, 0, len);
            return;
    }
    int len1, audio_data_size;
    memset( stream , 0 , len);
    if(is->isPause.load(std::memory_order_relaxed)) return;

    // static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
    // static unsigned int audio_buf_size = 0;
    // static unsigned int audio_buf_index = 0;
    /* len 是由 SDL 传入的 SDL 缓冲区的大小，如果这个缓冲未满，我们就一直往里填充数据 */
    /* audio_buf_index 和 audio_buf_size 标示我们自己用来放置解码出来的数据的缓冲区，*/
    // 这些数据待 copy 到 SDL 缓冲区；缓冲空时需 audio_decode_frame 解码更多帧
    while (len > 0)
    {
        if (is->audio_buf_index >= is->audio_buf_size) {
            audio_data_size = audio_decode_frame( is ,is->audio_buf,sizeof(is->audio_buf));
            /* audio_data_size < 0 标示没能解码出数据，我们默认播放静音 */
            if (audio_data_size <= 0) {
                /* silence */
                is->audio_buf_size = 1024;
                /* 清零，静音 */
                memset(is->audio_buf, 0, is->audio_buf_size);
            } else {
                is->audio_buf_size = audio_data_size;
            }
            is->audio_buf_index = 0;
        }
        /* 查看 stream 可用空间，决定一次 copy 多少数据，剩下的下次继续 copy */
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 <= 0)
            break;
        if (len1 > len) {
            len1 = len;
        }
        memset( stream , 0 , len1);
        //混音函数 sdl 2.0 版本使用该函数 替换 SDL_MixAudio
        SDL_MixAudioFormat(stream, (uint8_t *) is->audio_buf + is->audio_buf_index,
                           AUDIO_S16SYS,len1,100);
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}

//对于音频来说，一个 packet 里面，可能含有多帧(frame)数据。
//解码音频帧函数
int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size)
{
    AVPacket pkt;
    int audio_pkt_size = 0;
    int len1, data_size;
    int sampleSize = 0;
    AVCodecContext *aCodecCtx = is->pAudioCodecCtx;
    AVFrame *audioFrame = is->audioFrame/*av_frame_alloc()*/;
    PacketQueue *audioq = is->audioq;
    AVFrame wanted_frame = is->out_frame;
    if( !aCodecCtx|| !audioFrame ||!audioq) return -1;
    /*swr_ctx 已在 run() 中初始化，此处直接复用*/
    struct SwrContext *swr_ctx = is->swr_ctx;
    int convert_len;
    int n = 0;
    for(;;)
    {
        if( is->quit.load(std::memory_order_relaxed)) return -1;
        if( is->isPause.load(std::memory_order_relaxed)) return -1;
        if( !audioq ) return -1;
        if(packet_queue_get(audioq, &pkt, 0) <= 0) //一定注意
        {
            if ((is->seek_flag_audio.load() != 1) && is->readFinished.load()
                && is->audioq->nb_packets == 0)
                is->quit.store(true, std::memory_order_relaxed);
            return -1;
        }

        if (pkt.size >= 6 && memcmp(pkt.data, FLUSH_DATA, 6) == 0)
        {
            avcodec_flush_buffers(is->pAudioCodecCtx);
            if (is->seek_flag_audio.load())
                is->audio_clock_us.store(is->seek_time_us.load(), std::memory_order_relaxed);
            else
                is->audio_clock_us.store(0, std::memory_order_relaxed);
            av_packet_unref(&pkt);
            continue;
        }

        audio_pkt_size = pkt.size;
        while(audio_pkt_size > 0)
        {
            if( is->quit.load(std::memory_order_relaxed)) return -1;
            int got_picture;
            av_frame_unref(audioFrame);
            int ret = avcodec_send_packet(aCodecCtx, &pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                qDebug() << "Error sending audio packet.";
                audio_pkt_size = 0;
                continue;
            }
            got_picture = avcodec_receive_frame(aCodecCtx, audioFrame);
            if (got_picture < 0 && got_picture != AVERROR(EAGAIN) && got_picture != AVERROR_EOF) {
                qDebug() << "Error in decoding audio frame.";
                audio_pkt_size = 0;
                continue;
            }
            if (got_picture < 0) {
                audio_pkt_size = 0;
                continue;
            }
            if (audioFrame->nb_samples <= 0) {
                audio_pkt_size = 0;
                continue;
            }

            int64_t frame_clock_us = 0;
            if (audioFrame->best_effort_timestamp != AV_NOPTS_VALUE) {
                frame_clock_us = static_cast<int64_t>(
                    audioFrame->best_effort_timestamp
                    * av_q2d(is->audio_st->time_base) * 1000000);
            } else if (pkt.pts != AV_NOPTS_VALUE) {
                frame_clock_us = static_cast<int64_t>(
                    pkt.pts * av_q2d(is->audio_st->time_base) * 1000000);
            }
            is->audio_clock_us.store(frame_clock_us, std::memory_order_relaxed);

            if (is->seek_flag_audio.load()) {
                if (frame_clock_us < is->seek_time_us.load(std::memory_order_relaxed))
                    break;
                is->seek_flag_audio.store(0, std::memory_order_relaxed);
            }

            data_size = 0;
            const int bytesPerSample =
                av_get_bytes_per_sample(static_cast<AVSampleFormat>(wanted_frame.format));
            const int outChannels = qMax(1, wanted_frame.channels);
            int maxOutSamples = 0;
            if (bytesPerSample > 0)
                maxOutSamples = buf_size / (bytesPerSample * outChannels);
            if (maxOutSamples <= 0)
                maxOutSamples = 4096;

            if (swr_ctx) {
                convert_len = swr_convert(swr_ctx, &audio_buf, maxOutSamples,
                                          (const uint8_t **)audioFrame->data,
                                          audioFrame->nb_samples);
                if (convert_len > 0 && bytesPerSample > 0)
                    data_size = convert_len * outChannels * bytesPerSample;
            }

            if (data_size > buf_size)
                data_size = buf_size;

            audio_pkt_size -= pkt.size;
            av_packet_unref(&pkt);
            if (data_size > 0)
                return data_size;
            continue;
        }
        av_packet_unref(&pkt); //新版考虑使用 av_packet_unref() 函数来代替
    }
}
//查找数据流函数
int find_stream_index(AVFormatContext *pformat_ctx, int *video_stream, int
                      *audio_stream)
{
    assert(video_stream != NULL || audio_stream != NULL);
    int i = 0;
    int audio_index = -1;
    int video_index = -1;
    for (i = 0; i < pformat_ctx->nb_streams; i++)
    {
        if (pformat_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_index = i;
        }
        if (pformat_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_index = i;
        }
    }
    //注意以下两个判断有可能返回-1.
    if (video_stream == NULL)
    {
        *audio_stream = audio_index;
        return *audio_stream;
    }
    if (audio_stream == NULL)
    {
        *video_stream = video_index;
        return *video_stream;
    }
    *video_stream = video_index;
    *audio_stream = audio_index;
    if (video_index < 0)
        return -1;
    return 0;
}
