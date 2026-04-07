#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include <QImage>

namespace VideoPlay {

class VideoWidget : public QWidget {
    Q_OBJECT

public:
    enum AspectRatioMode { Fit, Stretch, Crop };

    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget() override = default;

    void setFrame(const QImage& frame);
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
    QRect calculateTargetRect(const QRect& widgetRect, const QSize& frameSize) const;

    QImage m_currentFrame;
    AspectRatioMode m_aspectRatio;
};

} // namespace VideoPlay

#endif // VIDEOWIDGET_H
