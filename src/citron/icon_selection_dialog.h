#pragma once

#include <vector>
#include <QDialog>
#include <QNetworkAccessManager>
#include "citron/util/steam_grid_db.h"

class QGridLayout;
class QScrollArea;
class QLabel;

class IconSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit IconSelectionDialog(QWidget* parent, u64 program_id, const QString& game_name,
                                 Citron::SteamGridDB* sgdb);
    ~IconSelectionDialog() override;

public slots:
    void onNavigated(int dx, int dy);
    void onActivated();
    void onCancelled();

private:
    void FetchOptions();
    void OnOptionsFetched(bool success, std::vector<Citron::SteamGridDBPoster> options);
    void AddIconOption(const Citron::SteamGridDBPoster& poster, int index);
    void DownloadThumbnail(const QString& url, QLabel* target_label);
    void OnIconSelected(const std::string& url);

    u64 m_program_id;
    QString m_game_name;
    Citron::SteamGridDB* m_sgdb;
    QNetworkAccessManager m_network_manager;

    QScrollArea* m_scroll_area;
    QWidget* m_container;
    QGridLayout* m_layout;
    QLabel* m_loading_label;
    QList<QPushButton*> m_buttons;
    int m_current_index = -1;
};
