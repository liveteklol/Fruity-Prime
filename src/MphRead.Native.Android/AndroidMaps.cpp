#include "../MphRead.Native/Mods/MapGen/MapCatalog.hpp"
#include "AndroidMaps.hpp"

#if !defined(__ANDROID__)
#error "AndroidMaps is only valid for the Android native target."
#endif

#include "../MphRead.Native/Mods/Launcher/Portable/GameFiles.hpp"
#include "../MphRead.Native/Mods/MapGen/CustomRooms.hpp"
#include "../MphRead.Native/Mods/MapGen/MapBundle.hpp"
#include "../MphRead.Native/Metadata/Metadata.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    template <typename T>
    class LocalRef final
    {
    public:
        LocalRef() = default;

        LocalRef(JNIEnv* env, T value) noexcept
            : _env(env), _value(value)
        {
        }

        ~LocalRef()
        {
            Reset();
        }

        LocalRef(const LocalRef&) = delete;
        LocalRef& operator=(const LocalRef&) = delete;

        LocalRef(LocalRef&& other) noexcept
            : _env(other._env), _value(other._value)
        {
            other._env = nullptr;
            other._value = nullptr;
        }

        LocalRef& operator=(LocalRef&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                _env = other._env;
                _value = other._value;
                other._env = nullptr;
                other._value = nullptr;
            }
            return *this;
        }

        T Get() const noexcept
        {
            return _value;
        }

        explicit operator bool() const noexcept
        {
            return _value != nullptr;
        }

        void Reset() noexcept
        {
            if (_env != nullptr && _value != nullptr)
            {
                _env->DeleteLocalRef(_value);
            }
            _env = nullptr;
            _value = nullptr;
        }

    private:
        JNIEnv* _env = nullptr;
        T _value = nullptr;
    };

    std::string ToUtf8(std::u16string_view value);

    std::string JavaExceptionMessage(JNIEnv* env, jthrowable throwable)
    {
        if (throwable == nullptr)
        {
            return "Android Java exception";
        }

        LocalRef<jclass> throwableClass(env, env->GetObjectClass(throwable));
        if (env->ExceptionCheck() || !throwableClass)
        {
            env->ExceptionClear();
            return "Android Java exception";
        }

        const jmethodID getMessage = env->GetMethodID(
            throwableClass.Get(), "getMessage", "()Ljava/lang/String;"
        );
        if (env->ExceptionCheck() || getMessage == nullptr)
        {
            env->ExceptionClear();
            return "Android Java exception";
        }

        LocalRef<jstring> message(
            env,
            static_cast<jstring>(env->CallObjectMethod(throwable, getMessage))
        );
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            return "Android Java exception";
        }
        if (!message)
        {
            return "Android Java exception";
        }

        const jsize length = env->GetStringLength(message.Get());
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            return "Android Java exception";
        }
        const jchar* chars = env->GetStringChars(message.Get(), nullptr);
        if (env->ExceptionCheck() || chars == nullptr)
        {
            env->ExceptionClear();
            return "Android Java exception";
        }

        std::u16string text;
        try
        {
            text.assign(
                reinterpret_cast<const char16_t*>(chars),
                static_cast<std::size_t>(length)
            );
        }
        catch (...)
        {
            env->ReleaseStringChars(message.Get(), chars);
            throw;
        }
        env->ReleaseStringChars(message.Get(), chars);
        return ToUtf8(text);
    }

    [[noreturn]] void ThrowPendingJavaException(JNIEnv* env)
    {
        LocalRef<jthrowable> throwable(env, env->ExceptionOccurred());
        env->ExceptionClear();
        throw std::runtime_error(JavaExceptionMessage(env, throwable.Get()));
    }

    void CheckJavaException(JNIEnv* env)
    {
        if (env->ExceptionCheck())
        {
            ThrowPendingJavaException(env);
        }
    }

    LocalRef<jstring> NewJavaString(JNIEnv* env, std::u16string_view value)
    {
        static_assert(sizeof(char16_t) == sizeof(jchar));
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<jsize>::max()))
        {
            throw std::length_error("string is too long for Android JNI");
        }
        static const jchar empty = 0;
        const jchar* chars = value.empty()
            ? &empty
            : reinterpret_cast<const jchar*>(value.data());
        LocalRef<jstring> result(
            env,
            env->NewString(chars, static_cast<jsize>(value.size()))
        );
        CheckJavaException(env);
        if (!result)
        {
            throw std::bad_alloc();
        }
        return result;
    }

    std::u16string JavaStringToUtf16(JNIEnv* env, jstring value)
    {
        if (value == nullptr)
        {
            throw std::runtime_error("Object reference not set to an instance of an object.");
        }
        const jsize length = env->GetStringLength(value);
        CheckJavaException(env);
        const jchar* chars = env->GetStringChars(value, nullptr);
        CheckJavaException(env);
        if (chars == nullptr)
        {
            throw std::bad_alloc();
        }

        std::u16string result;
        try
        {
            result.assign(
                reinterpret_cast<const char16_t*>(chars),
                static_cast<std::size_t>(length)
            );
        }
        catch (...)
        {
            env->ReleaseStringChars(value, chars);
            throw;
        }
        env->ReleaseStringChars(value, chars);
        return result;
    }

    void AppendUtf8(std::string& output, std::uint32_t value)
    {
        if (value <= 0x7FU)
        {
            output.push_back(static_cast<char>(value));
        }
        else if (value <= 0x7FFU)
        {
            output.push_back(static_cast<char>(0xC0U | (value >> 6)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
        else if (value <= 0xFFFFU)
        {
            output.push_back(static_cast<char>(0xE0U | (value >> 12)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0U | (value >> 18)));
            output.push_back(static_cast<char>(0x80U | ((value >> 12) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }

    std::string ToUtf8(std::u16string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            const std::uint32_t first = static_cast<std::uint16_t>(value[index]);
            if (first >= 0xD800U && first <= 0xDBFFU)
            {
                if (index + 1 < value.size())
                {
                    const std::uint32_t second = static_cast<std::uint16_t>(value[index + 1]);
                    if (second >= 0xDC00U && second <= 0xDFFFU)
                    {
                        const std::uint32_t codePoint = 0x10000U
                            + ((first - 0xD800U) << 10)
                            + (second - 0xDC00U);
                        AppendUtf8(result, codePoint);
                        ++index;
                        continue;
                    }
                }
                AppendUtf8(result, 0xFFFDU);
                continue;
            }
            if (first >= 0xDC00U && first <= 0xDFFFU)
            {
                AppendUtf8(result, 0xFFFDU);
                continue;
            }
            AppendUtf8(result, first);
        }
        return result;
    }

    std::u16string CombinePath(std::u16string_view first, std::u16string_view second)
    {
        if (!second.empty() && second.front() == u'/')
        {
            return std::u16string(second);
        }
        if (first.empty())
        {
            return std::u16string(second);
        }
        if (second.empty())
        {
            return std::u16string(first);
        }

        std::u16string result(first);
        if (result.back() != u'/')
        {
            result.push_back(u'/');
        }
        result.append(second);
        return result;
    }

    char16_t FoldOrdinalAscii(char16_t value) noexcept
    {
        if (value >= u'A' && value <= u'Z')
        {
            return static_cast<char16_t>(value + (u'a' - u'A'));
        }
        // .NET OrdinalIgnoreCase uppercases these two BMP code points to
        // ASCII letters. They are the only non-ASCII simple uppercase
        // mappings into Basic Latin and therefore matter when the suffix is
        // ASCII, as every extension used here is.
        if (value == u'\u0131')
        {
            return u'i';
        }
        if (value == u'\u017F')
        {
            return u's';
        }
        return value;
    }

    bool EqualsAsciiIgnoreCase(char16_t left, char16_t right) noexcept
    {
        return FoldOrdinalAscii(left) == FoldOrdinalAscii(right);
    }

    bool EndsWithOrdinalIgnoreCase(std::u16string_view value, std::u16string_view suffix) noexcept
    {
        if (value.size() < suffix.size())
        {
            return false;
        }
        const std::size_t start = value.size() - suffix.size();
        for (std::size_t index = 0; index < suffix.size(); ++index)
        {
            if (!EqualsAsciiIgnoreCase(value[start + index], suffix[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool EndsWithOrdinalIgnoreCase(std::u16string_view value, std::string_view suffix) noexcept
    {
        if (value.size() < suffix.size())
        {
            return false;
        }
        const std::size_t start = value.size() - suffix.size();
        for (std::size_t index = 0; index < suffix.size(); ++index)
        {
            const auto right = static_cast<char16_t>(
                static_cast<unsigned char>(suffix[index])
            );
            if (!EqualsAsciiIgnoreCase(value[start + index], right))
            {
                return false;
            }
        }
        return true;
    }

    std::filesystem::path FileSystemPath(std::u16string_view value)
    {
        return std::filesystem::path(std::u16string(value));
    }

    bool FileExists(std::u16string_view path)
    {
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::status(
            FileSystemPath(path), error
        );
        if (error)
        {
            return false;
        }
        return std::filesystem::exists(status) && !std::filesystem::is_directory(status);
    }

    std::vector<std::uint8_t> ReadAllBytes(std::u16string_view path)
    {
        std::ifstream stream(FileSystemPath(path), std::ios::binary | std::ios::ate);
        if (!stream)
        {
            throw std::runtime_error("could not open file for reading");
        }

        const std::streampos end = stream.tellg();
        if (end < 0)
        {
            throw std::runtime_error("could not determine file length");
        }
        const auto length = static_cast<std::uintmax_t>(end);
        if (length > static_cast<std::uintmax_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw std::runtime_error("The file is too long. This operation is currently limited to supporting files less than 2 gigabytes in size.");
        }

        std::vector<std::uint8_t> result(static_cast<std::size_t>(length));
        stream.seekg(0, std::ios::beg);
        if (!result.empty())
        {
            stream.read(
                reinterpret_cast<char*>(result.data()),
                static_cast<std::streamsize>(result.size())
            );
            if (!stream)
            {
                throw std::runtime_error("could not read file");
            }
        }
        return result;
    }

    void WriteAllBytes(std::u16string_view path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream stream(FileSystemPath(path), std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            throw std::runtime_error("could not open file for writing");
        }
        if (!bytes.empty())
        {
            stream.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())
            );
            if (!stream)
            {
                throw std::runtime_error("could not write file");
            }
        }
        stream.close();
        if (!stream)
        {
            throw std::runtime_error("could not close file");
        }
    }

    jmethodID GetMethodId(
        JNIEnv* env,
        jclass type,
        const char* name,
        const char* signature
    )
    {
        const jmethodID method = env->GetMethodID(type, name, signature);
        CheckJavaException(env);
        if (method == nullptr)
        {
            throw std::runtime_error(std::string("Android method not found: ") + name);
        }
        return method;
    }

    template <typename Body>
    void UseAssetBytes(
        JNIEnv* env,
        jobject assets,
        jmethodID openMethod,
        std::u16string_view assetPath,
        Body&& body
    )
    {
        LocalRef<jstring> javaPath = NewJavaString(env, assetPath);
        LocalRef<jobject> source(
            env,
            env->CallObjectMethod(assets, openMethod, javaPath.Get())
        );
        CheckJavaException(env);
        if (!source)
        {
            throw std::runtime_error("Object reference not set to an instance of an object.");
        }

        LocalRef<jclass> sourceClass(env, env->GetObjectClass(source.Get()));
        CheckJavaException(env);
        if (!sourceClass)
        {
            throw std::runtime_error("Android stream class could not be obtained");
        }
        const jmethodID readMethod = GetMethodId(env, sourceClass.Get(), "read", "([B)I");
        const jmethodID closeMethod = GetMethodId(env, sourceClass.Get(), "close", "()V");

        std::exception_ptr bodyException;
        try
        {
            std::vector<std::uint8_t> bytes;
            {
                constexpr jsize BufferSize = 81920;
                LocalRef<jbyteArray> buffer(env, env->NewByteArray(BufferSize));
                CheckJavaException(env);
                if (!buffer)
                {
                    throw std::bad_alloc();
                }

                for (;;)
                {
                    const jint count = env->CallIntMethod(source.Get(), readMethod, buffer.Get());
                    CheckJavaException(env);
                    if (count <= 0)
                    {
                        break;
                    }

                    const std::size_t oldSize = bytes.size();
                    const std::size_t added = static_cast<std::size_t>(count);
                    const std::size_t maxLength = static_cast<std::size_t>(
                        std::numeric_limits<std::int32_t>::max()
                    );
                    if (oldSize > maxLength - added)
                    {
                        throw std::runtime_error("Stream was too long.");
                    }
                    bytes.resize(oldSize + added);
                    env->GetByteArrayRegion(
                        buffer.Get(),
                        0,
                        count,
                        reinterpret_cast<jbyte*>(bytes.data() + oldSize)
                    );
                    CheckJavaException(env);
                }
            }

            std::forward<Body>(body)(bytes);
        }
        catch (...)
        {
            bodyException = std::current_exception();
        }

        env->CallVoidMethod(source.Get(), closeMethod);
        CheckJavaException(env);
        if (bodyException)
        {
            std::rethrow_exception(bodyException);
        }
    }

    void LogLine(std::string_view text)
    {
        std::cout << text << '\n';
    }
}

namespace MphRead::Droid
{
    void AndroidMaps::Install(JNIEnv* env, jobject assets, std::u16string_view root)
    {
        if (root.empty())
        {
            return;
        }

        const std::u16string directory = CombinePath(root, AssetFolder);
        Mods::MapGen::CustomRooms::MapDirectory(ToUtf8(directory));

        // The game's room lists are made once, when the library loads, from the
        // map folder as it was *then* -- the one beside the executable, which
        // on this platform is not where the maps are. Every custom map, the
        // downloaded packs included, was therefore missing from the lists until
        // something happened to read the folder again (a pack arriving does:
        // GameFilesChanged), which is why the maps came back after adding one.
        // Read it again now that it is known, whichever way this returns.
        struct RefreshRooms final
        {
            ~RefreshRooms()
            {
                try
                {
                    Mods::MapGen::CustomRooms::Rescan();
                    (void)::MphRead::Metadata::AppendLateRooms();
                }
                catch (const std::exception& ex)
                {
                    LogLine(std::string("[android] could not list the custom maps: ") + ex.what());
                }
            }
        } refreshRooms;
        if (assets == nullptr)
        {
            return;
        }
        if (env == nullptr)
        {
            throw std::invalid_argument("env");
        }

        try
        {
            std::filesystem::create_directories(FileSystemPath(directory));

            LocalRef<jclass> assetsClass(env, env->GetObjectClass(assets));
            CheckJavaException(env);
            if (!assetsClass)
            {
                throw std::runtime_error("Android AssetManager class could not be obtained");
            }
            const jmethodID listMethod = GetMethodId(
                env,
                assetsClass.Get(),
                "list",
                "(Ljava/lang/String;)[Ljava/lang/String;"
            );
            const jmethodID openMethod = GetMethodId(
                env,
                assetsClass.Get(),
                "open",
                "(Ljava/lang/String;)Ljava/io/InputStream;"
            );

            LocalRef<jstring> folder = NewJavaString(env, AssetFolder);
            LocalRef<jobjectArray> names(
                env,
                static_cast<jobjectArray>(env->CallObjectMethod(assets, listMethod, folder.Get()))
            );
            CheckJavaException(env);

            const jsize count = names ? env->GetArrayLength(names.Get()) : 0;
            CheckJavaException(env);
            LogLine(
                "[android] " + std::to_string(count) + " bundled map files -> "
                + ToUtf8(directory)
            );

            for (jsize index = 0; index < count; ++index)
            {
                LocalRef<jstring> javaName(
                    env,
                    static_cast<jstring>(env->GetObjectArrayElement(names.Get(), index))
                );
                CheckJavaException(env);
                const std::u16string name = JavaStringToUtf16(env, javaName.Get());

                if (!EndsWithOrdinalIgnoreCase(name, u".json")
                    && !EndsWithOrdinalIgnoreCase(name, u".bsp")
                    && !EndsWithOrdinalIgnoreCase(name, u".tex")
                    && !EndsWithOrdinalIgnoreCase(
                        name,
                        std::string_view(Mods::MapGen::MapBundle::Extension)
                    ))
                {
                    continue;
                }

                try
                {
                    const std::u16string target = CombinePath(directory, name);
                    std::u16string assetPath(AssetFolder);
                    assetPath.push_back(u'/');
                    assetPath.append(name);

                    bool unchanged = false;
                    UseAssetBytes(
                        env,
                        assets,
                        openMethod,
                        assetPath,
                        [&](const std::vector<std::uint8_t>& bytes)
                        {
                            if (FileExists(target))
                            {
                                const std::vector<std::uint8_t> current = ReadAllBytes(target);
                                const std::vector<std::uint8_t> comparison(bytes);
                                if (current == comparison)
                                {
                                    unchanged = true;
                                    return;
                                }
                            }

                            const std::vector<std::uint8_t> writeBytes(bytes);
                            WriteAllBytes(target, writeBytes);
                            LogLine("[android] unpacked " + ToUtf8(name));
                        }
                    );
                    if (unchanged)
                    {
                        continue;
                    }
                }
                catch (const std::exception& ex)
                {
                    LogLine(
                        "[android] could not unpack " + ToUtf8(name) + ": " + ex.what()
                    );
                }
            }
        }
        catch (const std::exception& ex)
        {
            LogLine(
                std::string("[android] could not unpack the bundled maps: ") + ex.what()
            );
        }
    }

    void AndroidMaps::EnsureBuilt()
    {
        try
        {
            if (!Mods::Launcher::GameFiles::Ready())
            {
                return;
            }
            Mods::Launcher::GameFiles::ApplyPaths();
            // The job the desktop runs (list what the folder holds, build it),
            // not a copy of it: the launcher reads its progress, so maps being
            // built show as such and the lists refresh when they are done.
            // Nothing is downloaded here. Waited for, since callers load a
            // match or render previews from what it builds.
            Mods::MapGen::CustomRooms::StartGenerating();
            while (Mods::MapGen::CustomRooms::Progress().Running)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        catch (const std::exception& ex)
        {
            LogLine(
                std::string("[android] could not build the custom maps: ") + ex.what()
            );
        }
    }
}
