#include "ui/videorenderer.h"

#include <QPainter>
#include <QPaintEvent>
#include <QFontMetrics>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

namespace VideoPlay {

VideoRenderer::VideoRenderer(QWidget* parent)
    : QWidget(parent)
    , m_aspectRatio(Fit)
    , m_subtitleColor(Qt::white)
    , m_subtitleOutlineColor(Qt::black)
    , m_subtitleOutlineWidth(3)
    , m_subtitleBottomMargin(50)
    , m_cachedWidth(0)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    
    // 默认字幕字体
    m_subtitleFont.setFamily("Microsoft YaHei");
    m_subtitleFont.setPixelSize(24);
    m_subtitleFont.setBold(true);
}

void VideoRenderer::setFrame(const QImage& frame)
{
    m_currentFrame = frame;
    update();
}

void VideoRenderer::setAspectRatioMode(AspectRatioMode mode)
{
    if (m_aspectRatio != mode) {
        m_aspectRatio = mode;
        update();
    }
}

VideoRenderer::AspectRatioMode VideoRenderer::aspectRatioMode() const
{
    return m_aspectRatio;
}

void VideoRenderer::setSubtitleText(const QString& text)
{
    if (m_subtitleText != text) {
        m_subtitleText = text;
        m_cachedSubtitleText.clear(); // 清除缓存，强制重新渲染
        update();
    }
}

void VideoRenderer::setSubtitleFont(const QFont& font)
{
    m_subtitleFont = font;
    m_subtitleFont.setBold(true);
    m_cachedSubtitleText.clear();
    update();
}

void VideoRenderer::setSubtitleFontFamily(const QString& family)
{
    if (m_subtitleFont.family() != family) {
        m_subtitleFont.setFamily(family);
        m_cachedSubtitleText.clear();
        update();
    }
}

void VideoRenderer::setSubtitleFontSize(int size)
{
    if (m_subtitleFont.pixelSize() != size) {
        m_subtitleFont.setPixelSize(size);
        m_cachedSubtitleText.clear();
        update();
    }
}

void VideoRenderer::setSubtitleColor(const QColor& color)
{
    if (m_subtitleColor != color) {
        m_subtitleColor = color;
        m_cachedSubtitleText.clear();
        update();
    }
}

void VideoRenderer::setSubtitleOutlineColor(const QColor& color)
{
    if (m_subtitleOutlineColor != color) {
        m_subtitleOutlineColor = color;
        m_cachedSubtitleText.clear();
        update();
    }
}

void VideoRenderer::setSubtitleOutlineWidth(int width)
{
    if (m_subtitleOutlineWidth != width) {
        m_subtitleOutlineWidth = width;
        m_cachedSubtitleText.clear();
        update();
    }
}

void VideoRenderer::setSubtitleBottomMargin(int margin)
{
    if (m_subtitleBottomMargin != margin) {
        m_subtitleBottomMargin = margin;
        update();
    }
}

QRect VideoRenderer::calculateVideoRect(const QRect& widgetRect, const QSize& frameSize) const
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

void VideoRenderer::drawSubtitle(QPainter& painter, const QRect& videoRect)
{
    if (m_subtitleText.isEmpty())
        return;

    // 计算自适应字体大小（基于视频高度）
    int baseFontSize = qMax(16, videoRect.height() / 15);
    QFont font = m_subtitleFont;
    font.setPixelSize(baseFontSize);

    painter.setFont(font);
    QFontMetrics metrics(font);

    // 处理多行文本
    QStringList lines = m_subtitleText.split('\n');
    int lineSpacing = metrics.lineSpacing();
    int totalHeight = lineSpacing * lines.size();

    // 字幕显示区域（视频底部）
    int maxTextWidth = videoRect.width() - 40; // 左右各留 20px 边距
    int startY = videoRect.bottom() - m_subtitleBottomMargin - totalHeight;

    // 绘制每一行
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines[i];
        int lineWidth = metrics.horizontalAdvance(line);
        
        // 如果单行太长，需要截断或缩放（这里选择缩放字体）
        if (lineWidth > maxTextWidth && !line.isEmpty()) {
            qreal scale = qreal(maxTextWidth) / lineWidth;
            int adjustedSize = qMax(12, int(baseFontSize * scale * 0.9));
            QFont adjustedFont = font;
            adjustedFont.setPixelSize(adjustedSize);
            painter.setFont(adjustedFont);
            QFontMetrics adjustedMetrics(adjustedFont);
            lineWidth = adjustedMetrics.horizontalAdvance(line);
        }

        int x = videoRect.center().x() - lineWidth / 2;
        int y = startY + i * lineSpacing + metrics.ascent();

        // 绘制描边（阴影效果）
        if (m_subtitleOutlineWidth > 0) {
            painter.setPen(m_subtitleOutlineColor);
            for (int dx = -m_subtitleOutlineWidth; dx <= m_subtitleOutlineWidth; ++dx) {
                for (int dy = -m_subtitleOutlineWidth; dy <= m_subtitleOutlineWidth; ++dy) {
                    if (dx != 0 || dy != 0) {
                        painter.drawText(x + dx, y + dy, line);
                    }
                }
            }
        }

        // 绘制主文本
        painter.setPen(m_subtitleColor);
        painter.drawText(x, y, line);
        
        // 恢复字体（如果之前调整过）
        painter.setFont(font);
    }
}

void VideoRenderer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // 绘制黑色背景
    painter.fillRect(rect(), Qt::black);

    // 计算视频绘制区域
    QRect videoRect = calculateVideoRect(rect(), m_currentFrame.size());

    // 绘制视频帧
    if (!m_currentFrame.isNull()) {
        if (m_aspectRatio == Crop) {
            painter.setClipRect(rect());
        }
        painter.drawImage(videoRect, m_currentFrame);
        painter.setClipping(false);
    }

    // 绘制字幕（在视频区域底部）
    drawSubtitle(painter, videoRect.isEmpty() ? rect() : videoRect);
}

void VideoRenderer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_cachedSubtitleText.clear(); // 清除字幕缓存，需要重新计算布局
    update();
}

void VideoRenderer::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();
    }
}

void VideoRenderer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
}

void VideoRenderer::wheelEvent(QWheelEvent* event)
{
    int delta = event->angleDelta().y() > 0 ? 5 : -5;
    emit volumeChangedByWheel(delta);
}

void VideoRenderer::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void VideoRenderer::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            emit fileDropped(urls.first().toLocalFile());
        }
    }
}

} // namespace VideoPlay
