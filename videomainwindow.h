#ifndef VIDEOMAINWINDOW_H
#define VIDEOMAINWINDOW_H

#include <QMainWindow>
#include <QScrollArea>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QWidget>
#include <QButtonGroup>
#include <QFileDialog>
#include <QFile>
#include "networkmanager.h"
#include "videocard.h"
#include <QPointer>
#include <QList>
#include <QResizeEvent>
#include <QJsonArray>
#include <QHash>

class PlayerDialog;
class UserProfileDialog;

class VideoMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit VideoMainWindow(NetworkManager *networkManager,
                             QWidget *parent = nullptr);
    ~VideoMainWindow();

signals:
    void SIG_logout();

private slots:
    void onSearchClicked();
    void onUploadClicked();
    void onUserAvatarClicked();

    void onNavDiscoverClicked();
    void onNavMyVideosClicked();

    void onVideoCardClicked(int videoId);

    void onVideoListResult(bool success, const QString &message, const QJsonArray &videos,
                           quint64 requestId);
    void onRecommendFeedResult(bool success, const QString &message, const QJsonArray &videos,
                               quint64 requestId);
    void onSearchVideosResult(bool success, const QString &message, const QJsonArray &videos,
                              const QString &keyword, quint64 requestId);
    void onGetLikeStatusResult(bool success, const QString &message, const QList<int> &likedIds);
    void onToggleLikeResult(bool success, const QString &message, int videoId,
                            bool liked, int likesCount);
    void onVideoLikeClicked(int videoId);
    void onFeedHotClicked();
    void onFeedLatestClicked();
    void onVideoInfoResult(bool success, const QString &message, const QJsonObject &videoInfo);

    void onUploadStartResult(bool success, const QString &message, const QString &uploadId);
    void onUploadChunkResult(bool success, const QString &message, int chunkIndex);
    void onUploadEndResult(bool success, const QString &message);
    void onUploadResumeResult(bool success, const QString &message,
                              const QString &uploadId, int nextChunkIndex);
    void onUploadProgress(const QString &uploadId, qint64 bytesSent, qint64 bytesTotal);

    void onLocalVideoClicked(const QString &filePath);

    void playHLSVideo(const QString &hlsUrl, int fallbackVideoId = -1);
    void showPlayerDialog(const QString &filePath, const QString &windowTitle = QString(),
                          int fallbackVideoId = -1);
    void startDownloadAndPlay(int videoId);

    void onDownloadStartResult(bool success, const QString &message, int videoId,
                               qint64 fileSize, int totalChunks);
    void onDownloadChunkResult(bool success, const QString &message, int chunkIndex,
                               const QByteArray &chunkData);

    void onOpenProfile();
    void onMyVideosResult(bool success, const QString &message, const QJsonArray &videos);
    void onDeleteVideoResult(bool success, const QString &message);

private:
    void initUI();
    void connectNetworkSignals();
    QWidget* createTopBar();
    QWidget* createSideBar();
    QWidget* createVideoGrid();

    void loadVideoList();
    void loadDiscoverFeed();
    void loadSearchResults();
    void exitSearchMode();
    void showDiscoverVideos(bool success, const QString &message, const QJsonArray &videos);
    void syncLikeStatusForCards();
    VideoCard *findVideoCard(int videoId) const;
    void clearVideoGrid();
    void addVideoCard(const QJsonObject &videoObj);

    struct PendingUploadInfo {
        QString uploadId;
        QString filePath;
        QString fileHash;
        int totalChunks = 0;
        QString username;
        bool isValid() const { return !uploadId.isEmpty() && !filePath.isEmpty(); }
    };

    void startUpload(const QString &filePath);
    void resumeUpload();
    void pumpUploadChunks();
    void tryFinishUpload();
    bool openUploadFile();
    void closeUploadFile();
    PendingUploadInfo loadPendingUpload() const;
    void savePendingUpload();
    void clearPendingUpload();

    int computeGridColumnCount() const;
    void relayoutVideoGrid();

    void resizeEvent(QResizeEvent *event) override;

    NetworkManager *m_networkManager;
    QGridLayout *m_gridLayout;
    QScrollArea *m_scrollArea;
    QList<VideoCard *> m_videoCards;
    QWidget *m_emptyLabel;
    int m_gridColumnCount;
    QLineEdit   *m_searchEdit;
    QPushButton *m_userAvatarBtn;
    int          m_currentPage;
    int          m_currentPlayingVideoId;
    QHash<int, QString> m_hlsUrlByVideoId;

    QButtonGroup *m_navButtonGroup;
    QPushButton *m_btnDiscover;
    QPushButton *m_btnMyVideos;

    QButtonGroup *m_feedModeGroup;
    QPushButton *m_btnFeedHot;
    QPushButton *m_btnFeedLatest;
    bool m_useHotFeed;
    QString m_searchKeyword;
    QWidget *m_feedBar;
    quint64 m_latestListRequestId;

    QString      m_uploadId;
    QString      m_uploadFilePath;
    QFile       *m_uploadFile;
    int          m_uploadTotalChunks;
    int          m_uploadNextToSend;
    int          m_uploadInFlight;
    QString      m_uploadFileHash;
    static const int UPLOAD_WINDOW_SIZE = 4;

    QPointer<PlayerDialog> m_activePlayerDialog;
    QPointer<UserProfileDialog> m_profileDialog;

    int m_downloadPlayVideoId;
    int m_downloadPlayTotalChunks;
    int m_downloadPlayNextChunk;
    QString m_downloadPlayTempPath;
    QFile *m_downloadPlayFile;
};

#endif // VIDEOMAINWINDOW_H
