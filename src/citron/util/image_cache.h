#pragma once

#include <QCache>
#include <QPixmap>
#include <QString>
#include <optional>
#include <string>
#include "citron/custom_metadata.h"
#include "common/common_types.h"

namespace Citron {

class ImageCache {
public:
    static QPixmap GetCustomIcon(u64 program_id) {
        if (auto* cached = GetIconCache().object(program_id)) {
            return *cached;
        }

        auto path = CustomMetadata::GetInstance().GetCustomIconPath(program_id);
        if (path && !path->empty()) {
            QPixmap pixmap;
            if (pixmap.load(QString::fromStdString(*path))) {
                GetIconCache().insert(program_id, new QPixmap(pixmap));
                return pixmap;
            }
        }
        return QPixmap();
    }

    static QPixmap GetCustomPoster(u64 program_id) {
        if (auto* cached = GetPosterCache().object(program_id)) {
            return *cached;
        }

        auto path = CustomMetadata::GetInstance().GetCustomPosterPath(program_id);
        if (path && !path->empty()) {
            QPixmap pixmap;
            if (pixmap.load(QString::fromStdString(*path))) {
                GetPosterCache().insert(program_id, new QPixmap(pixmap));
                return pixmap;
            }
        }
        return QPixmap();
    }

    static void Clear() {
        GetIconCache().clear();
        GetPosterCache().clear();
    }

    static void InvalidateIcon(u64 program_id) {
        GetIconCache().remove(program_id);
    }

    static void InvalidatePoster(u64 program_id) {
        GetPosterCache().remove(program_id);
    }

private:
    static QCache<u64, QPixmap>& GetIconCache() {
        static QCache<u64, QPixmap> cache(200); // Cache up to 200 icons
        return cache;
    }

    static QCache<u64, QPixmap>& GetPosterCache() {
        static QCache<u64, QPixmap> cache(100); // Cache up to 100 posters
        return cache;
    }
};

} // namespace Citron
