#include "playerdialog.h"
#include "ui_playerdialog.h"
#include <QCloseEvent>
#include <QMessageBox>
#include <QStyle>
#include <QMouseEvent>
#include <QTimer>
#include <QKeyEvent>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

PlayerDialog::PlayerDialog(const QString& filePath, QWidget *parent, bool quietOnError,
                           bool removeFileOnClose)
    : QDialog(parent)
    , ui(new Ui::PlayerDialog)
    , m_player(nullptr)
    , m_sliderDragging(false)
    , m_pendingSeekMs(-1)
    , m_quietOnError(quietOnError)
{
    ui->setupUi(this);
    ui->slider_progress->installEventFilter(this);
    slot_PlayerStateChanged(PlayerState::Stop);

    if (QWidget *parentWindow = parentWidget()) {
        const QSize parentSize = parentWindow->size();
        const int w = qBound(480, static_cast<int>(parentSize.width() * 0.75), parentSize.width());
        const int h = qBound(360, static_cast<int>(parentSize.height() * 0.75), parentSize.height());
        resize(w, h);
    }

    if (removeFileOnClose && !filePath.isEmpty())
        m_removeOnClosePath = QFileInfo(filePath).absoluteFilePath();

    if (!filePath.isEmpty())
        openFile(filePath);
}

PlayerDialog::~PlayerDialog()
{
    shutdownPlayer();
    removeCachedTempFile();
    delete ui;
    ui = nullptr;
}

void PlayerDialog::removeCachedTempFile()
{
    if (m_removeOnClosePath.isEmpty())
        return;
    if (QFile::exists(m_removeOnClosePath) && !QFile::remove(m_removeOnClosePath))
        qWarning() << "删除下载临时文件失败：" << m_removeOnClosePath;
    else
        qDebug() << "已回收下载临时文件：" << m_removeOnClosePath;
    m_removeOnClosePath.clear();
}

void PlayerDialog::initPlayer()
{
    m_player = new VideoPlayer;

    connect(m_player, SIGNAL(SIG_PlayerStateChanged(int)),
            this, SLOT(slot_PlayerStateChanged(int)), Qt::QueuedConnection);
    connect(m_player, SIGNAL(SIG_TotalTime(qint64)),
            this, SLOT(slot_getTotalTime(qint64)), Qt::QueuedConnection);
    connect(m_player, &VideoPlayer::SIG_playbackError,
            this, [this](const QString &reason) {
        emit playbackFailed(reason);
        if (!m_quietOnError)
            QMessageBox::warning(this, QStringLiteral("播放失败"), reason);
        slot_PlayerStateChanged(PlayerState::Stop);
    });
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(slot_TimerTimeOut()));
    m_timer.setInterval(33);

    slot_PlayerStateChanged(PlayerState::Stop);
    ui->slider_progress->installEventFilter(this);
}

void PlayerDialog::openFile(const QString& filePath)
{
    if (filePath.isEmpty())
        return;

    m_timer.stop();
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
        m_player->stop(true);
        delete m_player;
        m_player = nullptr;
    }
    initPlayer();

    if (ui && ui->wdg_show) {
        const bool isRemote = filePath.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || filePath.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
        ui->wdg_show->setPlaceholderText(
            isRemote ? QStringLiteral("正在缓冲，请稍候…") : QStringLiteral("正在加载…"));
    }

    m_player->setFileName(filePath);
    QTimer::singleShot(0, this, [this]() {
        if (m_player && !m_player->isRunning())
            m_player->start();
    });
}

void PlayerDialog::closeEvent(QCloseEvent *event)
{
    shutdownPlayer();
    QDialog::closeEvent(event);
}

void PlayerDialog::shutdownPlayer()
{
    m_timer.stop();
    if (!m_player)
        return;

    disconnect(m_player, nullptr, this, nullptr);
    m_player->stop(true);
    delete m_player;
    m_player = nullptr;
}

void PlayerDialog::on_pb_resume_clicked()
{
    if (!m_player || m_player->playerState() != PlayerState::Pause ) return;
    m_player->play();
    ui->pb_resume->hide();
    ui->pb_pause->show();
}

void PlayerDialog::on_pb_pause_clicked()
{
    if (!m_player || m_player->playerState() != PlayerState::Playing ) return;
    m_player->pause();
    ui->pb_resume->show();
    ui->pb_pause->hide();
}

void PlayerDialog::on_pb_fullscreen_clicked()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void PlayerDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
        return;
    }
    if (event->key() == Qt::Key_F11) {
        on_pb_fullscreen_clicked();
        return;
    }
    QDialog::keyPressEvent(event);
}

void PlayerDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (!ui || event->type() != QEvent::WindowStateChange)
        return;
    ui->pb_fullscreen->setText(isFullScreen() ? QStringLiteral("退出全屏")
                                              : QStringLiteral("全屏"));
}

void PlayerDialog::slot_PlayerStateChanged(int state)
{
    if (!ui)
        return;
    switch( state )
    {
    case PlayerState::Stop:
        m_timer.stop();
        ui->slider_progress->setValue(0);
        ui->lb_totalTime->setText("00:00:00");
        ui->lb_curTime->setText("00:00:00");
        ui->pb_pause->hide();
        ui->pb_resume->show();
        break;
    case PlayerState::Playing:
        ui->pb_resume->hide();
        ui->pb_pause->show();
        m_timer.start();
        break;
    case PlayerState::Seeking:
        ui->pb_resume->hide();
        ui->pb_pause->show();
        if (!m_timer.isActive())
            m_timer.start();
        break;
    }
}

void PlayerDialog::applyPendingSeek()
{
    if (m_pendingSeekMs < 0 || !m_player)
        return;
    const int ms = m_pendingSeekMs;
    m_pendingSeekMs = -1;
    m_player->seek(static_cast<int64_t>(ms) * 1000LL);
}

void PlayerDialog::slot_getTotalTime(qint64 uSec)
{
    if (!ui)
        return;
    const qint64 totalMs = uSec / 1000;
    ui->slider_progress->setRange(0, static_cast<int>(totalMs));
    const qint64 Sec = totalMs / 1000;
    QString hStr = QString("00%1").arg(Sec/3600);
    QString mStr = QString("00%1").arg(Sec/60%60);
    QString sStr = QString("00%1").arg(Sec%60);
    QString str = QString("%1:%2:%3").arg(hStr.right(2)).arg(mStr.right(2)).arg(sStr.right(2));
    ui->lb_totalTime->setText(str);
}

void PlayerDialog::slot_TimerTimeOut()
{
    if (!ui || !m_player)
        return;
    if (QObject::sender() == &m_timer)
    {
        QImage frame;
        if (m_player->takeDisplayFrame(&frame))
            ui->wdg_show->slot_setImage(frame);

        qint64 ms = static_cast<qint64>(m_player->getCurrentTime() / 1000);
        if (!m_sliderDragging)
            ui->slider_progress->setValue(static_cast<int>(ms));
        const qint64 Sec = ms / 1000;
        QString hStr = QString("00%1").arg(Sec/3600);
        QString mStr = QString("00%1").arg(Sec/60%60);
        QString sStr = QString("00%1").arg(Sec%60);
        QString str =
                QString("%1:%2:%3").arg(hStr.right(2)).arg(mStr.right(2)).arg(sStr.right(2));
        ui->lb_curTime->setText(str);
        if(ui->slider_progress->value() == ui->slider_progress->maximum()
                && m_player->playerState() == PlayerState::Stop)
        {
            slot_PlayerStateChanged( PlayerState::Stop );
        }else if(ui->slider_progress->value() + 1 ==
                 ui->slider_progress->maximum()
                 && m_player->playerState() == PlayerState::Stop)
        {
            slot_PlayerStateChanged( PlayerState::Stop );
        }
    }
}

bool PlayerDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (!ui || !m_player)
        return QDialog::eventFilter(obj, event);
    if (obj == ui->slider_progress) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            const int min = ui->slider_progress->minimum();
            const int max = ui->slider_progress->maximum();
            const int value = QStyle::sliderValueFromPosition(
                min, max, mouseEvent->pos().x(), ui->slider_progress->width());
            m_sliderDragging = true;
            m_pendingSeekMs = value;
            ui->slider_progress->setValue(value);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            const int min = ui->slider_progress->minimum();
            const int max = ui->slider_progress->maximum();
            const int value = QStyle::sliderValueFromPosition(
                min, max, mouseEvent->pos().x(), ui->slider_progress->width());
            m_sliderDragging = false;
            m_pendingSeekMs = value;
            ui->slider_progress->setValue(value);
            applyPendingSeek();
            return true;
        }
        return false;
    }
    return QDialog::eventFilter(obj, event);
}
