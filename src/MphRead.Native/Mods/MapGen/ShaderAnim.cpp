#include "ShaderAnim.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>
#include <string_view>

namespace MphRead::Mods::MapGen::ShaderAnim
{
    namespace
    {
        // Numbers as whole ten-thousandths: written and read the same in any
        // locale, which a float printed with a decimal comma would not be.
        constexpr float Unit = 10000.0F;

        void Put(std::string& out, float value)
        {
            out += std::to_string(static_cast<long long>(std::llround(static_cast<double>(value) * Unit)));
        }

        [[nodiscard]] float Get(std::string_view text)
        {
            long long value = 0;
            std::from_chars(text.data(), text.data() + text.size(), value);
            return static_cast<float>(static_cast<double>(value) / Unit);
        }

        [[nodiscard]] std::int32_t GetInt(std::string_view text)
        {
            std::int32_t value = 0;
            std::from_chars(text.data(), text.data() + text.size(), value);
            return value;
        }

        [[nodiscard]] std::vector<std::string_view> Split(std::string_view text, char separator)
        {
            std::vector<std::string_view> parts;
            std::size_t start = 0;
            while (start <= text.size())
            {
                const std::size_t end = text.find(separator, start);
                parts.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
                if (end == std::string_view::npos)
                {
                    break;
                }
                start = end + 1;
            }
            return parts;
        }

        void PutWave(std::string& out, const Wave& wave)
        {
            out += wave.Function;
            for (const float value : {wave.Base, wave.Amplitude, wave.Phase, wave.Frequency})
            {
                out += ',';
                Put(out, value);
            }
        }

        [[nodiscard]] Wave GetWave(const std::vector<std::string_view>& parts, std::size_t first)
        {
            Wave wave;
            if (first < parts.size())
            {
                wave.Function = std::string(parts[first]);
            }
            float* const fields[] = {&wave.Base, &wave.Amplitude, &wave.Phase, &wave.Frequency};
            for (std::size_t i = 0; i < 4 && first + 1 + i < parts.size(); ++i)
            {
                *fields[i] = Get(parts[first + 1 + i]);
            }
            return wave;
        }

        [[nodiscard]] float Fraction(float value)
        {
            return value - std::floor(value);
        }
    }

    bool EntryMeta::MovesTexcoords() const
    {
        return !TcMods.empty();
    }

    bool EntryMeta::Animated() const
    {
        if (Fade.has_value() || FrameCount > 1)
        {
            return true;
        }
        for (const TcMod& mod : TcMods)
        {
            if (mod.Kind == "scroll" || mod.Kind == "rotate" || mod.Kind == "stretch" || mod.Kind == "turb")
            {
                return true;
            }
        }
        return false;
    }

    std::string Encode(const EntryMeta& meta)
    {
        std::string out = "role=" + meta.Role
            + ";of=" + std::to_string(meta.Of)
            + ";order=" + std::to_string(meta.Order)
            + ";frame=" + std::to_string(meta.Frame)
            + ";blend=" + meta.Blend
            + ";lit=" + (meta.Lit ? "1" : "0")
            + ";clamp=" + (meta.Clamp ? "1" : "0")
            + ";sides=" + (meta.TwoSided ? "2" : "1");
        if (meta.CloudHeight > 0.0F)
        {
            out += ";cloud=";
            Put(out, meta.CloudHeight);
        }
        if (!meta.TcMods.empty())
        {
            out += ";tc=";
            for (std::size_t m = 0; m < meta.TcMods.size(); ++m)
            {
                const TcMod& mod = meta.TcMods[m];
                if (m > 0)
                {
                    out += '/';
                }
                out += mod.Kind;
                out += ':';
                if (mod.Wave.has_value())
                {
                    PutWave(out, *mod.Wave);
                }
                else
                {
                    for (std::size_t v = 0; v < mod.Values.size(); ++v)
                    {
                        if (v > 0)
                        {
                            out += ',';
                        }
                        Put(out, mod.Values[v]);
                    }
                }
            }
        }
        if (meta.Fade.has_value())
        {
            out += ";fade=";
            PutWave(out, *meta.Fade);
        }
        if (meta.FrameCount > 0)
        {
            out += ";frames=" + std::to_string(meta.FrameCount) + ";fps=";
            Put(out, meta.FrameRate);
        }
        return out;
    }

    EntryMeta Decode(const std::string& text)
    {
        EntryMeta meta;
        for (const std::string_view field : Split(text, ';'))
        {
            const std::size_t equals = field.find('=');
            if (equals == std::string_view::npos)
            {
                continue;
            }
            const std::string_view key = field.substr(0, equals);
            const std::string_view value = field.substr(equals + 1);
            if (key == "role") meta.Role = std::string(value);
            else if (key == "of") meta.Of = GetInt(value);
            else if (key == "order") meta.Order = GetInt(value);
            else if (key == "frame") meta.Frame = GetInt(value);
            else if (key == "blend") meta.Blend = std::string(value);
            else if (key == "lit") meta.Lit = value == "1";
            else if (key == "clamp") meta.Clamp = value == "1";
            else if (key == "sides") meta.TwoSided = value == "2";
            else if (key == "frames") meta.FrameCount = GetInt(value);
            else if (key == "cloud") meta.CloudHeight = Get(value);
            else if (key == "fps") meta.FrameRate = Get(value);
            else if (key == "fade") meta.Fade = GetWave(Split(value, ','), 0);
            else if (key == "tc")
            {
                for (const std::string_view entry : Split(value, '/'))
                {
                    const std::size_t colon = entry.find(':');
                    if (colon == std::string_view::npos)
                    {
                        continue;
                    }
                    TcMod mod;
                    mod.Kind = std::string(entry.substr(0, colon));
                    const std::vector<std::string_view> parts = Split(entry.substr(colon + 1), ',');
                    if (mod.Kind == "stretch")
                    {
                        mod.Wave = GetWave(parts, 0);
                    }
                    else
                    {
                        for (std::size_t v = 0; v < parts.size() && v < mod.Values.size(); ++v)
                        {
                            mod.Values[v] = Get(parts[v]);
                        }
                    }
                    meta.TcMods.push_back(std::move(mod));
                }
            }
        }
        return meta;
    }

    float WaveValue(const Wave& wave, float seconds)
    {
        const float x = Fraction(wave.Phase + seconds * wave.Frequency);
        float shape = 0.0F;
        if (wave.Function == "triangle")
        {
            shape = x < 0.25F ? 4.0F * x : (x < 0.75F ? 2.0F - 4.0F * x : 4.0F * x - 4.0F);
        }
        else if (wave.Function == "square")
        {
            shape = x < 0.5F ? 1.0F : -1.0F;
        }
        else if (wave.Function == "sawtooth")
        {
            shape = x;
        }
        else if (wave.Function == "inversesawtooth")
        {
            shape = 1.0F - x;
        }
        else if (wave.Function == "noise")
        {
            // Quake's is random each frame; a flicker that repeats each loop
            // is near enough, and the same on every machine.
            const float t = seconds * std::max(wave.Frequency, 1.0F);
            shape = 0.5F * std::sin(t * 7.3F) + 0.3F * std::sin(t * 13.1F + 1.0F) + 0.2F * std::sin(t * 29.7F + 2.0F);
        }
        else
        {
            shape = std::sin(x * 2.0F * std::numbers::pi_v<float>);
        }
        return wave.Base + shape * wave.Amplitude;
    }

    TexcoordFrame Texcoords(const std::vector<TcMod>& mods, float seconds)
    {
        // st' = L st + o, each tcMod applied over the ones before it, as
        // Quake applies them (RB_CalcScrollTexCoords and the rest).
        float l00 = 1.0F, l01 = 0.0F, l10 = 0.0F, l11 = 1.0F;
        float o0 = 0.0F, o1 = 0.0F;
        const auto apply = [&](float a00, float a01, float a10, float a11, float b0, float b1)
        {
            const float n00 = a00 * l00 + a01 * l10;
            const float n01 = a00 * l01 + a01 * l11;
            const float n10 = a10 * l00 + a11 * l10;
            const float n11 = a10 * l01 + a11 * l11;
            const float p0 = a00 * o0 + a01 * o1 + b0;
            const float p1 = a10 * o0 + a11 * o1 + b1;
            l00 = n00; l01 = n01; l10 = n10; l11 = n11;
            o0 = p0; o1 = p1;
        };
        for (const TcMod& mod : mods)
        {
            const std::array<float, 6>& v = mod.Values;
            if (mod.Kind == "scroll")
            {
                apply(1.0F, 0.0F, 0.0F, 1.0F, v[0] * seconds, v[1] * seconds);
            }
            else if (mod.Kind == "scale")
            {
                apply(v[0], 0.0F, 0.0F, v[1], 0.0F, 0.0F);
            }
            else if (mod.Kind == "rotate")
            {
                const float radians = -v[0] * seconds * std::numbers::pi_v<float> / 180.0F;
                const float c = std::cos(radians);
                const float s = std::sin(radians);
                apply(c, -s, s, c, 0.5F - 0.5F * c + 0.5F * s, 0.5F - 0.5F * s - 0.5F * c);
            }
            else if (mod.Kind == "stretch" && mod.Wave.has_value())
            {
                const float value = WaveValue(*mod.Wave, seconds);
                const float p = std::abs(value) < 0.0001F ? 1.0F : 1.0F / value;
                apply(p, 0.0F, 0.0F, p, 0.5F - 0.5F * p, 0.5F - 0.5F * p);
            }
            else if (mod.Kind == "turb")
            {
                // Quake sways each vertex by where it is; the whole image
                // swaying together is what is seen from any distance.
                const float sway = v[1] * std::sin((v[2] + seconds * v[3]) * 2.0F * std::numbers::pi_v<float>);
                apply(1.0F, 0.0F, 0.0F, 1.0F, sway, sway);
            }
            else if (mod.Kind == "transform")
            {
                apply(v[0], v[2], v[1], v[3], v[4], v[5]);
            }
        }
        // As a scale, a turn about the middle and a move.
        TexcoordFrame frame;
        frame.ScaleS = std::hypot(l00, l10);
        frame.Rotate = std::atan2(l10, l00);
        const float c = std::cos(frame.Rotate);
        const float s = std::sin(frame.Rotate);
        frame.ScaleT = -l01 * s + l11 * c;
        frame.TranslateS = o0 - 0.5F + (c * 0.5F - s * 0.5F);
        frame.TranslateT = o1 - 0.5F + (s * 0.5F + c * 0.5F);
        return frame;
    }

    float Looped(float perSecond, float cycle)
    {
        const float cycles = std::round(perSecond * LoopSeconds / cycle);
        return cycles * cycle / LoopSeconds;
    }
}
