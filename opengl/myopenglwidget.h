#pragma once

#include <QWidget>
#include <QImage>

// 视频帧显示（QPainter，避免 OpenGL 固定管线在 Windows 上崩溃）
class MyOpenGLWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MyOpenGLWidget(QWidget *parent = nullptr);

    void slot_setImage(QImage img);
    void setPlaceholderText(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
    QString m_placeholderText;
};
