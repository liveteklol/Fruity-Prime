#include "GlStateGuard.hpp"
#if !defined(__ANDROID__)
#include "GL.hpp"

#include <algorithm>

#if defined(_WIN32)
#define FRUITY_GL_GUARD_CALL __stdcall
#else
#define FRUITY_GL_GUARD_CALL
#endif

namespace OpenTK::Graphics::OpenGL
{
    namespace
    {
        using U1 = void(FRUITY_GL_GUARD_CALL*)(unsigned);
        using U2 = void(FRUITY_GL_GUARD_CALL*)(unsigned, unsigned);
        using U4 = void(FRUITY_GL_GUARD_CALL*)(unsigned, unsigned, unsigned, unsigned);
        using I2 = void(FRUITY_GL_GUARD_CALL*)(unsigned, int);
        using I4 = void(FRUITY_GL_GUARD_CALL*)(int, int, int, int);
        using B4 = void(FRUITY_GL_GUARD_CALL*)(unsigned char, unsigned char, unsigned char, unsigned char);
        using B1 = void(FRUITY_GL_GUARD_CALL*)(unsigned char);
        using GetIntegervFn = void(FRUITY_GL_GUARD_CALL*)(unsigned, int*);
        using IsEnabledFn = unsigned char(FRUITY_GL_GUARD_CALL*)(unsigned);
        using GetErrorFn = unsigned(FRUITY_GL_GUARD_CALL*)();

        struct Entry final
        {
            GetIntegervFn GetIntegerv = nullptr;
        GetErrorFn GetError = nullptr;
            IsEnabledFn IsEnabled = nullptr;
            U1 Enable = nullptr, Disable = nullptr, ActiveTexture = nullptr, UseProgram = nullptr;
            U1 BindVertexArray = nullptr, DisableVertexAttribArray = nullptr, FrontFace = nullptr;
            U1 DepthFunc = nullptr;
            B1 DepthMask = nullptr;
            U2 BindFramebuffer = nullptr, BindBuffer = nullptr, BindTexture = nullptr, BindSampler = nullptr;
            U2 VertexAttribDivisor = nullptr, BlendEquationSeparate = nullptr;
            U4 BlendFuncSeparate = nullptr;
            I2 PixelStorei = nullptr;
            I4 Viewport = nullptr, Scissor = nullptr;
            B4 ColorMask = nullptr;
        };

        template <typename T>
        T Load(const char* name) noexcept
        {
            return reinterpret_cast<T>(GetEntryPoint(name));
        }

        // Per context, so a context made after a renderer switch resolves its own.
        Entry Resolve() noexcept
        {
            Entry e;
            e.GetIntegerv = Load<GetIntegervFn>("glGetIntegerv");
            e.GetError = Load<GetErrorFn>("glGetError");
            e.IsEnabled = Load<IsEnabledFn>("glIsEnabled");
            e.Enable = Load<U1>("glEnable");
            e.Disable = Load<U1>("glDisable");
            e.ActiveTexture = Load<U1>("glActiveTexture");
            e.UseProgram = Load<U1>("glUseProgram");
            e.BindVertexArray = Load<U1>("glBindVertexArray");
            e.DisableVertexAttribArray = Load<U1>("glDisableVertexAttribArray");
            e.FrontFace = Load<U1>("glFrontFace");
            e.DepthFunc = Load<U1>("glDepthFunc");
            e.DepthMask = Load<B1>("glDepthMask");
            e.BindFramebuffer = Load<U2>("glBindFramebuffer");
            e.BindBuffer = Load<U2>("glBindBuffer");
            e.BindTexture = Load<U2>("glBindTexture");
            e.BindSampler = Load<U2>("glBindSampler");
            e.VertexAttribDivisor = Load<U2>("glVertexAttribDivisor");
            e.BlendEquationSeparate = Load<U2>("glBlendEquationSeparate");
            e.BlendFuncSeparate = Load<U4>("glBlendFuncSeparate");
            e.PixelStorei = Load<I2>("glPixelStorei");
            e.Viewport = Load<I4>("glViewport");
            e.Scissor = Load<I4>("glScissor");
            e.ColorMask = Load<B4>("glColorMask");
            return e;
        }

        // The capabilities the game turns on and off between draws, with the
        // compatibility profile's fixed-function ones it still uses.
        constexpr std::array<unsigned, 9> Capabilities{
            0x0C11, // SCISSOR_TEST
            0x0BE2, // BLEND
            0x0B71, // DEPTH_TEST
            0x0B44, // CULL_FACE
            0x0B90, // STENCIL_TEST
            0x0BC0, // ALPHA_TEST
            0x8037, // POLYGON_OFFSET_FILL
            0x8DB9, // FRAMEBUFFER_SRGB
            0x809D, // MULTISAMPLE
        };
        constexpr std::array<unsigned, 6> PackNames{0x0D00, 0x0D01, 0x0D02, 0x0D03, 0x0D04, 0x0D05};
        constexpr std::array<unsigned, 8> UnpackNames{0x0CF0, 0x0CF1, 0x0CF2, 0x0CF3, 0x0CF4, 0x0CF5, 0x806D, 0x806E};
        constexpr std::array<int, 6> PackDefaults{0, 0, 0, 0, 0, 4};
        constexpr std::array<int, 8> UnpackDefaults{0, 0, 0, 0, 0, 4, 0, 0};
        constexpr unsigned Texture0 = 0x84C0;
        constexpr unsigned Texture2D = 0x0DE1;
        constexpr unsigned TextureRectangle = 0x84F5;

        int Get(const Entry& e, unsigned name, int fallback) noexcept
        {
            int value = fallback;
            if (e.GetIntegerv != nullptr) e.GetIntegerv(name, &value);
            return value;
        }

        // Fixed-function texture units the game samples from; the generic
        // count can be far larger and only names bindings, not enables.
        constexpr int FixedUnits = 4;

        // Qt Quick's own GL errors are its own: the game's renderer checks
        // for errors after its next allocation and would take them as its.
        void Drain(const Entry& e) noexcept
        {
            if (e.GetError == nullptr) return;
            for (int i = 0; i < 64 && e.GetError() != 0; ++i)
            {
            }
        }
    }

    GlStateGuard::GlStateGuard()
    {
        Capture();
    }

    GlStateGuard::~GlStateGuard()
    {
        Restore();
    }

    void GlStateGuard::Capture()
    {
        const Entry e = Resolve();
        if (e.GetIntegerv == nullptr) return;
        _framebuffer = Get(e, 0x8CA6, 0);
        e.GetIntegerv(0x0BA2, _viewport.data());
        e.GetIntegerv(0x0C10, _scissor.data());
        e.GetIntegerv(0x0C23, _colorWrite.data());
        _program = Get(e, 0x8B8D, 0);
        _activeTexture = Get(e, 0x84E0, static_cast<int>(Texture0));
        _blendSrcRgb = Get(e, 0x80C9, 1);
        _blendDstRgb = Get(e, 0x80C8, 0);
        _blendSrcAlpha = Get(e, 0x80CB, 1);
        _blendDstAlpha = Get(e, 0x80CA, 0);
        _blendEquationRgb = Get(e, 0x8009, 0x8006);
        _blendEquationAlpha = Get(e, 0x883D, 0x8006);
        _depthFunction = Get(e, 0x0B74, 0x0201);
        _depthWrite = Get(e, 0x0B72, 1);
        _frontFace = Get(e, 0x0B46, 0x0901);
        _arrayBuffer = Get(e, 0x8894, 0);
        _pixelPackBuffer = Get(e, 0x88ED, 0);
        _pixelUnpackBuffer = Get(e, 0x88EF, 0);
        for (std::size_t i = 0; i < PackNames.size(); ++i) _pack[i] = Get(e, PackNames[i], PackDefaults[i]);
        for (std::size_t i = 0; i < UnpackNames.size(); ++i) _unpack[i] = Get(e, UnpackNames[i], UnpackDefaults[i]);
        _enabled.clear();
        for (const unsigned cap : Capabilities)
            _enabled.push_back(e.IsEnabled != nullptr && e.IsEnabled(cap) != 0 ? 1 : 0);
        const int units = std::clamp(Get(e, 0x8B4D, 8), 1, 32);
        _unitTexture2D.assign(static_cast<std::size_t>(units), 0);
        _unitRectangle.assign(static_cast<std::size_t>(units), 0);
        _unitTexture2DEnabled.assign(static_cast<std::size_t>(FixedUnits), 0);
        if (e.ActiveTexture == nullptr) return;
        for (int i = 0; i < units; ++i)
        {
            e.ActiveTexture(Texture0 + static_cast<unsigned>(i));
            _unitTexture2D[static_cast<std::size_t>(i)] = Get(e, 0x8069, 0);
            _unitRectangle[static_cast<std::size_t>(i)] = Get(e, 0x84F6, 0);
            if (i < FixedUnits && e.IsEnabled != nullptr)
                _unitTexture2DEnabled[static_cast<std::size_t>(i)] = e.IsEnabled(Texture2D) != 0 ? 1 : 0;
        }
        e.ActiveTexture(static_cast<unsigned>(_activeTexture));
    }

    void GlStateGuard::Restore() noexcept
    {
        const Entry e = Resolve();
        if (e.GetIntegerv == nullptr) return;
        Drain(e);
        // The game draws with VAO 0, client memory and immediate mode.
        if (e.BindVertexArray != nullptr) e.BindVertexArray(0);
        if (e.BindBuffer != nullptr)
        {
            e.BindBuffer(0x8893, 0);
            e.BindBuffer(0x8892, static_cast<unsigned>(_arrayBuffer));
            e.BindBuffer(0x88EB, static_cast<unsigned>(_pixelPackBuffer));
            e.BindBuffer(0x88EC, static_cast<unsigned>(_pixelUnpackBuffer));
        }
        if (e.DisableVertexAttribArray != nullptr)
        {
            // Attribute 0 aliases gl_Vertex in the compatibility profile; a
            // divisor left on an aliased attribute flattens the game's arrays.
            const int attribs = std::clamp(Get(e, 0x8869, 16), 1, 32);
            for (int i = 0; i < attribs; ++i)
            {
                e.DisableVertexAttribArray(static_cast<unsigned>(i));
                if (e.VertexAttribDivisor != nullptr) e.VertexAttribDivisor(static_cast<unsigned>(i), 0);
            }
        }
        if (e.BindFramebuffer != nullptr) e.BindFramebuffer(0x8D40, static_cast<unsigned>(_framebuffer));
        if (e.Viewport != nullptr) e.Viewport(_viewport[0], _viewport[1], _viewport[2], _viewport[3]);
        if (e.Scissor != nullptr) e.Scissor(_scissor[0], _scissor[1], _scissor[2], _scissor[3]);
        if (e.ColorMask != nullptr)
            e.ColorMask(_colorWrite[0] != 0, _colorWrite[1] != 0, _colorWrite[2] != 0, _colorWrite[3] != 0);
        for (std::size_t i = 0; i < Capabilities.size() && i < _enabled.size(); ++i)
        {
            if (_enabled[i] != 0) { if (e.Enable != nullptr) e.Enable(Capabilities[i]); }
            else if (e.Disable != nullptr) e.Disable(Capabilities[i]);
        }
        if (e.BlendFuncSeparate != nullptr)
            e.BlendFuncSeparate(static_cast<unsigned>(_blendSrcRgb), static_cast<unsigned>(_blendDstRgb),
                static_cast<unsigned>(_blendSrcAlpha), static_cast<unsigned>(_blendDstAlpha));
        if (e.BlendEquationSeparate != nullptr)
            e.BlendEquationSeparate(static_cast<unsigned>(_blendEquationRgb), static_cast<unsigned>(_blendEquationAlpha));
        if (e.DepthFunc != nullptr) e.DepthFunc(static_cast<unsigned>(_depthFunction));
        if (e.DepthMask != nullptr) e.DepthMask(_depthWrite != 0 ? 1 : 0);
        if (e.FrontFace != nullptr) e.FrontFace(static_cast<unsigned>(_frontFace));
        if (e.UseProgram != nullptr) e.UseProgram(static_cast<unsigned>(_program));
        if (e.PixelStorei != nullptr)
        {
            for (std::size_t i = 0; i < PackNames.size(); ++i) e.PixelStorei(PackNames[i], _pack[i]);
            for (std::size_t i = 0; i < UnpackNames.size(); ++i) e.PixelStorei(UnpackNames[i], _unpack[i]);
        }
        if (e.ActiveTexture == nullptr) return;
        for (std::size_t i = 0; i < _unitTexture2D.size(); ++i)
        {
            e.ActiveTexture(Texture0 + static_cast<unsigned>(i));
            if (e.BindSampler != nullptr) e.BindSampler(static_cast<unsigned>(i), 0);
            if (e.BindTexture != nullptr)
            {
                e.BindTexture(TextureRectangle, static_cast<unsigned>(_unitRectangle[i]));
                e.BindTexture(Texture2D, static_cast<unsigned>(_unitTexture2D[i]));
            }
            if (i < _unitTexture2DEnabled.size())
            {
                if (_unitTexture2DEnabled[i] != 0) { if (e.Enable != nullptr) e.Enable(Texture2D); }
                else if (e.Disable != nullptr) e.Disable(Texture2D);
            }
        }
        e.ActiveTexture(static_cast<unsigned>(_activeTexture));
        Drain(e);
    }
}
#endif
