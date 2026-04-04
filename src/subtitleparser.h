#ifndef SUBTITLEPARSER_H
#define SUBTITLEPARSER_H

#include <QObject>
#include <QString>
#include <QList>

struct SubtitleEntry {
    qint64 startTime;  // milliseconds
    qint64 endTime;    // milliseconds
    QString text;
};

class SubtitleParser : public QObject {
    Q_OBJECT
public:
    explicit SubtitleParser(QObject* parent = nullptr);
    bool loadFile(const QString& filePath);
    QString subtitleAt(qint64 ms) const;
    QList<SubtitleEntry> entries() const;
    void clear();
    bool isLoaded() const;

private:
    bool parseSRT(const QString& content);
    bool parseASS(const QString& content);
    bool parseVTT(const QString& content);
    qint64 parseTimecode(const QString& timecode);

    QList<SubtitleEntry> m_entries;
    QString m_filePath;
};

#endif // SUBTITLEPARSER_H
