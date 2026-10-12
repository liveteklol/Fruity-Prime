#include "DownloadsModel.hpp"

#include "ShellBridge.hpp"
#include "../../MphRead.Native/Mods/MapGen/CustomRooms.hpp"
#include "../../MphRead.Native/Mods/MapGen/MapCatalog.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <thread>
#include <utility>

namespace MphRead::Qt
{
    using ::MphRead::Mods::MapGen::MapCatalog;

    struct DownloadsModel::Pack
    {
        MapCatalog::Pack Info;
        // Level -> its picture's file URL, once fetched.
        std::map<std::string, QString> Pictures;
    };

    namespace
    {
        // Work off the window's thread, then `done` back on it while the
        // page is still there.
        void Off(QObject* owner, std::function<std::function<void()>()> work)
        {
            QPointer<QObject> guard(owner);
            std::thread([guard, work = std::move(work)]()
            {
                std::function<void()> done = work();
                QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, done = std::move(done)]()
                {
                    if (guard != nullptr && done)
                    {
                        done();
                    }
                }, ::Qt::QueuedConnection);
            }).detach();
        }
    }

    DownloadsModel::DownloadsModel(QObject* parent)
        : QObject(parent), _packs(std::make_shared<std::vector<Pack>>())
    {
        refresh();
    }

    DownloadsModel::~DownloadsModel() = default;

    int DownloadsModel::Missing() const
    {
        int count = 0;
        for (const Pack& pack : *_packs)
        {
            count += pack.Info.Installed ? 0 : 1;
        }
        return count;
    }

    void DownloadsModel::refresh()
    {
        if (_loading)
        {
            return;
        }
        _loading = true;
        _note = QStringLiteral("Looking for maps on the server…");
        emit stateChanged();
        Off(this, [this]() -> std::function<void()>
        {
            std::optional<std::vector<MapCatalog::Pack>> listed = MapCatalog::List();
            return [this, listed = std::move(listed)]()
            {
                _loading = false;
                if (!listed.has_value())
                {
                    _note = QStringLiteral("The map server could not be reached.");
                    emit stateChanged();
                    return;
                }
                auto packs = std::make_shared<std::vector<Pack>>();
                for (const MapCatalog::Pack& info : *listed)
                {
                    Pack pack{info, {}};
                    // Pictures already fetched stay.
                    for (const Pack& old : *_packs)
                    {
                        if (old.Info.File == info.File)
                        {
                            pack.Pictures = old.Pictures;
                        }
                    }
                    packs->push_back(std::move(pack));
                }
                _packs = std::move(packs);
                _note = _packs->empty() ? QStringLiteral("There are no maps on the server yet.")
                    : QString();
                Rebuild();
                emit stateChanged();
                FetchPreviews();
            };
        });
    }

    void DownloadsModel::FetchPreviews()
    {
        std::vector<std::pair<std::string, std::string>> wanted;
        for (const Pack& pack : *_packs)
        {
            for (const auto& [level, picture] : pack.Info.Previews)
            {
                if (!pack.Pictures.contains(level))
                {
                    wanted.emplace_back(level, picture);
                }
            }
        }
        if (wanted.empty())
        {
            return;
        }
        Off(this, [this, wanted]() -> std::function<void()>
        {
            std::map<std::string, QString> fetched;
            for (const auto& [level, picture] : wanted)
            {
                if (const std::optional<std::string> path = MapCatalog::Preview(picture))
                {
                    fetched[picture] = QUrl::fromLocalFile(QString::fromStdString(*path)).toString();
                }
            }
            return [this, fetched = std::move(fetched)]()
            {
                for (Pack& pack : *_packs)
                {
                    for (const auto& [level, picture] : pack.Info.Previews)
                    {
                        if (const auto found = fetched.find(picture); found != fetched.end())
                        {
                            pack.Pictures[level] = found->second;
                        }
                    }
                }
                Rebuild();
            };
        });
    }

    void DownloadsModel::Rebuild()
    {
        _maps.clear();
        for (const Pack& pack : *_packs)
        {
            const QString file = QString::fromStdString(pack.Info.File);
            const bool queued = file == _current
                || std::find(_queue.begin(), _queue.end(), file) != _queue.end();
            for (const std::string& level : pack.Info.Levels)
            {
                QVariantMap entry;
                entry.insert(QStringLiteral("file"), file);
                entry.insert(QStringLiteral("level"), QString::fromStdString(level));
                entry.insert(QStringLiteral("size"), static_cast<double>(pack.Info.Size) / 1048576.0);
                const auto picture = pack.Pictures.find(level);
                entry.insert(QStringLiteral("preview"), picture != pack.Pictures.end() ? picture->second : QString());
                entry.insert(QStringLiteral("installed"), pack.Info.Installed);
                entry.insert(QStringLiteral("downloading"), queued);
                _maps.push_back(entry);
            }
        }
        emit mapsChanged();
    }

    void DownloadsModel::download(const QString& file)
    {
        const auto pack = std::find_if(_packs->begin(), _packs->end(),
            [&](const Pack& p) { return QString::fromStdString(p.Info.File) == file; });
        if (pack == _packs->end() || pack->Info.Installed || file == _current
            || std::find(_queue.begin(), _queue.end(), file) != _queue.end())
        {
            return;
        }
        _queue.push_back(file);
        Rebuild();
        Next();
    }

    void DownloadsModel::downloadAll()
    {
        for (const Pack& pack : *_packs)
        {
            const QString file = QString::fromStdString(pack.Info.File);
            if (!pack.Info.Installed && file != _current
                && std::find(_queue.begin(), _queue.end(), file) == _queue.end())
            {
                _queue.push_back(file);
            }
        }
        Rebuild();
        Next();
    }

    void DownloadsModel::Next()
    {
        if (!_current.isEmpty() || _queue.empty())
        {
            emit stateChanged();
            return;
        }
        _current = _queue.front();
        _queue.erase(_queue.begin());
        const auto pack = std::find_if(_packs->begin(), _packs->end(),
            [&](const Pack& p) { return QString::fromStdString(p.Info.File) == _current; });
        if (pack == _packs->end())
        {
            _current.clear();
            Next();
            return;
        }
        _note = QStringLiteral("Downloading %1…").arg(_current);
        emit stateChanged();
        const MapCatalog::Pack info = pack->Info;
        Off(this, [this, info]() -> std::function<void()>
        {
            const bool arrived = MapCatalog::Download(info);
            return [this, info, arrived]()
            {
                for (Pack& pack : *_packs)
                {
                    if (pack.Info.File == info.File && arrived)
                    {
                        pack.Info.Installed = true;
                    }
                }
                _note = arrived ? QStringLiteral("%1 downloaded: it is being prepared.").arg(_current)
                    : QStringLiteral("%1 could not be downloaded.").arg(_current);
                _current.clear();
                if (arrived)
                {
                    // Listed and built as the maps fetched at start are.
                    ::MphRead::Mods::MapGen::CustomRooms::NoteDownloaded(1);
                    if (ShellBridge* bridge = ShellBridge::Current())
                    {
                        bridge->GameFilesChanged();
                    }
                }
                Rebuild();
                Next();
            };
        });
    }
}
