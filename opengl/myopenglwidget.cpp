#include "myopenglwidget.h"
#include <QPainter>
#include <QPaintEvent>

MyOpenGLWidget::MyOpenGLWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);
}

void MyOpenGLWidget::setPlaceholderText(const QString &text)
{
    m_placeholderText = text;
    update();
}

void MyOpenGLWidget::slot_setImage(QImage img)
{
    if (img.isNull() || img.width() <= 0 || img.height() <= 0)
        return;
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_RGB32);
    m_image = std::move(img);
    m_placeholderText.clear();
    update();
}

void MyOpenGLWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (m_image.isNull()) {
        if (!m_placeholderText.isEmpty()) {
            painter.setPen(Qt::white);
            painter.drawText(rect(), Qt::AlignCenter, m_placeholderText);
        }
        return;
    }

    const QSize targetSize = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect dest(
        (width() - targetSize.width()) / 2,
        (height() - targetSize.height()) / 2,
        targetSize.width(),
        targetSize.height());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(dest, m_image);
}
