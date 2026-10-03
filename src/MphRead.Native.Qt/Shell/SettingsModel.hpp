#pragma once

#include "RowModel.hpp"

#include <QtCore/QObject>
#include <QtCore/QTimer>

#include "../../MphRead.Native/Mods/Input/GamepadUiRouter.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace MphRead
{
    class MenuSettings;
}

namespace MphRead::Qt
{
    // SettingsView's decisions for SettingsPage.qml: the five pages as row
    // models (Controls in its three sub-pages), what each row changes, the
    // key and pad capture, and Save. The page draws rows; this owns them.
    class SettingsModel : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(bool inGame READ InGame WRITE SetInGame NOTIFY inGameChanged)
        Q_PROPERTY(QObject* display READ Display CONSTANT)
        Q_PROPERTY(QObject* audio READ Audio CONSTANT)
        Q_PROPERTY(QObject* keyboard READ Keyboard CONSTANT)
        Q_PROPERTY(QObject* gamepad READ Gamepad CONSTANT)
        Q_PROPERTY(QObject* stylus READ Stylus CONSTANT)
        Q_PROPERTY(QObject* profile READ Profile CONSTANT)
        Q_PROPERTY(QObject* credits READ Credits CONSTANT)
        Q_PROPERTY(QString error READ Error NOTIFY errorChanged)
        // A key row is listening: every key belongs to it.
        Q_PROPERTY(bool listening READ Listening NOTIFY listeningChanged)

    public:
        explicit SettingsModel(QObject* parent = nullptr);
        ~SettingsModel() override;

        [[nodiscard]] bool InGame() const noexcept { return _inGame; }
        void SetInGame(bool value);
        [[nodiscard]] QObject* Display() { return &_display; }
        [[nodiscard]] QObject* Audio() { return &_audio; }
        [[nodiscard]] QObject* Keyboard() { return &_keyboard; }
        [[nodiscard]] QObject* Gamepad() { return &_gamepad; }
        [[nodiscard]] QObject* Stylus() { return &_stylus; }
        [[nodiscard]] QObject* Profile() { return &_profile; }
        [[nodiscard]] QObject* Credits() { return &_credits; }
        [[nodiscard]] QString Error() const { return _error; }
        [[nodiscard]] bool Listening() const noexcept { return _keyRow >= 0; }

        // Save (or Apply over a match); false leaves the page open with the error.
        Q_INVOKABLE bool save();
        // Leaving without saving puts back what was only previewed.
        Q_INVOKABLE void cancel();

        // KeyRow: listen on a row of the keyboard page, then answer it.
        Q_INVOKABLE void listenKey(int row);
        Q_INVOKABLE void stopKey();
        // A key event's key, native scan code and virtual key; true when it was taken.
        Q_INVOKABLE bool pressKey(int qtKey, quint32 scanCode, quint32 virtualKey, int modifiers, const QString& text);
        // Left 1, right 2, middle 4, back 8, forward 16 (Qt's buttons).
        Q_INVOKABLE void pressMouse(int button);
        Q_INVOKABLE void wheel(bool up);
        [[nodiscard]] Q_INVOKABLE int keyRow() const noexcept { return _keyRow; }
        // KeyRow.OpenControllerBinding: the pad's A on a key row opens the
        // matching pad row and listens there.
        Q_INVOKABLE void keyToPad(int row);

        // PadRow on the gamepad page: which slot, listening, the conflict's choice.
        Q_INVOKABLE void padSlot(int row, int slot);
        Q_INVOKABLE void padListen(int row);
        Q_INVOKABLE void padKey(int row, int qtKey);
        Q_INVOKABLE void padChoose(int row, int choice);
        Q_INVOKABLE void padLeave(int row);

        // Escape while a controller setup runs stops it; true when it did.
        Q_INVOKABLE bool escape();
        // Crosshair preview: the bars and ring for a style and size index.
        Q_INVOKABLE QVariantMap crosshair(int style, int size) const;

    signals:
        void inGameChanged();
        void errorChanged();
        void listeningChanged();
        void closed(bool saved);
        void gameFilesRequested();
        void stylusPlacementRequested();
        // Show the gamepad page with this pad row focused.
        void padRowRequested(int row);

    private:
        struct PadCapture;
        struct PadSetup;

        void BuildDisplay();
        void BuildAudio();
        void BuildKeyboard();
        void BuildGamepad();
        void BuildStylus();
        void BuildProfile();
        void BuildCredits();
        void Commit();
        void ResetControls();
        void RefreshDevices();
        void PadTick();
        void PadDone(int row);
        void PadChooseButton(int row, std::int32_t button);
        void PadResolve(int row);
        void AddSetupRows(std::vector<Row>& rows, const std::function<bool()>& open);
        void AddProfileRows(std::vector<Row>& rows, const std::function<bool()>& open);
        void SetupStart(bool mapping);
        void SetupTick();
        void SetupApply();
        void SetupStop(const QString& message);
        void ShareLogs();
        void KeyToPad(int row, std::int32_t pressed);
        void KeyTick();
        [[nodiscard]] Row* Get(RowModel& model, const QString& id);
        [[nodiscard]] int IndexOf(RowModel& model, const QString& id) const;

        std::shared_ptr<::MphRead::MenuSettings> _settings;
        // The Low Latency mode as Settings opened, put back by cancel.
        int _originalLowLatency = 0;
        bool _inGame = false;
        bool _saved = false;
        QString _error;
        RowModel _display;
        RowModel _audio;
        RowModel _keyboard;
        RowModel _gamepad;
        RowModel _stylus;
        RowModel _profile;
        RowModel _credits;
        bool _keyboardAdvanced = false;
        bool _gamepadAdvanced = false;
        bool _stylusAdvanced = false;
        int _keyRow = -1;
        // The key row that told the pad it is keyboard only.
        int _keyHintRow = -1;
        std::unique_ptr<::MphRead::Mods::Input::GamepadEdges> _keyEdges;
        QTimer _keyTimer;
        std::unique_ptr<PadCapture> _pad;
        std::unique_ptr<PadSetup> _setup;
        QTimer _setupTimer;
        QString _setupStatus;
        QString _profileStatus;
        bool _sharing = false;
        QString _shareError;
        std::shared_ptr<int> _lifetime = std::make_shared<int>(0);
        QTimer _padTimer;
        QTimer _deviceTimer;
        std::string _deviceList;
        std::int64_t _profileRevision = 0;
        std::int64_t _runtimeRevision = -1;
    };
}
