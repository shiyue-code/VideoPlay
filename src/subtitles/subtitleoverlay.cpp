#include "subtitles/subtitleoverlay.h"

#include <QDebug>
#include <QPainter>
#include <QPaintEvent>
#include <QFontMetrics>

namespace VideoPlay {

SubtitleOverlay::SubtitleOverlay(QWidget* parent)
    : QWidget(parent)
    , m_font("Arial", 24)
    , m_fontColor(Qt::white)
    , m_outlineColor(Qt::black)
    , m_outlineWidth(3)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void SubtitleOverlay::setText(const QString& text)
{
    if (m_text != text) {
        m_text = text;
        update();
    }
}

void SubtitleOverlay::setFontFamily(const QString& family)
{
    if (m_font.family() != family) {
        m_font.setFamily(family);
        update();
    }
}

void SubtitleOverlay::setFontSize(int size)
{
    if (m_font.pixelSize() != size) {
        m_font.setPixelSize(size);
        update();
    }
}

void SubtitleOverlay::setFontColor(const QColor& color)
{
    if (m_fontColor != color) {
        m_fontColor = color;
        update();
    }
}

void SubtitleOverlay::setOutlineColor(const QColor& color)
{
    if (m_outlineColor != color) {
        m_outlineColor = color;
        update();
    }
}

void SubtitleOverlay::setOutlineWidth(int width)
{
    if (m_outlineWidth != width) {
        m_outlineWidth = width;
        update();
    }
}

void SubtitleOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_text.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);

    QFont font = m_font;
    font.setPixelSize(qMax(16, height() / 12));
    font.setBold(true);
    painter.setFont(font);

    QRect textRect = rect().adjusted(10, 10, -10, -50);

    painter.setPen(Qt::black);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap, m_text);

    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap, m_text);
}

} // namespace VideoPlay
