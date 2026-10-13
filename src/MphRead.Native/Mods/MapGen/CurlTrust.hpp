#pragma once

#include "../DebugLog.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace MphRead::Mods::MapGen
{
    // libcurl built for Android knows no certificate authorities of its own,
    // and CURLOPT_CAPATH only works when OpenSSL can find a file under the hash
    // of the subject's name -- Android names them with the old, MD5 hash, so a
    // lookup by the new one finds nothing and every https request fails with
    // "could not be reached". The authorities are read instead, whatever their
    // names, into one bundle handed over in memory. The phone's own store: the
    // conscrypt module's on Android 14 and later, /system's before.
    class CurlTrust final
    {
    public:
        CurlTrust() = delete;

        // Trust for one handle: the phone's authorities on Android, nothing to
        // do elsewhere (the platform libcurl already finds its own).
        static void Apply(CURL* curl)
        {
#if defined(__ANDROID__)
            const std::string& bundle = Bundle();
            if (!bundle.empty())
            {
                curl_blob blob{};
                blob.data = const_cast<char*>(bundle.data());
                blob.len = bundle.size();
                blob.flags = CURL_BLOB_COPY;
                if (curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob) == CURLE_OK)
                {
                    return;
                }
            }
            curl_easy_setopt(curl, CURLOPT_CAPATH, "/system/etc/security/cacerts");
#else
            static_cast<void>(curl);
#endif
        }

        // Why a request failed, in the debug log: "could not be reached" says
        // nothing about a certificate, a timeout or a refused connection.
        static void Report(const char* what, const std::string& url, CURLcode result, CURL* curl)
        {
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            DebugLog::Line("maps", std::string(what) + " " + url + ": " + curl_easy_strerror(result)
                + " (curl " + std::to_string(static_cast<int>(result)) + ", http " + std::to_string(status) + ")");
        }

    private:
#if defined(__ANDROID__)
        [[nodiscard]] static const std::string& Bundle()
        {
            static const std::string bundle = Read();
            return bundle;
        }

        [[nodiscard]] static std::string Read()
        {
            const char* folders[] = {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"};
            std::string bundle;
            std::size_t files = 0;
            for (const char* folder : folders)
            {
                std::error_code error;
                std::vector<std::filesystem::path> names;
                for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
                {
                    names.push_back(it->path());
                }
                for (const std::filesystem::path& name : names)
                {
                    std::ifstream file(name, std::ios::binary);
                    if (!file)
                    {
                        continue;
                    }
                    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    if (text.find("-----BEGIN CERTIFICATE-----") == std::string::npos)
                    {
                        continue;
                    }
                    bundle += text;
                    if (bundle.back() != '\n')
                    {
                        bundle += '\n';
                    }
                    ++files;
                }
                if (files > 0)
                {
                    break;
                }
            }
            DebugLog::Line("maps", "certificate authorities read: " + std::to_string(files));
            return bundle;
        }
#endif
    };
}
