#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QThread>
#include <QImage>
#include <QMutex>
#include <atomic>



extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "SDL.h"
}



struct HlsSegInfo {

    QString url;
    int64_t duration;
    int64_t startTime;

};



#include "PacketQueue.h"



#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000
#define SDL_AUDIO_BUFFER_SIZE 1024



enum PlayerState {

    Playing = 0,
    Pause,
    Stop,
    Seeking
};



class VideoPlayer;



typedef struct VideoState {

    AVFormatContext *pFormatCtx;
    AVStream *audio_st;
    PacketQueue *audioq;
    AVCodecContext *pAudioCodecCtx;
    int audioStream;
    std::atomic<int64_t> audio_clock_us{0};
    SDL_AudioDeviceID audioID;

    AVFrame out_frame;

    uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];

    unsigned int audio_buf_size = 0;

    unsigned int audio_buf_index = 0;

    AVFrame *audioFrame;

    struct SwrContext *swr_ctx;



    AVStream *video_st;

    PacketQueue *videoq;

    AVCodecContext *pCodecCtx;

    int videoStream;

    std::atomic<int64_t> video_clock_us{0};

    SDL_Thread *video_tid;



    std::atomic<bool> isPause{false};

    std::atomic<bool> quit{false};

    std::atomic<bool> readFinished{false};

    std::atomic<bool> readThreadFinished{false};

    std::atomic<bool> videoThreadFinished{true};



    std::atomic<int> seek_req{0};

    std::atomic<int64_t> seek_pos_us{0};

    std::atomic<int> seek_flag_audio{0};

    std::atomic<int> seek_flag_video{0};

    std::atomic<int64_t> seek_time_us{0};



    int64_t start_time;

    std::atomic<bool> hls_is_seeking{false};



    VideoPlayer* m_player;



    VideoState()

        : pFormatCtx(nullptr)

        , audio_st(nullptr)

        , audioq(nullptr)

        , pAudioCodecCtx(nullptr)

        , audioStream(-1)

        , audioID(0)

        , audioFrame(nullptr)

        , swr_ctx(nullptr)

        , video_st(nullptr)

        , videoq(nullptr)

        , pCodecCtx(nullptr)

        , videoStream(-1)

        , video_tid(nullptr)

        , start_time(0)

        , m_player(nullptr)

    {}

} VideoState;



class VideoPlayer : public QThread

{

    Q_OBJECT

signals:

    void SIG_PlayerStateChanged(int flag);

    void SIG_TotalTime(qint64 uSec);

    void SIG_playbackError(const QString &reason);



public:

    VideoPlayer();

    ~VideoPlayer();



    void play();

    void pause();

    void seek(int64_t pos);

    void stop(bool isWait);

    void setFileName(const QString &fileName);



    PlayerState playerState() const;

    double getCurrentTime();

    int64_t getTotalTime();



    // SDL 视频线程写入，UI 线程读取；中间不经过 Qt signal

    void submitFrame(QImage img);

    bool takeDisplayFrame(QImage *out);



protected:

    void run() override;



private:

    static bool initSdl();

    static void quitSdl();



    void stopInternal(bool isWait, bool notifyStop);

    void ensureHlsPlaylistParsed();

    int findHlsSegmentForTime(int64_t posUs) const;

    void finishSeekingUi();

    int64_t parseHlsDuration(const QString& m3u8Url);

    void rebuildHlsFromSegment(int64_t seekPosUs);



    QString m_fileName;

    VideoState m_videoState;

    PlayerState m_playerState;

    QList<HlsSegInfo> m_hlsSegList;

    bool m_isHls;

    int64_t m_hlsSeekTarget;

    int64_t m_hlsDurationUs;



    QMutex m_displayMutex;

    QImage m_displayFrame;



    static QMutex s_sdlMutex;

    static int s_sdlRefCount;

};



#endif // VIDEOPLAYER_H

