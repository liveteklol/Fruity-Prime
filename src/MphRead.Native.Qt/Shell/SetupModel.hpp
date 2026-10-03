#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

#include <functional>
#include <memory>

namespace MphRead::Mods::Launcher
{
    class SetupProgress;
}

namespace MphRead::Qt
{
    // SetupScreen: pick the cartridge dump, unpack it with the log and the
    // progress bar, then render the map previews.
    class SetupModel : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(QString intro READ Intro CONSTANT)
        Q_PROPERTY(QString where READ Where CONSTANT)
        Q_PROPERTY(QString log READ Log NOTIFY changed)
        Q_PROPERTY(bool working READ Working NOTIFY changed)
        Q_PROPERTY(bool showProgress READ ShowProgress NOTIFY changed)
        Q_PROPERTY(double fraction READ Fraction NOTIFY changed)
        Q_PROPERTY(QString stage READ Stage NOTIFY changed)
        Q_PROPERTY(bool ready READ Ready NOTIFY changed)
        Q_PROPERTY(bool previewsShown READ PreviewsShown NOTIFY changed)
        Q_PROPERTY(bool previewsEnabled READ PreviewsEnabled NOTIFY changed)
        Q_PROPERTY(QString previewsText READ PreviewsText NOTIFY changed)

    public:
        explicit SetupModel(QObject* parent = nullptr);
        ~SetupModel() override;

        [[nodiscard]] QString Intro() const;
        [[nodiscard]] QString Where() const;
        [[nodiscard]] QString Log() const { return _log; }
        [[nodiscard]] bool Working() const noexcept { return _working; }
        [[nodiscard]] bool ShowProgress() const noexcept { return _showProgress; }
        [[nodiscard]] double Fraction() const noexcept { return _fraction; }
        [[nodiscard]] QString Stage() const { return _stage; }
        [[nodiscard]] bool Ready() const;
        [[nodiscard]] bool PreviewsShown() const noexcept { return _previewsShown; }
        [[nodiscard]] bool PreviewsEnabled() const noexcept { return _previewsEnabled; }
        [[nodiscard]] QString PreviewsText() const { return _previewsText; }

        Q_INVOKABLE void choose();
        Q_INVOKABLE void renderPreviews();

    signals:
        void changed();
        // Setup went through: the screen closes.
        void finished();

    private:
        using Progress = std::shared_ptr<::MphRead::Mods::Launcher::SetupProgress>;
        void Run(const QString& path);
        void Line(const Progress& progress, const QString& line);
        void Finish(const Progress& progress, bool ok);
        void RenderMissing(const Progress& progress, std::function<void()> completed);
        void RefreshPreviewEntry();
        void Tail(const QString& line);

        QString _log;
        bool _working = false;
        bool _showProgress = false;
        double _fraction = 0;
        QString _stage;
        bool _previewsShown = false;
        bool _previewsEnabled = true;
        QString _previewsText = QStringLiteral("Render map previews");
        std::shared_ptr<int> _lifetime = std::make_shared<int>(0);
    };
}
