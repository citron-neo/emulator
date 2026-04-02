#include <QTimer>
#include <QDateTime>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QResizeEvent>

#include "citron/game_details_panel.h"
#include "citron/game_list_p.h"
#include "citron/uisettings.h"

GameDetailsPanel::GameDetailsPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("GameDetailsPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(280);
    setupUI();
}

GameDetailsPanel::~GameDetailsPanel() = default;

void GameDetailsPanel::setupUI() {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);
    
    auto* header_container = new QWidget(this);
    header_container->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    auto* header_layout = new QVBoxLayout(header_container);
    header_layout->setContentsMargins(25, 25, 25, 10);
    header_layout->setSpacing(15);

    m_icon_label = new QLabel(header_container);
    m_icon_label->setFixedSize(160, 160);
    m_icon_label->setAlignment(Qt::AlignCenter);
    m_icon_label->setStyleSheet(QStringLiteral("border: 2px solid rgba(255,255,255,0.15); border-radius: 18px; background: #000;"));
    header_layout->addWidget(m_icon_label, 0, Qt::AlignCenter);
    
    m_title_card = new QFrame(header_container);
    m_title_card->setObjectName(QStringLiteral("titleCard"));
    m_title_card->setStyleSheet(QStringLiteral(
        "QFrame#titleCard {"
        "  background: rgba(255,255,255,0.04);"
        "  border-radius: 10px;"
        "  border: 1px solid rgba(255,255,255,0.1);"
        "  padding: 10px;"
        "}"
    ));
    auto* card_layout = new QVBoxLayout(m_title_card);
    card_layout->setContentsMargins(8, 8, 8, 8);
    
    m_title_label = new QLabel(m_title_card);
    m_title_label->setWordWrap(true);
    m_title_label->setAlignment(Qt::AlignCenter);
    QFont title_font = m_title_label->font();
    title_font.setPointSize(16);
    title_font.setWeight(QFont::Black);
    m_title_label->setFont(title_font);
    m_title_label->setStyleSheet(QStringLiteral("color: #fff; line-height: 1.1;"));
    card_layout->addWidget(m_title_label);
    header_layout->addWidget(m_title_card);
    
    m_meta_card = new QFrame(header_container);
    m_meta_card->setObjectName(QStringLiteral("metaCard"));
    m_meta_card->setFixedHeight(38);
    m_meta_card->setStyleSheet(QStringLiteral("QFrame#metaCard { background: rgba(255, 255, 255, 0.05); border-radius: 6px; border: 1px solid rgba(255, 255, 255, 0.05); }"));
    auto* meta_layout = new QHBoxLayout(m_meta_card);
    meta_layout->setContentsMargins(12, 0, 12, 0);
    m_id_label = new QLabel(m_meta_card);
    m_id_label->setAlignment(Qt::AlignCenter);
    QFont id_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    id_font.setPointSizeF(10.0);
    id_font.setBold(true);
    m_id_label->setFont(id_font);
    m_id_label->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.4); letter-spacing: 1.5px;"));
    meta_layout->addWidget(m_id_label);
    header_layout->addWidget(m_meta_card, 0, Qt::AlignCenter);
    main_layout->addWidget(header_container);

    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setStyleSheet(QStringLiteral("background: transparent;"));
    m_actions_container = new QWidget(m_scroll_area);
    m_actions_container->setStyleSheet(QStringLiteral("background: transparent;"));
    m_actions_layout = new QVBoxLayout(m_actions_container);
    m_actions_layout->setContentsMargins(25, 20, 25, 15);
    m_actions_layout->setSpacing(10);
    m_actions_layout->addStretch();
    m_scroll_area->setWidget(m_actions_container);
    main_layout->addWidget(m_scroll_area);
    
    setStyleSheet(QStringLiteral(
        "QWidget#GameDetailsPanel {"
        "  background: #1c1c22;"
        "  border: 2px solid #32323a;"
        "}"
    ));
    
    auto* panel_shadow = new QGraphicsDropShadowEffect(this);
    panel_shadow->setBlurRadius(30);
    panel_shadow->setOffset(-10, 0);
    panel_shadow->setColor(QColor(0, 0, 0, 80));
    setGraphicsEffect(panel_shadow);

    auto* icon_glow = new QGraphicsDropShadowEffect(m_icon_label);
    icon_glow->setBlurRadius(20);
    icon_glow->setOffset(0);
    m_icon_label->setGraphicsEffect(icon_glow);
    auto* pulse = new QVariantAnimation(this);
    pulse->setDuration(3000); 
    pulse->setStartValue(5.0);
    pulse->setEndValue(25.0);
    pulse->setEasingCurve(QEasingCurve::InOutSine);
    pulse->setLoopCount(-1);
    connect(pulse, &QVariantAnimation::valueChanged, this, [icon_glow](const QVariant& value) {
        const QString hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
        QColor accent = QColor(hex).isValid() ? QColor(hex) : QColor(0, 150, 255);
        accent.setAlphaF(0.2 + (value.toReal() / 50.0));
        icon_glow->setColor(accent);
        icon_glow->setBlurRadius(value.toReal());
    });
    pulse->start();
}

void GameDetailsPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    bool high_res = width() > 340;
    m_icon_label->setFixedSize(high_res ? 200 : 140, high_res ? 200 : 140);
    QFont tf = m_title_label->font();
    tf.setPointSize(high_res ? 19 : 14);
    m_title_label->setFont(tf);
}

void GameDetailsPanel::updateDetails(const QModelIndex& index) {
    if (!index.isValid()) { hide(); return; }
    u64 program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    if (program_id == m_current_program_id && isVisible()) return;
    applyDetails(index);
    show();
}

void GameDetailsPanel::ApplyTheme() { update(); }

void GameDetailsPanel::setControllerFocus(bool focus) {
    m_has_focus = focus;
    if (m_has_focus) {
        if (m_focused_button_index == -1 && !m_action_buttons.isEmpty()) m_focused_button_index = 0;
        if (m_focused_button_index >= 0 && m_focused_button_index < m_action_buttons.size()) m_action_buttons[m_focused_button_index]->setFocus();
    } else {
        if (m_focused_button_index >= 0 && m_focused_button_index < m_action_buttons.size()) m_action_buttons[m_focused_button_index]->clearFocus();
    }
}

void GameDetailsPanel::onNavigated(int dx, int dy) {
    if (!m_has_focus || m_action_buttons.isEmpty()) return;
    int next_index = m_focused_button_index + dy;
    if (next_index >= 0 && next_index < m_action_buttons.size()) {
        m_focused_button_index = next_index;
        m_action_buttons[m_focused_button_index]->setFocus();
        m_scroll_area->ensureWidgetVisible(m_action_buttons[m_focused_button_index]);
    }
}

void GameDetailsPanel::onActivated() {
    if (m_has_focus && m_focused_button_index >= 0 && m_focused_button_index < m_action_buttons.size()) m_action_buttons[m_focused_button_index]->animateClick();
}

void GameDetailsPanel::onCancelled() { emit focusReturned(); }

void GameDetailsPanel::applyDetails(const QModelIndex& index) {
    m_current_program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    m_current_path = index.data(GameListItemPath::FullPathRole).toString();
    QPixmap pixmap = index.data(GameListItemPath::HighResIconRole).value<QPixmap>();
    if (pixmap.isNull()) pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
    if (!pixmap.isNull()) {
        const int is = m_icon_label->width();
        QPixmap rounded(is, is);
        rounded.fill(Qt::transparent);
        {
            QPainter painter(&rounded); painter.setRenderHint(QPainter::Antialiasing);
            QPainterPath path; path.addRoundedRect(0, 0, is, is, 22, 22);
            painter.setClipPath(path); painter.drawPixmap(0, 0, pixmap.scaled(is, is, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        m_icon_label->setPixmap(rounded);
    }
    QString title = index.data(Qt::DisplayRole).toString();
    if (title.contains(QLatin1Char('\n'))) title = title.split(QLatin1Char('\n')).first();
    m_title_label->setText(title);
    m_id_label->setText(QStringLiteral("0x%1").arg(m_current_program_id, 16, 16, QLatin1Char('0')).toUpper());
    
    clearActions();
    addAction(tr("Launch Game"), QStringLiteral("start"));
    addAction(tr("Favorite"), QStringLiteral("favorite"));
    addAction(tr("Properties"), QStringLiteral("properties"));
    addAction(tr("Open Save Data"), QStringLiteral("save_data"));
    addAction(tr("Open Mod Location"), QStringLiteral("mod_data"));
    m_actions_layout->addStretch();
}

void GameDetailsPanel::clearActions() {
    m_action_buttons.clear(); m_focused_button_index = -1;
    QLayoutItem* item;
    while ((item = m_actions_layout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }
}

void GameDetailsPanel::addAction(const QString& label, const QString& action_id) {
    auto* btn = new QPushButton(label, this);
    btn->setFixedHeight(48); btn->setCursor(Qt::PointingHandCursor);
    const QString accent_hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
    const QColor accent = QColor(accent_hex).isValid() ? QColor(accent_hex) : QColor(0, 150, 255);
    btn->setStyleSheet(QString::fromLatin1(
        "QPushButton {"
        "  background: #121216; color: #ccc; border: 1px solid #2a2a35;"
        "  border-radius: 9px; font-weight: bold; padding-left: 15px; text-align: left; font-size: 10.5pt;"
        "}"
        "QPushButton:hover { background: #1a1a20; border-color: %4; color: #fff; }"
        "QPushButton:focus { background: rgba(%1, %2, %3, 0.25); border: 2px solid %4; color: #fff; }"
        "QPushButton:pressed { background: #050507; }"
    ).arg(accent.red()).arg(accent.green()).arg(accent.blue()).arg(accent.name()));
    connect(btn, &QPushButton::clicked, this, [this, action_id]() { emit actionTriggered(action_id, m_current_program_id, m_current_path); });
    m_action_buttons.append(btn); m_actions_layout->addWidget(btn);
}
