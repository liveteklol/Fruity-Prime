// Part of VulkanGraphicsDevice.cpp (one translation unit, like the other
// Vulkan *Internal.inc files): the command list -- recording, draw
// state, bindings, transfers and transitions -- kept apart from the device
// that owns and schedules it.
        // The frame and draw groups' uniform blocks (camera, lights, fog; the
        // matrix stack) are dynamic uniform buffers: their sets name upload
        // pages and the bind gives each block's offset. FRUITY_VK_STATIC_DRAW=1
        // (A/B checks only) keeps them plain uniform buffers.
        [[nodiscard]] inline bool DynamicUniformGroup(std::uint32_t group) noexcept
        {
            static const bool dynamic = std::getenv("FRUITY_VK_STATIC_DRAW") == nullptr;
            return dynamic && (group == static_cast<std::uint32_t>(SceneShaderAbi::Group::Frame)
                || group == static_cast<std::uint32_t>(SceneShaderAbi::Group::Draw));
        }
        // One group's dynamic offsets, in binding order.
        struct DynamicOffsets final
        {
            std::array<std::uint32_t, 4> Values{};
            std::uint32_t Count = 0;
            bool operator==(const DynamicOffsets&) const = default;
        };

        class VulkanCommandList final : public CommandList, public VulkanNativeOwner
        {
        public:
            explicit VulkanCommandList(std::shared_ptr<VulkanDeviceState> state, bool transferOnly = false);
            ~VulkanCommandList() override;
            void CloseNative() noexcept override;

            void Begin() override;
            void End() override;
            [[nodiscard]] CommandListReadiness QueryReadiness() const override
            {
                if (_closed || !_device->ContextPointer || _device->Closing) return CommandListReadiness::Unavailable;
                if (_recording) return CommandListReadiness::Recording;
                return _commandSlots->CanBeginWithoutWait() ? CommandListReadiness::Ready : CommandListReadiness::Busy;
            }
            bool TryBegin() override
            {
                if (_autoRestart || _recording) throw std::logic_error("Vulkan RHI: command list is already recording.");
                if (QueryReadiness() != CommandListReadiness::Ready)
                { if (_metricsEnabled) ++_metricOptionalSkips; return false; }
                Begin(); return true;
            }
            [[nodiscard]] bool TryPrepareOptionalWork() override
            {
                const auto ready = QueryReadiness();
                if (ready == CommandListReadiness::Busy || ready == CommandListReadiness::Unavailable)
                { if (_metricsEnabled) ++_metricOptionalSkips; return false; }
                return true;
            }
            void BeginDebugLabel(const DebugLabel& label) override;
            void EndDebugLabel() override;
            void InsertDebugMarker(const DebugLabel& label) override;
            void InitializeTimestamps(TimestampQuerySet& set) override;
            void WriteTimestamp(TimestampQuerySet& set, std::uint32_t index) override;
            void BeginRendering(const RenderingInfo& info) override;
            void EndRendering() override;
            void SetPipeline(const GraphicsPipeline& pipeline) override;
            void SetSmallConstants(const SmallDrawConstants& constants) override;
            void SetViewport(const Viewport& viewport) override
            {
                RequireRecording();
                _viewport = viewport;
                _hasViewport = true;
                _dynamicDirty = true;
            }
            void SetScissor(const Scissor& scissor) override
            {
                RequireRecording();
                _scissor = scissor;
                _scissorEnabled = true;
                _dynamicDirty = true;
            }
            void SetVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint64_t offset) override;
            void SetIndexBuffer(const Buffer& buffer, IndexType type, std::uint64_t offset) override;
            void SetBindingSet(std::uint32_t index, const BindingSet& set) override
            {
                RequireRecording();
                const auto* native = dynamic_cast<const VulkanBindingSet*>(&set);
                if (!native || native->DeviceState() != _device)
                    throw std::invalid_argument("Vulkan RHI: binding set belongs to another device.");
                native->ValidateResources();
                if (!_pipeline || _pipeline->IsDeferred() || index >= _pipeline->Desc().pipelineLayout.groups.size()
                    || set.Desc().layout->Desc() != _pipeline->Desc().pipelineLayout.groups[index])
                    throw std::invalid_argument("Vulkan RHI: binding set incompatible with pipeline.");
                auto snapshot = std::make_unique<VulkanBindingSet>(_device, native->Desc(),
                    _pipeline->BindingLayoutOwner(index));
                const auto descriptor = snapshot->Native(&_commandSlots->Descriptors());
                auto& vk = *_device->ContextPointer->_impl;
                vk.vkCmdBindDescriptorSets(_commandSlots->Buffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _pipeline->Layout(), index, 1, &descriptor, 0, nullptr);
                _boundSceneSets.fill(VK_NULL_HANDLE); _boundSceneLayout = {};
                _fastSetsBound = false;
                _genericSets[index] = {std::move(snapshot), descriptor, _bindingGeneration};
            }
            void SetStencilReference(std::uint32_t reference) override
            {
                RequireRecording();
                _stencilReference = reference;
                _dynamicDirty = true;
            }
            void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount,
                std::uint32_t firstVertex, std::uint32_t firstInstance) override;
            void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
                std::int32_t vertexOffset, std::uint32_t firstInstance) override;
            void CopyBuffer(const Buffer& source, std::uint64_t sourceOffset,
                Buffer& destination, std::uint64_t destinationOffset, std::uint64_t size) override;
            void CopyBufferToTexture(
                const Buffer& source, Texture& destination, const BufferTextureCopy& region) override;
            void CopyTextureToBuffer(
                const Texture& source, Buffer& destination, const BufferTextureCopy& region) override;
            void Transition(Buffer& resource, ResourceState before, ResourceState after) override;
            void Transition(Texture& resource, ResourceState before, ResourceState after) override;
            void BindSampledTexture(std::uint32_t slot, const Texture* texture, const Sampler* sampler) override;
            void ReadColor(const RenderingInfo& info, std::uint32_t x, std::uint32_t y,
                std::uint32_t width, std::uint32_t height, TextureFormat format, void* destination) override;
            void CopyColorAttachmentToTexture(Texture& destination, std::uint32_t width, std::uint32_t height) override;
            [[nodiscard]] bool SaveAttachmentDepth() override { return CopyAttachmentDepth(true); }
            [[nodiscard]] bool RestoreAttachmentDepth() override { return CopyAttachmentDepth(false); }

            // The scene's draws. A stream with a zero stride is one value for
            // every vertex: OpenGL's current attribute where a mesh has no array.
            struct SceneStream final
            {
                VkBuffer Buffer = VK_NULL_HANDLE;
                VkDeviceSize Offset = 0;
                VkDeviceSize Stride = 0;
            };
            struct SceneDraw final
            {
                std::array<SceneStream, 5> Streams{}; // position, normal, colour, texcoord, texcoord1
                VkBuffer IndexBuffer = VK_NULL_HANDLE;
                VkDeviceSize IndexOffset = 0;
                std::uint32_t IndexCount = 0;
                bool Lines = false;
                // Matrix-stack entries the draw's vertices index.
                std::uint32_t Matrices = static_cast<std::uint32_t>(MatrixStackCapacity);
            };
            using RingSlice = VulkanUploadArena::Slice;
            void DrawScene(const SceneDraw& draw);
            // WriteTexture at this point in the recorded stream: the texels
            // staged in this slot's ring, copied between the draws around it,
            // as a glTexImage2D between two draws takes effect.
            void UploadTexture(VulkanTexture& texture, std::span<const std::byte> texels);
            void UploadBuffer(VulkanBuffer& buffer, VkDeviceSize offset, std::span<const std::byte> bytes);
            [[nodiscard]] RingSlice Allocate(VkDeviceSize size, VkDeviceSize alignment = 16);
            [[nodiscard]] VkDeviceSize PendingUploadBytes() const noexcept { return _commandSlots->Uploads().UsedBytes(); }
            // Submit recorded work; wait only when reusing a pending slot.
            // Recording resumes on the next command.
            void Flush();
            void PrepareHostReadback(VulkanBuffer&);
            bool PollComplete();
            bool SupportsAsyncReadback() const noexcept override { return true; }
            ReadbackTicket EnqueueReadColor(const RenderingInfo&, std::uint32_t, std::uint32_t,
                std::uint32_t, std::uint32_t, TextureFormat) override;
            void Forget(const void* object);

        private:
            struct Target final
            {
                VulkanTexture* Color = nullptr;
                VulkanTexture* Depth = nullptr;
                VkImageView ColorView = VK_NULL_HANDLE;
                VkImageView DepthView = VK_NULL_HANDLE;
                std::uint32_t Width = 0;
                std::uint32_t Height = 0;
                Scissor Area{};
                bool Clear = false;
                bool ClearDepth = false;
                bool ClearStencil = false;
                ClearColor ClearValue{};
                float DepthValue = 1.0F;
                std::uint32_t StencilValue = 0;
            };
            struct VariantKey final
            {
                const GraphicsPipeline* Base = nullptr;
                const VulkanSceneProgram* Program = nullptr;
                TextureFormat Color = TextureFormat::Undefined;
                TextureFormat Depth = TextureFormat::Undefined;
                bool Lines = false;
                // The draw's stream strides, on the legacy path only: it has
                // no dynamic stride, so they are the pipeline's. Zero otherwise.
                std::array<std::uint32_t, 5> Strides{};
                bool operator==(const VariantKey&) const = default;
            };
            struct VariantHash final
            {
                std::size_t operator()(const VariantKey& key) const noexcept
                {
                    std::size_t h = std::hash<const void*>{}(key.Base);
                    h ^= std::hash<const void*>{}(key.Program) + 0x9e3779b9U + (h << 6) + (h >> 2);
                    h ^= (static_cast<std::size_t>(key.Color) << 1) ^ (static_cast<std::size_t>(key.Depth) << 9)
                        ^ (key.Lines ? 0x10000U : 0U);
                    for (const auto stride : key.Strides) h ^= stride + 0x9e3779b9U + (h << 6) + (h >> 2);
                    return h;
                }
            };
            struct Variant final
            {
                GraphicsPipelineDesc BaseDesc;
                std::shared_ptr<VulkanGraphicsPipeline> Native;
            };
            void RequireRecording();
            void BeginBuffer();
            // Wait for both slots' submitted work; their allocations are free.
            void WaitAll();
            void Materialize();
            void EndNative();
            void CloseRendering();
            void PrepareTransfer();
            void CopyRgbBufferTexture(VkBuffer, VkDeviceSize, const VulkanTexture&, const BufferTextureCopy&, bool upload);
            void CopyPhysicalTextureToBuffer(const VulkanTexture&, const VulkanBuffer&, const BufferTextureCopy&);
            void Barrier(VulkanTexture& texture, ResourceState after);
            void ApplyDynamicState();
            void RestoreGenericBindings();
            [[nodiscard]] VulkanGraphicsPipeline& VariantFor(const VulkanSceneProgram& program, const SceneDraw& draw);
            [[nodiscard]] VkDescriptorSet AllocateSet(const VulkanBindingLayout& layout);
            void EnsureWindowTargets(std::uint32_t width, std::uint32_t height);
            [[nodiscard]] VulkanTexture& Dummy();

            std::shared_ptr<VulkanDeviceState> _device;
            VulkanNativeRegistration _registration;
            bool _closed = false;
            bool _transferOnly = false;
            std::unique_ptr<VulkanCommandSlots> _commandSlots;
            bool _recording = false;
            std::vector<DebugLabel> _debugLabels;
            void NativeBeginLabel(const DebugLabel& label);
            VulkanTimestampSet& CheckedTimestamp(TimestampQuerySet& set);
            bool _autoRestart = false;
            const VulkanGraphicsPipeline* _pipeline = nullptr;
            // The scene program _pipeline's fragment shader names, resolved
            // once in SetPipeline instead of a dynamic_cast every draw.
            bool _pipelineNamesProgram = false;
            VulkanSceneProgram* _pipelineProgram = nullptr;

            bool _renderingOpen = false;
            bool _renderingActive = false;
            bool _clearsPending = false;
            bool _materializing = false;
            Target _target{};
            Viewport _viewport{};
            bool _hasViewport = false;
            Scissor _scissor{};
            bool _scissorEnabled = false;
            std::uint32_t _stencilReference = 0;
            bool _dynamicDirty = true;
            VkPipeline _boundNative = VK_NULL_HANDLE;
            const VulkanGraphicsPipeline* _smallPipeline = nullptr;
            SmallDrawConstants _smallConstants{};
            bool _smallCurrent = false;
            struct VertexBinding final
            {
                const VulkanBuffer* Buffer = nullptr;
                std::weak_ptr<void> Lifetime;
                VkDeviceSize Offset = 0;
            };
            struct SavedSet final
            {
                std::unique_ptr<VulkanBindingSet> Snapshot;
                VkDescriptorSet Native = VK_NULL_HANDLE;
                std::uint64_t Generation = 0;
            };
            std::map<std::uint32_t, VertexBinding> _vertexBindings;
            VertexBinding _indexBinding{};
            IndexType _indexType = IndexType::UInt32;
            std::map<std::uint32_t, SavedSet> _genericSets;
            std::uint64_t _bindingGeneration = 0;
            bool _genericDirty = true;

            std::array<std::pair<VulkanTexture*, const VulkanSampler*>, 4> _units{};
            std::unordered_map<VariantKey, Variant, VariantHash> _variants{};
            // The last few variants found. A pipeline's desc never changes and a
            // destroyed one is forgotten (Forget), so a hit needs neither the hash
            // nor the desc comparison. Draws alternate between a few pass pipelines.
            struct RecentVariant final { VariantKey Key{}; VulkanGraphicsPipeline* Native = nullptr; };
            std::array<RecentVariant, 4> _recentVariants{};
            std::size_t _recentVariantNext = 0;
            void RememberVariant(const VariantKey& key, VulkanGraphicsPipeline* native)
            {
                _recentVariants[_recentVariantNext] = {key, native};
                _recentVariantNext = (_recentVariantNext + 1) % _recentVariants.size();
            }

            struct SampledDescriptor final
            {
                VkImageView View = VK_NULL_HANDLE;
                VkSampler Sampler = VK_NULL_HANDLE;
                VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
                std::uint64_t Identity = 0, Generation = 0, SamplerIdentity = 0;
                bool operator==(const SampledDescriptor&) const = default;
            };
            using ProgramUniformState = VulkanSceneBindingState<RingSlice, VkDescriptorSet, SampledDescriptor>;
            std::array<ProgramUniformState, static_cast<std::size_t>(SceneProgram::Backdrop) + 1> _programUniforms{};
            VulkanSceneDescriptorState<VkDescriptorSet, SampledDescriptor> _sceneDescriptors;
            std::uint64_t _descriptorEpoch = 1;
            std::array<VkDescriptorSet, SceneShaderAbi::GroupCount> _boundSceneSets{};
            std::array<DynamicOffsets, SceneShaderAbi::GroupCount> _boundOffsets{};
            const PipelineLayout* _boundSceneLayout = nullptr;
            std::uint32_t _boundSmallBytes = 0;
            std::array<std::pair<const void*, std::uint64_t>, static_cast<std::size_t>(SceneProgram::Backdrop) + 1> _programIdentities{};
            std::uint64_t _metricFixedSlots = 0, _metricFixedOverflow = 0;
            std::uint64_t _metricOptionalSkips = 0;
            struct PersistentMaterial final
            {
                struct Slot final
                {
                    VkDescriptorSet Set = VK_NULL_HANDLE;
                    std::uint64_t Generation = 0, Recording = 0;
                    std::array<SampledDescriptor, 4> Textures{};
                };
                VulkanResources::BufferStorage Uniform{};
                std::uint64_t ProgramIdentity = 0;
                VkDeviceSize Stride = 0;
                std::array<Slot, FramesInFlight> Slots{};
            };
            static constexpr std::size_t MaterialsPerPage = 64, MaterialCapacity = 4096;
            using PersistentMaterialPage = std::array<PersistentMaterial, MaterialsPerPage>;
            std::vector<std::unique_ptr<PersistentMaterialPage>> _persistentMaterials;
            std::size_t _persistentMaterialCount = 0;
            std::shared_ptr<VulkanDescriptorAllocator> _persistentDescriptors;
            std::uint64_t _metricPersistentHits = 0, _metricPersistentMisses = 0, _metricPersistentFull = 0;
            VkDescriptorSet PersistentMaterialSet(const VulkanSceneProgram&, const std::array<SampledDescriptor, 4>&);
            void ClearPersistentMaterials();

            // Performance mode's scene path (DrawSceneFast). Each command slot
            // has one host-visible ring of records -- Frame, Light, Fog,
            // Material and Draw blocks -- appended to while the slot records
            // and reset when it records again, after its fence. A block is
            // copied only when its generation moved since the last record of
            // its kind; a draw binds them by dynamic offset.
            static constexpr std::size_t FastRecordKinds = 5;
            struct FastSlot final
            {
                VulkanResources::BufferStorage Storage{};
                const VulkanSceneProgram* Program = nullptr;
                std::uint32_t Scale = 1;
                // Per kind: where its region starts, how long it is, the block
                // size a descriptor reads, and the next record's offset.
                std::array<VkDeviceSize, FastRecordKinds> Base{}, Size{}, Range{}, Cursor{};
                VkDeviceSize Written = 0;
                VkDescriptorSet Records = VK_NULL_HANDLE;
                std::uint64_t Recording = 0;
                bool Overflowed = false;
                std::array<const void*, FastRecordKinds> LastBlock{};
                std::array<std::uint64_t, FastRecordKinds> LastGeneration{};
                std::array<std::uint32_t, FastRecordKinds> LastOffset{};
                std::uint32_t LastMatrixCount = 0;
            };
            struct FastTextureKey final
            {
                VkImageView View = VK_NULL_HANDLE;
                VkSampler Sampler = VK_NULL_HANDLE;
                VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
                std::uint64_t Identity = 0, Generation = 0, SamplerIdentity = 0;
                bool operator==(const FastTextureKey&) const = default;
            };
            struct FastTextureHash final
            {
                std::size_t operator()(const FastTextureKey& key) const noexcept
                {
                    std::size_t h = std::hash<VkImageView>{}(key.View);
                    const auto mix = [&h](std::size_t value) { h ^= value + 0x9e3779b9U + (h << 6) + (h >> 2); };
                    mix(std::hash<VkSampler>{}(key.Sampler));
                    for (const std::uint64_t value : {static_cast<std::uint64_t>(key.Layout), key.Identity,
                        key.Generation, key.SamplerIdentity})
                        mix(std::hash<std::uint64_t>{}(value));
                    return h;
                }
            };
            [[nodiscard]] bool DrawSceneFast(VulkanSceneProgram& program, const SceneDraw& draw,
                const std::array<SampledDescriptor, 4>& textures);
            [[nodiscard]] bool BeginFastSlot(FastSlot& slot, const VulkanSceneProgram& program);
            [[nodiscard]] bool AppendFast(FastSlot& slot, std::size_t kind, const void* data, VkDeviceSize bytes,
                std::uint32_t& offset);
            [[nodiscard]] VkDescriptorSet FastTextureSet(const VulkanSceneProgram& program,
                const std::array<SampledDescriptor, 4>& textures);
            [[nodiscard]] VulkanGraphicsPipeline& FastVariantFor(const VulkanSceneProgram& program, const SceneDraw& draw);
            [[nodiscard]] GraphicsPipelineDesc SceneVariantDesc(const VulkanSceneProgram& program, const VariantKey& key) const;
            void FlushFast();
            void ClearFastTextures();
            void CloseFast() noexcept;
            std::array<FastSlot, FramesInFlight> _fastSlots{};
            std::shared_ptr<VulkanDescriptorAllocator> _fastRecordSets, _fastTextures;
            std::unordered_map<FastTextureKey, VkDescriptorSet, FastTextureHash> _fastTextureSets;
            std::unordered_map<VariantKey, Variant, VariantHash> _fastVariants;
            std::array<RecentVariant, 4> _fastRecent{};
            std::size_t _fastRecentNext = 0;
            // What DrawSceneFast last bound; anything else binding descriptor
            // sets clears _fastSetsBound.
            bool _fastSetsBound = false;
            VkDescriptorSet _fastBoundRecords = VK_NULL_HANDLE, _fastBoundTexture = VK_NULL_HANDLE;
            std::array<std::uint32_t, FastRecordKinds> _fastBoundOffsets{};
            std::array<std::byte, 128> _fastPushed{};
            std::uint64_t _metricFastDraws = 0;
            // Opt-in instrumentation: no per-draw clocks or allocations.
            bool _metricsEnabled = std::getenv("FRUITY_RENDER_METRICS") != nullptr;
            std::uint64_t _metricDraws = 0, _metricAllocations = 0, _metricUpdates = 0,
                _metricBinds = 0, _metricPipelines = 0, _metricHits = 0, _metricMisses = 0,
                _metricSmallConstants = 0;

            std::unique_ptr<VulkanTexture> _dummy;
            std::unique_ptr<VulkanSampler> _dummySampler;
            // SaveAttachmentDepth's copy: grown, never shrunk, with the target.
            std::unique_ptr<VulkanTexture> _savedDepth;
            bool CopyAttachmentDepth(bool save);
        };

        VulkanCommandList::VulkanCommandList(std::shared_ptr<VulkanDeviceState> state, bool transferOnly)
            : _device(std::move(state)), _registration(*_device, *this, true), _transferOnly(transferOnly)
        {
            auto& vk = *_device->ContextPointer->_impl;
            try
            {
                const VulkanDescriptorAllocator::Capacity capacity{2048, {2048, 0, 4096, 0, 4096}};
                const auto makeUploads = [&] {
                    return std::make_unique<VulkanUploadArena>(VulkanUploadArena::Dispatch{
                        [device = _device.get()](VkDeviceSize size) {
                            const auto storage = device->Resources->CreateBuffer({size,
                                BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Uniform | BufferUsage::TransferSrc,
                                MemoryUsage::CpuToGpu});
                            VulkanUploadArena::Page page{storage.Buffer, storage.Allocation, storage.Mapped, size};
                            ++device->UploadPages;
                            ++device->UploadPageCreations;
                            return page;
                        },
                        [device = _device.get()](const VulkanUploadArena::Page& page) {
                            vmaDestroyBuffer(device->Allocator, page.Buffer, page.Allocation);
                            --device->UploadPages;
                        },
                        [device = _device.get()](const VulkanUploadArena::Page& page, VkDeviceSize offset, VkDeviceSize size) {
                            Check(vmaFlushAllocation(device->Allocator, page.Allocation, offset, size),
                                "vmaFlushAllocation(upload arena)");
                        }});
                };
                _commandSlots = std::make_unique<VulkanCommandSlots>(VulkanCommandSlots::Dispatch{
                    vk.device, vk.graphicsFamily, vk.vkCreateCommandPool, vk.vkDestroyCommandPool, vk.vkAllocateCommandBuffers,
                    vk.vkCreateFence, vk.vkDestroyFence, vk.vkGetFenceStatus, vk.vkWaitForFences, vk.vkResetFences,
                    vk.vkResetCommandPool, vk.vkBeginCommandBuffer, vk.vkEndCommandBuffer,
                    [device = _device.get()](const VkSubmitInfo2& work, VkFence fence) { return device->Scheduler->Submit(work, fence); },
                    [device = _device.get()] { return device->Scheduler->Poll(); },
                    [device = _device.get()] { ++device->HostWaits; },
                    [device = _device.get()] { device->CollectRetired(); },
                    [device = _device.get()](VkCommandBuffer buffer) {
                        device->ContextPointer->_impl->Name(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<std::uint64_t>(buffer),
                            "RHI resource command buffer");
                    }, makeUploads, [device = _device.get(), capacity] { return device->MakeDescriptors(capacity); },
                    [device = _device.get()] {
                        return std::make_unique<VulkanTransferScratch>(VulkanTransferScratch::Dispatch{
                            [device](VkDeviceSize size) {
                                const auto storage = device->Resources->CreateBuffer({size,
                                    BufferUsage::TransferSrc | BufferUsage::TransferDst, MemoryUsage::GpuOnly});
                                ++device->ScratchPages;
                                return VulkanTransferScratch::Page{storage.Buffer, storage.Allocation, size};
                            }, [device](const VulkanTransferScratch::Page& page) {
                                vmaDestroyBuffer(device->Allocator, page.Buffer, page.Allocation); --device->ScratchPages;
                            }});
                    }});
                _device->SceneForgetters[this] = [this](const void* object) { Forget(object); };
                _device->SceneViewReplacers[this] = [this](VkImageView before, VkImageView after) {
                    ClearPersistentMaterials();
                    ++_descriptorEpoch;
                    for (auto& state : _programUniforms) { state.Sets = {}; state.Textures = {}; } _boundSceneSets.fill(VK_NULL_HANDLE);
                    for (auto& [slot, set] : _genericSets) set.Generation = 0;
                    _genericDirty = true;
                    const bool matches = _target.ColorView == before || _target.DepthView == before;
                    if (!matches) return;
                    if (!after)
                    {
                        _renderingOpen = false;
                        _clearsPending = false;
                        _target = {};
                    }
                    else
                    {
                        if (_target.ColorView == before) _target.ColorView = after;
                        if (_target.DepthView == before) _target.DepthView = after;
                    }
                };
                if (_transferOnly) ++_device->UploadCommandLists;
                else ++_device->CommandLists;
            }
            catch (...)
            {
                _device->SceneForgetters.erase(this);
                _device->SceneViewReplacers.erase(this);
                _commandSlots.reset();
                throw;
            }
        }

        VulkanCommandList::~VulkanCommandList() { CloseNative(); }
        void VulkanCommandList::CloseNative() noexcept
        {
            if (_closed) return;
            _device->SceneFlushers.erase(this);
            _device->SceneForgetters.erase(this);
            _device->SceneViewReplacers.erase(this);
            if (_device->RecordingList == this) _device->RecordingList = nullptr;
            if (_device->ContextPointer == nullptr) return;
            auto& vk = *_device->ContextPointer->_impl;
            if (vk.device == VK_NULL_HANDLE) return;
            if (_recording && !_device->Closing)
            {
                try { Flush(); } catch (...) {}
            }
            if (!_device->Closing) { try { WaitAll(); } catch (...) {} }
            ClearPersistentMaterials();
            CloseFast();
            _variants.clear(); _recentVariants = {};
            _genericSets.clear(); _vertexBindings.clear(); _indexBinding = {};
            _dummy.reset();
            _savedDepth.reset();
            if (_transferOnly) --_device->UploadCommandLists;
            else if (--_device->CommandLists == 0) _device->ReleaseWindowTarget();
            _dummySampler.reset();
            if (_commandSlots) _commandSlots->Close(_device->Closing);
            _commandSlots.reset();
            _recording = _autoRestart = _renderingActive = _renderingOpen = _clearsPending = false;
            _target = {}; _pipeline = nullptr; _units = {}; _programUniforms = {};
            _closed = true;
        }

        void VulkanCommandList::WaitAll()
        {
            _commandSlots->WaitAll();
        }

        void VulkanCommandList::BeginBuffer()
        {
            _device->RequireAlive();
            // One list holds unsubmitted work at a time: whatever another
            // list recorded goes to the queue before this one records, so
            // the queue sees lists in the order they drew.
            _device->FlushScene(this);
            _commandSlots->Begin();
            _recording = true;
            ++_bindingGeneration;
            _genericDirty = true;
            _dynamicDirty = true;
            _boundNative = VK_NULL_HANDLE;
            _smallPipeline = nullptr;
            _smallCurrent = false;
            for (auto& state : _programUniforms) state.Sets = {};
            _device->SceneFlushers[this] = [this] { Flush(); };
            _boundSceneSets.fill(VK_NULL_HANDLE); _boundSceneLayout = {}; _boundSmallBytes = 0;
            _fastSetsBound = false;
            _device->RecordingList = this;
            for (const auto& label : _debugLabels) NativeBeginLabel(label);
        }

        void VulkanCommandList::Begin()
        {
            _device->RequireAlive();
            // Native buffers may have been submitted by another list, a frame
            // boundary or readback. That does not end the caller's Begin/End.
            if (_autoRestart || _recording) throw std::logic_error("Vulkan RHI: command list is already recording.");
            BeginBuffer();
            _autoRestart = true;
            _pipeline = nullptr;
            _genericSets.clear(); _vertexBindings.clear(); _indexBinding = {};
        }

        void VulkanCommandList::End()
        {
            _device->RequireAlive();
            if (!_autoRestart) throw std::logic_error("Vulkan RHI: command list is not recording.");
            if (!_debugLabels.empty()) throw std::logic_error("GPU debug label scope was not ended.");
            _device->FlushScene(this);
            Flush();
            WaitAll();
            _autoRestart = false;
            _renderingOpen = false;
        }

        void VulkanCommandList::Flush()
        {
            if (!_recording) return;
            auto& vk = *_device->ContextPointer->_impl;
            if (_renderingOpen && _clearsPending) Materialize();
            if (_renderingActive) EndNative();
            if (vk.endLabel) for (std::size_t i = 0; i < _debugLabels.size(); ++i) vk.endLabel(_commandSlots->Buffer());
            FlushFast();
            _commandSlots->End();
            _recording = false;
            _device->SceneFlushers.erase(this);
            if (_device->RecordingList == this) _device->RecordingList = nullptr;
            _commandSlots->Submit();
            if (_metricsEnabled)
                std::cout << "[vulkan-metrics] draws=" << _metricDraws
                    << " scene_descriptor_allocations=" << _metricAllocations
                    << " scene_descriptor_updates=" << _metricUpdates
                    << " scene_descriptor_bind_commands=" << _metricBinds
                    << " scene_descriptor_cache_hits=" << _metricHits
                    << " scene_descriptor_cache_misses=" << _metricMisses
                    << " scene_pipeline_bind_commands=" << _metricPipelines
                    << " small_constant_updates=" << _metricSmallConstants
                    << " fixed_descriptor_slots=" << _metricFixedSlots
                    << " fixed_descriptor_overflows=" << _metricFixedOverflow
                    << " persistent_material_hits=" << _metricPersistentHits
                    << " persistent_material_misses=" << _metricPersistentMisses
                    << " persistent_material_full=" << _metricPersistentFull
                    << " optional_work_busy_skip_count=" << _metricOptionalSkips
                    << " fast_scene_draws=" << _metricFastDraws << '\n';
            for (auto& state : _programUniforms) state.Sets = {};
            _boundNative = VK_NULL_HANDLE;
        }

        void VulkanCommandList::RequireRecording()
        {
            _device->RequireAlive();
            if (_recording) return;
            if (!_autoRestart) throw std::logic_error("Vulkan RHI: command list is not recording.");
            BeginBuffer();
        }

        void VulkanCommandList::NativeBeginLabel(const DebugLabel& label)
        {
            auto& vk = *_device->ContextPointer->_impl;
            if (!vk.caps.supportsDebugLabels) return;
            VkDebugUtilsLabelEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
            info.pLabelName = label.name.c_str(); std::copy(label.color.begin(), label.color.end(), info.color);
            vk.beginLabel(_commandSlots->Buffer(), &info);
        }
        void VulkanCommandList::BeginDebugLabel(const DebugLabel& label)
        {
            ValidateDebugLabel(label); RequireRecording();
            if (!_device->ContextPointer->Caps().supportsDebugLabels) return;
            if (_debugLabels.size() == 32) throw std::logic_error("GPU debug label nesting exceeds 32.");
            _debugLabels.push_back(label); NativeBeginLabel(_debugLabels.back());
        }
        void VulkanCommandList::EndDebugLabel()
        {
            RequireRecording();
            if (!_device->ContextPointer->Caps().supportsDebugLabels) return;
            if (_debugLabels.empty()) throw std::logic_error("No GPU debug label to end.");
            _device->ContextPointer->_impl->endLabel(_commandSlots->Buffer()); _debugLabels.pop_back();
        }
        void VulkanCommandList::InsertDebugMarker(const DebugLabel& label)
        {
            ValidateDebugLabel(label); RequireRecording();
            auto& vk = *_device->ContextPointer->_impl;
            if (!vk.caps.supportsDebugLabels) return;
            VkDebugUtilsLabelEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
            info.pLabelName = label.name.c_str(); std::copy(label.color.begin(), label.color.end(), info.color);
            vk.insertLabel(_commandSlots->Buffer(), &info);
        }
        VulkanTimestampSet& VulkanCommandList::CheckedTimestamp(TimestampQuerySet& set)
        {
            auto* native = dynamic_cast<VulkanTimestampSet*>(&set);
            if (!native || native->Device != _device || !native->Pool)
                throw std::invalid_argument("Vulkan timestamp belongs to another or closed session.");
            return *native;
        }
        void VulkanCommandList::InitializeTimestamps(TimestampQuerySet& set)
        {
            RequireRecording();
            if (_renderingOpen) throw std::logic_error("Initialize GPU timestamps outside rendering.");
            auto& native = CheckedTimestamp(set); native.Writes.Initialize();
            auto& vk = *_device->ContextPointer->_impl;
            vk.vkCmdResetQueryPool(_commandSlots->Buffer(), native.Pool, 0, native.Count());
        }
        void VulkanCommandList::WriteTimestamp(TimestampQuerySet& set, std::uint32_t index)
        {
            RequireRecording(); auto& native = CheckedTimestamp(set); native.Writes.Write(index);
            _device->ContextPointer->_impl->vkCmdWriteTimestamp2(_commandSlots->Buffer(),
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, native.Pool, index);
        }

        void VulkanCommandList::Forget(const void* object)
        {
            ++_descriptorEpoch;
            ClearPersistentMaterials();
            for (auto& identity : _programIdentities)
            {
                if (identity.first != object) continue;
                for (std::uint32_t group = 0; group < SceneShaderAbi::GroupCount; ++group)
                    _commandSlots->RetireDescriptors((identity.second << 3) | group);
                identity = {};
            }
            _programUniforms = {}; _boundSceneSets.fill(VK_NULL_HANDLE);
            _boundSceneLayout = nullptr;
            _smallPipeline = nullptr; _smallCurrent = false;
            for (auto& unit : _units)
                if (unit.first == object || unit.second == object) unit = {};
            if (_target.Color == object || _target.Depth == object)
            {
                if (_renderingActive) EndNative();
                _renderingOpen = false;
                _clearsPending = false;
                _target = {};
            }
            for (auto it = _variants.begin(); it != _variants.end();)
                it = it->first.Program == object || it->first.Base == object ? _variants.erase(it) : std::next(it);
            _recentVariants = {};
            for (auto it = _fastVariants.begin(); it != _fastVariants.end();)
                it = it->first.Program == object || it->first.Base == object ? _fastVariants.erase(it) : std::next(it);
            _fastRecent = {};
            ClearFastTextures();
            for (auto& slot : _fastSlots)
                if (slot.Program == object) slot.Recording = 0, slot.Program = nullptr;
            if (_pipeline == object) { _pipeline = nullptr; _genericSets.clear(); }
        }

        void VulkanCommandList::Barrier(VulkanTexture& texture, ResourceState after)
        {
            if (texture.State() == after) return;
            Transition(texture, texture.State(), after);
        }

        void VulkanCommandList::EnsureWindowTargets(std::uint32_t width, std::uint32_t height)
        {
            auto& d = *_device;
            if (d.WindowColor && d.WindowColor->Desc().width == width && d.WindowColor->Desc().height == height) return;
            if (_renderingActive) EndNative();
            d.WindowColorView.reset();
            d.WindowDepthView.reset();
            d.WindowColor.reset();
            d.WindowDepth.reset();
            TextureDesc color{};
            color.width = width;
            color.height = height;
            color.format = TextureFormat::RGBA8Unorm;
            color.usage = TextureUsage::ColorAttachment | TextureUsage::TransferSrc | TextureUsage::Sampled;
            d.WindowColor = std::make_unique<VulkanTexture>(_device, color, TextureHandle{});
            d.WindowColorView = std::make_unique<VulkanTextureView>(*d.WindowColor, TextureViewDesc{});
            TextureDesc depth = color;
            depth.format = d.ContextPointer->Caps().depthStencilFormat;
            depth.usage = TextureUsage::DepthStencilAttachment;
            d.WindowDepth = std::make_unique<VulkanTexture>(_device, depth, TextureHandle{});
            d.WindowDepthView = std::make_unique<VulkanTextureView>(*d.WindowDepth, TextureViewDesc{});
        }

        void VulkanCommandList::BeginRendering(const RenderingInfo& info)
        {
            RequireRecording();
            CloseRendering();
            Target target{};
            target.Width = info.width;
            target.Height = info.height;
            target.Area = info.renderArea;
            const RenderingColorAttachment* color = info.colorAttachments.empty() ? nullptr : &info.colorAttachments[0];
            const RenderingDepthStencilAttachment* depth = info.depthStencilAttachment;
            if (info.swapchain)
            {
                EnsureWindowTargets(info.width, info.height);
                target.Color = _device->WindowColor.get();
                target.ColorView = _device->WindowColorView->Native();
                if (depth)
                {
                    target.Depth = _device->WindowDepth.get();
                    target.DepthView = _device->WindowDepthView->Native();
                }
            }
            else
            {
                if (color && color->view)
                {
                    const auto& view = CheckedResource<VulkanTextureView>(*color->view, _device);
                    if (view.DeviceState() != _device) throw std::invalid_argument("Vulkan RHI: color view belongs to another session.");
                    target.Color = const_cast<VulkanTexture*>(static_cast<const VulkanTexture*>(&view.TextureResource()));
                    target.ColorView = view.Native();
                }
                if (depth && depth->view)
                {
                    const auto& view = CheckedResource<VulkanTextureView>(*depth->view, _device);
                    if (view.DeviceState() != _device) throw std::invalid_argument("Vulkan RHI: depth view belongs to another session.");
                    target.Depth = const_cast<VulkanTexture*>(static_cast<const VulkanTexture*>(&view.TextureResource()));
                    target.DepthView = view.Native();
                }
            }
            // DontCare keeps the contents, as OpenGL does: only Clear clears.
            target.Clear = color && target.Color && color->loadOp == LoadOp::Clear;
            if (color) target.ClearValue = color->clearValue;
            if (depth && target.Depth)
            {
                target.ClearDepth = depth->depthLoadOp == LoadOp::Clear;
                target.ClearStencil = depth->stencilLoadOp == LoadOp::Clear;
                target.DepthValue = depth->clearDepth;
                target.StencilValue = depth->clearStencil;
            }
            _target = target;
            _renderingOpen = true;
            _clearsPending = target.Clear || target.ClearDepth || target.ClearStencil;
            _scissorEnabled = info.renderArea.width > 0 && info.renderArea.height > 0;
            if (_scissorEnabled) _scissor = info.renderArea;
            _dynamicDirty = true;
        }

        void VulkanCommandList::CloseRendering()
        {
            if (_renderingOpen && _clearsPending) Materialize();
            if (_renderingActive) EndNative();
            _renderingOpen = false;
        }

        void VulkanCommandList::EndRendering()
        {
            _device->RequireAlive();
            if (!_autoRestart) throw std::logic_error("Vulkan RHI: command list is not recording.");
            if (!_recording && !_renderingOpen) return;
            CloseRendering();
        }

        void VulkanCommandList::PrepareTransfer()
        {
            // Close the native rendering instance, keeping its logical target
            // and contents for the next draw. Clear exactly once before copy.
            if (!_materializing && _renderingOpen && _clearsPending) Materialize();
            if (_renderingActive) EndNative();
        }

        void VulkanCommandList::Materialize()
        {
            RequireRecording();
            struct Guard final
            {
                bool& Active;
                explicit Guard(bool& active) : Active(active) { Active = true; }
                ~Guard() { Active = false; }
            } guard(_materializing);
            auto& vk = *_device->ContextPointer->_impl;
            if (_target.Color) Barrier(*_target.Color, ResourceState::ColorAttachment);
            if (_target.Depth) Barrier(*_target.Depth, ResourceState::DepthStencilWrite);
            const bool first = _clearsPending;
            VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            color.imageView = _target.ColorView;
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = first && _target.Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            const bool rgb = _target.Color && IsRgbTextureFormat(_target.Color->Desc().format);
            color.clearValue.color = {{_target.ClearValue.red, _target.ClearValue.green,
                _target.ClearValue.blue, rgb ? 1.0F : _target.ClearValue.alpha}};
            VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            depth.imageView = _target.DepthView;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth.loadOp = first && _target.ClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth.clearValue.depthStencil = {_target.DepthValue, _target.StencilValue};
            VkRenderingAttachmentInfo stencil = depth;
            stencil.loadOp = first && _target.ClearStencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            const bool hasStencil = _target.Depth && (VulkanResources::AspectMask(_target.Depth->Desc().format) & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
            std::uint32_t width = _target.Width;
            std::uint32_t height = _target.Height;
            for (const VulkanTexture* texture : {_target.Color, _target.Depth})
                if (texture)
                {
                    width = std::min(width, texture->Desc().width);
                    height = std::min(height, texture->Desc().height);
                }
            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            if (_target.Area.width > 0 && _target.Area.height > 0)
            {
                const auto x = std::clamp<std::int32_t>(_target.Area.x, 0, static_cast<std::int32_t>(width));
                const auto y = std::clamp<std::int32_t>(_target.Area.y, 0, static_cast<std::int32_t>(height));
                rendering.renderArea = {{x, y}, {std::min(_target.Area.width, width - static_cast<std::uint32_t>(x)),
                    std::min(_target.Area.height, height - static_cast<std::uint32_t>(y))}};
            }
            else
                rendering.renderArea = {{0, 0}, {width, height}};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = _target.Color ? 1U : 0U;
            rendering.pColorAttachments = _target.Color ? &color : nullptr;
            rendering.pDepthAttachment = _target.Depth ? &depth : nullptr;
            rendering.pStencilAttachment = hasStencil ? &stencil : nullptr;
            vk.vkCmdBeginRendering(_commandSlots->Buffer(), &rendering);
            _renderingActive = true;
            _clearsPending = false;
            _dynamicDirty = true;
        }

        void VulkanCommandList::EndNative()
        {
            _device->ContextPointer->_impl->vkCmdEndRendering(_commandSlots->Buffer());
            _renderingActive = false;
        }

        void VulkanCommandList::SetPipeline(const GraphicsPipeline& pipeline)
        {
            _device->RequireAlive();
            const auto* native = dynamic_cast<const VulkanGraphicsPipeline*>(&pipeline);
            if (!native || native->DeviceState() != _device)
                throw std::invalid_argument("Vulkan RHI: pipeline belongs to another device.");
            RequireRecording();
            if (_boundNative != native->Native()) _genericDirty = true;
            for (auto it = _genericSets.begin(); it != _genericSets.end();)
                if (it->first >= native->Desc().pipelineLayout.groups.size()
                    || it->second.Snapshot->Desc().layout->Desc() != native->Desc().pipelineLayout.groups[it->first])
                    it = _genericSets.erase(it);
                else ++it;
            _pipeline = native;
            _pipelineNamesProgram = false;
            _pipelineProgram = nullptr;
            if (native->IsDeferred())
            {
                // glUseProgram: a pass naming no program leaves the last one current.
                if (const auto* fragment = dynamic_cast<const VulkanShader*>(native->Desc().fragmentShader))
                {
                    _device->CurrentSceneProgram = fragment->SceneProgram;
                    _pipelineNamesProgram = true;
                    _pipelineProgram = static_cast<VulkanSceneProgram*>(fragment->SceneProgram);
                }
                return;
            }
            auto& vk = *_device->ContextPointer->_impl;
            vk.vkCmdBindPipeline(_commandSlots->Buffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, native->Native());
            _boundNative = native->Native();
            _smallPipeline = native;
            _boundSceneSets.fill(VK_NULL_HANDLE); _boundSceneLayout = {};
        }

        void VulkanCommandList::SetSmallConstants(const SmallDrawConstants& constants)
        {
            RequireRecording();
            if (!_smallPipeline || _smallPipeline->Desc().smallConstantBytes != sizeof(constants)
                || constants.alphaTest < 0 || constants.alphaTest > 2)
                throw std::invalid_argument("Vulkan RHI: incompatible small draw constants.");
            auto& vk = *_device->ContextPointer->_impl;
            if (!_smallCurrent || std::memcmp(&_smallConstants, &constants, sizeof(constants)) != 0)
            {
                vk.vkCmdPushConstants(_commandSlots->Buffer(), _smallPipeline->Layout(),
                    VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(constants), &constants);
                _smallConstants = constants;
                _smallCurrent = true;
                _fastSetsBound = false;
                if (_metricsEnabled) ++_metricSmallConstants;
            }
        }

        void VulkanCommandList::RestoreGenericBindings()
        {
            // Validate borrowed identities before any native use. Retaining a
            // descriptor snapshot does not retain the underlying pixel/buffer
            // data or make a freed wrapper usable.
            for (const auto& input : _pipeline->Desc().vertexBuffers)
            {
                const auto found = _vertexBindings.find(input.slot);
                if (found == _vertexBindings.end() || found->second.Lifetime.expired())
                    throw std::logic_error("Vulkan RHI: draw needs a live vertex buffer.");
            }
            for (const auto& [slot, set] : _genericSets) set.Snapshot->ValidateResources();
            for (const auto& [slot, set] : _genericSets) set.Snapshot->ValidateSampledAccess();
            auto& vk = *_device->ContextPointer->_impl;
            const bool restore = _genericDirty || _boundNative != _pipeline->Native();
            if (restore)
            {
                _boundSceneSets.fill(VK_NULL_HANDLE); _boundSceneLayout = {};
                vk.vkCmdBindPipeline(_commandSlots->Buffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline->Native());
                _boundNative = _pipeline->Native();
                for (const auto& [slot, binding] : _vertexBindings)
                {
                    if (binding.Lifetime.expired()) continue;
                    const auto buffer = binding.Buffer->Native();
                    vk.vkCmdBindVertexBuffers2(_commandSlots->Buffer(), slot, 1, &buffer, &binding.Offset, nullptr, nullptr);
                }
                if (_indexBinding.Buffer && !_indexBinding.Lifetime.expired())
                    vk.vkCmdBindIndexBuffer(_commandSlots->Buffer(), _indexBinding.Buffer->Native(), _indexBinding.Offset,
                        _indexType == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            }
            for (auto& [slot, set] : _genericSets)
            {
                const bool fresh = set.Generation != _bindingGeneration || !set.Snapshot->TextureDescriptorsCurrent();
                if (fresh)
                {
                    // This command slot owns the descriptor pool and resets it
                    // only after submission completion. Never reuse a set from
                    // a previous native buffer/pool generation.
                    set.Native = set.Snapshot->Native(&_commandSlots->Descriptors());
                    set.Generation = _bindingGeneration;
                }
                if (restore || fresh)
                {
                    vk.vkCmdBindDescriptorSets(_commandSlots->Buffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                        _pipeline->Layout(), slot, 1, &set.Native, 0, nullptr);
                    _fastSetsBound = false;
                }
            }
            _genericDirty = false;
        }

        void VulkanCommandList::ApplyDynamicState()
        {
            if (!_dynamicDirty) return;
            auto& vk = *_device->ContextPointer->_impl;
            std::uint32_t width = _target.Width;
            std::uint32_t height = _target.Height;
            for (const VulkanTexture* texture : {_target.Color, _target.Depth})
                if (texture)
                {
                    width = std::min(width, texture->Desc().width);
                    height = std::min(height, texture->Desc().height);
                }
            VkViewport viewport{0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F};
            if (_hasViewport)
                viewport = {_viewport.x, _viewport.y, _viewport.width, _viewport.height,
                    _viewport.minDepth, _viewport.maxDepth};
            if (viewport.width <= 0.0F) viewport.width = 1.0F;
            if (viewport.height <= 0.0F) viewport.height = 1.0F;
            vk.vkCmdSetViewport(_commandSlots->Buffer(), 0, 1, &viewport);
            VkRect2D scissor{{0, 0}, {width, height}};
            if (_scissorEnabled)
            {
                const auto x = std::clamp<std::int32_t>(_scissor.x, 0, static_cast<std::int32_t>(width));
                const auto y = std::clamp<std::int32_t>(_scissor.y, 0, static_cast<std::int32_t>(height));
                scissor = {{x, y}, {std::min(_scissor.width, width - static_cast<std::uint32_t>(x)),
                    std::min(_scissor.height, height - static_cast<std::uint32_t>(y))}};
            }
            vk.vkCmdSetScissor(_commandSlots->Buffer(), 0, 1, &scissor);
            if (_pipeline && (_pipeline->IsDeferred()))
                vk.vkCmdSetStencilReference(_commandSlots->Buffer(), VK_STENCIL_FACE_FRONT_AND_BACK, _stencilReference);
            _dynamicDirty = false;
        }

        VulkanGraphicsPipeline& VulkanCommandList::VariantFor(const VulkanSceneProgram& program, const SceneDraw& draw)
        {
            const bool lines = draw.Lines;
            VariantKey key{_pipeline, &program,
                _target.Color ? _target.Color->Desc().format : TextureFormat::Undefined,
                _target.Depth ? _target.Depth->Desc().format : TextureFormat::Undefined, lines};
            if (_device->ContextPointer->_impl->legacy)
                for (std::size_t i = 0; i < key.Strides.size(); ++i)
                    key.Strides[i] = static_cast<std::uint32_t>(draw.Streams[i].Stride);
            for (const auto& recent : _recentVariants)
                if (recent.Native && recent.Key == key) return *recent.Native;
            const auto found = _variants.find(key);
            if (found != _variants.end() && found->second.BaseDesc == _pipeline->Desc())
            {
                RememberVariant(key, found->second.Native.get());
                return *found->second.Native;
            }
            const GraphicsPipelineDesc base = _pipeline->Desc();
            GraphicsPipelineDesc desc = SceneVariantDesc(program, key);
            auto native = std::make_shared<VulkanGraphicsPipeline>(_device, desc, true);
            auto& entry = _variants[key];
            if (entry.Native && _boundSceneLayout == &entry.Native->Desc().pipelineLayout)
            {
                _boundSceneLayout = nullptr;
                _boundSceneSets.fill(VK_NULL_HANDLE);
                _smallPipeline = nullptr;
                _smallCurrent = false;
            }
            entry.BaseDesc = base;
            entry.Native = std::move(native);
            // The entry's previous pipeline, if any, is gone: drop it from the recent ones.
            for (auto& recent : _recentVariants) if (recent.Key == key) recent = {};
            RememberVariant(key, entry.Native.get());
            return *entry.Native;
        }

        // The executable state of a scene draw: the pass pipeline's
        // fixed-function state with the program's modules and layout.
        GraphicsPipelineDesc VulkanCommandList::SceneVariantDesc(const VulkanSceneProgram& program,
            const VariantKey& key) const
        {
            const bool lines = key.Lines;
            GraphicsPipelineDesc desc = _pipeline->Desc();
            desc.vertexShader = program.Vertex.get();
            desc.fragmentShader = program.Fragment.get();
            desc.pipelineLayout.groups.clear();
            for (const auto& layout : program.Layouts) desc.pipelineLayout.groups.push_back(layout->Desc());
            desc.smallConstantBytes = program.Id == SceneProgram::Main ? sizeof(SmallDrawConstants) : 0;
            desc.topology = lines ? PrimitiveTopology::LineList : PrimitiveTopology::TriangleList;
            // The frontend's winding is OpenGL's, in a target whose rows run
            // the other way up in Vulkan: the same triangle turns the other way.
            desc.rasterizer.frontFace = desc.rasterizer.frontFace == FrontFace::CounterClockwise
                ? FrontFace::Clockwise : FrontFace::CounterClockwise;
            // glStencilFunc/glStencilOp set both faces.
            desc.depthStencil.back = desc.depthStencil.front;
            BlendAttachmentDesc blend = desc.blendAttachments.empty() ? BlendAttachmentDesc{} : desc.blendAttachments[0];
            desc.colorFormats.clear();
            desc.blendAttachments.clear();
            if (_target.Color)
            {
                desc.colorFormats.push_back(_target.Color->Desc().format);
                if (IsRgbTextureFormat(_target.Color->Desc().format))
                    blend.writeMask = static_cast<ColorWriteMask>(static_cast<std::uint8_t>(blend.writeMask) & 7U);
                desc.blendAttachments.push_back(blend);
            }
            desc.depthStencilFormat = _target.Depth ? _target.Depth->Desc().format : TextureFormat::Undefined;
            if (!_target.Depth)
            {
                desc.depthStencil.depthTestEnable = false;
                desc.depthStencil.depthWriteEnable = false;
                desc.depthStencil.stencilTestEnable = false;
            }
            desc.vertexBuffers = {{0, 12}, {1, 12}, {2, 16}, {3, 12}};
            if (program.Id == SceneProgram::Backdrop) desc.vertexBuffers.push_back({4, 8});
            if (_device->ContextPointer->_impl->legacy)
                for (auto& buffer : desc.vertexBuffers) buffer.stride = key.Strides[buffer.slot];
            // Only the inputs the program declares: the post programs read
            // position and texcoord and nothing else.
            if (program.Id == SceneProgram::Main)
                desc.vertexAttributes = {{0, 0, VertexFormat::Float3, 0}, {1, 1, VertexFormat::Float3, 0},
                    {2, 2, VertexFormat::Float4, 0}, {3, 3, VertexFormat::Float3, 0}};
            else if (program.Id == SceneProgram::Backdrop)
                desc.vertexAttributes = {{0, 0, VertexFormat::Float3, 0}, {3, 3, VertexFormat::Float3, 0},
                    {4, 4, VertexFormat::Float2, 0}};
            else
                desc.vertexAttributes = {{0, 0, VertexFormat::Float3, 0}, {3, 3, VertexFormat::Float3, 0}};
            return desc;
        }

        VkDescriptorSet VulkanCommandList::AllocateSet(const VulkanBindingLayout& layout)
        {
            return _commandSlots->Descriptors().Allocate(layout.Native(), layout.Desc());
        }

        VulkanCommandList::RingSlice VulkanCommandList::Allocate(VkDeviceSize size, VkDeviceSize alignment)
        {
            RequireRecording();
            return _commandSlots->Uploads().Allocate(size, alignment);
        }

        VulkanTexture& VulkanCommandList::Dummy()
        {
            if (!_dummy)
            {
                // What an unbound unit samples in OpenGL: opaque black.
                TextureDesc desc{};
                desc.width = 1;
                desc.height = 1;
                desc.format = TextureFormat::RGBA8Unorm;
                desc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
                _dummy = std::make_unique<VulkanTexture>(_device, desc, TextureHandle{});
                _dummySampler = std::make_unique<VulkanSampler>(_device, SamplerDesc{});
                // Recorded into this list, with no submission: slices already
                // allocated for the draw being recorded must survive.
                if (_renderingActive) EndNative();
                Transition(*_dummy, ResourceState::Undefined, ResourceState::CopyDst);
                const VkClearColorValue black{{0.0F, 0.0F, 0.0F, 1.0F}};
                const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                _device->ContextPointer->_impl->vkCmdClearColorImage(_commandSlots->Buffer(), _dummy->Native(),
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
                Transition(*_dummy, ResourceState::CopyDst, ResourceState::ShaderRead);
            }
            return *_dummy;
        }

        void VulkanCommandList::BindSampledTexture(std::uint32_t slot, const Texture* texture, const Sampler* sampler)
        {
            RequireRecording();
            if (slot >= _units.size()) throw std::out_of_range("Vulkan RHI: texture unit out of range.");
            if ((texture != nullptr) != (sampler != nullptr)) throw std::invalid_argument("Vulkan RHI: texture and sampler must be paired.");
            auto* image = texture ? const_cast<VulkanTexture*>(&CheckedResource<VulkanTexture>(*texture, _device)) : nullptr;
            const auto* filtering = sampler ? &CheckedResource<VulkanSampler>(*sampler, _device) : nullptr;
            if (image && !Has(image->Desc().usage, TextureUsage::Sampled))
                throw std::invalid_argument("Vulkan RHI: bound texture requires Sampled usage.");
            _units[slot] = {image, filtering};
        }

        void VulkanCommandList::ClearPersistentMaterials()
        {
            // Forget/resize callers have submitted recorded uses. Neither the
            // immutable UBO nor its descriptor pool is destroyed before that
            // serial completes. The closure carries only native ownership.
            auto entries = std::move(_persistentMaterials);
            _persistentMaterials.clear();
            auto descriptors = std::move(_persistentDescriptors);
            _persistentMaterialCount = 0;
            for (const auto& page : entries)
                if (page) for (const auto& entry : *page)
                    if (entry.Uniform.Buffer)
                    {
                        _device->Retire([allocator = _device->Allocator, buffer = entry.Uniform] {
                            vmaDestroyBuffer(allocator, buffer.Buffer, buffer.Allocation);
                        });
                        --_device->Buffers;
                    }
            if (descriptors) _device->Retire([descriptors = std::move(descriptors)] { descriptors->Close(); });
            for (auto& state : _programUniforms) state.Sets = {};
        }

        VkDescriptorSet VulkanCommandList::PersistentMaterialSet(const VulkanSceneProgram& program,
            const std::array<SampledDescriptor, 4>& textures)
        {
            const FrameTelemetry::Scope measured(FrameTelemetry::Phase::Material);
            FrameTelemetry::Count(FrameTelemetry::Counter::PersistentLookup);
            const auto identity = program.MaterialOwnerIdentity();
            const auto* foundBlock = program.MaterialBlock();
            if (program.Id != SceneProgram::Main || !identity || !foundBlock) return VK_NULL_HANDLE;
            const auto group = static_cast<std::uint32_t>(SceneShaderAbi::Group::Material);
            const auto pageIndex = static_cast<std::size_t>(identity / MaterialsPerPage);
            if (_persistentMaterials.size() <= pageIndex) _persistentMaterials.resize(pageIndex + 1);
            if (!_persistentMaterials[pageIndex])
                _persistentMaterials[pageIndex] = std::make_unique<PersistentMaterialPage>();
            auto& entry = (*_persistentMaterials[pageIndex])[identity % MaterialsPerPage];
            if (entry.ProgramIdentity && entry.ProgramIdentity != program.Identity)
                throw std::logic_error("Persistent material survived its program lifetime.");
            auto& slot = entry.Slots[_commandSlots->SlotIndex()];
            const bool unchanged = slot.Generation == foundBlock->Generation && slot.Textures == textures;
            if (slot.Set && unchanged)
            {
                slot.Recording = _bindingGeneration;
                if (_metricsEnabled) ++_metricPersistentHits;
                return slot.Set;
            }
            // Never overwrite a set or uniform range already referenced by this
            // recording, even when one owner draws multiple variants in a pass.
            if (slot.Recording == _bindingGeneration)
            { if (_metricsEnabled) ++_metricPersistentFull; return VK_NULL_HANDLE; }
            if (!entry.Uniform.Buffer && _persistentMaterialCount >= MaterialCapacity)
            { if (_metricsEnabled) ++_metricPersistentFull; return VK_NULL_HANDLE; }
            if (_metricsEnabled) ++_metricPersistentMisses;
            if (!_persistentDescriptors)
            {
                constexpr auto capacity = static_cast<std::uint32_t>(MaterialCapacity * FramesInFlight);
                _persistentDescriptors = _device->MakeDescriptors({capacity, {capacity, 0, capacity, 0, capacity}});
                _persistentDescriptors->ResetAfterCompletion({0});
            }
            auto& vk = *_device->ContextPointer->_impl;
            if (!entry.Uniform.Buffer)
            {
                const auto alignment = _device->UniformAlignment;
                entry.Stride = (foundBlock->Data.size() + alignment - 1) / alignment * alignment;
                entry.Uniform = _device->Resources->CreateBuffer({entry.Stride * FramesInFlight,
                    BufferUsage::Uniform, MemoryUsage::CpuToGpu});
                entry.ProgramIdentity = program.Identity;
                ++_persistentMaterialCount; ++_device->Buffers;
            }
            if (!entry.Uniform.Mapped) throw std::runtime_error("Persistent material buffer is not mapped.");
            const auto offset = entry.Stride * _commandSlots->SlotIndex();
            if (slot.Generation != foundBlock->Generation)
            {
                std::memcpy(static_cast<std::byte*>(entry.Uniform.Mapped) + offset,
                    foundBlock->Data.data(), foundBlock->Data.size());
                Check(vmaFlushAllocation(_device->Allocator, entry.Uniform.Allocation, offset,
                    foundBlock->Data.size()), "vmaFlushAllocation(material)");
            }
            const bool updateDescriptors = !slot.Set || slot.Textures != textures;
            if (updateDescriptors)
            {
                const auto& layout = *program.Layouts[group];
                if (!slot.Set) slot.Set = _persistentDescriptors->Allocate(layout.Native(), layout.Desc());
                const auto set = slot.Set;
                std::array<VkWriteDescriptorSet, 9> writes{};
                const VkDescriptorBufferInfo buffer{entry.Uniform.Buffer, offset, foundBlock->Data.size()};
                std::array<VkDescriptorImageInfo, 8> images{};
                std::uint32_t count = 1;
                writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                writes[0].dstSet = set; writes[0].dstBinding = foundBlock->Binding;
                writes[0].descriptorCount = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[0].pBufferInfo = &buffer;
                for (const auto& binding : program.Textures)
                {
                    if (binding.group != group) continue;
                    const auto unit = binding.unit;
                    images[unit * 2] = {VK_NULL_HANDLE, textures[unit].View, textures[unit].Layout};
                    images[unit * 2 + 1] = {textures[unit].Sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
                    for (std::uint32_t i = 0; i < 2; ++i)
                    {
                        auto& write = writes.at(count++);
                        write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet = set;
                        write.dstBinding = i ? binding.sampler : binding.image;
                        write.descriptorType = i ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                        write.descriptorCount = 1; write.pImageInfo = &images[unit * 2 + i];
                    }
                }
                vk.vkUpdateDescriptorSets(vk.device, count, writes.data(), 0, nullptr);
                if (_metricsEnabled) { ++_metricUpdates; }
            }
            slot.Generation = foundBlock->Generation; slot.Textures = textures;
            slot.Recording = _bindingGeneration;
            return slot.Set;
        }

        // Performance mode's scene path, the cpp-port renderer's way of
        // drawing: a draw is its blocks' record offsets in one bind, a
        // texture set from a cache, the vertex streams and the draw. The
        // blocks are main's own std140 bytes, copied into this slot's
        // record ring when their generation moved, and main_fast is main
        // with all five as dynamic uniform buffers in one set -- the same
        // picture without a uniform slice per draw, a descriptor write or
        // a set per material. False leaves the draw to the ordinary path
        // (not main, not wanted, legacy device, or this slot's records full
        // until it records again, larger).
        bool VulkanCommandList::DrawSceneFast(VulkanSceneProgram& program, const SceneDraw& draw,
            const std::array<SampledDescriptor, 4>& textures)
        {
            if (!program.FastVertex || !FastScene::Enabled()) return false;
            auto& vk = *_device->ContextPointer->_impl;
            if (vk.legacy) return false;
            auto& slot = _fastSlots.at(_commandSlots->SlotIndex());
            if (slot.Recording != _bindingGeneration && !BeginFastSlot(slot, program)) return false;
            if (slot.Overflowed || slot.Program != &program) return false;
            const auto alphaMode = static_cast<std::int32_t>(_pipeline->Desc().alphaTest);
            program.Write(SceneShaderAbi::ConstantIndex("alpha_test"), &alphaMode, sizeof(alphaMode), SceneShaderAbi::ValueType::Int);

            std::array<std::uint32_t, FastRecordKinds> offsets{};
            const auto matrices = std::clamp<std::uint32_t>(draw.Matrices, 1U, static_cast<std::uint32_t>(MatrixStackCapacity));
            for (std::size_t kind = 0; kind < FastRecordKinds; ++kind)
            {
                const auto& block = program.BlockAt(program.FastBlocks[kind]);
                const bool drawBlock = kind == FastRecordKinds - 1;
                if (slot.LastBlock[kind] == &block && slot.LastGeneration[kind] == block.Generation
                    && (!drawBlock || matrices <= slot.LastMatrixCount))
                {
                    offsets[kind] = slot.LastOffset[kind];
                    continue;
                }
                // A Draw record stops after the matrices the vertices use; the
                // rest of its block is never read.
                const VkDeviceSize bytes = drawBlock
                    ? std::min<VkDeviceSize>(program.FastMatrixOffset + VkDeviceSize{matrices} * 64U, block.Data.size())
                    : block.Data.size();
                std::uint32_t offset = 0;
                if (!AppendFast(slot, kind, block.Data.data(), bytes, offset)) return false;
                if (drawBlock) slot.LastMatrixCount = matrices;
                slot.LastBlock[kind] = &block;
                slot.LastGeneration[kind] = block.Generation;
                slot.LastOffset[kind] = offset;
                offsets[kind] = offset;
            }
            const VkDescriptorSet texture = FastTextureSet(program, textures);

            VulkanGraphicsPipeline& native = FastVariantFor(program, draw);
            const auto commands = _commandSlots->Buffer();
            if (native.Native() != _boundNative)
            {
                vk.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, native.Native());
                if (_metricsEnabled) ++_metricPipelines;
                _boundNative = native.Native();
                _genericDirty = true;
            }
            // The ordinary path's bookkeeping no longer describes what is bound.
            _boundSceneLayout = nullptr;
            _boundSceneSets.fill(VK_NULL_HANDLE);
            _smallPipeline = nullptr;
            _smallCurrent = false;
            ApplyDynamicState();
            const auto& small = program.BlockAt(program.FastSmall);
            const bool fresh = !_fastSetsBound;
            if (fresh || _fastBoundRecords != slot.Records || _fastBoundOffsets != offsets)
            {
                vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, native.Layout(), 0, 1, &slot.Records,
                    static_cast<std::uint32_t>(offsets.size()), offsets.data());
                if (_metricsEnabled) ++_metricBinds;
                _fastBoundRecords = slot.Records;
                _fastBoundOffsets = offsets;
            }
            if (fresh || _fastBoundTexture != texture)
            {
                vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, native.Layout(), 1, 1, &texture, 0, nullptr);
                if (_metricsEnabled) ++_metricBinds;
                _fastBoundTexture = texture;
            }
            if (fresh || std::memcmp(_fastPushed.data(), small.Data.data(), program.FastPushBytes) != 0)
            {
                vk.vkCmdPushConstants(commands, native.Layout(), VK_SHADER_STAGE_ALL_GRAPHICS, 0, program.FastPushBytes,
                    small.Data.data());
                if (_metricsEnabled) ++_metricSmallConstants;
                std::memcpy(_fastPushed.data(), small.Data.data(), program.FastPushBytes);
            }
            _fastSetsBound = true;

            std::array<VkBuffer, 5> buffers{};
            std::array<VkDeviceSize, 5> streamOffsets{};
            std::array<VkDeviceSize, 5> strides{};
            std::uint32_t streams = 0;
            for (std::size_t i = 0; i < 5; ++i)
            {
                if (draw.Streams[i].Buffer == VK_NULL_HANDLE) break;
                buffers[i] = draw.Streams[i].Buffer;
                streamOffsets[i] = draw.Streams[i].Offset;
                strides[i] = draw.Streams[i].Stride;
                streams = static_cast<std::uint32_t>(i + 1);
            }
            vk.vkCmdBindVertexBuffers2(commands, 0, streams, buffers.data(), streamOffsets.data(), nullptr, strides.data());
            vk.vkCmdBindIndexBuffer(commands, draw.IndexBuffer, draw.IndexOffset, VK_INDEX_TYPE_UINT32);
            vk.vkCmdDrawIndexed(commands, draw.IndexCount, 1, 0, 0, 0);
            if (_metricsEnabled) ++_metricFastDraws;
            return true;
        }

        // A slot's first fast draw of a recording: its records start over.
        // Its last submission is complete (the slot waited for its fence
        // before recording), so nothing still reads them. The ring is laid
        // out again -- larger after an overflow -- when needed.
        bool VulkanCommandList::BeginFastSlot(FastSlot& slot, const VulkanSceneProgram& program)
        {
            const bool grow = slot.Overflowed;
            slot.Recording = _bindingGeneration;
            slot.Overflowed = false;
            slot.Cursor = {};
            slot.Written = 0;
            slot.LastBlock = {};
            slot.LastGeneration = {};
            slot.LastOffset = {};
            slot.LastMatrixCount = 0;
            if (slot.Storage.Buffer && !grow && slot.Program == &program) return true;
            if (grow) slot.Scale = std::min<std::uint32_t>(slot.Scale * 2U, 64U);
            // Room for a busy frame: a record per draw of each kind (most
            // repeat), a Draw record with a few matrices for each draw.
            const auto alignment = _device->UniformAlignment;
            const auto aligned = [alignment](VkDeviceSize bytes) { return (bytes + alignment - 1U) / alignment * alignment; };
            constexpr std::array<VkDeviceSize, FastRecordKinds> counts{256, 256, 256, 2048, 4096};
            VkDeviceSize total = 0;
            for (std::size_t kind = 0; kind < FastRecordKinds; ++kind)
            {
                const auto blockSize = program.BlockAt(program.FastBlocks[kind]).Data.size();
                const VkDeviceSize typical = kind == FastRecordKinds - 1 ? program.FastMatrixOffset + 4U * 64U : blockSize;
                slot.Range[kind] = blockSize;
                slot.Base[kind] = total;
                // Plus one whole block, so a record's descriptor range never
                // runs past the region.
                slot.Size[kind] = aligned(typical) * counts[kind] * slot.Scale + aligned(blockSize);
                total += aligned(slot.Size[kind]);
            }
            if (slot.Storage.Buffer)
            {
                _device->Retire([allocator = _device->Allocator, storage = slot.Storage] {
                    vmaDestroyBuffer(allocator, storage.Buffer, storage.Allocation);
                });
                --_device->Buffers;
                slot.Storage = {};
            }
            slot.Program = nullptr;
            slot.Storage = _device->Resources->CreateBuffer({total, BufferUsage::Uniform, MemoryUsage::CpuToGpu});
            ++_device->Buffers;
            if (!slot.Storage.Mapped) throw std::runtime_error("Vulkan fast scene records are not mapped.");
            if (!_fastRecordSets)
            {
                _fastRecordSets = _device->MakeDescriptors({64, {static_cast<std::uint32_t>(64U * FastRecordKinds), 0, 0, 0, 0}});
                _fastRecordSets->ResetAfterCompletion({0});
            }
            const auto& layout = *program.FastLayouts[0];
            slot.Records = _fastRecordSets->Allocate(layout.Native(), layout.Desc());
            std::array<VkDescriptorBufferInfo, FastRecordKinds> infos{};
            std::array<VkWriteDescriptorSet, FastRecordKinds> writes{};
            for (std::uint32_t kind = 0; kind < FastRecordKinds; ++kind)
            {
                infos[kind] = {slot.Storage.Buffer, slot.Base[kind], slot.Range[kind]};
                writes[kind] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                writes[kind].dstSet = slot.Records;
                writes[kind].dstBinding = kind;
                writes[kind].descriptorCount = 1;
                writes[kind].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                writes[kind].pBufferInfo = &infos[kind];
            }
            auto& vk = *_device->ContextPointer->_impl;
            vk.vkUpdateDescriptorSets(vk.device, static_cast<std::uint32_t>(FastRecordKinds), writes.data(), 0, nullptr);
            slot.Program = &program;
            _fastSetsBound = false;
            return true;
        }

        bool VulkanCommandList::AppendFast(FastSlot& slot, std::size_t kind, const void* data, VkDeviceSize bytes,
            std::uint32_t& offset)
        {
            const auto alignment = _device->UniformAlignment;
            const auto start = (slot.Cursor[kind] + alignment - 1U) / alignment * alignment;
            // The descriptor reads a whole block from the record's offset.
            if (start + slot.Range[kind] > slot.Size[kind])
            {
                slot.Overflowed = true;
                return false;
            }
            std::memcpy(slot.Storage.Mapped + slot.Base[kind] + start, data, bytes);
            offset = static_cast<std::uint32_t>(start);
            slot.Cursor[kind] = start + bytes;
            slot.Written = std::max(slot.Written, slot.Base[kind] + start + bytes);
            return true;
        }

        // One set per texture and sampler, written once and kept until a
        // scene resource goes (Forget) -- cpp-port's textureSetFor.
        VkDescriptorSet VulkanCommandList::FastTextureSet(const VulkanSceneProgram& program,
            const std::array<SampledDescriptor, 4>& textures)
        {
            const auto& binding = program.Textures.front();
            const auto& texture = textures[binding.unit];
            const FastTextureKey key{texture.View, texture.Sampler, texture.Layout, texture.Identity,
                texture.Generation, texture.SamplerIdentity};
            if (const auto found = _fastTextureSets.find(key); found != _fastTextureSets.end()) return found->second;
            if (_fastTextureSets.size() >= 16384) ClearFastTextures();
            if (!_fastTextures)
            {
                _fastTextures = _device->MakeDescriptors({1024, {0, 0, 1024, 0, 1024}});
                _fastTextures->ResetAfterCompletion({0});
            }
            const auto& layout = *program.FastLayouts[1];
            const VkDescriptorSet set = _fastTextures->Allocate(layout.Native(), layout.Desc());
            const VkDescriptorImageInfo image{VK_NULL_HANDLE, texture.View, texture.Layout};
            const VkDescriptorImageInfo sampler{texture.Sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
            std::array<VkWriteDescriptorSet, 2> writes{};
            for (std::size_t i = 0; i < writes.size(); ++i)
            {
                writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                writes[i].dstSet = set;
                writes[i].dstBinding = i ? binding.sampler : binding.image;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                writes[i].pImageInfo = i ? &sampler : &image;
            }
            auto& vk = *_device->ContextPointer->_impl;
            vk.vkUpdateDescriptorSets(vk.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
            if (_metricsEnabled) ++_metricUpdates;
            _fastTextureSets.emplace(key, set);
            return set;
        }

        // VariantFor's pipeline with main_fast's modules, layouts and push range.
        VulkanGraphicsPipeline& VulkanCommandList::FastVariantFor(const VulkanSceneProgram& program, const SceneDraw& draw)
        {
            VariantKey key{_pipeline, &program,
                _target.Color ? _target.Color->Desc().format : TextureFormat::Undefined,
                _target.Depth ? _target.Depth->Desc().format : TextureFormat::Undefined, draw.Lines};
            for (const auto& recent : _fastRecent)
                if (recent.Native && recent.Key == key) return *recent.Native;
            const auto found = _fastVariants.find(key);
            if (found != _fastVariants.end() && found->second.BaseDesc == _pipeline->Desc())
            {
                _fastRecent[_fastRecentNext] = {key, found->second.Native.get()};
                _fastRecentNext = (_fastRecentNext + 1) % _fastRecent.size();
                return *found->second.Native;
            }
            GraphicsPipelineDesc desc = SceneVariantDesc(program, key);
            desc.vertexShader = program.FastVertex.get();
            desc.fragmentShader = program.FastFragment.get();
            desc.pipelineLayout.groups = {program.FastLayouts[0]->Desc(), program.FastLayouts[1]->Desc()};
            desc.smallConstantBytes = program.FastPushBytes;
            auto& entry = _fastVariants[key];
            entry.BaseDesc = _pipeline->Desc();
            entry.Native = std::make_shared<VulkanGraphicsPipeline>(_device, desc, true);
            for (auto& recent : _fastRecent) if (recent.Key == key) recent = {};
            _fastRecent[_fastRecentNext] = {key, entry.Native.get()};
            _fastRecentNext = (_fastRecentNext + 1) % _fastRecent.size();
            return *entry.Native;
        }

        // The host writes of the slot this buffer recorded, made visible
        // before it is submitted (a no-op on coherent memory).
        void VulkanCommandList::FlushFast()
        {
            auto& slot = _fastSlots.at(_commandSlots->SlotIndex());
            if (slot.Recording != _bindingGeneration || !slot.Written || !slot.Storage.Buffer) return;
            Check(vmaFlushAllocation(_device->Allocator, slot.Storage.Allocation, 0, slot.Written),
                "vmaFlushAllocation(fast scene)");
        }

        void VulkanCommandList::ClearFastTextures()
        {
            _fastTextureSets.clear();
            if (_fastTextures)
                _device->Retire([descriptors = std::move(_fastTextures)] { descriptors->Close(); });
            _fastTextures.reset();
            _fastSetsBound = false;
        }

        void VulkanCommandList::CloseFast() noexcept
        {
            for (auto& slot : _fastSlots)
            {
                if (slot.Storage.Buffer)
                {
                    try
                    {
                        _device->Retire([allocator = _device->Allocator, storage = slot.Storage] {
                            vmaDestroyBuffer(allocator, storage.Buffer, storage.Allocation);
                        });
                        --_device->Buffers;
                    }
                    catch (...) {}
                }
                slot = {};
            }
            try
            {
                _fastTextureSets.clear();
                if (_fastTextures) _device->Retire([descriptors = std::move(_fastTextures)] { descriptors->Close(); });
                if (_fastRecordSets) _device->Retire([descriptors = std::move(_fastRecordSets)] { descriptors->Close(); });
            }
            catch (...) {}
            _fastTextures.reset();
            _fastRecordSets.reset();
            _fastVariants.clear();
            _fastRecent = {};
        }

        void VulkanCommandList::DrawScene(const SceneDraw& draw)
        {
            const FrameTelemetry::Scope measured(FrameTelemetry::Phase::DrawScene);
            if (draw.IndexCount == 0) return;
            if (_metricsEnabled) ++_metricDraws;
            RequireRecording();
            if (!_renderingOpen) throw std::logic_error("Vulkan RHI: scene draw outside rendering.");
            if (!_pipeline || !_pipeline->IsDeferred())
                throw std::logic_error("Vulkan RHI: scene draw needs a scene pass pipeline.");
            auto* program = _pipelineNamesProgram ? _pipelineProgram
                : static_cast<VulkanSceneProgram*>(_device->CurrentSceneProgram);
            if (!program) throw std::logic_error("Vulkan RHI: scene draw with no program.");
            auto& vk = *_device->ContextPointer->_impl;

            std::array<SampledDescriptor, 4> textures{};
            for (const auto& binding : program->Textures)
            {
                const auto unit = binding.unit;
                VulkanTexture* texture = _units[unit].first;
                const VulkanSampler* sampler = _units[unit].second;
                if (!texture)
                {
                    texture = &Dummy();
                    sampler = _dummySampler.get();
                }
                if (!HasAny(texture->State(), ResourceState::ShaderRead))
                {
                    if (_renderingActive) EndNative();
                    Barrier(*texture, ResourceState::ShaderRead);
                }
                textures[unit] = {texture->SampledView(), sampler->Native(), ToVkState(texture->State(), true).Layout,
                    texture->Identity(), texture->Generation(), sampler->Identity()};
            }
            if (!_renderingActive) Materialize();
            if (DrawSceneFast(*program, draw, textures)) return;
            // What the ordinary path binds below replaces what DrawSceneFast bound.
            _fastSetsBound = false;

            VulkanGraphicsPipeline& native = VariantFor(*program, draw);
            if (native.Native() != _boundNative)
            {
                vk.vkCmdBindPipeline(_commandSlots->Buffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, native.Native());
                if (_metricsEnabled) ++_metricPipelines;
                _boundNative = native.Native();
            }
            if (!_smallPipeline || _smallPipeline->Desc().smallConstantBytes != native.Desc().smallConstantBytes)
                _smallCurrent = false;
            _smallPipeline = &native;
            // Compatibility for set N includes every set from zero through N
            // and identical push constant ranges, not just the current set.
            bool compatible = _boundSmallBytes == native.Desc().smallConstantBytes;
            const auto& groups = native.Desc().pipelineLayout.groups;
            for (std::size_t group = 0; group < groups.size(); ++group)
            {
                compatible = compatible && _boundSceneLayout && group < _boundSceneLayout->groups.size()
                    && _boundSceneLayout->groups[group] == groups[group];
                if (!compatible) _boundSceneSets[group] = VK_NULL_HANDLE;
            }
            _boundSceneLayout = &native.Desc().pipelineLayout;
            _boundSmallBytes = native.Desc().smallConstantBytes;
            ApplyDynamicState();

            const auto alphaMode = static_cast<std::int32_t>(_pipeline->Desc().alphaTest);
            program->Write(SceneShaderAbi::ConstantIndex("alpha_test"), &alphaMode, sizeof(alphaMode), SceneShaderAbi::ValueType::Int);
            auto& uniforms = _programUniforms.at(static_cast<std::size_t>(program->Id));
            auto& identity = _programIdentities.at(static_cast<std::size_t>(program->Id));
            if (identity.second != program->Identity)
            {
                if (identity.second)
                    for (std::uint32_t group = 0; group < SceneShaderAbi::GroupCount; ++group)
                        _commandSlots->RetireDescriptors((identity.second << 3) | group);
                identity = {program, program->Identity};
            }
            if (program->Blocks.size() > uniforms.Blocks.size())
                throw std::logic_error("Vulkan scene program exceeds its admitted uniform block capacity.");
            uniforms.Prepare(program->Identity, _bindingGeneration);
            auto& sets = uniforms.Sets;
            if (uniforms.SelectMaterialOwner(program->MaterialOwnerIdentity()))
            {
                for (std::size_t i = 0; i < program->Blocks.size(); ++i)
                    if (program->BlockAt(i).Group == static_cast<std::uint32_t>(SceneShaderAbi::Group::Material))
                    { uniforms.Blocks[i] = {}; sets[program->BlockAt(i).Group] = VK_NULL_HANDLE; }
            }
            for (const auto& binding : program->Textures)
                uniforms.SelectTexture(binding.unit, binding.group, textures[binding.unit]);
            const auto alignment = _device->UniformAlignment;
            std::array<DynamicOffsets, SceneShaderAbi::GroupCount> dynamicOffsets{};
            for (std::size_t i = 0; i < program->Blocks.size(); ++i)
            {
                const auto& block = program->BlockAt(i);
                if (block.Small)
                {
                    if (block.Data.size() != sizeof(SmallDrawConstants))
                        throw std::logic_error("Vulkan RHI: small constant ABI drift.");
                    SmallDrawConstants constants;
                    std::memcpy(&constants, block.Data.data(), sizeof(constants));
                    SetSmallConstants(constants);
                    continue;
                }
                auto& [slice, generation] = uniforms.Blocks[i];
                if (program->Id == SceneProgram::Main
                    && block.Group == static_cast<std::uint32_t>(SceneShaderAbi::Group::Material))
                {
                    if (sets[block.Group] && generation == block.Generation) continue;
                    if (const auto persistent = PersistentMaterialSet(*program, textures))
                    {
                        sets[block.Group] = persistent;
                        generation = block.Generation;
                        slice = {};
                        continue;
                    }
                }
                if (!slice.Buffer || generation != block.Generation)
                {
                    const VkBuffer page = slice.Buffer;
                    slice = Allocate(block.Data.size(), alignment);
                    std::memcpy(slice.Data, block.Data.data(), block.Data.size());
                    generation = block.Generation;
                    // A dynamic block's set stays valid while its slices come from one page.
                    if (!DynamicUniformGroup(block.Group) || slice.Buffer != page)
                    {
                        sets[block.Group] = VK_NULL_HANDLE;
                        ++uniforms.UniformVersions[block.Group];
                    }
                }
                if (DynamicUniformGroup(block.Group))
                {
                    auto& offsets = dynamicOffsets[block.Group];
                    offsets.Values.at(program->DynamicSlot.at(i)) = static_cast<std::uint32_t>(slice.Offset);
                    ++offsets.Count;
                }
            }
            {
                const FrameTelemetry::Scope descriptorPhase(FrameTelemetry::Phase::Descriptor);
                for (std::uint32_t group = 0; group < SceneShaderAbi::GroupCount; ++group)
                {
                    const auto& layout = *program->Layouts[group];
                    if (layout.Desc().entries.empty()) continue;
                    if (!sets[group])
                    {
                        std::uint64_t textureToken = 0;
                        for (const auto& binding : program->Textures)
                            if (binding.group == group) { textureToken = textures[binding.unit].Identity; break; }
                        FrameTelemetry::Count(FrameTelemetry::Counter::DescriptorLookup);
                        sets[group] = _sceneDescriptors.Find(program->Id, group, textureToken, program->Identity,
                            _bindingGeneration, _descriptorEpoch, uniforms.UniformVersions[group], uniforms.Textures);
                        if (sets[group])
                        { if (_metricsEnabled) ++_metricHits; FrameTelemetry::Count(FrameTelemetry::Counter::DescriptorHit); }
                        else
                        {
                            if (_metricsEnabled) ++_metricMisses;
                            FrameTelemetry::Count(FrameTelemetry::Counter::DescriptorMiss);
                            const auto fixedIdentity = (program->Identity << 3) | group;
                            // First use, only this command list's current slot. Small
                            // bounded page; all overflow uses the ordinary allocator.
                            const auto capacity = group == static_cast<std::uint32_t>(SceneShaderAbi::Group::Draw) ? 1024U
                                : group == static_cast<std::uint32_t>(SceneShaderAbi::Group::Frame) ? 128U : 32U;
                            _commandSlots->Descriptors().Preallocate(fixedIdentity, layout.Native(), layout.Desc(), capacity);
                            auto set = _commandSlots->Descriptors().AllocateFixed(fixedIdentity);
                            if (set)
                            {
                                if (_metricsEnabled) ++_metricFixedSlots;
                            }
                            else
                            {
                                set = AllocateSet(layout);
                                constexpr std::array groupCounters{FrameTelemetry::Counter::FrameDescriptors,
                                    FrameTelemetry::Counter::MaterialDescriptors, FrameTelemetry::Counter::DrawDescriptors,
                                    FrameTelemetry::Counter::PassDescriptors};
                                FrameTelemetry::Count(groupCounters.at(group));
                                if (_metricsEnabled) { ++_metricAllocations; ++_metricFixedOverflow; }
                            }
                            std::array<VkDescriptorBufferInfo, SceneShaderAbi::Bindings.size()> buffers{};
                            std::array<VkDescriptorImageInfo, 8> images{};
                            std::array<VkWriteDescriptorSet, SceneShaderAbi::Bindings.size()> writes{};
                            std::uint32_t count = 0;
                            const auto write = [&](std::uint32_t binding, VkDescriptorType type) -> VkWriteDescriptorSet& {
                                auto& item = writes.at(count++);
                                item = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                                item.dstSet = set; item.dstBinding = binding;
                                item.descriptorCount = 1; item.descriptorType = type;
                                return item;
                            };
                            for (std::size_t i = 0; i < program->Blocks.size(); ++i)
                            {
                                const auto& block = program->BlockAt(i);
                                if (block.Small || block.Group != group) continue;
                                const auto& slice = uniforms.Blocks[i].first;
                                const bool dynamic = DynamicUniformGroup(group);
                                buffers[i] = {slice.Buffer, dynamic ? 0 : slice.Offset, block.Data.size()};
                                write(block.Binding, dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                    : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER).pBufferInfo = &buffers[i];
                            }
                            for (const auto& binding : program->Textures)
                            {
                                if (binding.group != group) continue;
                                const auto unit = binding.unit;
                                images[unit * 2] = {VK_NULL_HANDLE, textures[unit].View, textures[unit].Layout};
                                images[unit * 2 + 1] = {textures[unit].Sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
                                write(binding.image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE).pImageInfo = &images[unit * 2];
                                write(binding.sampler, VK_DESCRIPTOR_TYPE_SAMPLER).pImageInfo = &images[unit * 2 + 1];
                            }
                            vk.vkUpdateDescriptorSets(vk.device, count, writes.data(), 0, nullptr);
                            if (_metricsEnabled) ++_metricUpdates;
                            sets[group] = set;
                            _sceneDescriptors.Store(program->Id, group, textureToken, program->Identity,
                                _bindingGeneration, _descriptorEpoch, uniforms.UniformVersions[group], uniforms.Textures, set);
                        }
                    }
                    else
                    { if (_metricsEnabled) ++_metricHits; FrameTelemetry::Count(FrameTelemetry::Counter::DescriptorHit); }
                    const auto& offsets = dynamicOffsets[group];
                    if (_boundSceneSets[group] != sets[group] || (offsets.Count && _boundOffsets[group] != offsets))
                    {
                        vk.vkCmdBindDescriptorSets(_commandSlots->Buffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            native.Layout(), group, 1, &sets[group], offsets.Count, offsets.Values.data());
                        if (_metricsEnabled) ++_metricBinds;
                        _boundSceneSets[group] = sets[group];
                        _boundOffsets[group] = offsets;
                    }
                }
            }

            const FrameTelemetry::Scope commandPhase(FrameTelemetry::Phase::Commands);
            std::array<VkBuffer, 5> buffers{};
            std::array<VkDeviceSize, 5> offsets{};
            std::array<VkDeviceSize, 5> strides{};
            std::uint32_t streams = 0;
            for (std::size_t i = 0; i < 5; ++i)
            {
                if (draw.Streams[i].Buffer == VK_NULL_HANDLE) break;
                buffers[i] = draw.Streams[i].Buffer;
                offsets[i] = draw.Streams[i].Offset;
                strides[i] = draw.Streams[i].Stride;
                streams = static_cast<std::uint32_t>(i + 1);
            }
            vk.vkCmdBindVertexBuffers2(_commandSlots->Buffer(), 0, streams, buffers.data(), offsets.data(), nullptr, strides.data());
            vk.vkCmdBindIndexBuffer(_commandSlots->Buffer(), draw.IndexBuffer, draw.IndexOffset, VK_INDEX_TYPE_UINT32);
            vk.vkCmdDrawIndexed(_commandSlots->Buffer(), draw.IndexCount, 1, 0, 0, 0);
        }

        void VulkanCommandList::UploadTexture(VulkanTexture& texture, std::span<const std::byte> texels)
        {
            RequireRecording();
            if (_renderingOpen && _clearsPending) Materialize();
            if (_renderingActive) EndNative();
            const auto pixelAlignment = VulkanResources::AspectMask(texture.Desc().format) == VK_IMAGE_ASPECT_COLOR_BIT
                ? StorageBytesPerPixel(texture.Desc().format) : 4U;
            // Native physical texels determine the aligned staging offset.
            const auto alignment = pixelAlignment == 12 ? 48U : 16U;
            const RingSlice staging = Allocate(texels.size(), alignment);
            std::memcpy(staging.Data, texels.data(), texels.size());
            const ResourceState previous = texture.State();
            Barrier(texture, ResourceState::CopyDst);
            BufferDesc source{};
            source.size = staging.Offset + texels.size();
            BufferTextureCopy region{};
            region.bufferOffset = staging.Offset;
            region.width = texture.Desc().width; region.height = texture.Desc().height;
            // The ring's host writes are made available by the flush before
            // this list is submitted.
            if (IsRgbTextureFormat(texture.Desc().format))
                CopyRgbBufferTexture(staging.Buffer, source.size, texture, region, true);
            else
            {
                const auto copy = ToVkBufferImageCopy(source, texture.Desc(), region);
                _device->ContextPointer->_impl->vkCmdCopyBufferToImage(_commandSlots->Buffer(), staging.Buffer, texture.Native(),
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            }
            const ResourceState finalState = previous != ResourceState::Undefined && previous != ResourceState::CopyDst
                ? previous
                : (Has(texture.Desc().usage, TextureUsage::Sampled) ? ResourceState::ShaderRead
                    : (Has(texture.Desc().usage, TextureUsage::ColorAttachment)
                        ? ResourceState::ColorAttachment : ResourceState::Common));
            Barrier(texture, finalState);
        }

        void VulkanCommandList::UploadBuffer(VulkanBuffer& buffer, VkDeviceSize offset,
            std::span<const std::byte> bytes)
        {
            RequireRecording();
            if (_renderingOpen && _clearsPending) Materialize();
            if (_renderingActive) EndNative();
            const auto staging = Allocate(bytes.size(), 16);
            std::memcpy(staging.Data, bytes.data(), bytes.size());
            const auto previous = buffer.State();
            if (previous != ResourceState::CopyDst) Transition(buffer, previous, ResourceState::CopyDst);
            const VkBufferCopy copy{staging.Offset, offset, bytes.size()};
            _device->ContextPointer->_impl->vkCmdCopyBuffer(_commandSlots->Buffer(), staging.Buffer, buffer.Native(), 1, &copy);
            if (previous != ResourceState::CopyDst && previous != ResourceState::Undefined)
                Transition(buffer, ResourceState::CopyDst, previous);
            else if (previous == ResourceState::Undefined && buffer.Desc().initialState != ResourceState::Undefined)
                Transition(buffer, ResourceState::CopyDst, buffer.Desc().initialState);
        }

        void VulkanCommandList::SetVertexBuffer(std::uint32_t slot, const Buffer& buffer, std::uint64_t offset)
        {
            RequireRecording();
            const auto& native = CheckedResource<VulkanBuffer>(buffer, _device);
            if (slot >= _device->ContextPointer->Caps().maxVertexBuffers
                || !Has(native.Desc().usage, BufferUsage::Vertex) || offset >= native.Desc().size)
                throw std::invalid_argument("Vulkan RHI: invalid vertex buffer binding.");
            const VkBuffer handle = native.Native();
            const VkDeviceSize at = offset;
            _device->ContextPointer->_impl->vkCmdBindVertexBuffers2(_commandSlots->Buffer(), slot, 1, &handle, &at, nullptr, nullptr);
            _vertexBindings[slot] = {&native, native.Lifetime(), offset};
        }

        void VulkanCommandList::SetIndexBuffer(const Buffer& buffer, IndexType type, std::uint64_t offset)
        {
            RequireRecording();
            const auto& native = CheckedResource<VulkanBuffer>(buffer, _device);
            const auto width = type == IndexType::UInt16 ? 2U : 4U;
            if (!Has(native.Desc().usage, BufferUsage::Index) || offset >= native.Desc().size || offset % width)
                throw std::invalid_argument("Vulkan RHI: invalid index buffer binding.");
            _device->ContextPointer->_impl->vkCmdBindIndexBuffer(_commandSlots->Buffer(), native.Native(), offset,
                type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            _indexBinding = {&native, native.Lifetime(), offset}; _indexType = type;
        }

        void VulkanCommandList::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount,
            std::uint32_t firstVertex, std::uint32_t firstInstance)
        {
            RequireRecording();
            if (!_renderingOpen) throw std::logic_error("Vulkan RHI: draw needs a rendering interval.");
            if (!_pipeline || _pipeline->IsDeferred()) throw std::logic_error("Vulkan RHI: draw needs a native pipeline.");
            RestoreGenericBindings();
            if (!_renderingActive) Materialize();
            ApplyDynamicState();
            _device->ContextPointer->_impl->vkCmdDraw(_commandSlots->Buffer(), vertexCount, instanceCount, firstVertex, firstInstance);
        }

        void VulkanCommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
            std::uint32_t firstIndex, std::int32_t vertexOffset, std::uint32_t firstInstance)
        {
            RequireRecording();
            if (!_renderingOpen) throw std::logic_error("Vulkan RHI: draw needs a rendering interval.");
            if (!_pipeline || _pipeline->IsDeferred()) throw std::logic_error("Vulkan RHI: draw needs a native pipeline.");
            if (!_indexBinding.Buffer || _indexBinding.Lifetime.expired())
                throw std::logic_error("Vulkan RHI: indexed draw needs a live index buffer.");
            const auto width = _indexType == IndexType::UInt16 ? 2U : 4U;
            const auto offset = _indexBinding.Offset + static_cast<std::uint64_t>(firstIndex) * width;
            const auto size = _indexBinding.Buffer->Desc().size;
            if (offset > size || static_cast<std::uint64_t>(indexCount) * width > size - offset)
                throw std::out_of_range("Vulkan RHI: index draw range exceeds the buffer.");
            RestoreGenericBindings();
            if (!_renderingActive) Materialize();
            ApplyDynamicState();
            _device->ContextPointer->_impl->vkCmdDrawIndexed(_commandSlots->Buffer(), indexCount, instanceCount,
                firstIndex, vertexOffset, firstInstance);
        }

        void VulkanCommandList::ReadColor(const RenderingInfo& info, std::uint32_t x, std::uint32_t y,
            std::uint32_t width, std::uint32_t height, TextureFormat format, void* destination)
        {
            if (format != TextureFormat::RGB8Unorm && format != TextureFormat::RGBA8Unorm)
                throw std::invalid_argument("Vulkan RHI: ReadColor reads RGB8 or RGBA8.");
            const std::size_t outBytes = format == TextureFormat::RGB8Unorm ? 3U : 4U;
            VulkanTexture* texture = nullptr;
            if (info.swapchain)
                texture = _device->WindowColor.get();
            else if (!info.colorAttachments.empty() && info.colorAttachments[0].view)
            {
                const auto& view = CheckedResource<VulkanTextureView>(*info.colorAttachments[0].view, _device);
                texture = const_cast<VulkanTexture*>(&CheckedResource<VulkanTexture>(view.TextureResource(), _device));
            }
            if (!texture || width == 0 || height == 0)
            {
                std::memset(destination, 0, static_cast<std::size_t>(width) * height * outBytes);
                return;
            }
            const bool restart = _autoRestart;
            if (!_recording) BeginBuffer();
            if (_renderingOpen && _clearsPending) Materialize();
            if (_renderingActive) EndNative();
            width = std::min(width, texture->Desc().width - std::min(x, texture->Desc().width));
            height = std::min(height, texture->Desc().height - std::min(y, texture->Desc().height));
            BufferDesc desc{};
            desc.size = static_cast<std::uint64_t>(width) * height * 4U;
            desc.usage = BufferUsage::TransferDst;
            desc.memoryUsage = MemoryUsage::GpuToCpu;
            VulkanBuffer readback(_device, desc);
            const ResourceState previous = texture->State();
            Barrier(*texture, ResourceState::CopySrc);
            Transition(readback, ResourceState::Undefined, ResourceState::CopyDst);
            BufferTextureCopy region{};
            region.x = x;
            region.y = y;
            region.width = width;
            region.height = height;
            // Diagnostic output reads physical RGBA8 storage. Public RGB
            // buffer/image copies use the logical three-byte packing below.
            CopyPhysicalTextureToBuffer(*texture, readback, region);
            if (previous != ResourceState::Undefined) Barrier(*texture, previous);
            Flush();
            WaitAll();
            _autoRestart = restart;
            void* mapped = nullptr;
            Check(vmaMapMemory(_device->Allocator, readback.Allocation(), &mapped), "vmaMapMemory(readback)");
            Check(vmaInvalidateAllocation(_device->Allocator, readback.Allocation(), 0, desc.size),
                "vmaInvalidateAllocation(readback)");
            const auto* source = static_cast<const std::uint8_t*>(mapped);
            auto* target = static_cast<std::uint8_t*>(destination);
            const bool rgb8 = texture->Desc().format == TextureFormat::RGB8Unorm;
            for (std::size_t i = 0, n = static_cast<std::size_t>(width) * height; i < n; ++i)
            {
                std::memcpy(target + i * outBytes, source + i * 4U, outBytes == 4U ? 4U : 3U);
                if (outBytes == 4U && rgb8) target[i * 4U + 3U] = 0xFF;
            }
            vmaUnmapMemory(_device->Allocator, readback.Allocation());
        }

        void VulkanCommandList::CopyColorAttachmentToTexture(Texture& destination, std::uint32_t width, std::uint32_t height)
        {
            RequireRecording();
            auto& target = CheckedResource<VulkanTexture>(destination, _device);
            if (!_renderingOpen) throw std::logic_error("Vulkan RHI: color copy needs a rendering interval.");
            VulkanTexture* source = _target.Color;
            if (!source) return;
            if (_renderingOpen && _clearsPending) Materialize();
            if (_renderingActive) EndNative();
            width = std::min({width, source->Desc().width, target.Desc().width});
            height = std::min({height, source->Desc().height, target.Desc().height});
            const ResourceState sourceState = source->State();
            Barrier(*source, ResourceState::CopySrc);
            Barrier(target, ResourceState::CopyDst);
            VkImageCopy region{};
            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.extent = {width, height, 1};
            _device->ContextPointer->_impl->vkCmdCopyImage(_commandSlots->Buffer(), source->Native(),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.Native(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            Barrier(*source, sourceState == ResourceState::Undefined ? ResourceState::ColorAttachment : sourceState);
            if (Has(target.Desc().usage, TextureUsage::Sampled)) Barrier(target, ResourceState::ShaderRead);
        }


        bool VulkanCommandList::CopyAttachmentDepth(bool save)
        {
            RequireRecording();
            if (!_renderingOpen || !_target.Depth) return false;
            VulkanTexture& depth = *_target.Depth;
            const TextureDesc& desc = depth.Desc();
            if (!Has(desc.usage, TextureUsage::TransferSrc) || !Has(desc.usage, TextureUsage::TransferDst)
                || !(VulkanResources::AspectMask(desc.format) & VK_IMAGE_ASPECT_DEPTH_BIT))
                return false;
            const std::uint32_t width = std::min(_target.Width, desc.width);
            const std::uint32_t height = std::min(_target.Height, desc.height);
            if (save && (!_savedDepth || _savedDepth->Desc().format != desc.format
                || _savedDepth->Desc().width < width || _savedDepth->Desc().height < height))
            {
                // The old copy's recorded uses are submitted before it is retired.
                if (_savedDepth) Flush();
                TextureDesc store = desc;
                store.width = std::max(width, _savedDepth ? _savedDepth->Desc().width : 0U);
                store.height = std::max(height, _savedDepth ? _savedDepth->Desc().height : 0U);
                store.usage = TextureUsage::TransferSrc | TextureUsage::TransferDst;
                _savedDepth = std::make_unique<VulkanTexture>(_device, store, TextureHandle{});
                RequireRecording();
            }
            if (!save && (!_savedDepth || _savedDepth->Desc().format != desc.format)) return false;
            if (_clearsPending) Materialize();
            if (_renderingActive) EndNative();
            VulkanTexture& source = save ? depth : *_savedDepth;
            VulkanTexture& destination = save ? *_savedDepth : depth;
            Barrier(source, ResourceState::CopySrc);
            Barrier(destination, ResourceState::CopyDst);
            VkImageCopy region{};
            region.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
            region.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
            region.extent = {width, height, 1};
            _device->ContextPointer->_impl->vkCmdCopyImage(_commandSlots->Buffer(), source.Native(),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination.Native(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            // The next draw resumes rendering, which takes the attachment back.
            Barrier(depth, ResourceState::DepthStencilWrite);
            return true;
        }

        void VulkanCommandList::CopyBuffer(const Buffer& source, std::uint64_t sourceOffset,
            Buffer& destination, std::uint64_t destinationOffset, std::uint64_t size)
        {
            RequireRecording();
            auto& src = CheckedResource<VulkanBuffer>(source, _device);
            auto& dst = CheckedResource<VulkanBuffer>(destination, _device);
            if (src.DeviceState() != _device || dst.DeviceState() != _device)
                throw std::invalid_argument("Vulkan RHI: buffers belong to another device.");
            if (!Has(src.Desc().usage, BufferUsage::TransferSrc)
                || !Has(dst.Desc().usage, BufferUsage::TransferDst))
                throw std::invalid_argument("Vulkan RHI: buffer copies need TransferSrc and TransferDst usage.");
            if (!HasAny(src.State(), ResourceState::CopySrc) || dst.State() != ResourceState::CopyDst)
                throw std::invalid_argument("Vulkan RHI: buffer copies require CopySrc and CopyDst states.");
            if (size == 0 || sourceOffset > src.Desc().size || size > src.Desc().size - sourceOffset
                || destinationOffset > dst.Desc().size || size > dst.Desc().size - destinationOffset)
                throw std::out_of_range("Vulkan RHI: buffer copy range is outside the resource.");
            if (&source == &destination && sourceOffset < destinationOffset + size
                && destinationOffset < sourceOffset + size)
                throw std::invalid_argument("Vulkan RHI: source and destination buffer copy ranges overlap.");
            VkBufferCopy region{};
            region.srcOffset = sourceOffset;
            region.dstOffset = destinationOffset;
            region.size = size;
            PrepareTransfer();
            _device->ContextPointer->_impl->vkCmdCopyBuffer(
                _commandSlots->Buffer(), src.Native(), dst.Native(), 1, &region);
        }

        void VulkanCommandList::CopyBufferToTexture(
            const Buffer& source, Texture& destination, const BufferTextureCopy& region)
        {
            RequireRecording();
            auto& src = CheckedResource<VulkanBuffer>(source, _device);
            auto& dst = CheckedResource<VulkanTexture>(destination, _device);
            if (src.DeviceState() != _device || dst.DeviceState() != _device)
                throw std::invalid_argument("Vulkan RHI: copy resources belong to another device.");
            if (!Has(src.Desc().usage, BufferUsage::TransferSrc)
                || !Has(dst.Desc().usage, TextureUsage::TransferDst))
                throw std::invalid_argument("Vulkan RHI: image upload needs TransferSrc and TransferDst usage.");
            if (!HasAny(src.State(), ResourceState::CopySrc) || dst.State() != ResourceState::CopyDst)
                throw std::invalid_argument("Vulkan RHI: image uploads require CopySrc and CopyDst states.");
            if (IsRgbTextureFormat(dst.Desc().format))
            { CopyRgbBufferTexture(src.Native(), src.Desc().size, dst, region, true); return; }
            const VkBufferImageCopy copy = ToVkBufferImageCopy(src.Desc(), dst.Desc(), region);
            PrepareTransfer();
            _device->ContextPointer->_impl->vkCmdCopyBufferToImage(_commandSlots->Buffer(),
                src.Native(), dst.Native(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        }

        void VulkanCommandList::CopyTextureToBuffer(
            const Texture& source, Buffer& destination, const BufferTextureCopy& region)
        {
            RequireRecording();
            auto& src = CheckedResource<VulkanTexture>(source, _device);
            auto& dst = CheckedResource<VulkanBuffer>(destination, _device);
            if (src.DeviceState() != _device || dst.DeviceState() != _device)
                throw std::invalid_argument("Vulkan RHI: copy resources belong to another device.");
            if (!Has(src.Desc().usage, TextureUsage::TransferSrc)
                || !Has(dst.Desc().usage, BufferUsage::TransferDst))
                throw std::invalid_argument("Vulkan RHI: image readback needs TransferSrc and TransferDst usage.");
            if (!HasAny(src.State(), ResourceState::CopySrc) || dst.State() != ResourceState::CopyDst)
                throw std::invalid_argument("Vulkan RHI: image readback requires CopySrc and CopyDst states.");
            if (IsRgbTextureFormat(src.Desc().format))
            { CopyRgbBufferTexture(dst.Native(), dst.Desc().size, src, region, false); return; }
            CopyPhysicalTextureToBuffer(src, dst, region);
        }

        void VulkanCommandList::CopyPhysicalTextureToBuffer(const VulkanTexture& src,
            const VulkanBuffer& dst, const BufferTextureCopy& region)
        {
            const VkBufferImageCopy copy = ToVkBufferImageCopy(dst.Desc(), src.Desc(), region);
            // Combined read permissions retain GENERAL. The copy layout must
            // agree with the explicit transition; copying does not narrow it.
            const VkImageLayout sourceLayout = ToVkState(src.State(), true).Layout;
            PrepareTransfer();
            _device->ContextPointer->_impl->vkCmdCopyImageToBuffer(_commandSlots->Buffer(),
                src.Native(), sourceLayout, dst.Native(), 1, &copy);
        }

        void VulkanCommandList::CopyRgbBufferTexture(VkBuffer buffer, VkDeviceSize bufferBytes, const VulkanTexture& texture,
            const BufferTextureCopy& region, bool upload)
        {
            const auto plan = VulkanRgbTransfer::Describe(texture.Desc(), bufferBytes, region);
            PrepareTransfer();
            const auto scratch = _commandSlots->Scratch().Allocate(plan.ScratchBytes, plan.NativeBytes);
            const auto copy = plan.ImageCopy(scratch.Offset);
            auto& vk = *_device->ContextPointer->_impl;
            const auto commands = _commandSlots->Buffer();
            const auto barrier = [&](VkAccessFlags2 destination) {
                VkBufferMemoryBarrier2 memory{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
                memory.srcStageMask = memory.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memory.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT; memory.dstAccessMask = destination;
                memory.srcQueueFamilyIndex = memory.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                memory.buffer = scratch.Buffer; memory.offset = scratch.Offset; memory.size = scratch.Size;
                VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &memory;
                vk.vkCmdPipelineBarrier2(commands, &dependency);
            };
            if (upload)
            {
                vk.vkCmdFillBuffer(commands, scratch.Buffer, scratch.Offset, scratch.Size, plan.AlphaWord);
                barrier(VK_ACCESS_2_TRANSFER_WRITE_BIT);
                plan.BufferCopies(scratch.Offset, true, [&](auto batch) {
                    vk.vkCmdCopyBuffer(commands, buffer, scratch.Buffer, static_cast<std::uint32_t>(batch.size()), batch.data());
                });
                barrier(VK_ACCESS_2_TRANSFER_READ_BIT);
                vk.vkCmdCopyBufferToImage(commands, scratch.Buffer, texture.Native(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            }
            else
            {
                vk.vkCmdCopyImageToBuffer(commands, texture.Native(), ToVkState(texture.State(), true).Layout,
                    scratch.Buffer, 1, &copy);
                barrier(VK_ACCESS_2_TRANSFER_READ_BIT);
                plan.BufferCopies(scratch.Offset, false, [&](auto batch) {
                    vk.vkCmdCopyBuffer(commands, scratch.Buffer, buffer, static_cast<std::uint32_t>(batch.size()), batch.data());
                });
            }
        }

        void VulkanCommandList::Transition(Buffer& resource, ResourceState before, ResourceState after)
        {
            RequireRecording();
            auto& buffer = CheckedResource<VulkanBuffer>(resource, _device);
            if (buffer.DeviceState() != _device)
                throw std::invalid_argument("Vulkan RHI: buffer belongs to another device.");
            if (buffer.State() != before || !IsValidTransition(before, after)
                || !IsValidBufferState(buffer.Desc(), before) || !IsValidBufferState(buffer.Desc(), after))
                throw std::invalid_argument("Vulkan RHI: buffer transition does not match its tracked state or declared usage.");
            PrepareTransfer();
            const StateMapping src = ToVkState(before, false);
            const StateMapping dst = ToVkState(after, false);
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = src.Stages;
            barrier.srcAccessMask = src.Access;
            barrier.dstStageMask = dst.Stages;
            barrier.dstAccessMask = dst.Access;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer.Native();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            _device->ContextPointer->_impl->vkCmdPipelineBarrier2(_commandSlots->Buffer(), &dependency);
            buffer.State(after);
        }

        void VulkanCommandList::Transition(Texture& resource, ResourceState before, ResourceState after)
        {
            RequireRecording();
            auto& texture = CheckedResource<VulkanTexture>(resource, _device);
            if (texture.DeviceState() != _device)
                throw std::invalid_argument("Vulkan RHI: texture belongs to another device.");
            if (texture.State() != before || !IsValidTransition(before, after)
                || !IsValidTextureState(texture.Desc(), before) || !IsValidTextureState(texture.Desc(), after))
                throw std::invalid_argument("Vulkan RHI: image transition does not match its tracked state, usage or format.");
            PrepareTransfer();
            // Preparing the pending clear can implicitly transition this very
            // attachment. Use its resulting native layout for the barrier.
            const bool storageOnly = Has(texture.Desc().usage, TextureUsage::Storage)
                && !Has(texture.Desc().usage, TextureUsage::Sampled);
            const StateMapping src = ToVkState(texture.State(), true, storageOnly);
            const StateMapping dst = ToVkState(after, true, storageOnly);
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = src.Stages;
            barrier.srcAccessMask = src.Access;
            barrier.dstStageMask = dst.Stages;
            barrier.dstAccessMask = dst.Access;
            barrier.oldLayout = src.Layout;
            barrier.newLayout = dst.Layout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture.Native();
            barrier.subresourceRange.aspectMask = VulkanResources::AspectMask(texture.Desc().format);
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = texture.Desc().mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = texture.Desc().arrayLayers;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            _device->ContextPointer->_impl->vkCmdPipelineBarrier2(_commandSlots->Buffer(), &dependency);
            texture.State(after);
        }
