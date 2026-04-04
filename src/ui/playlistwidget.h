#ifndef PLAYLISTWIDGET_H
#define PLAYLISTWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QStringList>

class PlaylistWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlaylistWidget(QWidget* parent = nullptr);

    void addItem(const QString& filePath);
    void addItems(const QStringList& files);
    void removeItem(int index);
    void clear();

    int count() const;
    QString item(int index) const;
    int currentIndex() const;
    void setCurrentIndex(int index);

signals:
    void itemDoubleClicked(int index);
    void itemRemoved(int index);
    void currentIndexChanged(int index);

private slots:
    void onItemDoubleClicked(QListWidgetItem* item);
    void onContextMenuRequested(const QPoint& pos);

private:
    void highlightCurrentItem();

    QListWidget* m_list;
    int m_currentIndex;
};

#endif // PLAYLISTWIDGET_H
