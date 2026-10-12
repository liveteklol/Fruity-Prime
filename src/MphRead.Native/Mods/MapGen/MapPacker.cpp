#include "ShaderAnim.hpp"
#include "MapPacker.hpp"

#include "CollisionObj.hpp"
#include "CustomRooms.hpp"
#include "../../Formats/Collision.hpp"
#include "../../Formats/Model.hpp"
#include "../../Program.hpp"
#include "../../Read.hpp"
#include "BuiltMap.hpp"
#include "MapBuilder.hpp"
#include "MapCollisionPacker.hpp"
#include "MapDefinition.hpp"
#include "MapNodePacker.hpp"
#include "MapTexturePack.hpp"
#include "Q3Import.hpp"
#include "RawStructs.hpp"
#include "../../Formats/Types.hpp"
#include "../../Utility/Repack.hpp"
#include "../../Utility/RepackCollision.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <wchar.h>
#endif

using ::MphRead::NativeRuntime::AppendUtf8;
using ::MphRead::NativeRuntime::DirectoryCreateDirectory;
using ::MphRead::NativeRuntime::FileWriteAllBytes;
using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::PathCombine;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::RoundToEven;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::Utf8ToUtf16;
using ::MphRead::NativeRuntime::Utf8ToUtf32;

namespace
{
    using MphRead::ColorRgb;
    using MphRead::DisplayList;
    using MphRead::Fixed;
    using MphRead::InstructionCode;
    using MphRead::ManagedArray;
    using MphRead::Material;
    using MphRead::Mesh;
    using MphRead::Node;
    using MphRead::ProgramException;
    using MphRead::RenderInstruction;
    using MphRead::RepeatMode;
    using MphRead::TextureFormat;
    using MphRead::Mods::MapGen::BuiltFace;
    using MphRead::Mods::MapGen::BuiltMap;
    using MphRead::Mods::MapGen::MapDefinition;
    using MphRead::Mods::MapGen::MapImport;
    using MphRead::Mods::MapGen::MapMaterial;
    using MphRead::Mods::MapGen::MapTexturePack;
    using MphRead::Utility::CollisionDataEditor;
    using MphRead::Utility::Repack;
    using OpenTK::Mathematics::Vector2;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    void WriteLine(std::string_view value)
    {
        ::MphRead::NativeRuntime::ConsoleWriteLine(value);
    }

    [[nodiscard]] std::int32_t ListCount(std::size_t count)
    {
        if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw std::length_error("List count exceeds Int32.MaxValue.");
        }
        return static_cast<std::int32_t>(count);
    }

    [[nodiscard]] std::int32_t ConvertToInt32Net(float value) noexcept
    {
        if (std::isnan(value))
        {
            return 0;
        }
        const double wide = static_cast<double>(value);
        if (wide < static_cast<double>(std::numeric_limits<std::int32_t>::min()))
        {
            return std::numeric_limits<std::int32_t>::min();
        }
        if (wide > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
        {
            return std::numeric_limits<std::int32_t>::max();
        }
        return static_cast<std::int32_t>(std::trunc(wide));
    }

    [[nodiscard]] std::int32_t RoundedInt32(float value) noexcept
    {
        return ConvertToInt32Net(RoundToEven(value));
    }

    [[nodiscard]] ManagedArray<Vector3>* FacePoints(BuiltFace* face)
    {
        if (face == nullptr)
        {
            throw System::NullReferenceException();
        }
        auto* points = face->Points();
        if (points == nullptr)
        {
            throw System::NullReferenceException();
        }
        return points;
    }

    [[nodiscard]] ManagedArray<Vector2>* FaceTexcoords(BuiltFace* face)
    {
        if (face == nullptr)
        {
            throw System::NullReferenceException();
        }
        auto* texcoords = face->Texcoords();
        if (texcoords == nullptr)
        {
            throw System::NullReferenceException();
        }
        return texcoords;
    }

    struct LowerInvariantEntry final
    {
        std::uint32_t From;
        std::uint32_t To;
    };

    constexpr std::array<LowerInvariantEntry, 1454> LowerInvariantMap{{
        {0x0041U, 0x0061U}, {0x0042U, 0x0062U}, {0x0043U, 0x0063U}, {0x0044U, 0x0064U},
        {0x0045U, 0x0065U}, {0x0046U, 0x0066U}, {0x0047U, 0x0067U}, {0x0048U, 0x0068U},
        {0x0049U, 0x0069U}, {0x004AU, 0x006AU}, {0x004BU, 0x006BU}, {0x004CU, 0x006CU},
        {0x004DU, 0x006DU}, {0x004EU, 0x006EU}, {0x004FU, 0x006FU}, {0x0050U, 0x0070U},
        {0x0051U, 0x0071U}, {0x0052U, 0x0072U}, {0x0053U, 0x0073U}, {0x0054U, 0x0074U},
        {0x0055U, 0x0075U}, {0x0056U, 0x0076U}, {0x0057U, 0x0077U}, {0x0058U, 0x0078U},
        {0x0059U, 0x0079U}, {0x005AU, 0x007AU}, {0x00C0U, 0x00E0U}, {0x00C1U, 0x00E1U},
        {0x00C2U, 0x00E2U}, {0x00C3U, 0x00E3U}, {0x00C4U, 0x00E4U}, {0x00C5U, 0x00E5U},
        {0x00C6U, 0x00E6U}, {0x00C7U, 0x00E7U}, {0x00C8U, 0x00E8U}, {0x00C9U, 0x00E9U},
        {0x00CAU, 0x00EAU}, {0x00CBU, 0x00EBU}, {0x00CCU, 0x00ECU}, {0x00CDU, 0x00EDU},
        {0x00CEU, 0x00EEU}, {0x00CFU, 0x00EFU}, {0x00D0U, 0x00F0U}, {0x00D1U, 0x00F1U},
        {0x00D2U, 0x00F2U}, {0x00D3U, 0x00F3U}, {0x00D4U, 0x00F4U}, {0x00D5U, 0x00F5U},
        {0x00D6U, 0x00F6U}, {0x00D8U, 0x00F8U}, {0x00D9U, 0x00F9U}, {0x00DAU, 0x00FAU},
        {0x00DBU, 0x00FBU}, {0x00DCU, 0x00FCU}, {0x00DDU, 0x00FDU}, {0x00DEU, 0x00FEU},
        {0x0100U, 0x0101U}, {0x0102U, 0x0103U}, {0x0104U, 0x0105U}, {0x0106U, 0x0107U},
        {0x0108U, 0x0109U}, {0x010AU, 0x010BU}, {0x010CU, 0x010DU}, {0x010EU, 0x010FU},
        {0x0110U, 0x0111U}, {0x0112U, 0x0113U}, {0x0114U, 0x0115U}, {0x0116U, 0x0117U},
        {0x0118U, 0x0119U}, {0x011AU, 0x011BU}, {0x011CU, 0x011DU}, {0x011EU, 0x011FU},
        {0x0120U, 0x0121U}, {0x0122U, 0x0123U}, {0x0124U, 0x0125U}, {0x0126U, 0x0127U},
        {0x0128U, 0x0129U}, {0x012AU, 0x012BU}, {0x012CU, 0x012DU}, {0x012EU, 0x012FU},
        {0x0132U, 0x0133U}, {0x0134U, 0x0135U}, {0x0136U, 0x0137U}, {0x0139U, 0x013AU},
        {0x013BU, 0x013CU}, {0x013DU, 0x013EU}, {0x013FU, 0x0140U}, {0x0141U, 0x0142U},
        {0x0143U, 0x0144U}, {0x0145U, 0x0146U}, {0x0147U, 0x0148U}, {0x014AU, 0x014BU},
        {0x014CU, 0x014DU}, {0x014EU, 0x014FU}, {0x0150U, 0x0151U}, {0x0152U, 0x0153U},
        {0x0154U, 0x0155U}, {0x0156U, 0x0157U}, {0x0158U, 0x0159U}, {0x015AU, 0x015BU},
        {0x015CU, 0x015DU}, {0x015EU, 0x015FU}, {0x0160U, 0x0161U}, {0x0162U, 0x0163U},
        {0x0164U, 0x0165U}, {0x0166U, 0x0167U}, {0x0168U, 0x0169U}, {0x016AU, 0x016BU},
        {0x016CU, 0x016DU}, {0x016EU, 0x016FU}, {0x0170U, 0x0171U}, {0x0172U, 0x0173U},
        {0x0174U, 0x0175U}, {0x0176U, 0x0177U}, {0x0178U, 0x00FFU}, {0x0179U, 0x017AU},
        {0x017BU, 0x017CU}, {0x017DU, 0x017EU}, {0x0181U, 0x0253U}, {0x0182U, 0x0183U},
        {0x0184U, 0x0185U}, {0x0186U, 0x0254U}, {0x0187U, 0x0188U}, {0x0189U, 0x0256U},
        {0x018AU, 0x0257U}, {0x018BU, 0x018CU}, {0x018EU, 0x01DDU}, {0x018FU, 0x0259U},
        {0x0190U, 0x025BU}, {0x0191U, 0x0192U}, {0x0193U, 0x0260U}, {0x0194U, 0x0263U},
        {0x0196U, 0x0269U}, {0x0197U, 0x0268U}, {0x0198U, 0x0199U}, {0x019CU, 0x026FU},
        {0x019DU, 0x0272U}, {0x019FU, 0x0275U}, {0x01A0U, 0x01A1U}, {0x01A2U, 0x01A3U},
        {0x01A4U, 0x01A5U}, {0x01A6U, 0x0280U}, {0x01A7U, 0x01A8U}, {0x01A9U, 0x0283U},
        {0x01ACU, 0x01ADU}, {0x01AEU, 0x0288U}, {0x01AFU, 0x01B0U}, {0x01B1U, 0x028AU},
        {0x01B2U, 0x028BU}, {0x01B3U, 0x01B4U}, {0x01B5U, 0x01B6U}, {0x01B7U, 0x0292U},
        {0x01B8U, 0x01B9U}, {0x01BCU, 0x01BDU}, {0x01C4U, 0x01C6U}, {0x01C5U, 0x01C6U},
        {0x01C7U, 0x01C9U}, {0x01C8U, 0x01C9U}, {0x01CAU, 0x01CCU}, {0x01CBU, 0x01CCU},
        {0x01CDU, 0x01CEU}, {0x01CFU, 0x01D0U}, {0x01D1U, 0x01D2U}, {0x01D3U, 0x01D4U},
        {0x01D5U, 0x01D6U}, {0x01D7U, 0x01D8U}, {0x01D9U, 0x01DAU}, {0x01DBU, 0x01DCU},
        {0x01DEU, 0x01DFU}, {0x01E0U, 0x01E1U}, {0x01E2U, 0x01E3U}, {0x01E4U, 0x01E5U},
        {0x01E6U, 0x01E7U}, {0x01E8U, 0x01E9U}, {0x01EAU, 0x01EBU}, {0x01ECU, 0x01EDU},
        {0x01EEU, 0x01EFU}, {0x01F1U, 0x01F3U}, {0x01F2U, 0x01F3U}, {0x01F4U, 0x01F5U},
        {0x01F6U, 0x0195U}, {0x01F7U, 0x01BFU}, {0x01F8U, 0x01F9U}, {0x01FAU, 0x01FBU},
        {0x01FCU, 0x01FDU}, {0x01FEU, 0x01FFU}, {0x0200U, 0x0201U}, {0x0202U, 0x0203U},
        {0x0204U, 0x0205U}, {0x0206U, 0x0207U}, {0x0208U, 0x0209U}, {0x020AU, 0x020BU},
        {0x020CU, 0x020DU}, {0x020EU, 0x020FU}, {0x0210U, 0x0211U}, {0x0212U, 0x0213U},
        {0x0214U, 0x0215U}, {0x0216U, 0x0217U}, {0x0218U, 0x0219U}, {0x021AU, 0x021BU},
        {0x021CU, 0x021DU}, {0x021EU, 0x021FU}, {0x0220U, 0x019EU}, {0x0222U, 0x0223U},
        {0x0224U, 0x0225U}, {0x0226U, 0x0227U}, {0x0228U, 0x0229U}, {0x022AU, 0x022BU},
        {0x022CU, 0x022DU}, {0x022EU, 0x022FU}, {0x0230U, 0x0231U}, {0x0232U, 0x0233U},
        {0x023AU, 0x2C65U}, {0x023BU, 0x023CU}, {0x023DU, 0x019AU}, {0x023EU, 0x2C66U},
        {0x0241U, 0x0242U}, {0x0243U, 0x0180U}, {0x0244U, 0x0289U}, {0x0245U, 0x028CU},
        {0x0246U, 0x0247U}, {0x0248U, 0x0249U}, {0x024AU, 0x024BU}, {0x024CU, 0x024DU},
        {0x024EU, 0x024FU}, {0x0370U, 0x0371U}, {0x0372U, 0x0373U}, {0x0376U, 0x0377U},
        {0x037FU, 0x03F3U}, {0x0386U, 0x03ACU}, {0x0388U, 0x03ADU}, {0x0389U, 0x03AEU},
        {0x038AU, 0x03AFU}, {0x038CU, 0x03CCU}, {0x038EU, 0x03CDU}, {0x038FU, 0x03CEU},
        {0x0391U, 0x03B1U}, {0x0392U, 0x03B2U}, {0x0393U, 0x03B3U}, {0x0394U, 0x03B4U},
        {0x0395U, 0x03B5U}, {0x0396U, 0x03B6U}, {0x0397U, 0x03B7U}, {0x0398U, 0x03B8U},
        {0x0399U, 0x03B9U}, {0x039AU, 0x03BAU}, {0x039BU, 0x03BBU}, {0x039CU, 0x03BCU},
        {0x039DU, 0x03BDU}, {0x039EU, 0x03BEU}, {0x039FU, 0x03BFU}, {0x03A0U, 0x03C0U},
        {0x03A1U, 0x03C1U}, {0x03A3U, 0x03C3U}, {0x03A4U, 0x03C4U}, {0x03A5U, 0x03C5U},
        {0x03A6U, 0x03C6U}, {0x03A7U, 0x03C7U}, {0x03A8U, 0x03C8U}, {0x03A9U, 0x03C9U},
        {0x03AAU, 0x03CAU}, {0x03ABU, 0x03CBU}, {0x03CFU, 0x03D7U}, {0x03D8U, 0x03D9U},
        {0x03DAU, 0x03DBU}, {0x03DCU, 0x03DDU}, {0x03DEU, 0x03DFU}, {0x03E0U, 0x03E1U},
        {0x03E2U, 0x03E3U}, {0x03E4U, 0x03E5U}, {0x03E6U, 0x03E7U}, {0x03E8U, 0x03E9U},
        {0x03EAU, 0x03EBU}, {0x03ECU, 0x03EDU}, {0x03EEU, 0x03EFU}, {0x03F4U, 0x03B8U},
        {0x03F7U, 0x03F8U}, {0x03F9U, 0x03F2U}, {0x03FAU, 0x03FBU}, {0x03FDU, 0x037BU},
        {0x03FEU, 0x037CU}, {0x03FFU, 0x037DU}, {0x0400U, 0x0450U}, {0x0401U, 0x0451U},
        {0x0402U, 0x0452U}, {0x0403U, 0x0453U}, {0x0404U, 0x0454U}, {0x0405U, 0x0455U},
        {0x0406U, 0x0456U}, {0x0407U, 0x0457U}, {0x0408U, 0x0458U}, {0x0409U, 0x0459U},
        {0x040AU, 0x045AU}, {0x040BU, 0x045BU}, {0x040CU, 0x045CU}, {0x040DU, 0x045DU},
        {0x040EU, 0x045EU}, {0x040FU, 0x045FU}, {0x0410U, 0x0430U}, {0x0411U, 0x0431U},
        {0x0412U, 0x0432U}, {0x0413U, 0x0433U}, {0x0414U, 0x0434U}, {0x0415U, 0x0435U},
        {0x0416U, 0x0436U}, {0x0417U, 0x0437U}, {0x0418U, 0x0438U}, {0x0419U, 0x0439U},
        {0x041AU, 0x043AU}, {0x041BU, 0x043BU}, {0x041CU, 0x043CU}, {0x041DU, 0x043DU},
        {0x041EU, 0x043EU}, {0x041FU, 0x043FU}, {0x0420U, 0x0440U}, {0x0421U, 0x0441U},
        {0x0422U, 0x0442U}, {0x0423U, 0x0443U}, {0x0424U, 0x0444U}, {0x0425U, 0x0445U},
        {0x0426U, 0x0446U}, {0x0427U, 0x0447U}, {0x0428U, 0x0448U}, {0x0429U, 0x0449U},
        {0x042AU, 0x044AU}, {0x042BU, 0x044BU}, {0x042CU, 0x044CU}, {0x042DU, 0x044DU},
        {0x042EU, 0x044EU}, {0x042FU, 0x044FU}, {0x0460U, 0x0461U}, {0x0462U, 0x0463U},
        {0x0464U, 0x0465U}, {0x0466U, 0x0467U}, {0x0468U, 0x0469U}, {0x046AU, 0x046BU},
        {0x046CU, 0x046DU}, {0x046EU, 0x046FU}, {0x0470U, 0x0471U}, {0x0472U, 0x0473U},
        {0x0474U, 0x0475U}, {0x0476U, 0x0477U}, {0x0478U, 0x0479U}, {0x047AU, 0x047BU},
        {0x047CU, 0x047DU}, {0x047EU, 0x047FU}, {0x0480U, 0x0481U}, {0x048AU, 0x048BU},
        {0x048CU, 0x048DU}, {0x048EU, 0x048FU}, {0x0490U, 0x0491U}, {0x0492U, 0x0493U},
        {0x0494U, 0x0495U}, {0x0496U, 0x0497U}, {0x0498U, 0x0499U}, {0x049AU, 0x049BU},
        {0x049CU, 0x049DU}, {0x049EU, 0x049FU}, {0x04A0U, 0x04A1U}, {0x04A2U, 0x04A3U},
        {0x04A4U, 0x04A5U}, {0x04A6U, 0x04A7U}, {0x04A8U, 0x04A9U}, {0x04AAU, 0x04ABU},
        {0x04ACU, 0x04ADU}, {0x04AEU, 0x04AFU}, {0x04B0U, 0x04B1U}, {0x04B2U, 0x04B3U},
        {0x04B4U, 0x04B5U}, {0x04B6U, 0x04B7U}, {0x04B8U, 0x04B9U}, {0x04BAU, 0x04BBU},
        {0x04BCU, 0x04BDU}, {0x04BEU, 0x04BFU}, {0x04C0U, 0x04CFU}, {0x04C1U, 0x04C2U},
        {0x04C3U, 0x04C4U}, {0x04C5U, 0x04C6U}, {0x04C7U, 0x04C8U}, {0x04C9U, 0x04CAU},
        {0x04CBU, 0x04CCU}, {0x04CDU, 0x04CEU}, {0x04D0U, 0x04D1U}, {0x04D2U, 0x04D3U},
        {0x04D4U, 0x04D5U}, {0x04D6U, 0x04D7U}, {0x04D8U, 0x04D9U}, {0x04DAU, 0x04DBU},
        {0x04DCU, 0x04DDU}, {0x04DEU, 0x04DFU}, {0x04E0U, 0x04E1U}, {0x04E2U, 0x04E3U},
        {0x04E4U, 0x04E5U}, {0x04E6U, 0x04E7U}, {0x04E8U, 0x04E9U}, {0x04EAU, 0x04EBU},
        {0x04ECU, 0x04EDU}, {0x04EEU, 0x04EFU}, {0x04F0U, 0x04F1U}, {0x04F2U, 0x04F3U},
        {0x04F4U, 0x04F5U}, {0x04F6U, 0x04F7U}, {0x04F8U, 0x04F9U}, {0x04FAU, 0x04FBU},
        {0x04FCU, 0x04FDU}, {0x04FEU, 0x04FFU}, {0x0500U, 0x0501U}, {0x0502U, 0x0503U},
        {0x0504U, 0x0505U}, {0x0506U, 0x0507U}, {0x0508U, 0x0509U}, {0x050AU, 0x050BU},
        {0x050CU, 0x050DU}, {0x050EU, 0x050FU}, {0x0510U, 0x0511U}, {0x0512U, 0x0513U},
        {0x0514U, 0x0515U}, {0x0516U, 0x0517U}, {0x0518U, 0x0519U}, {0x051AU, 0x051BU},
        {0x051CU, 0x051DU}, {0x051EU, 0x051FU}, {0x0520U, 0x0521U}, {0x0522U, 0x0523U},
        {0x0524U, 0x0525U}, {0x0526U, 0x0527U}, {0x0528U, 0x0529U}, {0x052AU, 0x052BU},
        {0x052CU, 0x052DU}, {0x052EU, 0x052FU}, {0x0531U, 0x0561U}, {0x0532U, 0x0562U},
        {0x0533U, 0x0563U}, {0x0534U, 0x0564U}, {0x0535U, 0x0565U}, {0x0536U, 0x0566U},
        {0x0537U, 0x0567U}, {0x0538U, 0x0568U}, {0x0539U, 0x0569U}, {0x053AU, 0x056AU},
        {0x053BU, 0x056BU}, {0x053CU, 0x056CU}, {0x053DU, 0x056DU}, {0x053EU, 0x056EU},
        {0x053FU, 0x056FU}, {0x0540U, 0x0570U}, {0x0541U, 0x0571U}, {0x0542U, 0x0572U},
        {0x0543U, 0x0573U}, {0x0544U, 0x0574U}, {0x0545U, 0x0575U}, {0x0546U, 0x0576U},
        {0x0547U, 0x0577U}, {0x0548U, 0x0578U}, {0x0549U, 0x0579U}, {0x054AU, 0x057AU},
        {0x054BU, 0x057BU}, {0x054CU, 0x057CU}, {0x054DU, 0x057DU}, {0x054EU, 0x057EU},
        {0x054FU, 0x057FU}, {0x0550U, 0x0580U}, {0x0551U, 0x0581U}, {0x0552U, 0x0582U},
        {0x0553U, 0x0583U}, {0x0554U, 0x0584U}, {0x0555U, 0x0585U}, {0x0556U, 0x0586U},
        {0x10A0U, 0x2D00U}, {0x10A1U, 0x2D01U}, {0x10A2U, 0x2D02U}, {0x10A3U, 0x2D03U},
        {0x10A4U, 0x2D04U}, {0x10A5U, 0x2D05U}, {0x10A6U, 0x2D06U}, {0x10A7U, 0x2D07U},
        {0x10A8U, 0x2D08U}, {0x10A9U, 0x2D09U}, {0x10AAU, 0x2D0AU}, {0x10ABU, 0x2D0BU},
        {0x10ACU, 0x2D0CU}, {0x10ADU, 0x2D0DU}, {0x10AEU, 0x2D0EU}, {0x10AFU, 0x2D0FU},
        {0x10B0U, 0x2D10U}, {0x10B1U, 0x2D11U}, {0x10B2U, 0x2D12U}, {0x10B3U, 0x2D13U},
        {0x10B4U, 0x2D14U}, {0x10B5U, 0x2D15U}, {0x10B6U, 0x2D16U}, {0x10B7U, 0x2D17U},
        {0x10B8U, 0x2D18U}, {0x10B9U, 0x2D19U}, {0x10BAU, 0x2D1AU}, {0x10BBU, 0x2D1BU},
        {0x10BCU, 0x2D1CU}, {0x10BDU, 0x2D1DU}, {0x10BEU, 0x2D1EU}, {0x10BFU, 0x2D1FU},
        {0x10C0U, 0x2D20U}, {0x10C1U, 0x2D21U}, {0x10C2U, 0x2D22U}, {0x10C3U, 0x2D23U},
        {0x10C4U, 0x2D24U}, {0x10C5U, 0x2D25U}, {0x10C7U, 0x2D27U}, {0x10CDU, 0x2D2DU},
        {0x13A0U, 0xAB70U}, {0x13A1U, 0xAB71U}, {0x13A2U, 0xAB72U}, {0x13A3U, 0xAB73U},
        {0x13A4U, 0xAB74U}, {0x13A5U, 0xAB75U}, {0x13A6U, 0xAB76U}, {0x13A7U, 0xAB77U},
        {0x13A8U, 0xAB78U}, {0x13A9U, 0xAB79U}, {0x13AAU, 0xAB7AU}, {0x13ABU, 0xAB7BU},
        {0x13ACU, 0xAB7CU}, {0x13ADU, 0xAB7DU}, {0x13AEU, 0xAB7EU}, {0x13AFU, 0xAB7FU},
        {0x13B0U, 0xAB80U}, {0x13B1U, 0xAB81U}, {0x13B2U, 0xAB82U}, {0x13B3U, 0xAB83U},
        {0x13B4U, 0xAB84U}, {0x13B5U, 0xAB85U}, {0x13B6U, 0xAB86U}, {0x13B7U, 0xAB87U},
        {0x13B8U, 0xAB88U}, {0x13B9U, 0xAB89U}, {0x13BAU, 0xAB8AU}, {0x13BBU, 0xAB8BU},
        {0x13BCU, 0xAB8CU}, {0x13BDU, 0xAB8DU}, {0x13BEU, 0xAB8EU}, {0x13BFU, 0xAB8FU},
        {0x13C0U, 0xAB90U}, {0x13C1U, 0xAB91U}, {0x13C2U, 0xAB92U}, {0x13C3U, 0xAB93U},
        {0x13C4U, 0xAB94U}, {0x13C5U, 0xAB95U}, {0x13C6U, 0xAB96U}, {0x13C7U, 0xAB97U},
        {0x13C8U, 0xAB98U}, {0x13C9U, 0xAB99U}, {0x13CAU, 0xAB9AU}, {0x13CBU, 0xAB9BU},
        {0x13CCU, 0xAB9CU}, {0x13CDU, 0xAB9DU}, {0x13CEU, 0xAB9EU}, {0x13CFU, 0xAB9FU},
        {0x13D0U, 0xABA0U}, {0x13D1U, 0xABA1U}, {0x13D2U, 0xABA2U}, {0x13D3U, 0xABA3U},
        {0x13D4U, 0xABA4U}, {0x13D5U, 0xABA5U}, {0x13D6U, 0xABA6U}, {0x13D7U, 0xABA7U},
        {0x13D8U, 0xABA8U}, {0x13D9U, 0xABA9U}, {0x13DAU, 0xABAAU}, {0x13DBU, 0xABABU},
        {0x13DCU, 0xABACU}, {0x13DDU, 0xABADU}, {0x13DEU, 0xABAEU}, {0x13DFU, 0xABAFU},
        {0x13E0U, 0xABB0U}, {0x13E1U, 0xABB1U}, {0x13E2U, 0xABB2U}, {0x13E3U, 0xABB3U},
        {0x13E4U, 0xABB4U}, {0x13E5U, 0xABB5U}, {0x13E6U, 0xABB6U}, {0x13E7U, 0xABB7U},
        {0x13E8U, 0xABB8U}, {0x13E9U, 0xABB9U}, {0x13EAU, 0xABBAU}, {0x13EBU, 0xABBBU},
        {0x13ECU, 0xABBCU}, {0x13EDU, 0xABBDU}, {0x13EEU, 0xABBEU}, {0x13EFU, 0xABBFU},
        {0x13F0U, 0x13F8U}, {0x13F1U, 0x13F9U}, {0x13F2U, 0x13FAU}, {0x13F3U, 0x13FBU},
        {0x13F4U, 0x13FCU}, {0x13F5U, 0x13FDU}, {0x1C90U, 0x10D0U}, {0x1C91U, 0x10D1U},
        {0x1C92U, 0x10D2U}, {0x1C93U, 0x10D3U}, {0x1C94U, 0x10D4U}, {0x1C95U, 0x10D5U},
        {0x1C96U, 0x10D6U}, {0x1C97U, 0x10D7U}, {0x1C98U, 0x10D8U}, {0x1C99U, 0x10D9U},
        {0x1C9AU, 0x10DAU}, {0x1C9BU, 0x10DBU}, {0x1C9CU, 0x10DCU}, {0x1C9DU, 0x10DDU},
        {0x1C9EU, 0x10DEU}, {0x1C9FU, 0x10DFU}, {0x1CA0U, 0x10E0U}, {0x1CA1U, 0x10E1U},
        {0x1CA2U, 0x10E2U}, {0x1CA3U, 0x10E3U}, {0x1CA4U, 0x10E4U}, {0x1CA5U, 0x10E5U},
        {0x1CA6U, 0x10E6U}, {0x1CA7U, 0x10E7U}, {0x1CA8U, 0x10E8U}, {0x1CA9U, 0x10E9U},
        {0x1CAAU, 0x10EAU}, {0x1CABU, 0x10EBU}, {0x1CACU, 0x10ECU}, {0x1CADU, 0x10EDU},
        {0x1CAEU, 0x10EEU}, {0x1CAFU, 0x10EFU}, {0x1CB0U, 0x10F0U}, {0x1CB1U, 0x10F1U},
        {0x1CB2U, 0x10F2U}, {0x1CB3U, 0x10F3U}, {0x1CB4U, 0x10F4U}, {0x1CB5U, 0x10F5U},
        {0x1CB6U, 0x10F6U}, {0x1CB7U, 0x10F7U}, {0x1CB8U, 0x10F8U}, {0x1CB9U, 0x10F9U},
        {0x1CBAU, 0x10FAU}, {0x1CBDU, 0x10FDU}, {0x1CBEU, 0x10FEU}, {0x1CBFU, 0x10FFU},
        {0x1E00U, 0x1E01U}, {0x1E02U, 0x1E03U}, {0x1E04U, 0x1E05U}, {0x1E06U, 0x1E07U},
        {0x1E08U, 0x1E09U}, {0x1E0AU, 0x1E0BU}, {0x1E0CU, 0x1E0DU}, {0x1E0EU, 0x1E0FU},
        {0x1E10U, 0x1E11U}, {0x1E12U, 0x1E13U}, {0x1E14U, 0x1E15U}, {0x1E16U, 0x1E17U},
        {0x1E18U, 0x1E19U}, {0x1E1AU, 0x1E1BU}, {0x1E1CU, 0x1E1DU}, {0x1E1EU, 0x1E1FU},
        {0x1E20U, 0x1E21U}, {0x1E22U, 0x1E23U}, {0x1E24U, 0x1E25U}, {0x1E26U, 0x1E27U},
        {0x1E28U, 0x1E29U}, {0x1E2AU, 0x1E2BU}, {0x1E2CU, 0x1E2DU}, {0x1E2EU, 0x1E2FU},
        {0x1E30U, 0x1E31U}, {0x1E32U, 0x1E33U}, {0x1E34U, 0x1E35U}, {0x1E36U, 0x1E37U},
        {0x1E38U, 0x1E39U}, {0x1E3AU, 0x1E3BU}, {0x1E3CU, 0x1E3DU}, {0x1E3EU, 0x1E3FU},
        {0x1E40U, 0x1E41U}, {0x1E42U, 0x1E43U}, {0x1E44U, 0x1E45U}, {0x1E46U, 0x1E47U},
        {0x1E48U, 0x1E49U}, {0x1E4AU, 0x1E4BU}, {0x1E4CU, 0x1E4DU}, {0x1E4EU, 0x1E4FU},
        {0x1E50U, 0x1E51U}, {0x1E52U, 0x1E53U}, {0x1E54U, 0x1E55U}, {0x1E56U, 0x1E57U},
        {0x1E58U, 0x1E59U}, {0x1E5AU, 0x1E5BU}, {0x1E5CU, 0x1E5DU}, {0x1E5EU, 0x1E5FU},
        {0x1E60U, 0x1E61U}, {0x1E62U, 0x1E63U}, {0x1E64U, 0x1E65U}, {0x1E66U, 0x1E67U},
        {0x1E68U, 0x1E69U}, {0x1E6AU, 0x1E6BU}, {0x1E6CU, 0x1E6DU}, {0x1E6EU, 0x1E6FU},
        {0x1E70U, 0x1E71U}, {0x1E72U, 0x1E73U}, {0x1E74U, 0x1E75U}, {0x1E76U, 0x1E77U},
        {0x1E78U, 0x1E79U}, {0x1E7AU, 0x1E7BU}, {0x1E7CU, 0x1E7DU}, {0x1E7EU, 0x1E7FU},
        {0x1E80U, 0x1E81U}, {0x1E82U, 0x1E83U}, {0x1E84U, 0x1E85U}, {0x1E86U, 0x1E87U},
        {0x1E88U, 0x1E89U}, {0x1E8AU, 0x1E8BU}, {0x1E8CU, 0x1E8DU}, {0x1E8EU, 0x1E8FU},
        {0x1E90U, 0x1E91U}, {0x1E92U, 0x1E93U}, {0x1E94U, 0x1E95U}, {0x1E9EU, 0x00DFU},
        {0x1EA0U, 0x1EA1U}, {0x1EA2U, 0x1EA3U}, {0x1EA4U, 0x1EA5U}, {0x1EA6U, 0x1EA7U},
        {0x1EA8U, 0x1EA9U}, {0x1EAAU, 0x1EABU}, {0x1EACU, 0x1EADU}, {0x1EAEU, 0x1EAFU},
        {0x1EB0U, 0x1EB1U}, {0x1EB2U, 0x1EB3U}, {0x1EB4U, 0x1EB5U}, {0x1EB6U, 0x1EB7U},
        {0x1EB8U, 0x1EB9U}, {0x1EBAU, 0x1EBBU}, {0x1EBCU, 0x1EBDU}, {0x1EBEU, 0x1EBFU},
        {0x1EC0U, 0x1EC1U}, {0x1EC2U, 0x1EC3U}, {0x1EC4U, 0x1EC5U}, {0x1EC6U, 0x1EC7U},
        {0x1EC8U, 0x1EC9U}, {0x1ECAU, 0x1ECBU}, {0x1ECCU, 0x1ECDU}, {0x1ECEU, 0x1ECFU},
        {0x1ED0U, 0x1ED1U}, {0x1ED2U, 0x1ED3U}, {0x1ED4U, 0x1ED5U}, {0x1ED6U, 0x1ED7U},
        {0x1ED8U, 0x1ED9U}, {0x1EDAU, 0x1EDBU}, {0x1EDCU, 0x1EDDU}, {0x1EDEU, 0x1EDFU},
        {0x1EE0U, 0x1EE1U}, {0x1EE2U, 0x1EE3U}, {0x1EE4U, 0x1EE5U}, {0x1EE6U, 0x1EE7U},
        {0x1EE8U, 0x1EE9U}, {0x1EEAU, 0x1EEBU}, {0x1EECU, 0x1EEDU}, {0x1EEEU, 0x1EEFU},
        {0x1EF0U, 0x1EF1U}, {0x1EF2U, 0x1EF3U}, {0x1EF4U, 0x1EF5U}, {0x1EF6U, 0x1EF7U},
        {0x1EF8U, 0x1EF9U}, {0x1EFAU, 0x1EFBU}, {0x1EFCU, 0x1EFDU}, {0x1EFEU, 0x1EFFU},
        {0x1F08U, 0x1F00U}, {0x1F09U, 0x1F01U}, {0x1F0AU, 0x1F02U}, {0x1F0BU, 0x1F03U},
        {0x1F0CU, 0x1F04U}, {0x1F0DU, 0x1F05U}, {0x1F0EU, 0x1F06U}, {0x1F0FU, 0x1F07U},
        {0x1F18U, 0x1F10U}, {0x1F19U, 0x1F11U}, {0x1F1AU, 0x1F12U}, {0x1F1BU, 0x1F13U},
        {0x1F1CU, 0x1F14U}, {0x1F1DU, 0x1F15U}, {0x1F28U, 0x1F20U}, {0x1F29U, 0x1F21U},
        {0x1F2AU, 0x1F22U}, {0x1F2BU, 0x1F23U}, {0x1F2CU, 0x1F24U}, {0x1F2DU, 0x1F25U},
        {0x1F2EU, 0x1F26U}, {0x1F2FU, 0x1F27U}, {0x1F38U, 0x1F30U}, {0x1F39U, 0x1F31U},
        {0x1F3AU, 0x1F32U}, {0x1F3BU, 0x1F33U}, {0x1F3CU, 0x1F34U}, {0x1F3DU, 0x1F35U},
        {0x1F3EU, 0x1F36U}, {0x1F3FU, 0x1F37U}, {0x1F48U, 0x1F40U}, {0x1F49U, 0x1F41U},
        {0x1F4AU, 0x1F42U}, {0x1F4BU, 0x1F43U}, {0x1F4CU, 0x1F44U}, {0x1F4DU, 0x1F45U},
        {0x1F59U, 0x1F51U}, {0x1F5BU, 0x1F53U}, {0x1F5DU, 0x1F55U}, {0x1F5FU, 0x1F57U},
        {0x1F68U, 0x1F60U}, {0x1F69U, 0x1F61U}, {0x1F6AU, 0x1F62U}, {0x1F6BU, 0x1F63U},
        {0x1F6CU, 0x1F64U}, {0x1F6DU, 0x1F65U}, {0x1F6EU, 0x1F66U}, {0x1F6FU, 0x1F67U},
        {0x1F88U, 0x1F80U}, {0x1F89U, 0x1F81U}, {0x1F8AU, 0x1F82U}, {0x1F8BU, 0x1F83U},
        {0x1F8CU, 0x1F84U}, {0x1F8DU, 0x1F85U}, {0x1F8EU, 0x1F86U}, {0x1F8FU, 0x1F87U},
        {0x1F98U, 0x1F90U}, {0x1F99U, 0x1F91U}, {0x1F9AU, 0x1F92U}, {0x1F9BU, 0x1F93U},
        {0x1F9CU, 0x1F94U}, {0x1F9DU, 0x1F95U}, {0x1F9EU, 0x1F96U}, {0x1F9FU, 0x1F97U},
        {0x1FA8U, 0x1FA0U}, {0x1FA9U, 0x1FA1U}, {0x1FAAU, 0x1FA2U}, {0x1FABU, 0x1FA3U},
        {0x1FACU, 0x1FA4U}, {0x1FADU, 0x1FA5U}, {0x1FAEU, 0x1FA6U}, {0x1FAFU, 0x1FA7U},
        {0x1FB8U, 0x1FB0U}, {0x1FB9U, 0x1FB1U}, {0x1FBAU, 0x1F70U}, {0x1FBBU, 0x1F71U},
        {0x1FBCU, 0x1FB3U}, {0x1FC8U, 0x1F72U}, {0x1FC9U, 0x1F73U}, {0x1FCAU, 0x1F74U},
        {0x1FCBU, 0x1F75U}, {0x1FCCU, 0x1FC3U}, {0x1FD8U, 0x1FD0U}, {0x1FD9U, 0x1FD1U},
        {0x1FDAU, 0x1F76U}, {0x1FDBU, 0x1F77U}, {0x1FE8U, 0x1FE0U}, {0x1FE9U, 0x1FE1U},
        {0x1FEAU, 0x1F7AU}, {0x1FEBU, 0x1F7BU}, {0x1FECU, 0x1FE5U}, {0x1FF8U, 0x1F78U},
        {0x1FF9U, 0x1F79U}, {0x1FFAU, 0x1F7CU}, {0x1FFBU, 0x1F7DU}, {0x1FFCU, 0x1FF3U},
        {0x2126U, 0x03C9U}, {0x212AU, 0x006BU}, {0x212BU, 0x00E5U}, {0x2132U, 0x214EU},
        {0x2160U, 0x2170U}, {0x2161U, 0x2171U}, {0x2162U, 0x2172U}, {0x2163U, 0x2173U},
        {0x2164U, 0x2174U}, {0x2165U, 0x2175U}, {0x2166U, 0x2176U}, {0x2167U, 0x2177U},
        {0x2168U, 0x2178U}, {0x2169U, 0x2179U}, {0x216AU, 0x217AU}, {0x216BU, 0x217BU},
        {0x216CU, 0x217CU}, {0x216DU, 0x217DU}, {0x216EU, 0x217EU}, {0x216FU, 0x217FU},
        {0x2183U, 0x2184U}, {0x24B6U, 0x24D0U}, {0x24B7U, 0x24D1U}, {0x24B8U, 0x24D2U},
        {0x24B9U, 0x24D3U}, {0x24BAU, 0x24D4U}, {0x24BBU, 0x24D5U}, {0x24BCU, 0x24D6U},
        {0x24BDU, 0x24D7U}, {0x24BEU, 0x24D8U}, {0x24BFU, 0x24D9U}, {0x24C0U, 0x24DAU},
        {0x24C1U, 0x24DBU}, {0x24C2U, 0x24DCU}, {0x24C3U, 0x24DDU}, {0x24C4U, 0x24DEU},
        {0x24C5U, 0x24DFU}, {0x24C6U, 0x24E0U}, {0x24C7U, 0x24E1U}, {0x24C8U, 0x24E2U},
        {0x24C9U, 0x24E3U}, {0x24CAU, 0x24E4U}, {0x24CBU, 0x24E5U}, {0x24CCU, 0x24E6U},
        {0x24CDU, 0x24E7U}, {0x24CEU, 0x24E8U}, {0x24CFU, 0x24E9U}, {0x2C00U, 0x2C30U},
        {0x2C01U, 0x2C31U}, {0x2C02U, 0x2C32U}, {0x2C03U, 0x2C33U}, {0x2C04U, 0x2C34U},
        {0x2C05U, 0x2C35U}, {0x2C06U, 0x2C36U}, {0x2C07U, 0x2C37U}, {0x2C08U, 0x2C38U},
        {0x2C09U, 0x2C39U}, {0x2C0AU, 0x2C3AU}, {0x2C0BU, 0x2C3BU}, {0x2C0CU, 0x2C3CU},
        {0x2C0DU, 0x2C3DU}, {0x2C0EU, 0x2C3EU}, {0x2C0FU, 0x2C3FU}, {0x2C10U, 0x2C40U},
        {0x2C11U, 0x2C41U}, {0x2C12U, 0x2C42U}, {0x2C13U, 0x2C43U}, {0x2C14U, 0x2C44U},
        {0x2C15U, 0x2C45U}, {0x2C16U, 0x2C46U}, {0x2C17U, 0x2C47U}, {0x2C18U, 0x2C48U},
        {0x2C19U, 0x2C49U}, {0x2C1AU, 0x2C4AU}, {0x2C1BU, 0x2C4BU}, {0x2C1CU, 0x2C4CU},
        {0x2C1DU, 0x2C4DU}, {0x2C1EU, 0x2C4EU}, {0x2C1FU, 0x2C4FU}, {0x2C20U, 0x2C50U},
        {0x2C21U, 0x2C51U}, {0x2C22U, 0x2C52U}, {0x2C23U, 0x2C53U}, {0x2C24U, 0x2C54U},
        {0x2C25U, 0x2C55U}, {0x2C26U, 0x2C56U}, {0x2C27U, 0x2C57U}, {0x2C28U, 0x2C58U},
        {0x2C29U, 0x2C59U}, {0x2C2AU, 0x2C5AU}, {0x2C2BU, 0x2C5BU}, {0x2C2CU, 0x2C5CU},
        {0x2C2DU, 0x2C5DU}, {0x2C2EU, 0x2C5EU}, {0x2C2FU, 0x2C5FU}, {0x2C60U, 0x2C61U},
        {0x2C62U, 0x026BU}, {0x2C63U, 0x1D7DU}, {0x2C64U, 0x027DU}, {0x2C67U, 0x2C68U},
        {0x2C69U, 0x2C6AU}, {0x2C6BU, 0x2C6CU}, {0x2C6DU, 0x0251U}, {0x2C6EU, 0x0271U},
        {0x2C6FU, 0x0250U}, {0x2C70U, 0x0252U}, {0x2C72U, 0x2C73U}, {0x2C75U, 0x2C76U},
        {0x2C7EU, 0x023FU}, {0x2C7FU, 0x0240U}, {0x2C80U, 0x2C81U}, {0x2C82U, 0x2C83U},
        {0x2C84U, 0x2C85U}, {0x2C86U, 0x2C87U}, {0x2C88U, 0x2C89U}, {0x2C8AU, 0x2C8BU},
        {0x2C8CU, 0x2C8DU}, {0x2C8EU, 0x2C8FU}, {0x2C90U, 0x2C91U}, {0x2C92U, 0x2C93U},
        {0x2C94U, 0x2C95U}, {0x2C96U, 0x2C97U}, {0x2C98U, 0x2C99U}, {0x2C9AU, 0x2C9BU},
        {0x2C9CU, 0x2C9DU}, {0x2C9EU, 0x2C9FU}, {0x2CA0U, 0x2CA1U}, {0x2CA2U, 0x2CA3U},
        {0x2CA4U, 0x2CA5U}, {0x2CA6U, 0x2CA7U}, {0x2CA8U, 0x2CA9U}, {0x2CAAU, 0x2CABU},
        {0x2CACU, 0x2CADU}, {0x2CAEU, 0x2CAFU}, {0x2CB0U, 0x2CB1U}, {0x2CB2U, 0x2CB3U},
        {0x2CB4U, 0x2CB5U}, {0x2CB6U, 0x2CB7U}, {0x2CB8U, 0x2CB9U}, {0x2CBAU, 0x2CBBU},
        {0x2CBCU, 0x2CBDU}, {0x2CBEU, 0x2CBFU}, {0x2CC0U, 0x2CC1U}, {0x2CC2U, 0x2CC3U},
        {0x2CC4U, 0x2CC5U}, {0x2CC6U, 0x2CC7U}, {0x2CC8U, 0x2CC9U}, {0x2CCAU, 0x2CCBU},
        {0x2CCCU, 0x2CCDU}, {0x2CCEU, 0x2CCFU}, {0x2CD0U, 0x2CD1U}, {0x2CD2U, 0x2CD3U},
        {0x2CD4U, 0x2CD5U}, {0x2CD6U, 0x2CD7U}, {0x2CD8U, 0x2CD9U}, {0x2CDAU, 0x2CDBU},
        {0x2CDCU, 0x2CDDU}, {0x2CDEU, 0x2CDFU}, {0x2CE0U, 0x2CE1U}, {0x2CE2U, 0x2CE3U},
        {0x2CEBU, 0x2CECU}, {0x2CEDU, 0x2CEEU}, {0x2CF2U, 0x2CF3U}, {0xA640U, 0xA641U},
        {0xA642U, 0xA643U}, {0xA644U, 0xA645U}, {0xA646U, 0xA647U}, {0xA648U, 0xA649U},
        {0xA64AU, 0xA64BU}, {0xA64CU, 0xA64DU}, {0xA64EU, 0xA64FU}, {0xA650U, 0xA651U},
        {0xA652U, 0xA653U}, {0xA654U, 0xA655U}, {0xA656U, 0xA657U}, {0xA658U, 0xA659U},
        {0xA65AU, 0xA65BU}, {0xA65CU, 0xA65DU}, {0xA65EU, 0xA65FU}, {0xA660U, 0xA661U},
        {0xA662U, 0xA663U}, {0xA664U, 0xA665U}, {0xA666U, 0xA667U}, {0xA668U, 0xA669U},
        {0xA66AU, 0xA66BU}, {0xA66CU, 0xA66DU}, {0xA680U, 0xA681U}, {0xA682U, 0xA683U},
        {0xA684U, 0xA685U}, {0xA686U, 0xA687U}, {0xA688U, 0xA689U}, {0xA68AU, 0xA68BU},
        {0xA68CU, 0xA68DU}, {0xA68EU, 0xA68FU}, {0xA690U, 0xA691U}, {0xA692U, 0xA693U},
        {0xA694U, 0xA695U}, {0xA696U, 0xA697U}, {0xA698U, 0xA699U}, {0xA69AU, 0xA69BU},
        {0xA722U, 0xA723U}, {0xA724U, 0xA725U}, {0xA726U, 0xA727U}, {0xA728U, 0xA729U},
        {0xA72AU, 0xA72BU}, {0xA72CU, 0xA72DU}, {0xA72EU, 0xA72FU}, {0xA732U, 0xA733U},
        {0xA734U, 0xA735U}, {0xA736U, 0xA737U}, {0xA738U, 0xA739U}, {0xA73AU, 0xA73BU},
        {0xA73CU, 0xA73DU}, {0xA73EU, 0xA73FU}, {0xA740U, 0xA741U}, {0xA742U, 0xA743U},
        {0xA744U, 0xA745U}, {0xA746U, 0xA747U}, {0xA748U, 0xA749U}, {0xA74AU, 0xA74BU},
        {0xA74CU, 0xA74DU}, {0xA74EU, 0xA74FU}, {0xA750U, 0xA751U}, {0xA752U, 0xA753U},
        {0xA754U, 0xA755U}, {0xA756U, 0xA757U}, {0xA758U, 0xA759U}, {0xA75AU, 0xA75BU},
        {0xA75CU, 0xA75DU}, {0xA75EU, 0xA75FU}, {0xA760U, 0xA761U}, {0xA762U, 0xA763U},
        {0xA764U, 0xA765U}, {0xA766U, 0xA767U}, {0xA768U, 0xA769U}, {0xA76AU, 0xA76BU},
        {0xA76CU, 0xA76DU}, {0xA76EU, 0xA76FU}, {0xA779U, 0xA77AU}, {0xA77BU, 0xA77CU},
        {0xA77DU, 0x1D79U}, {0xA77EU, 0xA77FU}, {0xA780U, 0xA781U}, {0xA782U, 0xA783U},
        {0xA784U, 0xA785U}, {0xA786U, 0xA787U}, {0xA78BU, 0xA78CU}, {0xA78DU, 0x0265U},
        {0xA790U, 0xA791U}, {0xA792U, 0xA793U}, {0xA796U, 0xA797U}, {0xA798U, 0xA799U},
        {0xA79AU, 0xA79BU}, {0xA79CU, 0xA79DU}, {0xA79EU, 0xA79FU}, {0xA7A0U, 0xA7A1U},
        {0xA7A2U, 0xA7A3U}, {0xA7A4U, 0xA7A5U}, {0xA7A6U, 0xA7A7U}, {0xA7A8U, 0xA7A9U},
        {0xA7AAU, 0x0266U}, {0xA7ABU, 0x025CU}, {0xA7ACU, 0x0261U}, {0xA7ADU, 0x026CU},
        {0xA7AEU, 0x026AU}, {0xA7B0U, 0x029EU}, {0xA7B1U, 0x0287U}, {0xA7B2U, 0x029DU},
        {0xA7B3U, 0xAB53U}, {0xA7B4U, 0xA7B5U}, {0xA7B6U, 0xA7B7U}, {0xA7B8U, 0xA7B9U},
        {0xA7BAU, 0xA7BBU}, {0xA7BCU, 0xA7BDU}, {0xA7BEU, 0xA7BFU}, {0xA7C0U, 0xA7C1U},
        {0xA7C2U, 0xA7C3U}, {0xA7C4U, 0xA794U}, {0xA7C5U, 0x0282U}, {0xA7C6U, 0x1D8EU},
        {0xA7C7U, 0xA7C8U}, {0xA7C9U, 0xA7CAU}, {0xA7D0U, 0xA7D1U}, {0xA7D6U, 0xA7D7U},
        {0xA7D8U, 0xA7D9U}, {0xA7F5U, 0xA7F6U}, {0xFF21U, 0xFF41U}, {0xFF22U, 0xFF42U},
        {0xFF23U, 0xFF43U}, {0xFF24U, 0xFF44U}, {0xFF25U, 0xFF45U}, {0xFF26U, 0xFF46U},
        {0xFF27U, 0xFF47U}, {0xFF28U, 0xFF48U}, {0xFF29U, 0xFF49U}, {0xFF2AU, 0xFF4AU},
        {0xFF2BU, 0xFF4BU}, {0xFF2CU, 0xFF4CU}, {0xFF2DU, 0xFF4DU}, {0xFF2EU, 0xFF4EU},
        {0xFF2FU, 0xFF4FU}, {0xFF30U, 0xFF50U}, {0xFF31U, 0xFF51U}, {0xFF32U, 0xFF52U},
        {0xFF33U, 0xFF53U}, {0xFF34U, 0xFF54U}, {0xFF35U, 0xFF55U}, {0xFF36U, 0xFF56U},
        {0xFF37U, 0xFF57U}, {0xFF38U, 0xFF58U}, {0xFF39U, 0xFF59U}, {0xFF3AU, 0xFF5AU},
        {0x10400U, 0x10428U}, {0x10401U, 0x10429U}, {0x10402U, 0x1042AU}, {0x10403U, 0x1042BU},
        {0x10404U, 0x1042CU}, {0x10405U, 0x1042DU}, {0x10406U, 0x1042EU}, {0x10407U, 0x1042FU},
        {0x10408U, 0x10430U}, {0x10409U, 0x10431U}, {0x1040AU, 0x10432U}, {0x1040BU, 0x10433U},
        {0x1040CU, 0x10434U}, {0x1040DU, 0x10435U}, {0x1040EU, 0x10436U}, {0x1040FU, 0x10437U},
        {0x10410U, 0x10438U}, {0x10411U, 0x10439U}, {0x10412U, 0x1043AU}, {0x10413U, 0x1043BU},
        {0x10414U, 0x1043CU}, {0x10415U, 0x1043DU}, {0x10416U, 0x1043EU}, {0x10417U, 0x1043FU},
        {0x10418U, 0x10440U}, {0x10419U, 0x10441U}, {0x1041AU, 0x10442U}, {0x1041BU, 0x10443U},
        {0x1041CU, 0x10444U}, {0x1041DU, 0x10445U}, {0x1041EU, 0x10446U}, {0x1041FU, 0x10447U},
        {0x10420U, 0x10448U}, {0x10421U, 0x10449U}, {0x10422U, 0x1044AU}, {0x10423U, 0x1044BU},
        {0x10424U, 0x1044CU}, {0x10425U, 0x1044DU}, {0x10426U, 0x1044EU}, {0x10427U, 0x1044FU},
        {0x104B0U, 0x104D8U}, {0x104B1U, 0x104D9U}, {0x104B2U, 0x104DAU}, {0x104B3U, 0x104DBU},
        {0x104B4U, 0x104DCU}, {0x104B5U, 0x104DDU}, {0x104B6U, 0x104DEU}, {0x104B7U, 0x104DFU},
        {0x104B8U, 0x104E0U}, {0x104B9U, 0x104E1U}, {0x104BAU, 0x104E2U}, {0x104BBU, 0x104E3U},
        {0x104BCU, 0x104E4U}, {0x104BDU, 0x104E5U}, {0x104BEU, 0x104E6U}, {0x104BFU, 0x104E7U},
        {0x104C0U, 0x104E8U}, {0x104C1U, 0x104E9U}, {0x104C2U, 0x104EAU}, {0x104C3U, 0x104EBU},
        {0x104C4U, 0x104ECU}, {0x104C5U, 0x104EDU}, {0x104C6U, 0x104EEU}, {0x104C7U, 0x104EFU},
        {0x104C8U, 0x104F0U}, {0x104C9U, 0x104F1U}, {0x104CAU, 0x104F2U}, {0x104CBU, 0x104F3U},
        {0x104CCU, 0x104F4U}, {0x104CDU, 0x104F5U}, {0x104CEU, 0x104F6U}, {0x104CFU, 0x104F7U},
        {0x104D0U, 0x104F8U}, {0x104D1U, 0x104F9U}, {0x104D2U, 0x104FAU}, {0x104D3U, 0x104FBU},
        {0x10570U, 0x10597U}, {0x10571U, 0x10598U}, {0x10572U, 0x10599U}, {0x10573U, 0x1059AU},
        {0x10574U, 0x1059BU}, {0x10575U, 0x1059CU}, {0x10576U, 0x1059DU}, {0x10577U, 0x1059EU},
        {0x10578U, 0x1059FU}, {0x10579U, 0x105A0U}, {0x1057AU, 0x105A1U}, {0x1057CU, 0x105A3U},
        {0x1057DU, 0x105A4U}, {0x1057EU, 0x105A5U}, {0x1057FU, 0x105A6U}, {0x10580U, 0x105A7U},
        {0x10581U, 0x105A8U}, {0x10582U, 0x105A9U}, {0x10583U, 0x105AAU}, {0x10584U, 0x105ABU},
        {0x10585U, 0x105ACU}, {0x10586U, 0x105ADU}, {0x10587U, 0x105AEU}, {0x10588U, 0x105AFU},
        {0x10589U, 0x105B0U}, {0x1058AU, 0x105B1U}, {0x1058CU, 0x105B3U}, {0x1058DU, 0x105B4U},
        {0x1058EU, 0x105B5U}, {0x1058FU, 0x105B6U}, {0x10590U, 0x105B7U}, {0x10591U, 0x105B8U},
        {0x10592U, 0x105B9U}, {0x10594U, 0x105BBU}, {0x10595U, 0x105BCU}, {0x10C80U, 0x10CC0U},
        {0x10C81U, 0x10CC1U}, {0x10C82U, 0x10CC2U}, {0x10C83U, 0x10CC3U}, {0x10C84U, 0x10CC4U},
        {0x10C85U, 0x10CC5U}, {0x10C86U, 0x10CC6U}, {0x10C87U, 0x10CC7U}, {0x10C88U, 0x10CC8U},
        {0x10C89U, 0x10CC9U}, {0x10C8AU, 0x10CCAU}, {0x10C8BU, 0x10CCBU}, {0x10C8CU, 0x10CCCU},
        {0x10C8DU, 0x10CCDU}, {0x10C8EU, 0x10CCEU}, {0x10C8FU, 0x10CCFU}, {0x10C90U, 0x10CD0U},
        {0x10C91U, 0x10CD1U}, {0x10C92U, 0x10CD2U}, {0x10C93U, 0x10CD3U}, {0x10C94U, 0x10CD4U},
        {0x10C95U, 0x10CD5U}, {0x10C96U, 0x10CD6U}, {0x10C97U, 0x10CD7U}, {0x10C98U, 0x10CD8U},
        {0x10C99U, 0x10CD9U}, {0x10C9AU, 0x10CDAU}, {0x10C9BU, 0x10CDBU}, {0x10C9CU, 0x10CDCU},
        {0x10C9DU, 0x10CDDU}, {0x10C9EU, 0x10CDEU}, {0x10C9FU, 0x10CDFU}, {0x10CA0U, 0x10CE0U},
        {0x10CA1U, 0x10CE1U}, {0x10CA2U, 0x10CE2U}, {0x10CA3U, 0x10CE3U}, {0x10CA4U, 0x10CE4U},
        {0x10CA5U, 0x10CE5U}, {0x10CA6U, 0x10CE6U}, {0x10CA7U, 0x10CE7U}, {0x10CA8U, 0x10CE8U},
        {0x10CA9U, 0x10CE9U}, {0x10CAAU, 0x10CEAU}, {0x10CABU, 0x10CEBU}, {0x10CACU, 0x10CECU},
        {0x10CADU, 0x10CEDU}, {0x10CAEU, 0x10CEEU}, {0x10CAFU, 0x10CEFU}, {0x10CB0U, 0x10CF0U},
        {0x10CB1U, 0x10CF1U}, {0x10CB2U, 0x10CF2U}, {0x10D50U, 0x10D70U}, {0x10D51U, 0x10D71U},
        {0x10D52U, 0x10D72U}, {0x10D53U, 0x10D73U}, {0x10D54U, 0x10D74U}, {0x10D55U, 0x10D75U},
        {0x10D56U, 0x10D76U}, {0x10D57U, 0x10D77U}, {0x10D58U, 0x10D78U}, {0x10D59U, 0x10D79U},
        {0x10D5AU, 0x10D7AU}, {0x10D5BU, 0x10D7BU}, {0x10D5CU, 0x10D7CU}, {0x10D5DU, 0x10D7DU},
        {0x10D5EU, 0x10D7EU}, {0x10D5FU, 0x10D7FU}, {0x10D60U, 0x10D80U}, {0x10D61U, 0x10D81U},
        {0x10D62U, 0x10D82U}, {0x10D63U, 0x10D83U}, {0x10D64U, 0x10D84U}, {0x10D65U, 0x10D85U},
        {0x118A0U, 0x118C0U}, {0x118A1U, 0x118C1U}, {0x118A2U, 0x118C2U}, {0x118A3U, 0x118C3U},
        {0x118A4U, 0x118C4U}, {0x118A5U, 0x118C5U}, {0x118A6U, 0x118C6U}, {0x118A7U, 0x118C7U},
        {0x118A8U, 0x118C8U}, {0x118A9U, 0x118C9U}, {0x118AAU, 0x118CAU}, {0x118ABU, 0x118CBU},
        {0x118ACU, 0x118CCU}, {0x118ADU, 0x118CDU}, {0x118AEU, 0x118CEU}, {0x118AFU, 0x118CFU},
        {0x118B0U, 0x118D0U}, {0x118B1U, 0x118D1U}, {0x118B2U, 0x118D2U}, {0x118B3U, 0x118D3U},
        {0x118B4U, 0x118D4U}, {0x118B5U, 0x118D5U}, {0x118B6U, 0x118D6U}, {0x118B7U, 0x118D7U},
        {0x118B8U, 0x118D8U}, {0x118B9U, 0x118D9U}, {0x118BAU, 0x118DAU}, {0x118BBU, 0x118DBU},
        {0x118BCU, 0x118DCU}, {0x118BDU, 0x118DDU}, {0x118BEU, 0x118DEU}, {0x118BFU, 0x118DFU},
        {0x16E40U, 0x16E60U}, {0x16E41U, 0x16E61U}, {0x16E42U, 0x16E62U}, {0x16E43U, 0x16E63U},
        {0x16E44U, 0x16E64U}, {0x16E45U, 0x16E65U}, {0x16E46U, 0x16E66U}, {0x16E47U, 0x16E67U},
        {0x16E48U, 0x16E68U}, {0x16E49U, 0x16E69U}, {0x16E4AU, 0x16E6AU}, {0x16E4BU, 0x16E6BU},
        {0x16E4CU, 0x16E6CU}, {0x16E4DU, 0x16E6DU}, {0x16E4EU, 0x16E6EU}, {0x16E4FU, 0x16E6FU},
        {0x16E50U, 0x16E70U}, {0x16E51U, 0x16E71U}, {0x16E52U, 0x16E72U}, {0x16E53U, 0x16E73U},
        {0x16E54U, 0x16E74U}, {0x16E55U, 0x16E75U}, {0x16E56U, 0x16E76U}, {0x16E57U, 0x16E77U},
        {0x16E58U, 0x16E78U}, {0x16E59U, 0x16E79U}, {0x16E5AU, 0x16E7AU}, {0x16E5BU, 0x16E7BU},
        {0x16E5CU, 0x16E7CU}, {0x16E5DU, 0x16E7DU}, {0x16E5EU, 0x16E7EU}, {0x16E5FU, 0x16E7FU},
        {0x1E900U, 0x1E922U}, {0x1E901U, 0x1E923U}, {0x1E902U, 0x1E924U}, {0x1E903U, 0x1E925U},
        {0x1E904U, 0x1E926U}, {0x1E905U, 0x1E927U}, {0x1E906U, 0x1E928U}, {0x1E907U, 0x1E929U},
        {0x1E908U, 0x1E92AU}, {0x1E909U, 0x1E92BU}, {0x1E90AU, 0x1E92CU}, {0x1E90BU, 0x1E92DU},
        {0x1E90CU, 0x1E92EU}, {0x1E90DU, 0x1E92FU}, {0x1E90EU, 0x1E930U}, {0x1E90FU, 0x1E931U},
        {0x1E910U, 0x1E932U}, {0x1E911U, 0x1E933U}, {0x1E912U, 0x1E934U}, {0x1E913U, 0x1E935U},
        {0x1E914U, 0x1E936U}, {0x1E915U, 0x1E937U}, {0x1E916U, 0x1E938U}, {0x1E917U, 0x1E939U},
        {0x1E918U, 0x1E93AU}, {0x1E919U, 0x1E93BU}, {0x1E91AU, 0x1E93CU}, {0x1E91BU, 0x1E93DU},
        {0x1E91CU, 0x1E93EU}, {0x1E91DU, 0x1E93FU}, {0x1E91EU, 0x1E940U}, {0x1E91FU, 0x1E941U},
        {0x1E920U, 0x1E942U}, {0x1E921U, 0x1E943U}
    }};

    [[nodiscard]] std::shared_ptr<RenderInstruction> Instruction(
        InstructionCode code,
        std::initializer_list<std::uint32_t> arguments = {})
    {
        return std::make_shared<RenderInstruction>(
            code,
            std::make_shared<std::vector<std::uint32_t>>(arguments));
    }

    struct FanFaceOwner final
    {
        std::unique_ptr<ManagedArray<Vector3>> Points;
        std::unique_ptr<ManagedArray<Vector2>> Texcoords;
        std::unique_ptr<BuiltFace> Face;

        FanFaceOwner(
            Vector3 p0,
            Vector3 p1,
            Vector3 p2,
            Vector2 t0,
            Vector2 t1,
            Vector2 t2,
            Vector3 normal,
            std::int32_t material,
            float shade,
            std::vector<Vector3> colors = {})
            : Points(std::make_unique<ManagedArray<Vector3>>(3)),
              Texcoords(std::make_unique<ManagedArray<Vector2>>(3))
        {
            (*Points)[0] = p0;
            (*Points)[1] = p1;
            (*Points)[2] = p2;
            (*Texcoords)[0] = t0;
            (*Texcoords)[1] = t1;
            (*Texcoords)[2] = t2;
            Face = std::make_unique<BuiltFace>(
                std::move(Points),
                std::move(Texcoords),
                normal,
                material,
                shade);
            Face->Colors = std::move(colors);
        }
    };

    [[nodiscard]] std::vector<std::unique_ptr<FanFaceOwner>> Fan(BuiltFace* face)
    {
        ManagedArray<Vector3>* points = FacePoints(face);
        const std::size_t length = points->Length();
        std::vector<std::unique_ptr<FanFaceOwner>> result;
        if (length <= 2)
        {
            return result;
        }
        result.reserve(length - 2);
        for (std::size_t i = 1; i + 1 < length; ++i)
        {
            const Vector3 p0 = ManagedAt(points, 0);
            const Vector3 p1 = ManagedAt(points, i);
            const Vector3 p2 = ManagedAt(points, i + 1);
            ManagedArray<Vector2>* texcoords = FaceTexcoords(face);
            const Vector2 t0 = ManagedAt(texcoords, 0);
            const Vector2 t1 = ManagedAt(texcoords, i);
            const Vector2 t2 = ManagedAt(texcoords, i + 1);
            const Vector3 normal = face->Normal();
            const std::int32_t material = face->Material();
            const float shade = face->Shade();
            std::vector<Vector3> colors;
            if (face->Colors.size() == length)
            {
                colors = {face->Colors[0], face->Colors[i], face->Colors[i + 1]};
            }
            result.push_back(std::make_unique<FanFaceOwner>(
                p0, p1, p2, t0, t1, t2, normal, material, shade, std::move(colors)));
        }
        return result;
    }

    // A texture coordinate is a 16-bit count of sixteenths of a texel
    // (PackTexcoord), so a vertex sits no further than 2048 texels from its
    // texture's origin -- and a Quake level's floor tiles a 512-texel image
    // tens of times over. A face whose coordinates reach further is fanned
    // into triangles, each cut in two across its longest side in texture
    // space until it spans less than that, and each moved by whole repeats
    // of its texture to start at the origin, which draws the same.
    constexpr float TexcoordReach = 2040.0F;

    [[nodiscard]] bool TexcoordsFit(BuiltFace* face)
    {
        ManagedArray<Vector2>* texcoords = FaceTexcoords(face);
        for (std::size_t i = 0; i < texcoords->Length(); ++i)
        {
            const Vector2 t = (*texcoords)[i];
            if (std::fabs(t.X) > TexcoordReach || std::fabs(t.Y) > TexcoordReach)
            {
                return false;
            }
        }
        return true;
    }

    void FitTriangle(
        const std::array<Vector3, 3>& points,
        const std::array<Vector2, 3>& texcoords,
        const std::array<Vector3, 3>& colors,
        bool colored,
        BuiltFace* face,
        float width,
        float height,
        std::int32_t depth,
        std::vector<std::unique_ptr<FanFaceOwner>>& result)
    {
        Vector2 low = texcoords[0];
        Vector2 high = texcoords[0];
        for (const Vector2 t : texcoords)
        {
            low = Vector2(std::min(low.X, t.X), std::min(low.Y, t.Y));
            high = Vector2(std::max(high.X, t.X), std::max(high.Y, t.Y));
        }
        if ((high.X - low.X <= TexcoordReach && high.Y - low.Y <= TexcoordReach) || depth >= 16)
        {
            const Vector2 offset(
                std::floor(low.X / width) * width,
                std::floor(low.Y / height) * height);
            result.push_back(std::make_unique<FanFaceOwner>(
                points[0], points[1], points[2],
                Vector2(texcoords[0].X - offset.X, texcoords[0].Y - offset.Y),
                Vector2(texcoords[1].X - offset.X, texcoords[1].Y - offset.Y),
                Vector2(texcoords[2].X - offset.X, texcoords[2].Y - offset.Y),
                face->Normal(), face->Material(), face->Shade(),
                colored ? std::vector<Vector3>{colors[0], colors[1], colors[2]} : std::vector<Vector3>{}));
            return;
        }

        // The longest side, as the first two corners in the same winding.
        std::size_t first = 0;
        float longest = -1.0F;
        for (std::size_t i = 0; i < 3; ++i)
        {
            const Vector2 next = texcoords[(i + 1) % 3];
            const float reach = std::max(
                std::fabs(next.X - texcoords[i].X), std::fabs(next.Y - texcoords[i].Y));
            if (reach > longest)
            {
                longest = reach;
                first = i;
            }
        }
        const std::size_t a = first;
        const std::size_t b = (first + 1) % 3;
        const std::size_t c = (first + 2) % 3;
        const Vector3 middle(
            (points[a].X + points[b].X) * 0.5F,
            (points[a].Y + points[b].Y) * 0.5F,
            (points[a].Z + points[b].Z) * 0.5F);
        const Vector2 middleUv(
            (texcoords[a].X + texcoords[b].X) * 0.5F,
            (texcoords[a].Y + texcoords[b].Y) * 0.5F);
        const Vector3 middleColor(
            (colors[a].X + colors[b].X) * 0.5F,
            (colors[a].Y + colors[b].Y) * 0.5F,
            (colors[a].Z + colors[b].Z) * 0.5F);
        FitTriangle({points[a], middle, points[c]}, {texcoords[a], middleUv, texcoords[c]},
            {colors[a], middleColor, colors[c]}, colored, face, width, height, depth + 1, result);
        FitTriangle({middle, points[b], points[c]}, {middleUv, texcoords[b], texcoords[c]},
            {middleColor, colors[b], colors[c]}, colored, face, width, height, depth + 1, result);
    }

    void FitTexcoords(
        BuiltFace* face,
        float width,
        float height,
        std::vector<std::unique_ptr<FanFaceOwner>>& result)
    {
        ManagedArray<Vector3>* points = FacePoints(face);
        ManagedArray<Vector2>* texcoords = FaceTexcoords(face);
        for (std::size_t i = 1; i + 1 < points->Length(); ++i)
        {
            const bool colored = face->Colors.size() == points->Length();
            const Vector3 none(1.0F, 1.0F, 1.0F);
            FitTriangle(
                {(*points)[0], (*points)[i], (*points)[i + 1]},
                {(*texcoords)[0], (*texcoords)[i], (*texcoords)[i + 1]},
                {colored ? face->Colors[0] : none, colored ? face->Colors[i] : none, colored ? face->Colors[i + 1] : none},
                colored, face, width, height, 0, result);
        }
    }

    [[nodiscard]] std::uint32_t PackColor(Vector3 light) noexcept
    {
        const auto channel = [](float value) noexcept -> std::uint32_t
        {
            return static_cast<std::uint32_t>(std::clamp(RoundedInt32(31.0F * value), 0, 31));
        };
        return channel(light.X) | (channel(light.Y) << 5U) | (channel(light.Z) << 10U);
    }

    [[nodiscard]] std::uint32_t PackColor(float shade) noexcept
    {
        const std::int32_t rounded = RoundedInt32(31.0F * shade);
        const std::int32_t clamped = std::clamp(rounded, 0, 31);
        const std::uint32_t value = static_cast<std::uint32_t>(clamped);
        return value | (value << 5U) | (value << 10U);
    }

    [[nodiscard]] std::uint32_t PackNormal(Vector3 normal) noexcept
    {
        const auto component = [](float value) noexcept -> std::uint32_t
        {
            const std::int32_t rounded = RoundedInt32(value * 512.0F);
            const std::int32_t packed = std::clamp(rounded, -512, 511);
            return static_cast<std::uint32_t>(packed) & 0x3FFU;
        };
        return component(normal.X)
            | (component(normal.Y) << 10U)
            | (component(normal.Z) << 20U);
    }

    [[nodiscard]] std::uint32_t PackTexcoord(float u, float v) noexcept
    {
        const auto component = [](float value) noexcept -> std::uint32_t
        {
            const std::int32_t rounded = RoundedInt32(value * 16.0F);
            const std::int32_t packed = std::clamp(
                rounded,
                static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
                static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max()));
            return static_cast<std::uint32_t>(packed) & 0xFFFFU;
        };
        return component(u) | (component(v) << 16U);
    }

    [[nodiscard]] std::shared_ptr<RenderInstruction> PackVertex(
        Vector3 point,
        float scale)
    {
        const auto component = [scale](float value) -> std::uint32_t
        {
            const std::int32_t packed = Fixed::ToInt(value / scale);
            if (packed < std::numeric_limits<std::int16_t>::min()
                || packed > std::numeric_limits<std::int16_t>::max())
            {
                throw ProgramException(
                    "Vertex " + ::MphRead::NativeRuntime::ToString(value)
                    + " does not fit at scale " + ::MphRead::NativeRuntime::ToString(scale)
                    + "; raise the map's scaleFactor.");
            }
            return static_cast<std::uint32_t>(packed) & 0xFFFFU;
        };
        const std::uint32_t x = component(point.X);
        const std::uint32_t y = component(point.Y);
        const std::uint32_t z = component(point.Z);
        return Instruction(InstructionCode::VTX_16, {x | (y << 16U), z});
    }

    [[nodiscard]] std::int32_t EmitPrimitives(
        std::vector<std::shared_ptr<RenderInstruction>>& instructions,
        const std::vector<BuiltFace*>& faces,
        std::uint32_t primitiveType,
        float scale)
    {
        if (faces.empty())
        {
            return 0;
        }
        std::int32_t vertexCount = 0;
        instructions.push_back(
            Instruction(InstructionCode::BEGIN_VTXS, {primitiveType}));
        for (BuiltFace* face : faces)
        {
            if (face == nullptr)
            {
                throw System::NullReferenceException();
            }
            instructions.push_back(
                Instruction(InstructionCode::COLOR, {PackColor(face->Shade())}));
            instructions.push_back(
                Instruction(InstructionCode::NORMAL, {PackNormal(face->Normal())}));
            ManagedArray<Vector3>* points = FacePoints(face);
            const bool colored = face->Colors.size() == points->Length();
            for (std::size_t i = 0; i < points->Length(); ++i)
            {
                // Light at each corner, where the face has it.
                if (colored)
                {
                    instructions.push_back(
                        Instruction(InstructionCode::COLOR, {PackColor(face->Colors[i])}));
                }
                ManagedArray<Vector2>* texcoords = FaceTexcoords(face);
                const Vector2 texcoord = ManagedAt(texcoords, i);
                instructions.push_back(Instruction(
                    InstructionCode::TEXCOORD,
                    {PackTexcoord(texcoord.X, texcoord.Y)}));
                instructions.push_back(PackVertex(ManagedAt(points, i), scale));
                vertexCount = UncheckedAdd(vertexCount, 1);
            }
        }
        instructions.push_back(Instruction(InstructionCode::END_VTXS));
        return vertexCount;
    }

    [[nodiscard]] std::pair<std::vector<std::uint8_t>, std::int32_t> Assemble(
        BuiltMap* map,
        MapDefinition* def,
        const std::shared_ptr<std::vector<std::shared_ptr<Material>>>& materials,
        const std::shared_ptr<std::vector<std::shared_ptr<Repack::TextureInfo>>>& textures,
        const std::shared_ptr<std::vector<std::shared_ptr<Repack::PaletteInfo>>>& palettes,
        const std::vector<std::int32_t>* drawOrder = nullptr)
    {
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }
        const float scale = std::pow(2.0F, static_cast<float>(def->ScaleFactor()));
        auto renders = std::make_shared<std::vector<
            std::shared_ptr<const std::vector<std::shared_ptr<RenderInstruction>>>>>();
        auto meshes = std::make_shared<std::vector<std::shared_ptr<Mesh>>>();
        std::int32_t vertexCount = 0;
        const std::int32_t materialCount = ListCount(materials->size());
        std::vector<std::int32_t> order;
        if (drawOrder != nullptr)
        {
            order = *drawOrder;
        }
        else
        {
            for (std::int32_t materialId = 0; materialId < materialCount; ++materialId)
            {
                order.push_back(materialId);
            }
        }
        // The level's own faces, then each mover's (BuiltFace::Mover), each
        // in a node of its own that the room moves (Q3Movers).
        std::vector<std::int32_t> movers{-1};
        for (BuiltFace* face : map->Faces())
        {
            if (face != nullptr && face->Mover >= 0
                && std::find(movers.begin(), movers.end(), face->Mover) == movers.end())
            {
                movers.push_back(face->Mover);
            }
        }
        std::sort(movers.begin() + 1, movers.end());
        std::vector<std::pair<std::int32_t, std::int32_t>> nodeMeshes;
        for (const std::int32_t mover : movers)
        {
        const std::int32_t firstMesh = ListCount(meshes->size());
        for (const std::int32_t materialId : order)
        {
            std::vector<BuiltFace*> group;
            for (BuiltFace* face : map->Faces())
            {
                if (face == nullptr)
                {
                    throw System::NullReferenceException();
                }
                if (face->Material() == materialId && face->Mover == mover)
                {
                    group.push_back(face);
                }
            }
            if (group.empty())
            {
                continue;
            }

            auto instructions = std::make_shared<std::vector<std::shared_ptr<RenderInstruction>>>();

            // The material's texture size, for FitTexcoords; a material with
            // none of its own here is drawn as it comes.
            float width = 0.0F;
            float height = 0.0F;
            if (materials != nullptr && textures != nullptr
                && static_cast<std::size_t>(materialId) < materials->size())
            {
                const std::int32_t textureId = RequireReference((*materials)[static_cast<std::size_t>(materialId)]).TextureId;
                if (textureId >= 0 && static_cast<std::size_t>(textureId) < textures->size())
                {
                    const Repack::TextureInfo& texture = RequireReference((*textures)[static_cast<std::size_t>(textureId)]);
                    width = static_cast<float>(texture.Width);
                    height = static_cast<float>(texture.Height);
                }
            }

            std::vector<std::unique_ptr<FanFaceOwner>> fanOwners;
            std::vector<BuiltFace*> triangles;
            std::vector<BuiltFace*> quads;
            for (BuiltFace* face : group)
            {
                const std::size_t length = FacePoints(face)->Length();
                if (width > 0.0F && height > 0.0F && !TexcoordsFit(face))
                {
                    std::vector<std::unique_ptr<FanFaceOwner>> current;
                    FitTexcoords(face, width, height, current);
                    for (auto& owner : current)
                    {
                        triangles.push_back(owner->Face.get());
                        fanOwners.push_back(std::move(owner));
                    }
                }
                else if (length == 3)
                {
                    triangles.push_back(face);
                }
                else if (length == 4)
                {
                    quads.push_back(face);
                }
                else if (length > 4)
                {
                    std::vector<std::unique_ptr<FanFaceOwner>> current = Fan(face);
                    for (auto& owner : current)
                    {
                        triangles.push_back(owner->Face.get());
                        fanOwners.push_back(std::move(owner));
                    }
                }
            }
            vertexCount = UncheckedAdd(
                vertexCount,
                EmitPrimitives(*instructions, triangles, 0U, scale));
            vertexCount = UncheckedAdd(
                vertexCount,
                EmitPrimitives(*instructions, quads, 1U, scale));

            while (instructions->size() % 4U != 0U)
            {
                instructions->push_back(Instruction(InstructionCode::NOP));
            }
            meshes->push_back(MphRead::Mods::MapGen::RawStructs::MakeMesh(
                materialId,
                ListCount(renders->size())));
            renders->push_back(instructions);
        }
        nodeMeshes.emplace_back(firstMesh, ListCount(meshes->size()) - firstMesh);
        }

        auto nodes = std::make_shared<std::vector<std::shared_ptr<Node>>>();
        const std::u16string rootName = u"rmMain";
        nodes->push_back(MphRead::Mods::MapGen::RawStructs::MakeNode(
            std::u16string_view(rootName),
            0,
            0,
            -1,
            1));
        for (std::size_t n = 0; n < movers.size(); ++n)
        {
            const std::u16string name = n == 0 ? std::u16string(u"geo1")
                : ::MphRead::NativeRuntime::Utf8ToUtf16("q3model" + std::to_string(movers[n]));
            nodes->push_back(MphRead::Mods::MapGen::RawStructs::MakeNode(
                std::u16string_view(name),
                nodeMeshes[n].second,
                nodeMeshes[n].first,
                0,
                -1,
                n + 1 < movers.size() ? static_cast<std::int32_t>(n + 2) : -1));
        }

        auto dlists = std::make_shared<std::vector<DisplayList>>(renders->size());
        auto options = std::make_shared<Repack::RepackOptions>();
        options->IsRoom = true;
        options->Texture = Repack::RepackTexture::Inline;
        options->ComputeBounds = Repack::ComputeBounds::Capped;

        const auto emptyIds = std::make_shared<const std::vector<std::int32_t>>();
        const auto packed = Repack::PackModel(
            ConvertToInt32Net(scale),
            emptyIds,
            emptyIds,
            materials,
            textures,
            palettes,
            nodes,
            meshes,
            renders,
            dlists,
            options);
        return {packed.first, vertexCount};
    }

    // A room's animation (Read::LoadAnimation): one group of each kind,
    // ShaderAnim::LoopFrames long, from the texture pack's moving shaders --
    // their tcMods as texcoord animations, their waves as material alpha,
    // their animMaps as texture animations -- each by its material's name.
    [[nodiscard]] std::vector<std::uint8_t> BuildAnimation(
        const MapTexturePack& pack,
        const std::vector<std::string>& names)
    {
        namespace SA = ::MphRead::Mods::MapGen::ShaderAnim;
        using MphRead::Utility::BinaryWriter;
        constexpr std::int32_t Frames = SA::LoopFrames;
        // A track, one value every Blend frames and its last frames each,
        // as Model::InterpolateAnimation reads them.
        constexpr std::int32_t Blend = 4;
        constexpr std::int32_t Limit = ((Frames - 1) / Blend) * Blend;
        const auto sample = [&](const std::function<float(float)>& at)
        {
            std::vector<float> values;
            for (std::int32_t frame = 0; frame <= Limit - Blend; frame += Blend)
            {
                values.push_back(at(static_cast<float>(frame) / SA::FramesPerSecond));
            }
            for (std::int32_t frame = Limit; frame < Frames; ++frame)
            {
                values.push_back(at(static_cast<float>(frame) / SA::FramesPerSecond));
            }
            return values;
        };
        const auto constant = [](const std::vector<float>& values)
        {
            for (const float value : values)
            {
                if (std::abs(value - values.front()) > 0.00001F)
                {
                    return false;
                }
            }
            return true;
        };
        const auto fixed = [](float value)
        {
            return static_cast<std::int32_t>(std::lround(static_cast<double>(value) * 4096.0));
        };
        const auto angle = [](float radians)
        {
            const double turns = static_cast<double>(radians) / (2.0 * 3.14159265358979323846);
            return static_cast<std::uint16_t>(static_cast<std::int64_t>(std::llround(turns * 65536.0)) & 0xFFFF);
        };
        const auto writeName = [](BinaryWriter& writer, const std::string& name, std::size_t length)
        {
            for (std::size_t i = 0; i < length; ++i)
            {
                writer.Write(static_cast<std::uint8_t>(i < name.size() ? name[i] : 0));
            }
        };

        // Track lookup tables; index 0 of each is the value that does not move.
        std::vector<std::int32_t> scales{4096};
        std::vector<std::uint16_t> rotations{0};
        std::vector<std::int32_t> translations{0};
        std::vector<std::uint8_t> colors{31, 0};
        struct Track
        {
            std::uint16_t Length = 1;
            std::uint16_t Index = 0;
            std::uint8_t Blend = 0;
        };
        const auto addFixed = [&](std::vector<std::int32_t>& lut, const std::vector<float>& values, std::int32_t still)
        {
            Track track;
            if (constant(values))
            {
                const std::int32_t value = fixed(values.front());
                if (value == still)
                {
                    return track;
                }
                track.Index = static_cast<std::uint16_t>(lut.size());
                lut.push_back(value);
                return track;
            }
            track.Index = static_cast<std::uint16_t>(lut.size());
            track.Length = static_cast<std::uint16_t>(values.size());
            track.Blend = Blend;
            for (const float value : values)
            {
                lut.push_back(fixed(value));
            }
            return track;
        };

        struct UvAnimation
        {
            std::string Name;
            Track ScaleS, ScaleT, Rotate, TranslateS, TranslateT;
        };
        struct AlphaAnimation
        {
            std::string Name;
            std::int32_t Material = 0;
            Track Alpha;
        };
        struct FrameAnimation
        {
            std::string Name;
            std::int32_t Material = 0;
            std::int32_t Period = 0;
            std::vector<std::pair<std::int32_t, std::int32_t>> Keys;
        };
        std::vector<UvAnimation> uvs;
        std::vector<AlphaAnimation> alphas;
        std::vector<FrameAnimation> frameAnimations;

        std::unordered_map<std::int32_t, std::int32_t> entryBySource;
        for (std::size_t e = 0; e < pack.Entries().size(); ++e)
        {
            entryBySource.emplace(pack.Entries()[e].SourceIndex(), static_cast<std::int32_t>(e));
        }
        for (std::size_t e = 0; e < pack.Entries().size(); ++e)
        {
            const MapTexturePack::Entry& entry = pack.Entries()[e];
            if (entry.Meta().empty())
            {
                continue;
            }
            const SA::EntryMeta meta = SA::Decode(entry.Meta());
            if (meta.Role == "frame")
            {
                continue;
            }
            const std::string& name = names.at(e);
            if (meta.MovesTexcoords()
                && (rotations.size() < 60000 && scales.size() < 60000 && translations.size() < 60000))
            {
                std::vector<float> scaleS, scaleT, rotate, translateS, translateT;
                for (const float seconds : sample([](float t) { return t; }))
                {
                    const SA::TexcoordFrame frame = SA::Texcoords(meta.TcMods, seconds);
                    scaleS.push_back(frame.ScaleS);
                    scaleT.push_back(frame.ScaleT);
                    rotate.push_back(frame.Rotate);
                    translateS.push_back(frame.TranslateS);
                    translateT.push_back(frame.TranslateT);
                }
                UvAnimation uv;
                uv.Name = name;
                uv.ScaleS = addFixed(scales, scaleS, 4096);
                uv.ScaleT = addFixed(scales, scaleT, 4096);
                uv.TranslateS = addFixed(translations, translateS, 0);
                uv.TranslateT = addFixed(translations, translateT, 0);
                if (!constant(rotate) || angle(rotate.front()) != 0)
                {
                    uv.Rotate.Index = static_cast<std::uint16_t>(rotations.size());
                    if (constant(rotate))
                    {
                        rotations.push_back(angle(rotate.front()));
                    }
                    else
                    {
                        uv.Rotate.Length = static_cast<std::uint16_t>(rotate.size());
                        uv.Rotate.Blend = Blend;
                        for (const float value : rotate)
                        {
                            rotations.push_back(angle(value));
                        }
                    }
                }
                uvs.push_back(uv);
            }
            if (meta.Fade.has_value() && colors.size() < 60000)
            {
                AlphaAnimation alpha;
                alpha.Name = name;
                alpha.Material = static_cast<std::int32_t>(e);
                alpha.Alpha.Index = static_cast<std::uint16_t>(colors.size());
                const std::vector<float> values = sample([&](float t)
                    { return std::clamp(SA::WaveValue(*meta.Fade, t), 0.0F, 1.0F); });
                alpha.Alpha.Length = static_cast<std::uint16_t>(values.size());
                alpha.Alpha.Blend = Blend;
                for (const float value : values)
                {
                    colors.push_back(static_cast<std::uint8_t>(std::lround(value * 31.0F)));
                }
                alphas.push_back(alpha);
            }
            if (meta.FrameCount > 1 && meta.FrameRate > 0.0F)
            {
                FrameAnimation animation;
                animation.Name = name;
                animation.Material = static_cast<std::int32_t>(e);
                animation.Period = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(
                    static_cast<float>(meta.FrameCount) * SA::FramesPerSecond / meta.FrameRate)));
                for (std::size_t f = 0; f < pack.Entries().size(); ++f)
                {
                    const MapTexturePack::Entry& frameEntry = pack.Entries()[f];
                    if (frameEntry.Meta().empty())
                    {
                        continue;
                    }
                    const SA::EntryMeta frameMeta = SA::Decode(frameEntry.Meta());
                    if (frameMeta.Role == "frame" && frameMeta.Of == entry.SourceIndex())
                    {
                        const std::int32_t at = static_cast<std::int32_t>(std::lround(
                            static_cast<float>(frameMeta.Frame) * SA::FramesPerSecond / meta.FrameRate));
                        animation.Keys.emplace_back(std::min(at, animation.Period - 1), static_cast<std::int32_t>(f));
                    }
                }
                std::sort(animation.Keys.begin(), animation.Keys.end());
                if (animation.Keys.size() > 1)
                {
                    frameAnimations.push_back(std::move(animation));
                }
            }
        }
        if (uvs.empty() && alphas.empty() && frameAnimations.empty())
        {
            return {};
        }

        BinaryWriter writer;
        writer.Position(24);
        const auto align = [&writer]()
        {
            while (writer.Position() % 4U != 0U)
            {
                writer.Write(static_cast<std::uint8_t>(0));
            }
        };

        std::uint32_t materialGroup = 0;
        if (!alphas.empty())
        {
            const std::uint32_t colorOffset = static_cast<std::uint32_t>(writer.Position());
            for (const std::uint8_t value : colors)
            {
                writer.Write(value);
            }
            align();
            const std::uint32_t animationOffset = static_cast<std::uint32_t>(writer.Position());
            for (const AlphaAnimation& alpha : alphas)
            {
                writeName(writer, alpha.Name, 64);
                writer.Write(static_cast<std::uint32_t>(0));
                // Diffuse at full (index 0), ambient and specular none
                // (index 1): the colour the material already has.
                for (const std::uint16_t index : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{1}})
                {
                    writer.Write(static_cast<std::uint8_t>(0));
                    writer.Write(static_cast<std::uint8_t>(0));
                    writer.Write(static_cast<std::uint8_t>(0));
                    writer.Write(static_cast<std::uint8_t>(0));
                    for (std::size_t c = 0; c < 3; ++c) writer.Write(static_cast<std::uint16_t>(1));
                    for (std::size_t c = 0; c < 3; ++c) writer.Write(index);
                }
                for (std::size_t u = 0; u < 4; ++u) writer.Write(static_cast<std::uint32_t>(0));
                writer.Write(alpha.Alpha.Blend);
                writer.Write(static_cast<std::uint8_t>(0));
                writer.Write(alpha.Alpha.Length);
                writer.Write(alpha.Alpha.Index);
                writer.Write(static_cast<std::uint16_t>(alpha.Material));
            }
            materialGroup = static_cast<std::uint32_t>(writer.Position());
            writer.Write(static_cast<std::uint32_t>(Frames));
            writer.Write(colorOffset);
            writer.Write(static_cast<std::uint32_t>(alphas.size()));
            writer.Write(animationOffset);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint16_t>(0));
        }

        std::uint32_t texcoordGroup = 0;
        if (!uvs.empty())
        {
            const std::uint32_t scaleOffset = static_cast<std::uint32_t>(writer.Position());
            for (const std::int32_t value : scales) writer.Write(value);
            const std::uint32_t rotateOffset = static_cast<std::uint32_t>(writer.Position());
            for (const std::uint16_t value : rotations) writer.Write(value);
            align();
            const std::uint32_t translateOffset = static_cast<std::uint32_t>(writer.Position());
            for (const std::int32_t value : translations) writer.Write(value);
            const std::uint32_t animationOffset = static_cast<std::uint32_t>(writer.Position());
            for (const UvAnimation& uv : uvs)
            {
                writeName(writer, uv.Name, 32);
                writer.Write(uv.ScaleS.Blend);
                writer.Write(uv.ScaleT.Blend);
                writer.Write(uv.ScaleS.Length);
                writer.Write(uv.ScaleT.Length);
                writer.Write(uv.ScaleS.Index);
                writer.Write(uv.ScaleT.Index);
                writer.Write(uv.Rotate.Blend);
                writer.Write(static_cast<std::uint8_t>(0));
                writer.Write(uv.Rotate.Length);
                writer.Write(uv.Rotate.Index);
                writer.Write(uv.TranslateS.Blend);
                writer.Write(uv.TranslateT.Blend);
                writer.Write(uv.TranslateS.Length);
                writer.Write(uv.TranslateT.Length);
                writer.Write(uv.TranslateS.Index);
                writer.Write(uv.TranslateT.Index);
                writer.Write(static_cast<std::uint16_t>(0));
            }
            texcoordGroup = static_cast<std::uint32_t>(writer.Position());
            writer.Write(static_cast<std::uint32_t>(Frames));
            writer.Write(scaleOffset);
            writer.Write(rotateOffset);
            writer.Write(translateOffset);
            writer.Write(static_cast<std::uint32_t>(uvs.size()));
            writer.Write(animationOffset);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint16_t>(0));
        }

        std::uint32_t textureGroup = 0;
        if (!frameAnimations.empty())
        {
            std::vector<std::uint16_t> frameIndices;
            std::vector<std::uint16_t> ids;
            for (const FrameAnimation& animation : frameAnimations)
            {
                for (const auto& [frame, entry] : animation.Keys)
                {
                    frameIndices.push_back(static_cast<std::uint16_t>(frame));
                    ids.push_back(static_cast<std::uint16_t>(entry));
                }
            }
            const std::uint32_t frameOffset = static_cast<std::uint32_t>(writer.Position());
            for (const std::uint16_t value : frameIndices) writer.Write(value);
            align();
            const std::uint32_t textureOffset = static_cast<std::uint32_t>(writer.Position());
            for (const std::uint16_t value : ids) writer.Write(value);
            align();
            // Each entry is its own texture and palette (BuildModel).
            const std::uint32_t paletteOffset = static_cast<std::uint32_t>(writer.Position());
            for (const std::uint16_t value : ids) writer.Write(value);
            align();
            const std::uint32_t animationOffset = static_cast<std::uint32_t>(writer.Position());
            std::uint16_t start = 0;
            for (const FrameAnimation& animation : frameAnimations)
            {
                writeName(writer, animation.Name, 32);
                writer.Write(static_cast<std::uint16_t>(animation.Keys.size()));
                writer.Write(start);
                writer.Write(static_cast<std::uint16_t>(0));
                writer.Write(static_cast<std::uint16_t>(animation.Material));
                writer.Write(static_cast<std::uint16_t>(0));
                // Field2A: the frames it repeats over (Model::AnimateTextures).
                writer.Write(static_cast<std::uint16_t>(animation.Period));
                start = static_cast<std::uint16_t>(start + animation.Keys.size());
            }
            textureGroup = static_cast<std::uint32_t>(writer.Position());
            writer.Write(static_cast<std::uint16_t>(Frames));
            writer.Write(static_cast<std::uint16_t>(frameIndices.size()));
            writer.Write(static_cast<std::uint16_t>(ids.size()));
            writer.Write(static_cast<std::uint16_t>(ids.size()));
            writer.Write(static_cast<std::uint16_t>(frameAnimations.size()));
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(frameOffset);
            writer.Write(textureOffset);
            writer.Write(paletteOffset);
            writer.Write(animationOffset);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint16_t>(0));
        }

        // One group of each kind: the lists of their offsets, node and the
        // unused kind empty.
        const std::uint32_t nodeList = static_cast<std::uint32_t>(writer.Position());
        writer.Write(static_cast<std::uint32_t>(0));
        const std::uint32_t unusedList = static_cast<std::uint32_t>(writer.Position());
        writer.Write(static_cast<std::uint32_t>(0));
        const std::uint32_t materialList = static_cast<std::uint32_t>(writer.Position());
        writer.Write(materialGroup);
        const std::uint32_t texcoordList = static_cast<std::uint32_t>(writer.Position());
        writer.Write(texcoordGroup);
        const std::uint32_t textureList = static_cast<std::uint32_t>(writer.Position());
        writer.Write(textureGroup);
        writer.Position(0);
        writer.Write(nodeList);
        writer.Write(unusedList);
        writer.Write(materialList);
        writer.Write(texcoordList);
        writer.Write(textureList);
        writer.Write(static_cast<std::uint16_t>(1));
        writer.Write(static_cast<std::uint16_t>(0));
        return writer.ToArray();
    }

    [[nodiscard]] std::pair<std::vector<std::uint8_t>, std::int32_t> BuildModel(
        BuiltMap* map,
        MapTexturePack* pack,
        std::vector<std::uint8_t>* animation = nullptr)
    {
        if (map == nullptr)
        {
            throw System::NullReferenceException();
        }
        MapDefinition* def = map->Definition();

        auto textures = std::make_shared<std::vector<std::shared_ptr<Repack::TextureInfo>>>();
        auto palettes = std::make_shared<std::vector<std::shared_ptr<Repack::PaletteInfo>>>();
        auto materials = std::make_shared<std::vector<std::shared_ptr<Material>>>();
        if (pack == nullptr)
        {
            throw System::NullReferenceException();
        }
        // Draw order: the faces, then the layers the level's light falls on,
        // the light layer, and the glows it does not (ShaderAnim).
        std::vector<std::int32_t> faces;
        std::vector<std::int32_t> litLayers;
        std::vector<std::int32_t> glows;
        std::vector<std::string> names;
        for (const MapTexturePack::Entry& entry : pack->Entries())
        {
            const std::int32_t index = ListCount(materials->size());
            auto pixels = std::shared_ptr<const std::vector<std::uint8_t>>(
                std::addressof(entry.Pixels()),
                [](const std::vector<std::uint8_t>*) {});
            auto palette = std::shared_ptr<const std::vector<std::uint16_t>>(
                std::addressof(entry.Palette()),
                [](const std::vector<std::uint16_t>*) {});
            using PixelFormat = MapTexturePack::Entry::PixelFormat;
            const TextureFormat format = entry.Format() == PixelFormat::A5I3 ? TextureFormat::PaletteA5I3
                : (entry.Format() == PixelFormat::A3I5 ? TextureFormat::PaletteA3I5 : TextureFormat::Palette8Bit);
            textures->push_back(std::make_shared<Repack::TextureInfo>(
                format,
                format == TextureFormat::Palette8Bit,
                entry.Height(),
                entry.Width(),
                std::move(pixels)));
            palettes->push_back(
                std::make_shared<Repack::PaletteInfo>(std::move(palette)));

            ::MphRead::Mods::MapGen::ShaderAnim::EntryMeta meta;
            if (!entry.Meta().empty())
            {
                meta = ::MphRead::Mods::MapGen::ShaderAnim::Decode(entry.Meta());
            }
            std::string utf8 = entry.Name();
            // A moving material is found by its name (Model::AnimateTexcoords
            // and the rest), so its name is its own.
            if (!entry.Meta().empty())
            {
                // "+" marks a moving stage Quake adds (Material::Additive):
                // a glow that never changes keeps the see-through blend,
                // which over its own lit fitting reads truer than adding.
                const std::string prefix = "q" + std::to_string(index)
                    + (meta.Blend == "add" && meta.Animated() ? "+" : "_");
                utf8 = prefix + (utf8.size() > 30U - prefix.size() ? utf8.substr(utf8.size() - (30U - prefix.size())) : utf8);
            }
            std::u16string name = Utf8ToUtf16(utf8);
            if (name.size() > 30U)
            {
                name = name.substr(name.size() - 30U);
            }
            names.push_back(::MphRead::NativeRuntime::Utf16ToUtf8(name));
            const RepeatMode repeat = meta.Clamp ? RepeatMode::Clamp : RepeatMode::Repeat;
            const MphRead::RenderMode mode = meta.Role == "layer" ? MphRead::RenderMode::Decal
                : (meta.Blend == "add" ? MphRead::RenderMode::Translucent : MphRead::RenderMode::Normal);
            materials->push_back(MphRead::Mods::MapGen::RawStructs::MakeMaterial(
                std::u16string_view(name),
                ListCount(textures->size()) - 1,
                ListCount(palettes->size()) - 1,
                repeat,
                repeat,
                false,
                ColorRgb(31, 31, 31),
                ColorRgb(0, 0, 0),
                mode,
                meta.TwoSided ? MphRead::CullingMode::Neither : MphRead::CullingMode::Back));
            (meta.Role != "layer" ? faces : (meta.Lit ? litLayers : glows)).push_back(index);
        }
        if (materials->empty())
        {
            throw ProgramException("The texture pack is empty.");
        }
        // The light layer (Q3Import::LightLayer): black, as see-through as the
        // light is bright, drawn as a decal. Mirrored at both edges, which no
        // cartridge decal is, is what tells the renderer to filter it
        // smoothly whatever the texture filtering setting.
        for (std::size_t page = 0; page < map->LightPages.size(); ++page)
        {
            std::vector<std::uint8_t> texels(map->LightPages[page].size());
            for (std::size_t i = 0; i < texels.size(); ++i)
            {
                texels[i] = static_cast<std::uint8_t>((map->LightPages[page][i] & 0x1F) << 3);
            }
            textures->push_back(std::make_shared<Repack::TextureInfo>(
                TextureFormat::PaletteA5I3,
                false,
                128,
                128,
                std::make_shared<const std::vector<std::uint8_t>>(std::move(texels))));
            palettes->push_back(std::make_shared<Repack::PaletteInfo>(
                std::make_shared<const std::vector<std::uint16_t>>(std::vector<std::uint16_t>(8, 0))));
            const std::u16string name = Utf8ToUtf16("lightmap" + std::to_string(page));
            materials->push_back(MphRead::Mods::MapGen::RawStructs::MakeMaterial(
                std::u16string_view(name),
                ListCount(textures->size()) - 1,
                ListCount(palettes->size()) - 1,
                RepeatMode::Mirror,
                RepeatMode::Mirror,
                false,
                ColorRgb(31, 31, 31),
                ColorRgb(0, 0, 0),
                MphRead::RenderMode::Decal));
            litLayers.push_back(ListCount(materials->size()) - 1);
        }
        std::vector<std::int32_t> drawOrder = faces;
        drawOrder.insert(drawOrder.end(), litLayers.begin(), litLayers.end());
        drawOrder.insert(drawOrder.end(), glows.begin(), glows.end());
        if (animation != nullptr)
        {
            *animation = BuildAnimation(*pack, names);
        }
        return Assemble(map, def, materials, textures, palettes, &drawOrder);
    }

    [[nodiscard]] std::pair<std::vector<std::uint8_t>, std::int32_t> BuildModel(
        BuiltMap* map,
        std::vector<std::uint8_t>* animation = nullptr)
    {
        if (map == nullptr)
        {
            throw System::NullReferenceException();
        }
        MapDefinition* def = map->Definition();
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }
        MapImport* import = def->Import();
        std::shared_ptr<MapTexturePack> own = import == nullptr
            ? std::shared_ptr<MapTexturePack>{}
            : import->LoadTexturePack();
        if (own)
        {
            return BuildModel(map, own.get(), animation);
        }

        const std::shared_ptr<MphRead::ModelInstance> instance
            = MphRead::Read::GetRoomModelInstance(def->TextureSource());
        MphRead::ModelInstance* instanceValue = &RequireReference(instance);
        const std::shared_ptr<MphRead::Model> source = instanceValue->Model();
        MphRead::Model* sourceValue = &RequireReference(source);
        const auto* recolors = &RequireReference(sourceValue->Recolors);
        const std::shared_ptr<MphRead::Recolor>& recolorValue
            = ManagedAt(*recolors, 0);
        MphRead::Recolor* recolor = recolorValue.get();

        auto textures = std::make_shared<std::vector<std::shared_ptr<Repack::TextureInfo>>>();
        auto palettes = std::make_shared<std::vector<std::shared_ptr<Repack::PaletteInfo>>>();
        std::unordered_map<std::int32_t, std::int32_t> textureMap;
        std::unordered_map<std::int32_t, std::int32_t> paletteMap;
        auto materials = std::make_shared<std::vector<std::shared_ptr<Material>>>();

        const MapDefinition::MaterialList* sourceMaterials = def->Materials();
        if (sourceMaterials == nullptr)
        {
            throw System::NullReferenceException();
        }
        for (const std::shared_ptr<MapMaterial>& mapMaterialValue : *sourceMaterials)
        {
            MapMaterial* mapMaterial = &RequireReference(mapMaterialValue);
            if (mapMaterial->SourceMaterial() < 0)
            {
                const std::string& textureSource = def->TextureSource();
                const std::int32_t invalidMaterial = mapMaterial->SourceMaterial();
                throw ProgramException(
                    textureSource + " has no material "
                    + ::MphRead::NativeRuntime::ToString(invalidMaterial) + ".");
            }

            const std::int32_t materialForUpperBound = mapMaterial->SourceMaterial();
            const auto* modelMaterialsForCount = &RequireReference(sourceValue->Materials);
            if (materialForUpperBound >= ListCount(modelMaterialsForCount->size()))
            {
                const std::string& textureSource = def->TextureSource();
                const std::int32_t invalidMaterial = mapMaterial->SourceMaterial();
                throw ProgramException(
                    textureSource + " has no material "
                    + ::MphRead::NativeRuntime::ToString(invalidMaterial) + ".");
            }

            const auto* modelMaterialsForIndex = &RequireReference(sourceValue->Materials);
            const std::int32_t materialForIndex = mapMaterial->SourceMaterial();
            const std::shared_ptr<Material>& srcMaterialValue
                = ManagedAt(*modelMaterialsForIndex, materialForIndex);
            Material* srcMaterial = &RequireReference(srcMaterialValue);
            if (srcMaterial->TextureId < 0 || srcMaterial->PaletteId < 0)
            {
                const std::int32_t invalidMaterial = mapMaterial->SourceMaterial();
                const std::string& textureSource = def->TextureSource();
                throw ProgramException(
                    "Material " + ::MphRead::NativeRuntime::ToString(invalidMaterial)
                    + " of " + textureSource + " has no texture.");
            }

            std::int32_t textureId = 0;
            const auto textureFound = textureMap.find(srcMaterial->TextureId);
            if (textureFound == textureMap.end())
            {
                textureId = ListCount(textures->size());
                if (recolor == nullptr)
                {
                    throw System::NullReferenceException();
                }
                const auto* recolorTextures = &RequireReference(recolor->Textures);
                const MphRead::Texture& texture
                    = ManagedAt(*recolorTextures, srcMaterial->TextureId);
                const auto* textureDataLists = &RequireReference(recolor->TextureData);
                const std::shared_ptr<const std::vector<MphRead::TextureData>>& textureData
                    = ManagedAt(*textureDataLists, srcMaterial->TextureId);
                textures->push_back(Repack::ConvertData(texture, textureData));
                textureMap.emplace(srcMaterial->TextureId, textureId);
            }
            else
            {
                textureId = textureFound->second;
            }

            std::int32_t paletteId = 0;
            const auto paletteFound = paletteMap.find(srcMaterial->PaletteId);
            if (paletteFound == paletteMap.end())
            {
                paletteId = ListCount(palettes->size());
                if (recolor == nullptr)
                {
                    throw System::NullReferenceException();
                }
                const auto* paletteDataLists = &RequireReference(recolor->PaletteData);
                const std::shared_ptr<const std::vector<MphRead::PaletteData>>& sourcePalette
                    = ManagedAt(*paletteDataLists, srcMaterial->PaletteId);
                if (!sourcePalette)
                {
                    throw System::ArgumentNullException("source");
                }
                const auto* paletteValues = sourcePalette.get();
                auto data = std::make_shared<std::vector<std::uint16_t>>();
                data->reserve(paletteValues->size());
                for (const MphRead::PaletteData& value : *paletteValues)
                {
                    data->push_back(value.Data);
                }
                palettes->push_back(std::make_shared<Repack::PaletteInfo>(data));
                paletteMap.emplace(srcMaterial->PaletteId, paletteId);
            }
            else
            {
                paletteId = paletteFound->second;
            }

            const std::u16string name = Utf8ToUtf16(mapMaterial->Name());
            materials->push_back(MphRead::Mods::MapGen::RawStructs::MakeMaterial(
                std::u16string_view(name),
                textureId,
                paletteId,
                RepeatMode::Repeat,
                RepeatMode::Repeat,
                false,
                ColorRgb(31, 31, 31),
                ColorRgb(0, 0, 0)));
        }
        if (materials->empty())
        {
            throw ProgramException("A map needs at least one material.");
        }
        return Assemble(map, def, materials, textures, palettes);
    }

    [[nodiscard]] std::vector<std::uint8_t> BuildCollision(const std::vector<BuiltFace*>& solid)
    {
        auto editors = std::make_shared<
            std::vector<std::shared_ptr<CollisionDataEditor>>>();
        for (BuiltFace* face : solid)
        {
            if (face == nullptr)
            {
                throw System::NullReferenceException();
            }
            ManagedArray<Vector3>* facePoints = FacePoints(face);
            if (facePoints->Length() <= 10U)
            {
                auto editor = std::make_shared<CollisionDataEditor>();
                const Vector3 normal = face->Normal();
                editor->LayerMask = static_cast<std::uint16_t>(
                    4 | MphRead::Mods::MapGen::MapPacker::GetPrimaryAxis(normal));
                editor->Plane = Vector4(
                    normal,
                    Vector3::Dot(normal, ManagedAt(facePoints, 0)));
                editor->Damaging(face->Damaging());
                editor->Terrain(face->Terrain());
                editor->Slipperiness(face->Slipperiness);
                editor->Reflect(face->ReflectBeams);
                // the editor asks whether a face is there for players, beams
                // and the scan visor; the file stores whether to ignore it
                editor->Players(!face->IgnorePlayers);
                editor->Beams(!face->IgnoreBeams);
                editor->Scan(!face->IgnoreScan);
                for (std::size_t i = 0; i < facePoints->Length(); ++i)
                {
                    editor->Points->push_back(ManagedAt(facePoints, i));
                }
                editors->push_back(std::move(editor));
            }
            else
            {
                std::vector<std::unique_ptr<FanFaceOwner>> parts = Fan(face);
                for (const std::unique_ptr<FanFaceOwner>& owner : parts)
                {
                    BuiltFace* part = owner->Face.get();
                    auto editor = std::make_shared<CollisionDataEditor>();
                    const Vector3 normal = part->Normal();
                    ManagedArray<Vector3>* partPoints = FacePoints(part);
                    editor->LayerMask = static_cast<std::uint16_t>(
                        4 | MphRead::Mods::MapGen::MapPacker::GetPrimaryAxis(normal));
                    editor->Plane = Vector4(
                        normal,
                        Vector3::Dot(normal, ManagedAt(partPoints, 0)));
                    editor->Damaging(face->Damaging());
                    editor->Terrain(face->Terrain());
                    editor->Slipperiness(face->Slipperiness);
                    editor->Reflect(face->ReflectBeams);
                    editor->Players(!face->IgnorePlayers);
                    editor->Beams(!face->IgnoreBeams);
                    editor->Scan(!face->IgnoreScan);
                    for (std::size_t i = 0; i < partPoints->Length(); ++i)
                    {
                        editor->Points->push_back(ManagedAt(partPoints, i));
                    }
                    editors->push_back(std::move(editor));
                }
            }
        }
        if (editors->empty())
        {
            throw ProgramException("A map needs at least one solid face.");
        }
        return MphRead::Mods::MapGen::MapCollisionPacker::Pack(editors);
    }
}

namespace MphRead::Mods::MapGen
{
    void MapPacker::Generate(
        BuiltMap* map,
        const std::string& archiveDir,
        const std::string& entityDir,
        const std::string& nodeDir,
        bool verbose)
    {
        if (map == nullptr)
        {
            throw System::NullReferenceException();
        }
        MapDefinition* def = map->Definition();
        DirectoryCreateDirectory(archiveDir);
        DirectoryCreateDirectory(entityDir);
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }
        const std::string prefix = ::MphRead::NativeRuntime::ToLowerInvariant(def->Name());

        std::vector<std::uint8_t> animation;
        auto [model, vertices] = BuildModel(map, &animation);
        std::vector<std::uint8_t> collision = BuildCollision(map->Solid());
        std::vector<Editor::EntityEditorBase*>& entityList = map->Entities();
        std::vector<std::uint8_t> entities = Repack::PackEntities(
            std::span<Editor::EntityEditorBase* const>(
                entityList.data(), entityList.size()));
        auto [nodes, nodeCount, edges] = MapNodePacker::Pack(&map->Solid(), &map->Links);

        const std::string modelPath = PathCombine(archiveDir, prefix + "_Model.bin");
        FileWriteAllBytes(modelPath, model);
        const std::string animPath = PathCombine(archiveDir, prefix + "_Anim.bin");
        FileWriteAllBytes(animPath, animation.empty() ? std::vector<std::uint8_t>(24U) : animation);
        const std::string collisionPath = PathCombine(archiveDir, prefix + "_Collision.bin");
        FileWriteAllBytes(collisionPath, collision);
        // Each Quake 3 mover's own collision, beside the room's
        // (Q3MoverEntity).
        for (const auto& [mover, faces] : map->MoverSolid)
        {
            FileWriteAllBytes(PathCombine(archiveDir, prefix + "_q3model" + std::to_string(mover) + "_Collision.bin"),
                BuildCollision(faces));
        }
        const std::string entityPath = PathCombine(entityDir, prefix + "_Ent.bin");
        FileWriteAllBytes(entityPath, entities);
        DirectoryCreateDirectory(nodeDir);
        const std::string nodePath = PathCombine(nodeDir, prefix + "_Node.bin");
        FileWriteAllBytes(nodePath, nodes);

        if (verbose)
        {
            WriteLine(def->Name() + ": "
                + ::MphRead::NativeRuntime::ToString(ListCount(map->Faces().size()))
                + " polygons (" + ::MphRead::NativeRuntime::ToString(vertices)
                + " vertices), "
                + ::MphRead::NativeRuntime::ToString(ListCount(map->Solid().size()))
                + " collision faces, "
                + ::MphRead::NativeRuntime::ToString(ListCount(map->Entities().size()))
                + " entities");
            WriteLine("  " + ::MphRead::NativeRuntime::ToString(nodeCount)
                + " bot waypoints, " + ::MphRead::NativeRuntime::ToString(edges)
                + " routes between them");
            WriteLine("  model "
                + ::MphRead::NativeRuntime::ToString(model.size(), "N0")
                + " B, collision "
                + ::MphRead::NativeRuntime::ToString(collision.size(), "N0")
                + " B, entities "
                + ::MphRead::NativeRuntime::ToString(entities.size(), "N0")
                + " B, nodes "
                + ::MphRead::NativeRuntime::ToString(nodes.size(), "N0") + " B");
        }
    }

    void MapPacker::Generate(
        MapDefinition* def,
        const std::string& archiveDir,
        const std::string& entityDir,
        const std::string& nodeDir,
        bool verbose)
    {
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }
        std::shared_ptr<BuiltMap> map = def->Import() == nullptr
            ? MapBuilder::Build(def)
            : Q3Import::Build(def, verbose);
        ApplyCollision(map.get(), def, verbose);
        Generate(map.get(), archiveDir, entityDir, nodeDir, verbose);
    }

    // Swap the room's collision for the one in the map's .obj, if it names
    // one: the same answer for both builders. MapNodePacker reads the same
    // list, so the bots' waypoints follow the edit.
    void MapPacker::ApplyCollision(BuiltMap* map, MapDefinition* def, bool verbose)
    {
        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }
        MapCollision* collision = def->Collision();
        if (collision == nullptr)
        {
            return;
        }
        const std::string& source = ::MphRead::NativeRuntime::RequireReference(collision->Source);
        if (source.empty())
        {
            return;
        }
        const std::optional<std::vector<std::uint8_t>> bytes = collision->ReadBytes();
        if (!bytes.has_value())
        {
            throw ProgramException(def->Name() + " says its collision is " + source
                + ", which is not beside the map file, in " + CustomRooms::MapDirectory()
                + ", or with the game files. Write one with tools/collision-to-obj.py, or take the \"collision\" key out "
                "to go back to the collision the geometry makes.");
        }
        CollisionObj::Result read = CollisionObj::Read(*bytes, source, collision->ZUp);
        if (map == nullptr)
        {
            throw System::NullReferenceException();
        }
        const std::int32_t replaced = ListCount(map->Solid().size());
        const std::int32_t readFaceCount = ListCount(read.Faces.size());
        map->Solid().clear();
        for (std::unique_ptr<BuiltFace>& face : read.Faces)
        {
            map->Solid().push_back(map->OwnFace(std::move(face)));
        }
        if (verbose)
        {
            const std::string degenerate = read.Degenerate > 0
                ? " (" + ::MphRead::NativeRuntime::ToString(read.Degenerate)
                    + " enclosing no area, skipped)"
                : std::string();
            WriteLine("  collision from " + source + ": "
                + ::MphRead::NativeRuntime::ToString(readFaceCount)
                + " faces over "
                + ::MphRead::NativeRuntime::ToString(read.Vertices)
                + " vertices, in place of the geometry's "
                + ::MphRead::NativeRuntime::ToString(replaced) + degenerate);
        }
    }

    std::int32_t MapPacker::GetPrimaryAxis(Vector3 normal) noexcept
    {
        const float x = std::fabs(normal.X);
        const float y = std::fabs(normal.Y);
        const float z = std::fabs(normal.Z);
        if (y > x && y >= z)
        {
            return 1;
        }
        if (z > x && z > y)
        {
            return 2;
        }
        return 0;
    }
}
