#ifndef VIDEORENDERER_H
#define VIDEORENDERER_H

#include <QWidget>
#include <QImage>
#include <QFont>
#include <QColor>
#include <QMutex>

namespace VideoPlay {

/**
 * @brief 统一的视频渲染器，同时处理视频帧和字幕显示
 * 
 * 替代原有的 VideoWidget + SubtitleOverlay 分离方案，
 * 避免 Windows 上 QVideoWidget 原生窗口的 Z 序问题。
 * 
 * 所有渲染通过 QPainter 在单个 QWidget 中完成，包括：
 * - 视频帧渲染（保持宽高比）
 * - 字幕叠加渲染（支持样式设置）
 */
class VideoRenderer : public QWidget {
    Q_OBJECT

public:
    enum AspectRatioMode { 
        Fit,      // 保持比例，适应窗口（可能有黑边）
        Stretch,  // 拉伸填充
        Crop      // 保持比例，裁剪填充（无黑边）
    };

    explicit VideoRenderer(QWidget* parent = nullptr);
    ~VideoRenderer() override = default;

    // 视频帧设置
    void setFrame(const QImage& frame);
    void setAspectRatioMode(AspectRatioMode mode);
    AspectRatioMode aspectRatioMode() const;

    // 字幕设置
    void setSubtitleText(const QString& text);
    void setSubtitleFont(const QFont& font);
    void setSubtitleFontFamily(const QString& family);
    void setSubtitleFontSize(int size);
    void setSubtitleColor(const QColor& color);
    void setSubtitleOutlineColor(const QColor& color);
    void setSubtitleOutlineWidth(int width);
    void setSubtitleBottomMargin(int margin);

signals:
    // 鼠标/键盘事件转发
    void doubleClicked();
    void clicked();
    void volumeChangedByWheel(int delta);
    void fileDropped(const QString& filePath);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // 计算视频目标绘制区域
    QRect calculateVideoRect(const QRect& widgetRect, const QSize& frameSize) const;
    
    // 绘制字幕
    void drawSubtitle(QPainter& painter, const QRect& videoRect);
    
    // 视频相关
    QImage m_currentFrame;
    QMutex m_frameMutex;
    AspectRatioMode m_aspectRatio;
    
    // 字幕相关
    QString m_subtitleText;
    QFont m_subtitleFont;
    QColor m_subtitleColor;
    QColor m_subtitleOutlineColor;
    int m_subtitleOutlineWidth;
    int m_subtitleBottomMargin;
    
    // 预渲染的字幕缓存（优化性能）
    QPixmap m_subtitleCache;
    QString m_cachedSubtitleText;
    int m_cachedWidth;
};

} // namespace VideoPlay

#endif // VIDEORENDERER_H
