#pragma once

#include <QStyledItemDelegate>

class ChatRoomMemberDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ChatRoomMemberDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    class QTimer* anim_timer;
    int anim_offset = 0;

private slots:
    void AdvanceAnimations();
};
