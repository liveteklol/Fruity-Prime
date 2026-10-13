#include "MapCatalog.hpp"

#include "CurlTrust.hpp"
#include "CustomRooms.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Cryptography.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Json.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

namespace MphRead::Mods::MapGen
{
    namespace
    {
        using ::MphRead::NativeRuntime::PathFromUtf8;
        using ::MphRead::NativeRuntime::PathToUtf8;

        [[nodiscard]] std::string Base()
        {
            const char* overridden = std::getenv("FP_MAPS_URL");
            std::string url = overridden != nullptr && *overridden != 0 ? std::string(overridden) : std::string(MapCatalog::BaseUrl);
            if (!url.empty() && url.back() != '/')
            {
                url += '/';
            }
            return url;
        }

        using Entry = MapCatalog::Pack;

        std::size_t ToMemory(char* data, std::size_t size, std::size_t count, void* user)
        {
            auto* out = static_cast<std::string*>(user);
            out->append(data, size * count);
            return size * count;
        }

        std::size_t ToFile(char* data, std::size_t size, std::size_t count, void* user)
        {
            auto* out = static_cast<std::ofstream*>(user);
            out->write(data, static_cast<std::streamsize>(size * count));
            return out->good() ? size * count : 0;
        }

        // A GET into memory or into a file; false on any failure.
        [[nodiscard]] bool Get(const std::string& url, std::string* text, std::ofstream* file, long timeoutSeconds)
        {
            static const bool initialised = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
            if (!initialised)
            {
                return false;
            }
            CURL* curl = curl_easy_init();
            if (curl == nullptr)
            {
                return false;
            }
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            const char* protocols = std::getenv("FP_MAPS_URL") != nullptr ? "http,https" : "https";
            curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, protocols);
            curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, protocols);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "FruityPrime");
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            CurlTrust::Apply(curl);
            if (file != nullptr)
            {
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &ToFile);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
            }
            else
            {
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &ToMemory);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, text);
            }
            const CURLcode result = curl_easy_perform(curl);
            if (result != CURLE_OK)
            {
                CurlTrust::Report("GET", url, result, curl);
            }
            curl_easy_cleanup(curl);
            return result == CURLE_OK;
        }

        [[nodiscard]] std::optional<std::vector<Entry>> Catalogue()
        {
            using namespace ::MphRead::NativeRuntime;
            std::string text;
            if (!Get(Base() + "index.json", &text, nullptr, 15L))
            {
                return std::nullopt;
            }
            const JsonPtr root = JsonParse(text);
            const JsonPtr maps = root ? root->Get("maps") : nullptr;
            if (!maps || maps->Type() != JsonValue::Kind::Array)
            {
                return std::nullopt;
            }
            std::vector<Entry> entries;
            for (const JsonPtr& item : maps->Items())
            {
                const JsonPtr file = item ? item->Get("file") : nullptr;
                const JsonPtr size = item ? item->Get("size") : nullptr;
                const JsonPtr sha = item ? item->Get("sha256") : nullptr;
                if (!file || !size || !sha)
                {
                    continue;
                }
                // A name, never a path out of the folder.
                const std::string name = file->Text();
                if (name.empty() || name.find_first_of("/\\") != std::string::npos || name.rfind("..", 0) == 0
                    || name.size() < 5 || name.compare(name.size() - 4, 4, ".pk3") != 0)
                {
                    continue;
                }
                Entry entry;
                entry.File = name;
                entry.Size = size->AsInt64();
                entry.Sha256 = sha->Text();
                if (const JsonPtr levels = item->Get("levels"); levels && levels->Type() == JsonValue::Kind::Array)
                {
                    for (const JsonPtr& level : levels->Items())
                    {
                        if (level)
                        {
                            entry.Levels.push_back(level->Text());
                        }
                    }
                }
                if (const JsonPtr previews = item->Get("previews"); previews)
                {
                    for (const std::string& level : entry.Levels)
                    {
                        const JsonPtr picture = previews->Get(level);
                        const std::string file = picture ? picture->Text() : std::string();
                        if (!file.empty() && file.find_first_of("/\\") == std::string::npos && file.rfind("..", 0) != 0)
                        {
                            entry.Previews.emplace_back(level, file);
                        }
                    }
                }
                entries.push_back(std::move(entry));
            }
            return entries;
        }

        [[nodiscard]] std::string Hex(const std::array<std::uint8_t, 32>& digest)
        {
            static constexpr char Digits[] = "0123456789abcdef";
            std::string text;
            for (const std::uint8_t byte : digest)
            {
                text += Digits[byte >> 4];
                text += Digits[byte & 15];
            }
            return text;
        }

        // Whether the map folder holds a pack of this name and size anywhere.
        [[nodiscard]] bool Present(const std::filesystem::path& root, const Entry& entry)
        {
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::follow_directory_symlink, error), end;
                 !error && it != end; it.increment(error))
            {
                if (it->is_regular_file(error) && it->path().filename().string() == entry.File
                    && static_cast<std::int64_t>(it->file_size(error)) == entry.Size)
                {
                    return true;
                }
            }
            return false;
        }
    }

    namespace
    {
        [[nodiscard]] bool Fetch(const std::filesystem::path& folder, const Entry& entry)
        {
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            const std::filesystem::path part = folder / (entry.File + ".part");
            bool good = false;
            {
                std::ofstream out(part, std::ios::binary | std::ios::trunc);
                // A minute a few megabytes would be slow; a pack is tens.
                good = out.good() && Get(Base() + entry.File, nullptr, &out, 1800L);
            }
            if (good)
            {
                const std::vector<std::uint8_t> bytes = ::MphRead::NativeRuntime::FileReadAllBytes(PathToUtf8(part));
                good = static_cast<std::int64_t>(bytes.size()) == entry.Size
                    && Hex(::MphRead::NativeRuntime::Sha256HashData(bytes)) == entry.Sha256;
            }
            if (good)
            {
                std::filesystem::rename(part, folder / entry.File, error);
                good = !error;
            }
            if (!good)
            {
                std::filesystem::remove(part, error);
                ::MphRead::NativeRuntime::ConsoleWriteLine("[maps] could not download " + entry.File);
                return false;
            }
            ::MphRead::NativeRuntime::ConsoleWriteLine("[maps] downloaded " + entry.File);
            return true;
        }
    }

    bool MapCatalog::FetchForRoom(const std::string& roomKey)
    {
        using ::MphRead::NativeRuntime::ConsoleWriteLine;
        using ::MphRead::NativeRuntime::StringCompareOrdinalIgnoreCase;
        const std::string directory = CustomRooms::MapDirectory();
        if (roomKey.empty() || directory.empty())
        {
            return false;
        }
        const std::optional<std::vector<Entry>> catalogue = Catalogue();
        if (!catalogue)
        {
            ConsoleWriteLine("[maps] " + roomKey + " is not here and the map server could not be reached");
            return false;
        }
        for (const Entry& entry : *catalogue)
        {
            for (const std::string& level : entry.Levels)
            {
                if (StringCompareOrdinalIgnoreCase(level, roomKey) != 0)
                {
                    continue;
                }
                const std::filesystem::path root = PathFromUtf8(directory);
                return Present(root, entry) || Fetch(root / "downloaded", entry);
            }
        }
        ConsoleWriteLine("[maps] no pack on the map server holds " + roomKey);
        return false;
    }

    bool MapCatalog::EnsureRoom(const std::string& roomKey)
    {
        if (roomKey.empty() || roomKey == "none")
        {
            return true;
        }
        const auto known = [&]()
        {
            return ::MphRead::Metadata::GetRoomByName(roomKey).first != nullptr;
        };
        const auto list = [&]()
        {
            CustomRooms::Rescan();
            (void)::MphRead::Metadata::AppendLateRooms();
        };
        if (known())
        {
            return true;
        }
        // On disk already but not in the game's lists yet (a pack the Download
        // maps page fetched, or one dropped in the folder since the start).
        list();
        if (known())
        {
            return true;
        }
        if (!FetchForRoom(roomKey))
        {
            return false;
        }
        list();
        return known();
    }

    std::optional<std::vector<MapCatalog::Pack>> MapCatalog::List()
    {
        std::optional<std::vector<Entry>> catalogue = Catalogue();
        if (!catalogue)
        {
            return std::nullopt;
        }
        const std::string directory = CustomRooms::MapDirectory();
        if (!directory.empty())
        {
            const std::filesystem::path root = PathFromUtf8(directory);
            for (Entry& entry : *catalogue)
            {
                entry.Installed = Present(root, entry);
            }
        }
        return catalogue;
    }

    bool MapCatalog::Download(const Pack& pack)
    {
        const std::string directory = CustomRooms::MapDirectory();
        if (directory.empty())
        {
            return false;
        }
        return Fetch(PathFromUtf8(directory) / "downloaded", pack);
    }

    std::optional<std::string> MapCatalog::Preview(const std::string& picture)
    {
        if (picture.empty() || picture.find_first_of("/\\") != std::string::npos || picture.rfind("..", 0) == 0)
        {
            return std::nullopt;
        }
        const std::string directory = CustomRooms::MapDirectory();
        if (directory.empty())
        {
            return std::nullopt;
        }
        const std::filesystem::path folder = PathFromUtf8(directory) / "downloaded" / ".previews";
        const std::filesystem::path target = folder / PathFromUtf8(picture);
        std::error_code error;
        if (std::filesystem::is_regular_file(target, error) && std::filesystem::file_size(target, error) > 0)
        {
            return PathToUtf8(target);
        }
        std::filesystem::create_directories(folder, error);
        const std::filesystem::path part = folder / PathFromUtf8(picture + ".part");
        bool good = false;
        {
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            good = out.good() && Get(Base() + picture, nullptr, &out, 60L);
        }
        if (good)
        {
            std::filesystem::rename(part, target, error);
            good = !error;
        }
        if (!good)
        {
            std::filesystem::remove(part, error);
            return std::nullopt;
        }
        return PathToUtf8(target);
    }

    std::int32_t MapCatalog::Sync(
        const std::function<void(const std::string& pack, std::int32_t done, std::int32_t total)>& progress)
    {
        if (std::getenv("FP_NO_MAP_DOWNLOAD") != nullptr || CustomRooms::MapDirectory().empty())
        {
            return 0;
        }
        const std::optional<std::vector<Entry>> catalogue = Catalogue();
        if (!catalogue)
        {
            return 0;
        }
        const std::filesystem::path root = PathFromUtf8(CustomRooms::MapDirectory());
        std::vector<Entry> missing;
        for (const Entry& entry : *catalogue)
        {
            if (!Present(root, entry))
            {
                missing.push_back(entry);
            }
        }
        const std::filesystem::path folder = root / "downloaded";
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        std::int32_t arrived = 0;
        for (std::size_t i = 0; i < missing.size(); ++i)
        {
            const Entry& entry = missing[i];
            if (progress)
            {
                progress(entry.File, static_cast<std::int32_t>(i), static_cast<std::int32_t>(missing.size()));
            }
            if (Fetch(folder, entry))
            {
                ++arrived;
            }
        }
        return arrived;
    }
}
