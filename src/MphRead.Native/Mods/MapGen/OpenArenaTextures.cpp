#include "OpenArenaTextures.hpp"
#include "CurlTrust.hpp"
#include "../../Formats/Formats.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Json.hpp"
#include "../../NativeRuntime/System/ZipArchive.hpp"

#include <curl/curl.h>
#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace MphRead::Mods::MapGen
{
    namespace
    {
        using ::MphRead::NativeRuntime::PathFromUtf8;

        // FP_OPENARENA_URL points the client at another copy of what is
        // published (a local server, for testing), plain http allowed.
        [[nodiscard]] std::string Base()
        {
            const char* overridden = std::getenv("FP_OPENARENA_URL");
            return overridden != nullptr && *overridden != 0 ? std::string(overridden) : std::string(OpenArenaTextures::BaseUrl);
        }

        // How long a copy of the index is trusted before the server is asked
        // again, and how long after failing to reach it.
        constexpr auto IndexLifetime = std::chrono::hours(24);
        constexpr auto RetryAfter = std::chrono::hours(1);

        struct Index
        {
            std::string Version;
            // File name -> where its bytes start in the pack, and how many.
            std::unordered_map<std::string, std::pair<std::int64_t, std::int64_t>> Files;
            // Lower-case name without extension -> file name.
            std::unordered_map<std::string, std::string> ByStem;
            std::unordered_map<std::string, std::vector<std::string>> Shaders;
            std::vector<std::string> Scripts;
            std::string Pack;
        };

        std::mutex& Lock()
        {
            static std::mutex lock;
            return lock;
        }

        [[nodiscard]] std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }

        [[nodiscard]] std::string Stem(const std::string& name)
        {
            const std::string lower = Lower(name);
            for (const char* extension : {".tga", ".jpg", ".jpeg", ".png"})
            {
                const std::string ext(extension);
                if (lower.size() > ext.size() && lower.ends_with(ext))
                {
                    return lower.substr(0, lower.size() - ext.size());
                }
            }
            return lower;
        }

        std::size_t Write(char* data, std::size_t size, std::size_t count, void* user)
        {
            auto* out = static_cast<std::vector<std::uint8_t>*>(user);
            out->insert(out->end(), data, data + size * count);
            return size * count;
        }

        // A GET, the whole file or bytes [from, to] of it; nothing on any
        // failure.
        [[nodiscard]] std::optional<std::vector<std::uint8_t>> Get(
            const std::string& url, std::optional<std::pair<std::int64_t, std::int64_t>> range, long timeoutSeconds)
        {
            static const bool initialised = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
            if (!initialised)
            {
                return std::nullopt;
            }
            CURL* curl = curl_easy_init();
            if (curl == nullptr)
            {
                return std::nullopt;
            }
            std::vector<std::uint8_t> body;
            std::string bytes;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            const char* protocols = std::getenv("FP_OPENARENA_URL") != nullptr ? "http,https" : "https";
            curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, protocols);
            curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, protocols);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &Write);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "FruityPrime");
            CurlTrust::Apply(curl);
            if (range.has_value())
            {
                bytes = std::to_string(range->first) + "-" + std::to_string(range->second);
                curl_easy_setopt(curl, CURLOPT_RANGE, bytes.c_str());
            }
            const CURLcode result = curl_easy_perform(curl);
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            if (result != CURLE_OK)
            {
                CurlTrust::Report("GET", url, result, curl);
            }
            curl_easy_cleanup(curl);
            if (result != CURLE_OK)
            {
                return std::nullopt;
            }
            if (range.has_value())
            {
                const std::int64_t length = range->second - range->first + 1;
                if (status == 206 && static_cast<std::int64_t>(body.size()) == length)
                {
                    return body;
                }
                // A server that ignores ranges sends the whole pack.
                if (status == 200 && static_cast<std::int64_t>(body.size()) > range->second)
                {
                    return std::vector<std::uint8_t>(body.begin() + range->first, body.begin() + range->second + 1);
                }
                return std::nullopt;
            }
            if (status != 200)
            {
                return std::nullopt;
            }
            return body;
        }

        [[nodiscard]] std::optional<std::string> ReadText(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                return std::nullopt;
            }
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        void WriteFile(const std::filesystem::path& path, const void* data, std::size_t size)
        {
            std::error_code ignored;
            std::filesystem::create_directories(path.parent_path(), ignored);
            const std::filesystem::path partial = path.string() + ".part";
            {
                std::ofstream file(partial, std::ios::binary | std::ios::trunc);
                file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
                if (!file)
                {
                    std::filesystem::remove(partial, ignored);
                    return;
                }
            }
            std::filesystem::rename(partial, path, ignored);
        }

        [[nodiscard]] std::optional<Index> Parse(const std::string& text)
        {
            using namespace ::MphRead::NativeRuntime;
            try
            {
                const JsonPtr root = JsonParse(text);
                if (!root || root->Type() != JsonValue::Kind::Object)
                {
                    return std::nullopt;
                }
                Index index;
                if (const JsonPtr version = root->Get("version"))
                {
                    index.Version = version->Text();
                }
                if (const JsonPtr pack = root->Get("pack"))
                {
                    index.Pack = pack->Text();
                }
                if (const JsonPtr files = root->Get("files"))
                {
                    for (const auto& [name, place] : files->Members())
                    {
                        if (place && place->Items().size() == 2)
                        {
                            index.Files[name] = {place->Items()[0]->AsInt64(), place->Items()[1]->AsInt64()};
                            index.ByStem[Stem(name)] = name;
                        }
                    }
                }
                if (const JsonPtr shaders = root->Get("shaders"))
                {
                    for (const auto& [name, images] : shaders->Members())
                    {
                        std::vector<std::string>& list = index.Shaders[Lower(name)];
                        for (const JsonPtr& image : images->Items())
                        {
                            list.push_back(Lower(image->Text()));
                        }
                    }
                }
                if (const JsonPtr scripts = root->Get("scripts"))
                {
                    for (const JsonPtr& script : scripts->Items())
                    {
                        index.Scripts.push_back(script->Text());
                    }
                }
                if (index.Pack.empty() || index.Files.empty())
                {
                    return std::nullopt;
                }
                return index;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        // The index: the copy kept, unless it is old or there is none and the
        // server has not just been tried; asked for again then.
        [[nodiscard]] const std::optional<Index>& LoadIndex(const std::filesystem::path& directory)
        {
            static bool loaded = false;
            static std::optional<Index> index;
            if (loaded)
            {
                return index;
            }
            loaded = true;
            const std::filesystem::path kept = directory / "index.json";
            const std::filesystem::path tried = directory / "index.tried";
            std::error_code error;
            const auto now = std::filesystem::file_time_type::clock::now();
            const auto age = [&](const std::filesystem::path& path) -> std::optional<std::filesystem::file_time_type::duration>
            {
                std::error_code ignored;
                const auto time = std::filesystem::last_write_time(path, ignored);
                if (ignored)
                {
                    return std::nullopt;
                }
                return now - time;
            };
            const auto keptAge = age(kept);
            const auto triedAge = age(tried);
            const bool fresh = keptAge.has_value() && *keptAge < IndexLifetime;
            const bool recentlyTried = triedAge.has_value() && *triedAge < RetryAfter;
            if (!fresh && !recentlyTried)
            {
                WriteFile(tried, "", 0);
                if (auto body = Get(Base() + "index.json", std::nullopt, 15))
                {
                    const std::string text(body->begin(), body->end());
                    if (Parse(text).has_value())
                    {
                        WriteFile(kept, text.data(), text.size());
                    }
                }
            }
            if (auto text = ReadText(kept))
            {
                index = Parse(*text);
            }
            (void)error;
            return index;
        }

        // A zip of the files under `root`, stored: what MapTextureBake reads.
        void WritePack(const std::filesystem::path& root, const std::filesystem::path& pack)
        {
            struct Entry
            {
                std::string Name;
                std::uint32_t Crc = 0;
                std::uint32_t Size = 0;
                std::uint32_t Offset = 0;
            };
            std::vector<std::pair<std::string, std::filesystem::path>> files;
            std::error_code error;
            for (auto it = std::filesystem::recursive_directory_iterator(root, error);
                 !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
            {
                if (it->is_regular_file() && it->path().extension() != ".part")
                {
                    files.emplace_back(std::filesystem::relative(it->path(), root).generic_string(), it->path());
                }
            }
            std::sort(files.begin(), files.end());
            const auto put16 = [](std::string& out, std::uint32_t value)
            {
                out.push_back(static_cast<char>(value & 0xFF));
                out.push_back(static_cast<char>((value >> 8) & 0xFF));
            };
            const auto put32 = [&](std::string& out, std::uint32_t value)
            {
                put16(out, value & 0xFFFF);
                put16(out, value >> 16);
            };
            std::string zip;
            std::vector<Entry> entries;
            for (const auto& [name, path] : files)
            {
                const std::optional<std::string> data = ReadText(path);
                if (!data.has_value())
                {
                    continue;
                }
                Entry entry;
                entry.Name = name;
                entry.Size = static_cast<std::uint32_t>(data->size());
                entry.Crc = static_cast<std::uint32_t>(crc32(0L,
                    reinterpret_cast<const Bytef*>(data->data()), static_cast<uInt>(data->size())));
                entry.Offset = static_cast<std::uint32_t>(zip.size());
                put32(zip, 0x04034B50);
                put16(zip, 10);
                put16(zip, 0);
                put16(zip, 0);
                put16(zip, 0);
                put16(zip, 0);
                put32(zip, entry.Crc);
                put32(zip, entry.Size);
                put32(zip, entry.Size);
                put16(zip, static_cast<std::uint32_t>(name.size()));
                put16(zip, 0);
                zip += name;
                zip += *data;
                entries.push_back(std::move(entry));
            }
            const std::uint32_t directory = static_cast<std::uint32_t>(zip.size());
            for (const Entry& entry : entries)
            {
                put32(zip, 0x02014B50);
                put16(zip, 20);
                put16(zip, 10);
                put16(zip, 0);
                put16(zip, 0);
                put16(zip, 0);
                put16(zip, 0);
                put32(zip, entry.Crc);
                put32(zip, entry.Size);
                put32(zip, entry.Size);
                put16(zip, static_cast<std::uint32_t>(entry.Name.size()));
                put16(zip, 0);
                put16(zip, 0);
                put16(zip, 0);
                put16(zip, 0);
                put32(zip, 0);
                put32(zip, entry.Offset);
                zip += entry.Name;
            }
            const std::uint32_t directorySize = static_cast<std::uint32_t>(zip.size()) - directory;
            put32(zip, 0x06054B50);
            put16(zip, 0);
            put16(zip, 0);
            put16(zip, static_cast<std::uint32_t>(entries.size()));
            put16(zip, static_cast<std::uint32_t>(entries.size()));
            put32(zip, directorySize);
            put32(zip, directory);
            put16(zip, 0);
            WriteFile(pack, zip.data(), zip.size());
        }
    }

    std::string OpenArenaTextures::Directory()
    {
        return ::MphRead::Paths::Combine(::MphRead::Paths::FileSystem(), "_archives", "_openarena");
    }

    std::optional<std::string> OpenArenaTextures::Pack()
    {
        const std::string path = ::MphRead::Paths::Combine(Directory(), "openarena.pk3");
        if (::MphRead::NativeRuntime::FileExists(path))
        {
            return path;
        }
        return std::nullopt;
    }

    std::string OpenArenaTextures::Stamp()
    {
        const std::lock_guard<std::mutex> guard(Lock());
        const std::optional<Index>& index = LoadIndex(PathFromUtf8(Directory()));
        return index.has_value() ? "openarena " + index->Version : std::string();
    }

    void OpenArenaTextures::Fetch(const std::string& level, const std::vector<std::string>& shaders)
    {
        const std::lock_guard<std::mutex> guard(Lock());
        const std::filesystem::path directory = PathFromUtf8(Directory());
        const std::optional<Index>& index = LoadIndex(directory);
        if (!index.has_value())
        {
            return;
        }
        const std::filesystem::path files = directory / "files";

        // What the level carries itself, which is never fetched.
        std::unordered_set<std::string> own;
        try
        {
            if (const auto archive = ::MphRead::NativeRuntime::ZipArchive::OpenRead(level))
            {
                for (std::size_t i = 0; i < archive->Count(); ++i)
                {
                    own.insert(Stem(archive->FullName(i)));
                }
            }
        }
        catch (...)
        {
        }

        std::set<std::string> wanted(index->Scripts.begin(), index->Scripts.end());
        for (const std::string& shader : shaders)
        {
            const std::string name = Lower(shader);
            std::vector<std::string> images{name};
            if (const auto found = index->Shaders.find(name); found != index->Shaders.end())
            {
                images.insert(images.end(), found->second.begin(), found->second.end());
            }
            for (const std::string& image : images)
            {
                if (own.contains(Stem(image)))
                {
                    continue;
                }
                if (const auto file = index->ByStem.find(Stem(image)); file != index->ByStem.end())
                {
                    wanted.insert(file->second);
                }
            }
        }

        // Not fetched yet, in the order they lie in the pack, neighbours
        // fetched in one request.
        std::vector<std::pair<std::pair<std::int64_t, std::int64_t>, std::string>> missing;
        for (const std::string& name : wanted)
        {
            std::error_code ignored;
            const auto place = index->Files.find(name);
            if (place != index->Files.end() && !std::filesystem::exists(files / PathFromUtf8(name), ignored))
            {
                missing.push_back({place->second, name});
            }
        }
        if (missing.empty())
        {
            return;
        }
        std::sort(missing.begin(), missing.end());
        const std::string url = Base() + index->Pack;
        constexpr std::int64_t Gap = 256 * 1024;
        constexpr std::int64_t Most = 8 * 1024 * 1024;
        std::size_t fetched = 0;
        std::int64_t bytes = 0;
        for (std::size_t first = 0; first < missing.size();)
        {
            std::size_t last = first;
            std::int64_t end = missing[first].first.first + missing[first].first.second;
            while (last + 1 < missing.size()
                && missing[last + 1].first.first - end <= Gap
                && missing[last + 1].first.first + missing[last + 1].first.second - missing[first].first.first <= Most)
            {
                ++last;
                end = std::max(end, missing[last].first.first + missing[last].first.second);
            }
            const std::int64_t start = missing[first].first.first;
            if (const auto body = Get(url, std::pair<std::int64_t, std::int64_t>{start, end - 1}, 120))
            {
                for (std::size_t i = first; i <= last; ++i)
                {
                    const auto [offset, size] = missing[i].first;
                    WriteFile(files / PathFromUtf8(missing[i].second), body->data() + (offset - start),
                        static_cast<std::size_t>(size));
                    ++fetched;
                    bytes += size;
                }
            }
            first = last + 1;
        }
        if (fetched > 0)
        {
            WritePack(files, directory / "openarena.pk3");
            ::MphRead::NativeRuntime::ConsoleWriteLine("[openarena] fetched " + std::to_string(fetched)
                + " files (" + std::to_string(bytes / 1024) + " KB) from " + Base());
        }
    }
}
