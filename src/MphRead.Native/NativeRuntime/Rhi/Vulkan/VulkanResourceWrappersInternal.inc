// Part of VulkanGraphicsDevice.cpp (one translation unit, like the other
// Vulkan *Internal.inc files): the public resource wrappers (buffer,
// sampler, texture, view). Native creation is VulkanResources; destruction
// is the device's retirement.
        class VulkanBuffer final : public Buffer, public VulkanNativeOwner
        {
        public:
            VulkanBuffer(std::shared_ptr<VulkanDeviceState> state, const BufferDesc& desc);
            ~VulkanBuffer() override;
            void CloseNative() noexcept override;
            [[nodiscard]] const BufferDesc& Desc() const noexcept override { return _desc; }

            [[nodiscard]] VkBuffer Native() const noexcept { return _buffer; }
            [[nodiscard]] VmaAllocation Allocation() const noexcept { return _allocation; }
            [[nodiscard]] std::byte* Mapped() const noexcept { return _mapped; }
            [[nodiscard]] std::weak_ptr<void> Lifetime() const noexcept { return _lifetime; }
            [[nodiscard]] ResourceState State() const noexcept { return _state; }
            void State(ResourceState value) noexcept { _state = value; }
            [[nodiscard]] const std::shared_ptr<VulkanDeviceState>& DeviceState() const noexcept
            {
                return _device;
            }

        private:
            std::shared_ptr<VulkanDeviceState> _device;
            VulkanNativeRegistration _registration;
            BufferDesc _desc{};
            VkBuffer _buffer = VK_NULL_HANDLE;
            VmaAllocation _allocation = VK_NULL_HANDLE;
            std::byte* _mapped = nullptr;
            ResourceState _state = ResourceState::Undefined;
            std::shared_ptr<void> _lifetime = std::make_shared<int>(0);
        };

        class VulkanTexture;
        class VulkanTextureView;

        class VulkanSampler final : public Sampler, public VulkanNativeOwner
        {
        public:
            VulkanSampler(std::shared_ptr<VulkanDeviceState> state, const SamplerDesc& desc);
            ~VulkanSampler() override;
            void CloseNative() noexcept override;
            [[nodiscard]] const SamplerDesc& Desc() const noexcept override { return _desc; }
            [[nodiscard]] VkSampler Native() const noexcept { return _sampler; }
            [[nodiscard]] std::uint64_t Identity() const noexcept { return _identity; }
            [[nodiscard]] std::weak_ptr<void> Lifetime() const noexcept { return _lifetime; }
            [[nodiscard]] const std::shared_ptr<VulkanDeviceState>& DeviceState() const noexcept { return _device; }

        private:
            std::shared_ptr<VulkanDeviceState> _device;
            VulkanNativeRegistration _registration;
            SamplerDesc _desc{};
            VkSampler _sampler = VK_NULL_HANDLE;
            const std::uint64_t _identity = NextSceneResourceIdentity();
            std::shared_ptr<void> _lifetime = std::make_shared<int>(0);
        };

        class VulkanTexture final : public Texture, public VulkanNativeOwner
        {
        public:
            VulkanTexture(std::shared_ptr<VulkanDeviceState> state,
                const TextureDesc& desc, TextureHandle handle);
            ~VulkanTexture() override;
            void CloseNative() noexcept override;
            [[nodiscard]] const TextureDesc& Desc() const noexcept override { return _desc; }
            [[nodiscard]] TextureHandle Handle() const noexcept override { return _handle; }
            [[nodiscard]] VkImage Native() const noexcept { return _image; }
            [[nodiscard]] std::uint64_t Identity() const noexcept { return _identity; }
            [[nodiscard]] std::uint64_t Generation() const noexcept { return _generation; }
            [[nodiscard]] ResourceState State() const noexcept { return _state; }
            void State(ResourceState value) noexcept { _state = value; }
            [[nodiscard]] const std::shared_ptr<VulkanDeviceState>& DeviceState() const noexcept
            {
                return _device;
            }
            std::weak_ptr<void> Lifetime() const noexcept { return _lifetime; }
            void Resize(std::uint32_t width, std::uint32_t height);
            void Register(VulkanTextureView& view);
            void Unregister(VulkanTextureView& view) noexcept;
            // The whole image as a shader reads it: depth only for a depth
            // format, alpha one for RGB8. Lives as long as the image does.
            [[nodiscard]] VkImageView SampledView();

        private:
            void CreateImage();
            VkImageView _sampledView = VK_NULL_HANDLE;
            void DestroyImage() noexcept;
            std::shared_ptr<VulkanDeviceState> _device;
            VulkanNativeRegistration _registration;
            TextureDesc _desc{};
            TextureHandle _handle{};
            VkImage _image = VK_NULL_HANDLE;
            const std::uint64_t _identity = NextSceneResourceIdentity();
            std::uint64_t _generation = 1;
            VmaAllocation _allocation = VK_NULL_HANDLE;
            ResourceState _state = ResourceState::Undefined;
            std::vector<VulkanTextureView*> _views{};
            std::shared_ptr<void> _lifetime = std::make_shared<int>(0);
        };

        class VulkanTextureView final : public TextureView
        {
        public:
            VulkanTextureView(VulkanTexture& texture, const TextureViewDesc& desc);
            ~VulkanTextureView() override;
            [[nodiscard]] const TextureViewDesc& Desc() const noexcept override { return _desc; }
            [[nodiscard]] const Texture& TextureResource() const noexcept override { return _texture; }
            [[nodiscard]] VkImageView Native() const noexcept { return _view; }
            [[nodiscard]] bool TextureAlive() const noexcept { return !_textureLifetime.expired(); }
            [[nodiscard]] std::weak_ptr<void> Lifetime() const noexcept { return _lifetime; }
            const std::shared_ptr<VulkanDeviceState>& DeviceState() const noexcept { return _device; }
            void CreateNative();
            void DestroyNative() noexcept;
            void SwapNative(VulkanTextureView& other) noexcept { std::swap(_view, other._view); }

        private:
            VulkanTexture& _texture;
            std::shared_ptr<VulkanDeviceState> _device;
            std::weak_ptr<void> _textureLifetime;
            std::shared_ptr<void> _lifetime = std::make_shared<int>(0);
            TextureViewDesc _desc{};
            VkImageView _view = VK_NULL_HANDLE;
        };
