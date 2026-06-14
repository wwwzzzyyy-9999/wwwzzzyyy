#ifndef PLAYERDIALOG_H
#define PLAYERDIALOG_H

#include <QDialog>
#include "videoplayer.h"
#include <QTimer>

class QCloseEvent;

QT_BEGIN_NAMESPACE
namespace Ui { class PlayerDialog; }
QT_END_NAMESPACE

class PlayerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PlayerDialog(const QString& filePath = QString(), QWidget *parent = nullptr,
                          bool quietOnError = false, bool removeFileOnClose = false);
    ~PlayerDialog();

signals:
    void playbackFailed(const QString &reason);

private slots:
    void on_pb_resume_clicked();
    void on_pb_pause_clicked();
    void on_pb_fullscreen_clicked();
    void slot_PlayerStateChanged(int state);
    void slot_getTotalTime(qint64);
    void slot_TimerTimeOut();

    bool eventFilter(QObject* obj , QEvent* event);
    void applyPendingSeek();
    void shutdownPlayer();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void initPlayer();
    void openFile(const QString& filePath);
    void removeCachedTempFile();

    Ui::PlayerDialog *ui;
    VideoPlayer* m_player;
    QTimer m_timer;
    bool m_sliderDragging;
    int m_pendingSeekMs;
    bool m_quietOnError;
    QString m_removeOnClosePath;
};

#endif // PLAYERDIALOG_H
