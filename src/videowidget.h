#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include <QVideoSink>
#include <QImage>

class VideoWidget : public QWidget
{
    Q_OBJECT
public:
    enum AspectRatioMode { Fit, Stretch, Crop };

    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget() override = default;

    QVideoSink* videoSink() const;
    void setAspectRatioMode(AspectRatioMode mode);
    AspectRatioMode aspectRatioMode() const;

signals:
    void doubleClicked();
    void clicked();
    void volumeChangedByWheel(int delta);
    void fileDropped(const QString& filePath);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void onVideoFrameChanged(const QVideoFrame& frame);
    QRect calculateTargetRect(const QRect& widgetRect, const QSize& frameSize) const;

    QVideoSink* m_sink;
    AspectRatioMode m_aspectRatio;
    QImage m_currentFrame;
    bool m_showOverlay;
};

#endif // VIDEOWIDGET_H
