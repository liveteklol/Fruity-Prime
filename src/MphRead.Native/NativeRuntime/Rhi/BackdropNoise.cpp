#include "BackdropNoise.hpp"
#include "GraphicsDevice.hpp"

namespace MphRead::NativeRuntime::Rhi
{
    BackdropNoiseResources CreateBackdropNoiseResources(GraphicsDevice& device)
    {
        static constexpr auto bytes = BackdropNoiseBytes();
        TextureDesc desc{};
        desc.width = desc.height = BackdropNoiseSize;
        desc.format = TextureFormat::RGBA8Unorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
        BackdropNoiseResources resources{};
        resources.Texture = device.CreateTexture(desc);
        device.WriteTexture(*resources.Texture, {BackdropNoiseSize, BackdropNoiseSize, desc.format, bytes.data()});
        SamplerDesc sampler{};
        sampler.minFilter = sampler.magFilter = sampler.mipFilter = Filter::Nearest;
        sampler.addressU = sampler.addressV = sampler.addressW = SamplerAddressMode::Repeat;
        sampler.minLod = sampler.maxLod = 0;
        resources.Sampler = device.CreateSampler(sampler);
        return resources;
    }
}
