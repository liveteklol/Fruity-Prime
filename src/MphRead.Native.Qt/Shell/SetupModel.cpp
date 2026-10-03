#include "SetupModel.hpp"

#include "Await.hpp"
#include "ShellBridge.hpp"

#include "../../MphRead.Native/Mods/Branding.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/GameFiles.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/NativeFilePicker.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/SetupProgress.hpp"
#include "../../MphRead.Native/Mods/ThumbnailGenerator.hpp"
#include "../../MphRead.Native/Mods/ThumbnailHost.hpp"

#include <QtCore/QMetaObject>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

#include <chrono>
#include <future>
#include <thread>

namespace MphRead::Qt
{
    namespace
    {
        using ::MphRead::Mods::Launcher::GameFiles;

        [[nodiscard]] QString Q(const std::string& text)
        {
            return QString::fromStdString(text);
        }
    }

    SetupModel::SetupModel(QObject* parent) : QObject(parent)
    {
        RefreshPreviewEntry();
    }

    SetupModel::~SetupModel() = default;

    QString SetupModel::Intro() const
    {
        return Q(std::string(::MphRead::Mods::Branding::Name))
            + QStringLiteral(" needs your own Metroid Prime Hunters cartridge dump. It unpacks what it needs next to "
                             "this program and leaves the file alone. No game data is included in this download, and "
                             "none is downloaded.");
    }

    QString SetupModel::Where() const
    {
        if (!GameFiles::InProcessSetup())
        {
            return {};
        }
        return QStringLiteral("The unpacked files land in ") + Q(GameFiles::Root())
            + QStringLiteral(" -- this device's own folder for the app, which shows up over USB under Android/data. "
                             "Files already copied there are found without picking anything.");
    }

    bool SetupModel::Ready() const
    {
        return GameFiles::Ready();
    }

    void SetupModel::Tail(const QString& line)
    {
        QStringList lines;
        for (const QString& item : (_log + QLatin1Char('\n') + line).split(QLatin1Char('\n')))
        {
            if (!item.isEmpty())
            {
                lines.push_back(item);
            }
        }
        while (lines.size() > 8)
        {
            lines.removeFirst();
        }
        _log = lines.join(QLatin1Char('\n'));
    }

    void SetupModel::choose()
    {
        if (_working)
        {
            return;
        }
        if (!::MphRead::Mods::Launcher::NativeFilePicker::Available())
        {
            _log = QStringLiteral("This desktop has no file dialog to open. Install zenity or kdialog and press this again.");
            emit changed();
            return;
        }
        auto picker = ::MphRead::Mods::Launcher::NativeFilePicker::OpenFile(
            "Your Metroid Prime Hunters cartridge dump", "Nintendo DS ROM", "nds");
        Await(this, std::move(picker), [this](std::optional<std::string> chosen)
        {
            if (chosen.has_value())
            {
                Run(Q(*chosen));
            }
        });
    }

    void SetupModel::Run(const QString& path)
    {
        _working = true;
        _log.clear();
        const auto progress = std::make_shared<::MphRead::Mods::Launcher::SetupProgress>();
        _showProgress = true;
        _fraction = 0;
        _stage = QStringLiteral("Starting");
        emit changed();

        const std::weak_ptr<int> alive = _lifetime;
        auto task = std::async(std::launch::async, [this, alive, path = path.toStdString(), progress]()
        {
            return GameFiles::RunSetup(path, [this, alive, progress](const std::string& line)
            {
                QMetaObject::invokeMethod(this, [this, alive, progress, text = Q(line)]()
                {
                    if (!alive.expired())
                    {
                        Line(progress, text);
                    }
                }, ::Qt::QueuedConnection);
            });
        }).share();
        Await(this, std::move(task), [this, progress](bool ok)
        {
            if (ok)
            {
                RenderMissing(progress, [this, progress]() { Finish(progress, true); });
            }
            else
            {
                Finish(progress, false);
            }
        });
    }

    void SetupModel::Line(const Progress& progress, const QString& line)
    {
        Tail(line);
        if (progress != nullptr && progress->Observe(line.toStdString()))
        {
            _fraction = progress->Fraction();
            _stage = Q(progress->Stage());
        }
        emit changed();
    }

    void SetupModel::Finish(const Progress& progress, bool ok)
    {
        progress->Finish(ok);
        _fraction = progress->Fraction();
        _stage = Q(progress->Stage());
        _working = false;
        Tail(ok ? QStringLiteral("Ready to play.") : QStringLiteral("Setup did not finish."));
        RefreshPreviewEntry();
        if (ok)
        {
            _showProgress = false;
            if (ShellBridge* const bridge = ShellBridge::Current())
            {
                bridge->GameFilesChanged();
            }
        }
        emit changed();
        if (ok)
        {
            emit finished();
        }
    }

    void SetupModel::renderPreviews()
    {
        _previewsEnabled = false;
        _previewsText = QStringLiteral("Rendering...");
        emit changed();
        RenderMissing(nullptr, [this]()
        {
            _previewsEnabled = true;
            _previewsText = QStringLiteral("Render map previews");
            RefreshPreviewEntry();
            emit changed();
        });
    }

    void SetupModel::RenderMissing(const Progress& progress, std::function<void()> completed)
    {
        if (!::MphRead::Mods::ThumbnailHost::CanRender())
        {
            completed();
            return;
        }
        Tail(QStringLiteral("Rendering map previews..."));
        emit changed();
        const std::weak_ptr<int> alive = _lifetime;
        auto task = ::MphRead::Mods::ThumbnailHost::RenderMissingAsync([this, alive, progress](const std::string& line)
        {
            QMetaObject::invokeMethod(this, [this, alive, progress, text = Q(line)]()
            {
                if (!alive.expired())
                {
                    Line(progress, text);
                }
            }, ::Qt::QueuedConnection);
        });
        Await(this, std::move(task), [completed = std::move(completed)](int) { completed(); });
    }

    void SetupModel::RefreshPreviewEntry()
    {
        if (!GameFiles::Ready() || !::MphRead::Mods::ThumbnailHost::CanRender())
        {
            _previewsShown = false;
            return;
        }
        const std::size_t missing = ::MphRead::Mods::ThumbnailGenerator::MissingThumbnails().size();
        _previewsShown = missing > 0;
        _previewsEnabled = missing > 0;
    }
}
