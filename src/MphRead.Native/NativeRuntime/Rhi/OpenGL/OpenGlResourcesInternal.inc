// Included inside the OpenGL implementation namespace, after the device
// forward declaration. These native objects never cross the RHI boundary.
class OpenGlBuffer final : public Buffer
{
public:
    OpenGlBuffer(OpenGlGraphicsDevice& device, const BufferDesc& desc, std::int32_t name)
        : _device(&device), _desc(desc), _name(name), _state(desc.initialState) {}
    ~OpenGlBuffer() override;
    const BufferDesc& Desc() const noexcept override { return _desc; }
    std::int32_t Name() const noexcept { return _name; }
    ResourceState State() const noexcept { return _state; }
    void State(ResourceState state) noexcept { _state = state; }
    OpenGlGraphicsDevice* Device() const noexcept { return _device; }
    void Detach() noexcept { _device = nullptr; _name = 0; }
    std::weak_ptr<void> Lifetime() const noexcept { return _lifetime; }
private:
    OpenGlGraphicsDevice* _device;
    BufferDesc _desc;
    std::int32_t _name;
    ResourceState _state;
    std::shared_ptr<void> _lifetime = std::make_shared<int>(0);
};

class OpenGlBindingLayout final : public BindingLayout
{
public:
    OpenGlBindingLayout(OpenGlGraphicsDevice& device, BindingLayoutDesc desc)
        : _device(&device), _lifetime(DeviceLifetime(device)), _desc(std::move(desc))
    {
        std::unordered_set<std::uint32_t> bindings;
        for (const auto& entry : _desc.entries)
        {
            if (!entry.count || entry.stages == ShaderStage::None
                || !bindings.insert(entry.binding).second)
                throw std::invalid_argument("OpenGL RHI: invalid or duplicate binding declaration.");
        }
    }
    const BindingLayoutDesc& Desc() const noexcept override { return _desc; }
    OpenGlGraphicsDevice* Device() const noexcept { return _lifetime.expired() ? nullptr : _device; }
private:
    OpenGlGraphicsDevice* _device;
    std::weak_ptr<void> _lifetime;
    BindingLayoutDesc _desc;
};

// A set keeps its layout description alive independently of the caller's
// temporary layout object, just as pipeline layout descriptions do.
class OpenGlBindingSet final : public BindingSet
{
public:
    OpenGlBindingSet(OpenGlGraphicsDevice& device, const BindingSetDesc& desc);
    void ValidateResources() const;
    const BindingSetDesc& Desc() const noexcept override { return _desc; }
    OpenGlGraphicsDevice* Device() const noexcept { return _lifetime.expired() ? nullptr : _device; }
private:
    OpenGlGraphicsDevice* _device;
    std::weak_ptr<void> _lifetime;
    OpenGlBindingLayout _layout;
    BindingSetDesc _desc;
    std::vector<std::weak_ptr<void>> _resourceLifetimes;
};

struct OpenGlSamplerStorage final
{
    OpenGlSamplerStorage(OpenGlGraphicsDevice& device, SamplerDesc desc);
    ~OpenGlSamplerStorage();
    OpenGlGraphicsDevice* Device;
    SamplerDesc Desc;
    unsigned Name = 0;
};

class OpenGlSampler final : public Sampler
{
public:
    explicit OpenGlSampler(std::shared_ptr<OpenGlSamplerStorage> storage) : _storage(std::move(storage)) {}
    const SamplerDesc& Desc() const noexcept override { return _storage->Desc; }
    unsigned Name() const noexcept { return _storage->Name; }
    OpenGlGraphicsDevice* Device() const noexcept { return _storage->Device; }
    std::weak_ptr<void> Lifetime() const noexcept { return _lifetime; }
private:
    std::shared_ptr<OpenGlSamplerStorage> _storage;
    std::shared_ptr<void> _lifetime = std::make_shared<int>(0);
};
