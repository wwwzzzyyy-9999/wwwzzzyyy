#ifndef NETWORKMANAGER_H

#define NETWORKMANAGER_H



#include "protocol.h"



#include <QObject>

#include <QTcpSocket>

#include <QString>

#include <QSettings>

#include <QJsonArray>

#include <QJsonObject>

#include <QList>

#include <QQueue>



struct NetworkMessage {

    MsgType type;

    QString data;

    QString action;

    ResponseCode code;

    QJsonObject respData;

};



class NetworkManager : public QObject

{

    Q_OBJECT

public:

    explicit NetworkManager(QObject *parent = nullptr);

    ~NetworkManager();



    void connectToServer();

    void sendRegister(const QString& username, const QString& password);

    void sendLogin(const QString& username, const QString& password);



    quint64 sendGetVideoList(int offset, int limit);

    quint64 sendGetRecommendFeed(int offset, int limit);

    quint64 sendSearchVideos(const QString &keyword, int offset = 0, int limit = 20);

    void sendGetVideoInfo(int videoId);

    void sendIncrementPlay(int videoId);

    void sendGetLikeStatus(const QList<int> &videoIds);

    void sendToggleLike(int videoId);



    void sendUploadStart(const QString& fileName, qint64 fileSize, int totalChunks);

    void sendUploadChunk(const QString& uploadId, int chunkIndex, int totalChunks, const QByteArray& chunkData);

    void sendUploadEnd(const QString& uploadId, const QString& fileHash);

    void sendUploadResume(const QString& uploadId);



    static QString calculateFileHash(const QString& filePath);



    void sendDownloadStart(int videoId, int startChunkIndex = 0);

    void sendDownloadChunkAck(int videoId, int chunkIndex);



    void setToken(const QString& token);

    QString getToken() const;

    QString pendingUploadUsername() const { return m_loggedInUsername; }

    bool isLoggedIn() const;

    bool isSocketConnected() const;

    QString getServerIp() const;

    quint16 getServerPort() const;



    void sendGetMyVideos();

    void sendDeleteVideo(int videoId);



signals:

    void SIG_connected();

    void SIG_disconnected();

    void SIG_error(const QString& errorString);



    void SIG_registerResult(bool success, const QString& message);

    void SIG_loginResult(bool success, const QString& message, const QString& token);



    void SIG_videoListResult(bool success, const QString& message, const QJsonArray& videos,

                             quint64 requestId);

    void SIG_recommendFeedResult(bool success, const QString& message, const QJsonArray& videos,

                                 quint64 requestId);

    void SIG_searchVideosResult(bool success, const QString& message, const QJsonArray& videos,

                                 const QString& keyword, quint64 requestId);

    void SIG_videoInfoResult(bool success, const QString& message, const QJsonObject& videoInfo);

    void SIG_incrementPlayResult(bool success, const QString& message);

    void SIG_getLikeStatusResult(bool success, const QString& message, const QList<int>& likedIds);

    void SIG_toggleLikeResult(bool success, const QString& message, int videoId,

                              bool liked, int likesCount);



    void SIG_uploadStartResult(bool success, const QString& message, const QString& uploadId);

    void SIG_uploadChunkResult(bool success, const QString& message, int chunkIndex);

    void SIG_uploadEndResult(bool success, const QString& message);

    void SIG_uploadResumeResult(bool success, const QString& message, const QString& uploadId, int nextChunkIndex);



    void SIG_downloadStartResult(bool success, const QString& message, int videoId, qint64 fileSize, int totalChunks);

    void SIG_downloadChunkResult(bool success, const QString& message, int chunkIndex, const QByteArray& chunkData);



    void SIG_uploadProgress(const QString& uploadId, qint64 bytesSent, qint64 bytesTotal);

    void SIG_downloadProgress(int videoId, qint64 bytesReceived, qint64 bytesTotal);



    void SIG_myVideosResult(bool success, const QString& message, const QJsonArray& videos);

    void SIG_deleteVideoResult(bool success, const QString& message);



private slots:

    void onConnected();

    void onDisconnected();

    void onReadyRead();

    void onError(QAbstractSocket::SocketError socketError);



private:

    void sendGetPasswordSalt(const QString& username);

    void sendLoginHash(const QString& username, const QString& passwordHash);

    void sendMessage(const NetworkMessage& msg);

    NetworkMessage parseMessage(const QByteArray& data);

    void loadConfig();

    void dispatchResponse(const NetworkMessage& msg);



    QTcpSocket* m_socket;

    QByteArray m_buffer;

    QString m_serverIp;

    quint16 m_serverPort;

    bool m_isLoggedIn;

    QString m_token;

    QString m_loggedInUsername;

    QString m_currentUploadId;

    qint64 m_currentUploadFileSize;

    int m_currentUploadChunkSize;

    int m_currentUploadTotalChunks;

    int m_currentUploadSentChunks;



    int m_currentVideoId;

    qint64 m_currentDownloadFileSize;

    int m_currentDownloadTotalChunks;

    int m_currentDownloadReceivedChunks;



    quint64 m_requestSerial;

    QQueue<quint64> m_videoListRequestQueue;

    QQueue<quint64> m_recommendFeedRequestQueue;

    QQueue<quint64> m_searchVideosRequestQueue;



    quint64 nextListRequestId(QQueue<quint64> &queue);



    QString m_pendingLoginUsername;

    QString m_pendingLoginPlainPassword;

};



#endif // NETWORKMANAGER_H

