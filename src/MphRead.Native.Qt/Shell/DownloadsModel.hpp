#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>

#include <memory>
#include <vector>

namespace MphRead::Qt
{
    // The Download maps page: the levels on FruityPrime's map server
    // (MapGen::MapCatalog), each with its picture -- fetched in the
    // background -- and whether the map folder has it, and the downloads
    // asked for, one pack at a time. A pack that arrives is listed and
    // prepared as the others are (ShellBridge::GameFilesChanged).
    class DownloadsModel : public QObject
    {
        Q_OBJECT
        // One a level: file, level, size (MB), preview (a file URL or ""),
        // installed, downloading.
        Q_PROPERTY(QVariantList maps READ Maps NOTIFY mapsChanged)
        Q_PROPERTY(bool loading READ Loading NOTIFY stateChanged)
        Q_PROPERTY(bool busy READ Busy NOTIFY stateChanged)
        Q_PROPERTY(QString note READ Note NOTIFY stateChanged)
        Q_PROPERTY(int missing READ Missing NOTIFY mapsChanged)

    public:
        explicit DownloadsModel(QObject* parent = nullptr);
        ~DownloadsModel() override;

        [[nodiscard]] QVariantList Maps() const { return _maps; }
        [[nodiscard]] bool Loading() const noexcept { return _loading; }
        [[nodiscard]] bool Busy() const noexcept { return !_queue.empty() || _current.size() > 0; }
        [[nodiscard]] QString Note() const { return _note; }
        [[nodiscard]] int Missing() const;

        Q_INVOKABLE void refresh();
        Q_INVOKABLE void download(const QString& file);
        Q_INVOKABLE void downloadAll();

    signals:
        void mapsChanged();
        void stateChanged();

    private:
        struct Pack;
        void Rebuild();
        void Next();
        void FetchPreviews();

        std::shared_ptr<std::vector<Pack>> _packs;
        QVariantList _maps;
        bool _loading = false;
        QString _note;
        std::vector<QString> _queue;
        QString _current;
        std::shared_ptr<bool> _alive;
    };
}
