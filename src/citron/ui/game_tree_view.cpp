#include "citron/ui/game_tree_view.h"
#include <QMouseEvent>
#include <QHeaderView>
#include <QPainter>
#include <QScrollBar>
#include "citron/uisettings.h"
#include "citron/theme.h"

GameTreeView::GameTreeView(QWidget* parent) : QTreeView(parent) {
    setFrameStyle(QFrame::NoFrame);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAllColumnsShowFocus(true);
    setUniformRowHeights(false);
    setSortingEnabled(true);
    setIndentation(20);
    setAlternatingRowColors(false);
    header()->setHighlightSections(false);
    header()->setStretchLastSection(true);
    header()->setSectionsMovable(true);
    ApplyTheme();

    connect(this, &QTreeView::activated, this, &GameTreeView::itemActivated);
}

void GameTreeView::ApplyTheme() {
    const bool is_dark = UISettings::IsDarkTheme();
    const QString accent_color = Theme::GetAccentColor();

    const QString header_bg_hex =
        QString::fromStdString(UISettings::values.custom_header_bg_color.GetValue());
    QColor header_bg_color = QColor(header_bg_hex).isValid()
                               ? QColor(header_bg_hex)
                               : (is_dark ? QColor(28, 28, 30) : QColor(236, 236, 240));
    const u8 header_opacity = UISettings::values.custom_header_opacity.GetValue();
    header_bg_color.setAlpha(header_opacity);

    const QString header_text_hex =
        QString::fromStdString(UISettings::values.custom_header_text_color.GetValue());
    const QColor header_text_color =
        QColor(header_text_hex).isValid()
            ? QColor(header_text_hex)
            : (is_dark ? QColor(208, 208, 224) : QColor(26, 26, 30));

    const QString header_bg_rgba =
        QStringLiteral("rgba(%1, %2, %3, %4)")
            .arg(header_bg_color.red())
            .arg(header_bg_color.green())
            .arg(header_bg_color.blue())
            .arg(header_bg_color.alphaF());

    const QString list_bg_hex =
        QString::fromStdString(UISettings::values.custom_list_bg_color.GetValue());
    QString list_bg_qss = QColor(list_bg_hex).isValid()
                              ? QStringLiteral("background-color: %1 !important;").arg(list_bg_hex)
                              : QStringLiteral("background: transparent !important;");

    QString qss = QStringLiteral(
                      "QTreeView { %1 border: none; outline: 0; "
                      "selection-background-color: transparent !important; "
                      "show-decoration-selected: 0; }"
                      "QTreeView::item { padding: 0px; border: none; background: transparent; }"
                      "QTreeView::item:selected { background: transparent !important; outline: 0; }"
                      "QTreeView::item:hover { background: transparent !important; }"
                      "QHeaderView { background: %2 !important; border: none; }"
                      "QHeaderView::section { background-color: %2 !important; color: %3 !important; "
                      "border: none; border-bottom: 1px solid rgba(255,255,255,0.1); "
                      "padding: 6px 10px; font-weight: bold; font-size: 9pt; }"
                      "QHeaderView::section:hover { background-color: rgba(255,255,255,0.1); }"
                      
                      "QScrollBar:vertical {"
                      "    background: transparent;"
                      "    width: 12px;"
                      "    margin: 0px;"
                      "}"
                      "QScrollBar::handle:vertical {"
                      "    background: %4;"
                      "    min-height: 20px;"
                      "    border-radius: 6px;"
                      "    margin: 2px;"
                      "}"
                      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
                      "    height: 0px;"
                      "    background: none;"
                      "}"
                      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
                      "    background: none;"
                      "}")
                      .arg(list_bg_qss, header_bg_rgba, header_text_color.name(), accent_color);
    
    setStyleSheet(qss);
    if (verticalScrollBar()) {
        verticalScrollBar()->setStyleSheet(qss);
    }
    if (header()) {
        header()->setStyleSheet(qss);
    }
}

void GameTreeView::setModel(QAbstractItemModel* model) {
    QTreeView::setModel(model);
}

void GameTreeView::onNavigated(int dx, int dy) {
    if (!m_has_focus) return;
    auto* cur_model = model();
    if (!cur_model || cur_model->rowCount() == 0) return;

    QModelIndex current = currentIndex();
    if (!current.isValid()) {
        QModelIndex first = cur_model->index(0, 0);
        setCurrentIndex(first);
        scrollTo(first);
        return;
    }

    QAbstractItemView::CursorAction action;
    if (dy > 0) action = QAbstractItemView::MoveDown;
    else if (dy < 0) action = QAbstractItemView::MoveUp;
    else if (dx > 0) action = QAbstractItemView::MoveRight;
    else if (dx < 0) action = QAbstractItemView::MoveLeft;
    else return;

    QModelIndex next = moveCursor(action, Qt::NoModifier);
    if (next.isValid()) {
        setCurrentIndex(next);
        scrollTo(next);
    }
}

void GameTreeView::onActivated() {
    if (!m_has_focus) return;
    QModelIndex index = currentIndex();
    if (index.isValid()) {
        emit itemActivated(index);
    }
}

void GameTreeView::onCancelled() {}
 
void GameTreeView::paintEvent(QPaintEvent* event) {
    const QString bg_path =
        QString::fromStdString(UISettings::values.custom_game_list_bg_path.GetValue());
    if (!bg_path.isEmpty()) {
        if (m_bg_path != bg_path) {
            m_bg_path = bg_path;
            m_bg_pixmap.load(m_bg_path);
        }

        if (!m_bg_pixmap.isNull() && !viewport()->size().isEmpty()) {
            QPainter painter(viewport());
            painter.setRenderHint(QPainter::SmoothPixmapTransform);

            // Draw scaled background
            QPixmap scaled = m_bg_pixmap.scaled(viewport()->size(), Qt::KeepAspectRatioByExpanding,
                                                Qt::SmoothTransformation);
            
            if (!scaled.isNull()) {
                // Center the image
                int x = (viewport()->width() - scaled.width()) / 2;
                int y = (viewport()->height() - scaled.height()) / 2;
                painter.drawPixmap(x, y, scaled);

                // Apply dimming overlay based on opacity setting (0-255)
                // 255 = fully visible image, 0 = fully dimmed
                const u8 opacity = UISettings::values.custom_game_list_bg_opacity.GetValue();
                if (opacity < 255) {
                    painter.fillRect(viewport()->rect(), QColor(0, 0, 0, 255 - opacity));
                }
            }
        }
    }
    QTreeView::paintEvent(event);
}

void GameTreeView::mouseDoubleClickEvent(QMouseEvent* event) {
    QTreeView::mouseDoubleClickEvent(event);
}

void GameTreeView::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    QTreeView::selectionChanged(selected, deselected);
    if (!selected.indexes().isEmpty()) {
        emit itemSelectionChanged(selected.indexes().first());
    }
}
