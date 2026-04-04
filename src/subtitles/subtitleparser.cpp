#include "subtitles/subtitleparser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QFileInfo>

SubtitleParser::SubtitleParser(QObject* parent)
    : QObject(parent)
{
}

bool SubtitleParser::loadFile(const QString& filePath)
{
    clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QString content = stream.readAll();
    file.close();

    m_filePath = filePath;
    QString suffix = QFileInfo(filePath).suffix().toLower();

    if (suffix == "srt") {
        return parseSRT(content);
    } else if (suffix == "ass" || suffix == "ssa") {
        return parseASS(content);
    } else if (suffix == "vtt") {
        return parseVTT(content);
    }

    clear();
    return false;
}

QString SubtitleParser::subtitleAt(qint64 ms) const
{
    for (const SubtitleEntry& entry : m_entries) {
        if (ms >= entry.startTime && ms <= entry.endTime) {
            return entry.text;
        }
    }
    return QString();
}

QList<SubtitleEntry> SubtitleParser::entries() const
{
    return m_entries;
}

void SubtitleParser::clear()
{
    m_entries.clear();
    m_filePath.clear();
}

bool SubtitleParser::isLoaded() const
{
    return !m_entries.isEmpty();
}

bool SubtitleParser::parseSRT(const QString& content)
{
    static const QRegularExpression blockRegex(
        R"((\d+)\s*\n(\d{2}:\d{2}:\d{2}[,\.]\d{3})\s*-->\s*(\d{2}:\d{2}:\d{2}[,\.]\d{3})\s*\n((?:.*\n?)*?)(?=\n\n|\n*$))",
        QRegularExpression::MultilineOption);

    QRegularExpressionMatchIterator it = blockRegex.globalMatch(content);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        if (match.hasMatch()) {
            SubtitleEntry entry;
            entry.startTime = parseTimecode(match.captured(2));
            entry.endTime = parseTimecode(match.captured(3));
            entry.text = match.captured(4).trimmed();
            entry.text.remove(QRegularExpression(R"(<[^>]*>)"));
            m_entries.append(entry);
        }
    }

    return !m_entries.isEmpty();
}

bool SubtitleParser::parseASS(const QString& content)
{
    static const QRegularExpression eventRegex(
        R"(Dialogue:\s*\d+,(\d+:\d{2}:\d{2}\.\d{2}),(\d+:\d{2}:\d{2}\.\d{2}),.*?,.*?,.*?,.*?,.*?,.*?,(.*))");

    QRegularExpressionMatchIterator it = eventRegex.globalMatch(content);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        if (match.hasMatch()) {
            SubtitleEntry entry;
            entry.startTime = parseTimecode(match.captured(1));
            entry.endTime = parseTimecode(match.captured(2));

            QString text = match.captured(3);
            text.replace(R"(\N)", "\n");
            text.replace(R"(\n)", "\n");
            
            // Remove ASS override tags like \pos(x,y), \fs20, etc.
            static const QRegularExpression assTagRegex(R"(\\[a-zA-Z]+\([^)]*\))");
            static const QRegularExpression assSimpleTagRegex(R"(\\[a-zA-Z]+\d+)");
            text.remove(assTagRegex);
            text.remove(assSimpleTagRegex);
            text = text.trimmed();

            if (!text.isEmpty()) {
                entry.text = text;
                m_entries.append(entry);
            }
        }
    }

    return !m_entries.isEmpty();
}

bool SubtitleParser::parseVTT(const QString& content)
{
    static const QRegularExpression blockRegex(
        R"((?:.*\n)?(\d{2}:\d{2}:\d{2}\.\d{3}|\d{2}:\d{2}\.\d{3})\s*-->\s*(\d{2}:\d{2}:\d{2}\.\d{3}|\d{2}:\d{2}\.\d{3})\s*\n((?:.*\n?)*?)(?=\n\n|\n*$))",
        QRegularExpression::MultilineOption);

    QRegularExpressionMatchIterator it = blockRegex.globalMatch(content);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        if (match.hasMatch()) {
            SubtitleEntry entry;
            entry.startTime = parseTimecode(match.captured(1));
            entry.endTime = parseTimecode(match.captured(2));
            entry.text = match.captured(3).trimmed();
            entry.text.remove(QRegularExpression(R"(<[^>]*>)"));
            m_entries.append(entry);
        }
    }

    return !m_entries.isEmpty();
}

qint64 SubtitleParser::parseTimecode(const QString& timecode)
{
    static const QRegularExpression tcRegex(
        R"((?:(\d{2}):)?(\d{2}):(\d{2})[,.](\d{2,3}))");

    QRegularExpressionMatch match = tcRegex.match(timecode);
    if (!match.hasMatch()) {
        return 0;
    }

    int hours = match.captured(1).isEmpty() ? 0 : match.captured(1).toInt();
    int minutes = match.captured(2).toInt();
    int seconds = match.captured(3).toInt();
    int millis = match.captured(4).toInt();

    if (match.captured(4).length() == 2) {
        millis *= 10;
    }

    return static_cast<qint64>(hours) * 3600000LL +
           static_cast<qint64>(minutes) * 60000LL +
           static_cast<qint64>(seconds) * 1000LL +
           static_cast<qint64>(millis);
}
