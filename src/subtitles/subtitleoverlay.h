#ifndef SUBTITLEOVERLAY_H
#define SUBTITLEOVERLAY_H

#include <QWidget>
#include <QString>
#include <QFont>
#include <QColor>

namespace VideoPlay {

class SubtitleOverlay : public QWidget {
    Q_OBJECT

public:
    explicit SubtitleOverlay(QWidget* parent = nullptr);
    void setText(const QString& text);
    void setFontFamily(const QString& family);
    void setFontSize(int size);
    void setFontColor(const QColor& color);
    void setOutlineColor(const QColor& color);
    void setOutlineWidth(int width);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_text;
    QFont m_font;
    QColor m_fontColor;
    QColor m_outlineColor;
    int m_outlineWidth;
};

} // namespace VideoPlay

#endif // SUBTITLEOVERLAY_H
