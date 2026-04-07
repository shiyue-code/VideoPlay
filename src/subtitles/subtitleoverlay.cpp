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
    setAttribute(Qt::WA_TranslucentBackground);
}

void SubtitleOverlay::setText(const QString& text)
{
    if (m_text != text) {
        m_text = text;
        qDebug() << "SubtitleOverlay setText:" << text << "isEmpty:" << text.isEmpty() << "isNull:" << text.isNull();
        qDebug() << "SubtitleOverlay::update() called";
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

    qDebug() << "SubtitleOverlay::paintEvent called, text:" << m_text << "isEmpty:" << m_text.isEmpty();

    if (m_text.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);

    QFont font = m_font;
    font.setPixelSize(qMax(16, height() / 12));
    font.setBold(true);
    painter.setFont(font);

    // Only fill the area behind the text (not the whole overlay) for better readability
    QRect textRect = rect().adjusted(10, 10, -10, -50);
    QFontMetrics metrics(font);
    QStringList lines = m_text.split("\n");
    int lineSpacing = metrics.lineSpacing();
    int totalHeight = lineSpacing * lines.size();
    int y = textRect.bottom() - totalHeight - 10;
    for (const QString& line : lines) {
        QRect lineRect = metrics.boundingRect(textRect.left(), y, textRect.width(), lineSpacing, Qt::AlignHCenter, line);
        painter.setBrush(QColor(0, 0, 0, 160));
        painter.setPen(Qt::NoPen);
        painter.drawRect(lineRect.adjusted(-12, -6, 12, 6));
        y += lineSpacing;
    }
    y = textRect.bottom() - totalHeight - 10;
    for (const QString& line : lines) {
        QRect lineRect = metrics.boundingRect(textRect.left(), y, textRect.width(), lineSpacing, Qt::AlignHCenter, line);
        painter.setPen(Qt::black);
        painter.drawText(lineRect, Qt::AlignHCenter | Qt::AlignBottom, line);
        painter.setPen(Qt::white);
        painter.drawText(lineRect, Qt::AlignHCenter | Qt::AlignBottom, line);
        y += lineSpacing;
    }
}

} // namespace VideoPlay
