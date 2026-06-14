#ifndef VIDEOCARD_H
#define VIDEOCARD_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QImage>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

class VideoCard : public QWidget
{
    Q_OBJECT

public:
    explicit VideoCard(int videoId,
                      const QString &coverPath,
                      const QString &title,
                      const QString &author,
                      int playCount,
                      int likesCount = 0,
                      QWidget *parent = nullptr);

    ~VideoCard();

    int getVideoId() const { return m_videoId; }
    int getLikesCount() const { return m_likesCount; }

    void setLikeState(bool liked, int likesCount);

signals:
    void SIG_cardClicked(int videoId);
    void SIG_likeClicked(int videoId);

private slots:
    void onImageLoaded(const QImage &image);
    void onCoverDownloaded(QNetworkReply *reply);
    void onLikeButtonClicked();

private:
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    void initUI();
    void updateCoverGeometry();
    void updateLikeButton();

    static QString formatPlayCount(int count);
    static QString formatLikeCount(int count);

private:
    int     m_videoId;
    QString m_coverPath;
    QString m_title;
    QString m_author;
    int     m_playCount;
    int     m_likesCount;
    bool    m_liked;

    QLabel *m_coverLabel;
    QLabel *m_titleLabel;
    QLabel *m_authorLabel;
    QLabel *m_playCountLabel;
    QPushButton *m_likeBtn;
    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_coverReply;
    QImage m_coverImage;
};

#endif // VIDEOCARD_H
