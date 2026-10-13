#include "GeoCountry.hpp"

#include "../../DebugLog.hpp"
#include "GameFiles.hpp"
#include "LauncherPrefs.hpp"
#include "../../../NativeRuntime/System/AssetLoader.hpp"
#include "../../../NativeRuntime/System/IO.hpp"
#include "../../../NativeRuntime/System/Managed.hpp"
#include "../../../NativeRuntime/System/Streams.hpp"

#include <exception>

namespace MphRead::Mods::Launcher::Gui
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::string GeoCountry::Of(const std::optional<Runtime::Address>& address)
    {
        if (!address.has_value() || address->Family != Runtime::AddressFamily::InterNetwork)
        {
            return "";
        }
        const auto& b = address->Bytes;
        if (b[0] == 127 || IsPrivate(*address))
        {
            return "";
        }
        Load();
        if (_first.empty() || _codes.empty() || _index.empty())
        {
            return "";
        }
        const std::uint32_t value = (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16)
            | (static_cast<std::uint32_t>(b[2]) << 8) | b[3];
        // The last range whose first address is at or below this one.
        std::int64_t lo = 0;
        std::int64_t hi = static_cast<std::int64_t>(_first.size()) - 1;
        std::int64_t hit = -1;
        while (lo <= hi)
        {
            const std::int64_t mid = (lo + hi) / 2;
            if (_first[static_cast<std::size_t>(mid)] <= value)
            {
                hit = mid;
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }
        if (hit < 0)
        {
            return "";
        }
        const std::uint8_t codeIndex = Runtime::ManagedAt(_index, hit);
        const std::string& code = Runtime::ManagedAt(_codes, codeIndex);
        // ZZ is the table's own "nowhere".
        return code == "ZZ" ? "" : code;
    }

    bool GeoCountry::IsPrivate(const Runtime::Address& address)
    {
        const auto& b = address.Bytes;
        return address.Family == Runtime::AddressFamily::InterNetwork
            && (b[0] == 10 || (b[0] == 192 && b[1] == 168) || (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
                || (b[0] == 169 && b[1] == 254));
    }

    // The table's bytes: from the assets beside the program, or -- on Android,
    // where they are copied into the app's own folder and the working
    // directory is what finds them -- from the folders the launcher keeps its
    // files in, whichever of them holds the copy.
    std::vector<std::uint8_t> GeoCountry::ReadTable()
    {
        constexpr const char* uri = "avares://FruityPrime/Assets/Geo/ipcountry.bin.gz";
        try
        {
            return ::MphRead::NativeRuntime::AssetLoader::Open(uri);
        }
        catch (const std::exception& first)
        {
            for (const std::string& root : {LauncherPrefs::Directory(), GameFiles::Root()})
            {
                if (root.empty())
                {
                    continue;
                }
                const std::string path = Runtime::PathCombine(root, "Assets/Geo/ipcountry.bin.gz");
                if (Runtime::FileExists(path))
                {
                    DebugLog::Line("geo", "country table read from " + path + " (assets: " + first.what() + ")");
                    return Runtime::FileReadAllBytes(path);
                }
            }
            throw;
        }
    }

    void GeoCountry::Load()
    {
        if (_tried)
        {
            return;
        }
        _tried = true;
        try
        {
            auto packed = std::make_shared<Runtime::MemoryStream>(ReadTable(), false);
            Runtime::DeflateStream gz(packed, Runtime::CompressionMode::Decompress, false,
                Runtime::DeflateStream::Format::GZip);
            const std::vector<std::uint8_t> data = gz.ReadToEnd();
            std::size_t at = 0;
            const auto need = [&](std::size_t n)
            {
                if (at + n > data.size())
                {
                    throw std::out_of_range("truncated");
                }
            };
            const auto u16 = [&]
            {
                need(2);
                const std::uint32_t v = data[at] | (data[at + 1] << 8);
                at += 2;
                return v;
            };
            const auto u32 = [&]
            {
                need(4);
                const std::uint32_t v = static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8)
                    | (static_cast<std::uint32_t>(data[at + 2]) << 16) | (static_cast<std::uint32_t>(data[at + 3]) << 24);
                at += 4;
                return v;
            };
            need(6);
            if (std::string(reinterpret_cast<const char*>(&data[0]), 6) != "FPGEO1")
            {
                return;
            }
            at = 6;
            const std::uint32_t countries = u16();
            std::vector<std::string> codes(countries);
            for (std::uint32_t i = 0; i < countries; i++)
            {
                need(2);
                codes[i] = std::string(reinterpret_cast<const char*>(&data[at]), 2);
                at += 2;
            }
            const std::uint32_t count = u32();
            std::vector<std::uint32_t> first(count);
            std::vector<std::uint8_t> index(count);
            for (std::uint32_t i = 0; i < count; i++)
            {
                first[i] = u32();
                need(1);
                index[i] = data[at++];
            }
            _codes = std::move(codes);
            _first = std::move(first);
            _index = std::move(index);
            DebugLog::Line("geo", "country table loaded: " + std::to_string(_first.size()) + " ranges");
        }
        catch (const std::exception& ex)
        {
            // A build without the table shows the neutral badge rather than
            // no browser -- and says why in the debug log, since a badge
            // with no flag looks the same whatever the reason.
            DebugLog::Line("geo", std::string("country table not loaded: ") + ex.what());
            // The first look can come before the game's files are in place (on
            // Android they are copied as the app starts): try again a few
            // times rather than for ever or never.
            if (++_failures < 3)
            {
                _tried = false;
            }
        }
    }
}
