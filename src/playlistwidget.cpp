#include "playlistwidget.h"

#include <QVBoxLayout>
#include <QMenu>
#include <QAction>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>

PlaylistWidget::PlaylistWidget(QWidget* parent)
    : QWidget(parent)
    , m_list(new QListWidget(this))
    , m_currentIndex(-1)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);

    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_list, &QListWidget::itemDoubleClicked,
            this, &PlaylistWidget::onItemDoubleClicked);
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &PlaylistWidget::onContextMenuRequested);
}

void PlaylistWidget::addItem(const QString& filePath)
{
    m_list->addItem(QFileInfo(filePath).fileName());
    m_list->item(m_list->count() - 1)->setData(Qt::UserRole, filePath);
}

void PlaylistWidget::addItems(const QStringList& files)
{
    for (const QString& file : files)
        addItem(file);
}

void PlaylistWidget::removeItem(int index)
{
    if (index < 0 || index >= m_list->count())
        return;

    delete m_list->takeItem(index);

    if (m_currentIndex == index) {
        m_currentIndex = -1;
        emit currentIndexChanged(m_currentIndex);
    } else if (m_currentIndex > index) {
        --m_currentIndex;
        emit currentIndexChanged(m_currentIndex);
    }

    emit itemRemoved(index);
}

void PlaylistWidget::clear()
{
    m_list->clear();
    m_currentIndex = -1;
    emit currentIndexChanged(m_currentIndex);
}

int PlaylistWidget::count() const
{
    return m_list->count();
}

QString PlaylistWidget::item(int index) const
{
    if (index < 0 || index >= m_list->count())
        return QString();

    return m_list->item(index)->data(Qt::UserRole).toString();
}

int PlaylistWidget::currentIndex() const
{
    return m_currentIndex;
}

void PlaylistWidget::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_list->count())
        return;

    m_currentIndex = index;
    highlightCurrentItem();
    emit currentIndexChanged(m_currentIndex);
}

void PlaylistWidget::onItemDoubleClicked(QListWidgetItem* item)
{
    int index = m_list->row(item);
    setCurrentIndex(index);
    emit itemDoubleClicked(index);
}

void PlaylistWidget::onContextMenuRequested(const QPoint& pos)
{
    QListWidgetItem* item = m_list->itemAt(pos);
    if (!item)
        return;

    int index = m_list->row(item);

    QMenu menu(this);

    QAction* playAction = menu.addAction(tr("Play"));
    QAction* removeAction = menu.addAction(tr("Remove"));
    menu.addSeparator();
    QAction* explorerAction = menu.addAction(tr("Show in Explorer"));

    QAction* selected = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (!selected)
        return;

    if (selected == playAction) {
        setCurrentIndex(index);
        emit itemDoubleClicked(index);
    } else if (selected == removeAction) {
        removeItem(index);
    } else if (selected == explorerAction) {
        QString path = item->data(Qt::UserRole).toString();
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    }
}

void PlaylistWidget::highlightCurrentItem()
{
    for (int i = 0; i < m_list->count(); ++i) {
        auto* listItem = m_list->item(i);
        if (i == m_currentIndex) {
            QFont font = listItem->font();
            font.setBold(true);
            listItem->setFont(font);
            listItem->setForeground(QColor(0, 120, 215));
            listItem->setSelected(true);
        } else {
            QFont font = listItem->font();
            font.setBold(false);
            listItem->setFont(font);
            listItem->setForeground(palette().color(QPalette::Text));
        }
    }
}
