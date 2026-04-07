#include "ui/videowidget.h"

#include <QDebug>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

namespace VideoPlay {

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
    , m_aspectRatio(Fit)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void VideoWidget::setFrame(const QImage& frame)
{
    m_currentFrame = frame;
    update();
}

void VideoWidget::setAspectRatioMode(AspectRatioMode mode)
{
    if (m_aspectRatio != mode) {
        m_aspectRatio = mode;
        update();
    }
}

VideoWidget::AspectRatioMode VideoWidget::aspectRatioMode() const
{
    return m_aspectRatio;
}

QRect VideoWidget::calculateTargetRect(const QRect& widgetRect, const QSize& frameSize) const
{
    if (frameSize.isEmpty())
        return widgetRect;

    const qreal widgetAspect = qreal(widgetRect.width()) / widgetRect.height();
    const qreal frameAspect = qreal(frameSize.width()) / frameSize.height();

    switch (m_aspectRatio) {
    case Stretch:
        return widgetRect;

    case Fit: {
        if (widgetAspect > frameAspect) {
            int w = int(widgetRect.height() * frameAspect);
            int x = (widgetRect.width() - w) / 2 + widgetRect.x();
            return QRect(x, widgetRect.y(), w, widgetRect.height());
        } else {
            int h = int(widgetRect.width() / frameAspect);
            int y = (widgetRect.height() - h) / 2 + widgetRect.y();
            return QRect(widgetRect.x(), y, widgetRect.width(), h);
        }
    }

    case Crop: {
        if (widgetAspect > frameAspect) {
            int h = int(widgetRect.width() / frameAspect);
            int y = (widgetRect.height() - h) / 2 + widgetRect.y();
            return QRect(widgetRect.x(), y, widgetRect.width(), h);
        } else {
            int w = int(widgetRect.height() * frameAspect);
            int x = (widgetRect.width() - w) / 2 + widgetRect.x();
            return QRect(x, widgetRect.y(), w, widgetRect.height());
        }
    }
    }

    return widgetRect;
}

void VideoWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    if (m_currentFrame.isNull()) {
        painter.fillRect(rect(), Qt::black);
        return;
    }

    painter.fillRect(rect(), Qt::black);

    QRect target = calculateTargetRect(rect(), m_currentFrame.size());

    if (m_aspectRatio == Crop) {
        painter.setClipRect(rect());
    }

    painter.drawImage(target, m_currentFrame);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();
    }
}

void VideoWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
}

void VideoWidget::wheelEvent(QWheelEvent* event)
{
    int delta = event->angleDelta().y() > 0 ? 5 : -5;
    emit volumeChangedByWheel(delta);
}

void VideoWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void VideoWidget::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            emit fileDropped(urls.first().toLocalFile());
        }
    }
}

} // namespace VideoPlay
