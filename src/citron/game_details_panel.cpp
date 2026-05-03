#include <QDateTime>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include "citron/game_details_panel.h"
#include "citron/game_list.h"
#include "citron/game_list_p.h"
#include "citron/theme.h"
#include "citron/uisettings.h"


GameDetailsPanel::GameDetailsPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("GameDetailsPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(280);
    setupUI();
    updateStyles();

    m_debounce_timer = new QTimer(this);
    m_debounce_timer->setSingleShot(true);
    m_debounce_timer->setInterval(50);
    connect(m_debounce_timer, &QTimer::timeout, this, [this] {
        if (m_pending_index.isValid()) {
            applyDetails(m_pending_index);
        }
    });
}

GameDetailsPanel::~GameDetailsPanel() = default;

void GameDetailsPanel::setupUI() {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    m_bg_label = new QLabel(this);
    m_bg_label->setObjectName(QStringLiteral("panelBackground"));
    m_bg_label->setScaledContents(true);
    m_bg_label->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* blur = new QGraphicsBlurEffect(this);
    blur->setBlurRadius(60);
    m_bg_label->setGraphicsEffect(blur);

    auto* content_container = new QWidget(this);
    content_container->setObjectName(QStringLiteral("contentContainer"));
    auto* content_layout = new QVBoxLayout(content_container);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(0);

    auto* header_container = new QWidget(this);
    header_container->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_header_layout = new QVBoxLayout(header_container);
    m_header_layout->addStretch(1);
    m_header_layout->setContentsMargins(35, 30, 35, 10);
    m_header_layout->setSpacing(0);

    m_icon_label = new QLabel(header_container);
    m_icon_label->setFixedSize(160, 160);
    m_icon_label->setAlignment(Qt::AlignCenter);

    m_title_label = new QLabel(header_container);
    m_title_label->setWordWrap(true);
    m_title_label->setAlignment(Qt::AlignCenter);
    QFont title_font = m_title_label->font();
    title_font.setPointSizeF(18.0);
    title_font.setWeight(QFont::Bold);
    title_font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    m_title_label->setFont(title_font);
    m_title_label->setStyleSheet(QStringLiteral("color: white;"));


    m_meta_card = new QFrame(header_container);
    m_meta_card->setObjectName(QStringLiteral("metaCard"));
    m_meta_card->setFixedHeight(30);
    // Dynamic width handled in resizeEvent
    
    auto* meta_inner_layout = new QHBoxLayout(m_meta_card);
    meta_inner_layout->setContentsMargins(0, 0, 0, 0);

    m_id_label = new QLabel(m_meta_card);
    m_id_label->setAlignment(Qt::AlignCenter);
    QFont id_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    id_font.setPointSizeF(10.5); // Slightly smaller for better fit
    id_font.setBold(true);
    id_font.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    m_id_label->setFont(id_font);
    m_id_label->setFont(id_font);
    meta_inner_layout->addWidget(m_id_label);
    
    m_header_layout->addWidget(m_icon_label, 0, Qt::AlignCenter);
    m_header_layout->addSpacing(15);
    m_header_layout->addWidget(m_title_label, 0, Qt::AlignCenter);
    m_header_layout->addSpacing(15);
    m_header_layout->addWidget(m_meta_card, 0, Qt::AlignCenter);
    m_header_layout->addStretch(1);

    content_layout->addWidget(header_container);

    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setStyleSheet(QStringLiteral("background: transparent;"));
    m_scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_actions_container = new QWidget(m_scroll_area);
    m_actions_container->setStyleSheet(QStringLiteral("background: transparent;"));
    m_actions_layout = new QVBoxLayout(m_actions_container);
    m_actions_layout->addStretch(1);
    m_actions_layout->setContentsMargins(25, 10, 25, 10);
    m_actions_layout->setSpacing(0);
    m_scroll_area->setWidget(m_actions_container);
    content_layout->addWidget(m_scroll_area);

    main_layout->addWidget(content_container);

    auto* icon_glow = new QGraphicsDropShadowEffect(m_icon_label);
    icon_glow->setBlurRadius(35);
    icon_glow->setOffset(0);
    m_icon_label->setGraphicsEffect(icon_glow);

    auto* pulse = new QVariantAnimation(this);
    pulse->setDuration(4000);
    pulse->setStartValue(10.0);
    pulse->setEndValue(45.0);
    pulse->setEasingCurve(QEasingCurve::InOutSine);
    pulse->setLoopCount(-1);
    connect(pulse, &QVariantAnimation::valueChanged, this, [icon_glow](const QVariant& value) {
        const QString hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
        QColor accent = QColor(hex).isValid() ? QColor(hex) : QColor(0, 150, 255);
        accent.setAlphaF(static_cast<float>(0.15 + (value.toReal() / 100.0)));
        icon_glow->setColor(accent);
        icon_glow->setBlurRadius(value.toReal());
    });
    pulse->start();
}

bool GameDetailsPanel::IsDarkMode() const {
    return Theme::IsDarkMode();
}

void GameDetailsPanel::updateStyles() {
    const bool is_dark = IsDarkMode();

    const QString panel_bg = is_dark ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
    const QString overlay_bg = is_dark ? QStringLiteral("rgba(15, 15, 20, 0.7)")
                                       : QStringLiteral("rgba(255, 255, 255, 0.75)");
    const QString panel_border = is_dark ? QStringLiteral("#222228") : QStringLiteral("#e0e0e0");

    setStyleSheet(
        QStringLiteral("QWidget#GameDetailsPanel { background: %1; border: none; }"
                       "QWidget#contentContainer { background: %2; border-left: 2px solid %3; }")
            .arg(panel_bg, overlay_bg, panel_border));

    const QString icon_bg = is_dark ? QStringLiteral("rgba(0,0,0,0.4)") : QStringLiteral("#f0f0f0");
    const QString icon_border =
        is_dark ? QStringLiteral("rgba(255,255,255,0.18)") : QStringLiteral("rgba(0,0,0,0.1)");
    m_icon_label->setStyleSheet(
        QStringLiteral("border: 2px solid %1; border-radius: 30px; background: %2;")
            .arg(icon_border, icon_bg));

    const QString title_color = is_dark ? QStringLiteral("#fff") : QStringLiteral("#111");
    const QString meta_bg = is_dark ? QStringLiteral("rgba(255, 255, 255, 0.08)")
                                    : QStringLiteral("rgba(0, 0, 0, 0.03)");
    const QString meta_border = is_dark ? QStringLiteral("rgba(255, 255, 255, 0.12)")
                                        : QStringLiteral("rgba(0, 0, 0, 0.08)");

    m_title_label->setStyleSheet(
        QStringLiteral("color: %1; background: transparent; font-weight: bold; line-height: 120%;").arg(title_color));

    m_meta_card->setStyleSheet(
        QStringLiteral("QFrame#metaCard {"
                       "  background: %1;"
                       "  border: 1px solid %2;"
                       "  border-radius: 15px;"
                       "  margin-top: 6px;"
                       "}")
            .arg(meta_bg, meta_border));

    m_id_label->setStyleSheet(QStringLiteral("color: %1; font-weight: bold; background: transparent; border: none;").arg(title_color));
}

void GameDetailsPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    m_bg_label->setGeometry(rect());

    int w = width();
    int target_size = qBound(140, w - 120, 280);

    if (m_icon_label->width() != target_size) {
        m_icon_label->setFixedSize(target_size, target_size);
        if (m_current_program_id != 0) {
            applyDetails(m_pending_index.isValid() ? QModelIndex(m_pending_index) : QModelIndex());
        }
    }

    // Update meta card width and margins to ensure centering on small displays
    const int margin = qBound(15, w / 10, 35);
    int top_margin = height() < 800 ? 30 : 40;
    m_header_layout->setContentsMargins(margin, top_margin, margin, 10);

    const int meta_w = qBound(160, w - (margin * 4), 240);
    if (m_meta_card->width() != meta_w) {
        m_meta_card->setFixedWidth(meta_w);
    }

    // (Removed dynamic font size overriding to respect settings/defaults)

    // (Removed dynamic font size overriding to respect settings/defaults)
}

void GameDetailsPanel::updateDetails(const QModelIndex& index) {
    if (!index.isValid()) {
        m_debounce_timer->stop();
        m_pending_index = QModelIndex();
        hide();
        return;
    }
    u64 program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    if (program_id == m_current_program_id && isVisible()) {
        m_debounce_timer->stop();
        return;
    }

    m_pending_index = index;
    m_debounce_timer->start();
    show();
}

void GameDetailsPanel::ApplyTheme() {
    updateStyles();

    // Refresh existing buttons without destroying them
    const bool is_dark = IsDarkMode();
    const QString accent_hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
    const QColor accent = QColor(accent_hex).isValid() ? QColor(accent_hex) : QColor(0, 150, 255);

    const QString btn_bg = is_dark ? QStringLiteral("#121216") : QStringLiteral("#f5f5f7");
    const QString btn_border = is_dark ? QStringLiteral("#2a2a35") : QStringLiteral("#d0d0d8");
    const QString btn_text = is_dark ? QStringLiteral("#ccc") : QStringLiteral("#444");
    const QString btn_hover_bg = is_dark ? QStringLiteral("#1a1a20") : QStringLiteral("#e8e8ed");
    const QString btn_hover_text = is_dark ? QStringLiteral("#fff") : QStringLiteral("#000");
    const QString btn_pressed_bg = is_dark ? QStringLiteral("#050507") : QStringLiteral("#dcdce2");

    const QString btn_style =
        QStringLiteral("QPushButton {"
                       "  background: %5; color: %7; border: 1px solid %6;"
                       "  border-radius: 12px; font-weight: bold; padding-left: 20px; text-align: "
                       "left; font-size: 11.5pt;"
                       "}"
                       "QPushButton:hover { background: %8; border-color: %4; color: %9; }"
                       "QPushButton:focus { background: rgba(%1, %2, %3, 0.2); border: 2px solid "
                       "%4; color: %9; }"
                       "QPushButton:pressed { background: %10; }")
            .arg(accent.red())
            .arg(accent.green())
            .arg(accent.blue())
            .arg(accent.name())
            .arg(btn_bg, btn_border, btn_text, btn_hover_bg, btn_hover_text, btn_pressed_bg);

    for (auto* btn : m_action_buttons) {
        btn->setStyleSheet(btn_style);
    }

    // Style the ScrollArea's scrollbar specifically
    const QString scroll_style =
        QStringLiteral("QScrollBar:vertical {"
                       "    background: transparent;"
                       "    width: 12px;"
                       "    margin: 0px;"
                       "}"
                       "QScrollBar::handle:vertical {"
                       "    background: %1;"
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
            .arg(accent_hex);

    if (m_scroll_area && m_scroll_area->verticalScrollBar()) {
        m_scroll_area->verticalScrollBar()->setStyleSheet(scroll_style);
    }

    update();
}

void GameDetailsPanel::setControllerFocus(bool focus) {
    m_has_focus = focus;
    if (m_has_focus) {
        if (m_focused_button_index == -1 && !m_action_buttons.isEmpty())
            m_focused_button_index = 0;
        if (m_focused_button_index >= 0 && m_focused_button_index < m_action_buttons.size())
            m_action_buttons[m_focused_button_index]->setFocus();
    } else {
        if (m_focused_button_index >= 0 && m_focused_button_index < m_action_buttons.size())
            m_action_buttons[m_focused_button_index]->clearFocus();
    }
}

void GameDetailsPanel::onNavigated(int dx, int dy) {
    if (!m_has_focus || m_action_buttons.isEmpty())
        return;
    int next_index = m_focused_button_index + dy;
    if (next_index >= 0 && next_index < m_action_buttons.size()) {
        m_focused_button_index = next_index;
        m_action_buttons[m_focused_button_index]->setFocus();
        m_scroll_area->ensureWidgetVisible(m_action_buttons[m_focused_button_index]);
    }
}

void GameDetailsPanel::onActivated() {
    if (m_has_focus && m_focused_button_index >= 0 &&
        m_focused_button_index < m_action_buttons.size())
        m_action_buttons[m_focused_button_index]->animateClick();
}

void GameDetailsPanel::onCancelled() {
    emit focusReturned();
}

void GameDetailsPanel::applyDetails(const QModelIndex& index) {
    m_current_program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    m_current_path = index.data(GameListItemPath::FullPathRole).toString();

    QPixmap pixmap = index.data(GameListItemPath::HighResIconRole).value<QPixmap>();
    if (pixmap.isNull())
        pixmap = index.data(Qt::DecorationRole).value<QPixmap>();

    if (!pixmap.isNull()) {
        const int is = m_icon_label->width();

        QPixmap rounded(is, is);
        rounded.fill(Qt::transparent);
        {
            QPainter painter(&rounded);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            QPainterPath path;
            path.addRoundedRect(0, 0, is, is, 28, 28);
            painter.setClipPath(path);
            painter.drawPixmap(
                0, 0,
                pixmap.scaled(is, is, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        }
        m_icon_label->setPixmap(rounded);
        m_bg_pixmap = pixmap;
        m_bg_label->setPixmap(m_bg_pixmap);
    }

    QString title = index.data(Qt::DisplayRole).toString();
    if (title.contains(QLatin1Char('\n')))
        title = title.split(QLatin1Char('\n')).first();
    m_title_label->setText(title);
    
    // Decisive Two-Line Limit: Uses QTextDocument for perfect line-count simulation
    QFont title_font = m_title_label->font();
    qreal point_size = 18.0;
    
    // Conservative width calculation
    const int margin = qBound(15, width() / 10, 35);
    const int target_width = std::max(160, width() - (margin * 2) - 40);
    
    QTextDocument doc;
    doc.setUndoRedoEnabled(false);
    
    while (point_size > 10.0) {
        title_font.setPointSizeF(point_size);
        doc.setDefaultFont(title_font);
        doc.setPlainText(title);
        doc.setTextWidth(target_width);
        
        // Strictly break only if it fits in 2 lines or fewer
        if (doc.lineCount() <= 2) {
            break;
        }
        point_size -= 0.5;
    }
    
    title_font.setPointSizeF(point_size);
    m_title_label->setFont(title_font);

    m_id_label->setText(
        QStringLiteral("0x%1").arg(m_current_program_id, 16, 16, QLatin1Char('0')).toUpper());

    clearActions();
    
    addAction(tr("Launch Game"), QStringLiteral("start"));
    m_actions_layout->addSpacing(10);
    addAction(tr("Favorite"), QStringLiteral("favorite"));
    m_actions_layout->addSpacing(10);
    addAction(tr("Properties"), QStringLiteral("properties"));
    m_actions_layout->addSpacing(10);
    addAction(tr("Open Save Data"), QStringLiteral("save_data"));
    m_actions_layout->addSpacing(10);
    addAction(tr("Open Mod Location"), QStringLiteral("mod_data"));
    m_actions_layout->addSpacing(10);
    addAction(tr("Download Icon..."), QStringLiteral("download_icon"));

    // Poster selection is a Grid View specific feature
    auto* game_list = qobject_cast<GameList*>(parent());
    if (game_list && game_list->GetViewMode() == GameList::ViewMode::Grid) {
        m_actions_layout->addSpacing(10);
        addAction(tr("Download Poster..."), QStringLiteral("download_poster"));
    }

    m_actions_layout->addStretch(1);
}

void GameDetailsPanel::clearActions() {
    m_action_buttons.clear();
    m_focused_button_index = -1;
    QLayoutItem* item;
    while ((item = m_actions_layout->takeAt(0)) != nullptr) {
        if (item->widget())
            delete item->widget();
        delete item;
    }
}

void GameDetailsPanel::addAction(const QString& label, const QString& action_id) {
    auto* btn = new QPushButton(label, this);
    btn->setFixedHeight(34);
    btn->setCursor(Qt::PointingHandCursor);

    const bool is_dark = IsDarkMode();
    const QString accent_hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
    const QColor accent = QColor(accent_hex).isValid() ? QColor(accent_hex) : QColor(0, 150, 255);

    const QString btn_bg = is_dark ? QStringLiteral("rgba(30, 30, 40, 0.4)")
                                   : QStringLiteral("rgba(245, 245, 247, 0.6)");
    const QString btn_border = is_dark ? QStringLiteral("rgba(255, 255, 255, 0.08)")
                                       : QStringLiteral("rgba(0, 0, 0, 0.1)");
    const QString btn_text = is_dark ? QStringLiteral("#eee") : QStringLiteral("#333");
    const QString btn_hover_bg = is_dark ? QStringLiteral("rgba(45, 45, 60, 0.6)")
                                         : QStringLiteral("rgba(232, 232, 237, 0.8)");
    const QString btn_hover_text = is_dark ? QStringLiteral("#fff") : QStringLiteral("#000");
    const QString btn_pressed_bg = is_dark ? QStringLiteral("rgba(10, 10, 15, 0.8)")
                                           : QStringLiteral("rgba(220, 220, 226, 0.9)");

    const QString btn_style =
        QStringLiteral(
            "QPushButton {"
            "  background: %5; color: %7; border: 1px solid %6;"
            "  border-radius: 12px; font-weight: bold; text-align: center; font-size: 11.5pt;"
            "}"
            "QPushButton:hover { background: %8; border-color: %4; color: %9; }"
            "QPushButton:focus { background: rgba(%1, %2, %3, 0.4); border: 2px solid %4; color: "
            "%9; }"
            "QPushButton:pressed { background: %10; }")
            .arg(accent.red())
            .arg(accent.green())
            .arg(accent.blue())
            .arg(accent.name())
            .arg(btn_bg, btn_border, btn_text, btn_hover_bg, btn_hover_text, btn_pressed_bg);

    btn->setStyleSheet(btn_style);

    connect(btn, &QPushButton::clicked, this, [this, action_id]() {
        emit actionTriggered(action_id, m_current_program_id, m_current_path);
    });
    m_action_buttons.append(btn);
    m_actions_layout->addWidget(btn);
}
