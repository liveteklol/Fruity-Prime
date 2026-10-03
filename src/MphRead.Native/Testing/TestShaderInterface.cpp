#include "../NativeRuntime/Rhi/VertexSemantics.hpp"
#include "../NativeRuntime/Rhi/SceneShaderAbi.hpp"
#include "../NativeRuntime/Rhi/VulkanShaderInterface.hpp"
#include "../Shaders.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Phase 5's contract, checked without a GL context: one semantic table, every
// backend's numbering distinct within itself, desktop shaders that read only
// the semantic inputs and the same logical constant capacities. Production
// Vulkan packing and compiled reflection have dedicated CPU fixtures.
namespace
{
    using namespace MphRead::NativeRuntime::Rhi;

    [[noreturn]] void Fail(std::string_view message)
    {
        throw std::runtime_error(std::string(message));
    }

    void Expect(bool value, std::string_view message)
    {
        if (!value)
        {
            Fail(message);
        }
    }

    void ExpectDistinct(const std::array<std::uint32_t, VertexSemanticCount>& table, std::string_view name)
    {
        const std::set<std::uint32_t> unique(table.begin(), table.end());
        Expect(unique.size() == table.size(), std::string(name) + " reuses a location");
    }

    void TestTablesAreDistinct()
    {
        ExpectDistinct(OpenGlDesktopLocations, "OpenGlDesktopLocations");
        ExpectDistinct(OpenGlEsLocations, "OpenGlEsLocations");
        ExpectDistinct(VulkanLocations, "VulkanLocations");
        const std::set<std::string_view> names(VertexSemanticNames.begin(), VertexSemanticNames.end());
        Expect(names.size() == VertexSemanticCount, "semantic names are not unique");
    }

    void TestLogicalSceneAbi()
    {
        using namespace SceneShaderAbi;
        Expect(IsValid() && GroupCount == 4, "logical groups must be valid and distinct");
        Expect(MaterialTexture.type == BindingType::SampledTexture
            && MaterialSampler.type == BindingType::Sampler, "image and sampler are separate resources");
        Expect(MaterialTexture.group == MaterialSampler.group
            && MaterialTexture.binding != MaterialSampler.binding, "material sampler has its own binding");
        for (const auto& binding : Bindings)
        {
            const auto mapped = Vulkan::MapBinding(binding);
            Expect(mapped.Set == static_cast<std::uint32_t>(binding.group)
                && mapped.Binding == binding.binding, "Vulkan adapter changed the logical ABI");
        }
        Expect(Vulkan::FrameBlock.Set != Vulkan::MaterialBlock.Set
            && Vulkan::MaterialBlock.Set != Vulkan::DrawBlock.Set
            && Vulkan::DrawBlock.Set != Vulkan::HudPostBlock.Set, "update groups collapsed");
    }

    void TestPipelineLayoutOwnsItsContract()
    {
        BindingLayoutDesc frame{{{0, BindingType::UniformBuffer, ShaderStage::AllGraphics, 1}}};
        BindingLayoutDesc material{{{1, BindingType::SampledTexture, ShaderStage::Fragment, 1},
            {2, BindingType::Sampler, ShaderStage::Fragment, 1}}};
        PipelineLayout layout{{frame, material, {}, {}}};
        const auto snapshot = layout;
        frame.entries.clear(); material.entries[0].binding = 99;
        Expect(layout == snapshot && layout.groups.size() == SceneShaderAbi::GroupCount,
            "pipeline groups must not refer back to temporary input layouts");
        auto different = layout;
        different.groups[1].entries[0].binding = 99;
        Expect(different != layout, "group binding changes affect pipeline identity");
        different = layout; different.groups.pop_back();
        Expect(different != layout, "group count affects pipeline identity");
    }

    void TestDesktopUsesTheCommonInterface()
    {
        Expect(OpenGlDesktopLocations == VulkanLocations,
            "desktop and Vulkan must share the explicit vertex input interface");
    }

    void TestVulkanMatchesThePlan()
    {
        Expect(Location(VulkanLocations, VertexSemantic::Position) == 0U, "vulkan position");
        Expect(Location(VulkanLocations, VertexSemantic::Normal) == 1U, "vulkan normal");
        Expect(Location(VulkanLocations, VertexSemantic::Color) == 2U, "vulkan colour");
        Expect(Location(VulkanLocations, VertexSemantic::TexCoord) == 3U, "vulkan texcoord");

    }

    void ExpectNoBuiltinInputs(const std::string& source, std::string_view name)
    {
        for (const char* builtin : {"gl_Vertex", "gl_Normal", "gl_Color", "gl_MultiTexCoord",
                 "gl_SecondaryColor", "gl_FogCoord"})
        {
            Expect(source.find(builtin) == std::string::npos,
                std::string(name) + " still reads " + builtin);
        }
    }

    void ExpectOnlySemanticAttributes(const std::string& source, std::string_view name)
    {
        std::size_t at = 0;
        while ((at = source.find("attribute ", at)) != std::string::npos)
        {
            const std::size_t end = source.find(';', at);
            const std::string line = source.substr(at, end - at);
            const std::string input = line.substr(line.find_last_of(' ') + 1U);
            Expect(std::find(VertexSemanticNames.begin(), VertexSemanticNames.end(), input)
                    != VertexSemanticNames.end(),
                std::string(name) + " declares a non-semantic input " + input);
            at = end;
        }
    }

    void TestDesktopShadersUseExplicitInputs()
    {
        using MphRead::Shaders;
        const std::pair<const std::string*, std::string_view> sources[]{
            {&Shaders::VertexShader, "VertexShader"},
            {&Shaders::FragmentShader, "FragmentShader"},
            {&Shaders::BackdropVertexShader, "BackdropVertexShader"},
            {&Shaders::BackdropFragmentShader, "BackdropFragmentShader"},
            {&Shaders::RttVertexShader, "RttVertexShader"},
            {&Shaders::RttFragmentShader, "RttFragmentShader"},
            {&Shaders::CelFragmentShader, "CelFragmentShader"},
            {&Shaders::ShiftFragmentShader, "ShiftFragmentShader"},
        };
        for (const auto& [source, name] : sources)
        {
            ExpectNoBuiltinInputs(*source, name);
            ExpectOnlySemanticAttributes(*source, name);
        }
        Expect(Shaders::VertexShader.find("attribute vec4 a_position;") != std::string::npos,
            "main vertex shader has no position input");
    }

    void TestLogicalConstantArrays()
    {
        const auto check = [](std::string_view program, std::string_view name, SceneShaderAbi::ValueType type, std::size_t count) {
            const auto found = std::find_if(SceneShaderAbi::Constants.begin(), SceneShaderAbi::Constants.end(),
                [&](const auto& member) { return member.program == program && member.name == name; });
            Expect(found != SceneShaderAbi::Constants.end() && found->type == type && found->count == count,
                "logical constant capacity/type drift");
        };
        check("main", "mtx_stack", SceneShaderAbi::ValueType::Mat4, MatrixStackCapacity);
        check("shift", "shift_table", SceneShaderAbi::ValueType::Float, ShiftTableLength);
        check("shift", "white_table", SceneShaderAbi::ValueType::Float, WhiteoutTableLength);
        Expect(SceneShaderAbi::Constants.size() == 55 && SceneShaderAbi::Textures.size() == 8,
            "production program interface coverage changed");
        Expect(SceneShaderAbi::TextureUnit("backdrop", "noise_tex") == 1,
            "backdrop noise must use the auxiliary texture unit");
    }

    void TestDesktopUniformContract()
    {
        using MphRead::Shaders;
        struct Program { std::string_view Name; const std::string* Vertex; const std::string* Fragment; };
        const Program programs[]{
            {"main", &Shaders::VertexShader, &Shaders::FragmentShader},
            {"composite", &Shaders::RttVertexShader, &Shaders::RttFragmentShader},
            {"cel", &Shaders::RttVertexShader, &Shaders::CelFragmentShader},
            {"shift", &Shaders::RttVertexShader, &Shaders::ShiftFragmentShader},
            {"backdrop", &Shaders::BackdropVertexShader, &Shaders::BackdropFragmentShader}};
        const std::regex declaration(R"(uniform (\w+)(?:\[(\d+)\])? (\w+);)");
        const auto typeName = [](SceneShaderAbi::ValueType type) -> std::string {
            using SceneShaderAbi::ValueType;
            switch (type) {
            case ValueType::Bool: return "bool"; case ValueType::Int: return "int"; case ValueType::Float: return "float";
            case ValueType::Vec3: return "vec3"; case ValueType::Vec4: return "vec4"; case ValueType::Mat4: return "mat4";
            }
            throw std::logic_error("Unknown logical shader value type.");
        };
        for (const auto& program : programs)
        {
            std::map<std::string, std::pair<std::string, unsigned long>> actual, expected;
            for (const auto* source : {program.Vertex, program.Fragment})
                for (auto i = std::sregex_iterator(source->begin(), source->end(), declaration); i != std::sregex_iterator(); ++i)
                {
                    const auto& match = *i;
                    const std::pair value{match[1].str(), match[2].matched ? std::stoul(match[2].str()) : 0UL};
                    const auto [previous, inserted] = actual.emplace(match[3].str(), value);
                    Expect(inserted || previous->second == value, "uniform type differs between stages");
                }
            for (const auto& member : SceneShaderAbi::Constants)
                if (member.program == program.Name)
                    expected.emplace(member.name, std::pair{typeName(member.type), static_cast<unsigned long>(member.count)});
            for (const auto& texture : SceneShaderAbi::Textures)
                if (texture.program == program.Name) expected.emplace(texture.name, std::pair{std::string("sampler2D"), 0UL});
            Expect(actual == expected, std::string(program.Name) + " desktop source differs from the logical ABI");
        }
    }

}

int main()
{
    try
    {
        TestTablesAreDistinct();
        TestLogicalSceneAbi();
        TestPipelineLayoutOwnsItsContract();
        TestDesktopUsesTheCommonInterface();
        TestVulkanMatchesThePlan();
        TestDesktopShadersUseExplicitInputs();
        TestLogicalConstantArrays();
        TestDesktopUniformContract();
        std::cout << "ShaderInterface tests passed.\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "ShaderInterface test failure: " << ex.what() << '\n';
        return 1;
    }
}
