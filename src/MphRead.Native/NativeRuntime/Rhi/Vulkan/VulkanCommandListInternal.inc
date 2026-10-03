// Part of VulkanGraphicsDevice.cpp (one translation unit, like the other
// Vulkan *Internal.inc files): the command list -- recording, draw
// state, bindings, transfers and transitions -- kept apart from the device
// that owns and schedules it.
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
            [[nodiscard]] VulkanGraphicsPipeline& VariantFor(const VulkanSceneProgram& program, bool lines);
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

            const VulkanSceneProgram* _setProgram = nullptr;
            struct SampledDescriptor final
            {
                VkImageView View = VK_NULL_HANDLE;
                VkSampler Sampler = VK_NULL_HANDLE;
                VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
                std::uint64_t Identity = 0, Generation = 0, SamplerIdentity = 0;
                bool operator==(const SampledDescriptor&) const = default;
            };
            std::array<SampledDescriptor, 4> _setTextures{};
            std::vector<std::pair<RingSlice, std::uint64_t>> _uniformSlices;
            std::array<VkDescriptorSet, SceneShaderAbi::GroupCount> _sets{};
            struct DescriptorKey final
            {
                std::array<std::uint64_t, 64> Words{};
                bool operator==(const DescriptorKey&) const = default;
            };
            struct DescriptorHash final
            {
                std::size_t operator()(const DescriptorKey& key) const noexcept
                {
                    std::size_t hash = 0;
                    for (const auto word : key.Words)
                        hash ^= std::hash<std::uint64_t>{}(word) + 0x9e3779b9U + (hash << 6) + (hash >> 2);
                    return hash;
                }
            };
            // Cleared at each native buffer/pool generation. Bounded; overflow
            // records an uncached set in the existing slot allocator.
            std::unordered_map<DescriptorKey, VkDescriptorSet, DescriptorHash> _descriptorCache;
            std::map<std::array<std::uint64_t, 3>, RingSlice> _uniformCache;
            std::array<VkDescriptorSet, SceneShaderAbi::GroupCount> _boundSceneSets{};
            PipelineLayout _boundSceneLayout{};
            std::uint32_t _boundSmallBytes = 0;
            std::unordered_map<const void*, std::uint64_t> _programIdentities;
            std::uint64_t _metricFixedSlots = 0, _metricFixedOverflow = 0;
            std::uint64_t _metricOptionalSkips = 0;
            struct PersistentMaterialKey final
            {
                DescriptorKey Objects;
                std::array<std::byte, 1024> Data{};
                std::uint32_t Size = 0;
                bool operator==(const PersistentMaterialKey&) const = default;
            };
            struct PersistentMaterialHash final
            {
                std::size_t operator()(const PersistentMaterialKey& key) const noexcept
                {
                    return DescriptorHash{}(key.Objects) ^ std::hash<std::string_view>{}(
                        {reinterpret_cast<const char*>(key.Data.data()), key.Size});
                }
            };
            struct PersistentMaterial final
            {
                VkDescriptorSet Set;
                VulkanResources::BufferStorage Uniform;
            };
            std::unordered_map<PersistentMaterialKey, PersistentMaterial, PersistentMaterialHash> _persistentMaterials;
            std::shared_ptr<VulkanDescriptorAllocator> _persistentDescriptors;
            std::uint64_t _metricPersistentHits = 0, _metricPersistentMisses = 0, _metricPersistentFull = 0;
            VkDescriptorSet PersistentMaterialSet(const VulkanSceneProgram&, const std::array<SampledDescriptor, 4>&);
            void ClearPersistentMaterials();
            // Opt-in instrumentation: no per-draw clocks or allocations.
            bool _metricsEnabled = std::getenv("FRUITY_RENDER_METRICS") != nullptr;
            std::uint64_t _metricDraws = 0, _metricAllocations = 0, _metricUpdates = 0,
                _metricBinds = 0, _metricPipelines = 0, _metricHits = 0, _metricMisses = 0,
                _metricSmallConstants = 0;

            std::unique_ptr<VulkanTexture> _dummy;
            std::unique_ptr<VulkanSampler> _dummySampler;
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
                    _descriptorCache.clear(); _boundSceneSets.fill(VK_NULL_HANDLE);
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
            _variants.clear();
            _genericSets.clear(); _vertexBindings.clear(); _indexBinding = {};
            _dummy.reset();
            if (_transferOnly) --_device->UploadCommandLists;
            else if (--_device->CommandLists == 0) _device->ReleaseWindowTarget();
            _dummySampler.reset();
            if (_commandSlots) _commandSlots->Close(_device->Closing);
            _commandSlots.reset();
            _recording = _autoRestart = _renderingActive = _renderingOpen = _clearsPending = false;
            _target = {}; _pipeline = nullptr; _units = {}; _setProgram = nullptr; _sets.fill(VK_NULL_HANDLE);
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
            _sets.fill(VK_NULL_HANDLE);
            _setProgram = nullptr;
            _device->SceneFlushers[this] = [this] { Flush(); };
            _descriptorCache.clear(); _uniformCache.clear();
            _boundSceneSets.fill(VK_NULL_HANDLE); _boundSceneLayout = {}; _boundSmallBytes = 0;
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
                    << " optional_work_busy_skip_count=" << _metricOptionalSkips << '\n';
            _sets.fill(VK_NULL_HANDLE);
            _setProgram = nullptr;
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
            ClearPersistentMaterials();
            if (const auto found = _programIdentities.find(object); found != _programIdentities.end())
            {
                for (std::uint32_t group = 0; group < SceneShaderAbi::GroupCount; ++group)
                    _commandSlots->RetireDescriptors((found->second << 3) | group);
                _programIdentities.erase(found);
            }
            _descriptorCache.clear(); _uniformCache.clear(); _boundSceneSets.fill(VK_NULL_HANDLE);
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
            if (_setProgram == object) _setProgram = nullptr;
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
            depth.format = TextureFormat::D24UnormS8Uint;
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
            if (native->IsDeferred())
            {
                // glUseProgram: a pass naming no program leaves the last one current.
                if (const auto* fragment = dynamic_cast<const VulkanShader*>(native->Desc().fragmentShader))
                    _device->CurrentSceneProgram = fragment->SceneProgram;
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
                    vk.vkCmdBindDescriptorSets(_commandSlots->Buffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                        _pipeline->Layout(), slot, 1, &set.Native, 0, nullptr);
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

        VulkanGraphicsPipeline& VulkanCommandList::VariantFor(const VulkanSceneProgram& program, bool lines)
        {
            const VariantKey key{_pipeline, &program,
                _target.Color ? _target.Color->Desc().format : TextureFormat::Undefined,
                _target.Depth ? _target.Depth->Desc().format : TextureFormat::Undefined, lines};
            const auto found = _variants.find(key);
            if (found != _variants.end() && found->second.BaseDesc == _pipeline->Desc())
                return *found->second.Native;
            GraphicsPipelineDesc desc = _pipeline->Desc();
            const GraphicsPipelineDesc base = desc;
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
            auto native = std::make_shared<VulkanGraphicsPipeline>(_device, desc, true);
            auto& entry = _variants[key];
            entry.BaseDesc = base;
            entry.Native = std::move(native);
            return *entry.Native;
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
            for (const auto& [key, entry] : entries)
            {
                _device->Retire([allocator = _device->Allocator, buffer = entry.Uniform] {
                    vmaDestroyBuffer(allocator, buffer.Buffer, buffer.Allocation);
                });
                --_device->Buffers;
            }
            if (descriptors) _device->Retire([descriptors = std::move(descriptors)] { descriptors->Close(); });
            _sets.fill(VK_NULL_HANDLE);
        }

        VkDescriptorSet VulkanCommandList::PersistentMaterialSet(const VulkanSceneProgram& program,
            const std::array<SampledDescriptor, 4>& textures)
        {
            if (program.Id != SceneProgram::Main) return VK_NULL_HANDLE;
            const auto group = static_cast<std::uint32_t>(SceneShaderAbi::Group::Material);
            const auto foundBlock = std::find_if(program.Blocks.begin(), program.Blocks.end(),
                [group](const auto& block) { return !block.Small && block.Group == group; });
            if (foundBlock == program.Blocks.end() || foundBlock->Data.size() > 1024) return VK_NULL_HANDLE;
            PersistentMaterialKey key;
            key.Objects.Words[0] = program.Identity;
            key.Objects.Words[1] = group;
            std::size_t word = 2;
            for (const auto& binding : program.Textures)
            {
                if (binding.group != group) continue;
                const auto& texture = textures[binding.unit];
                key.Objects.Words.at(word++) = texture.Identity; key.Objects.Words.at(word++) = texture.Generation;
                key.Objects.Words.at(word++) = texture.SamplerIdentity;
                key.Objects.Words.at(word++) = reinterpret_cast<std::uint64_t>(texture.View);
                key.Objects.Words.at(word++) = reinterpret_cast<std::uint64_t>(texture.Sampler);
                key.Objects.Words.at(word++) = texture.Layout;
            }
            key.Size = static_cast<std::uint32_t>(foundBlock->Data.size());
            std::copy(foundBlock->Data.begin(), foundBlock->Data.end(), key.Data.begin());
            if (const auto found = _persistentMaterials.find(key); found != _persistentMaterials.end())
            {
                if (_metricsEnabled) ++_metricPersistentHits;
                return found->second.Set;
            }
            // Immutable entries are never evicted or overwritten in flight.
            // A full cache uses the frame-local/fixed/generic path unchanged.
            if (_persistentMaterials.size() == 256)
            { if (_metricsEnabled) ++_metricPersistentFull; return VK_NULL_HANDLE; }
            if (_metricsEnabled) ++_metricPersistentMisses;
            if (!_persistentDescriptors)
            {
                _persistentDescriptors = _device->MakeDescriptors({256, {256, 0, 256, 0, 256}});
                _persistentDescriptors->ResetAfterCompletion({0});
            }
            auto& vk = *_device->ContextPointer->_impl;
            const auto uniform = _device->Resources->CreateBuffer({key.Size, BufferUsage::Uniform, MemoryUsage::CpuToGpu});
            try
            {
                if (!uniform.Mapped) throw std::runtime_error("Persistent material buffer is not mapped.");
                std::memcpy(uniform.Mapped, key.Data.data(), key.Size);
                Check(vmaFlushAllocation(_device->Allocator, uniform.Allocation, 0, key.Size), "vmaFlushAllocation(material)");
                const auto& layout = *program.Layouts[group];
                const auto set = _persistentDescriptors->Allocate(layout.Native(), layout.Desc());
                std::array<VkWriteDescriptorSet, 9> writes{};
                const VkDescriptorBufferInfo buffer{uniform.Buffer, 0, key.Size};
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
                _persistentMaterials.emplace(std::move(key), PersistentMaterial{set, uniform});
                ++_device->Buffers;
                if (_metricsEnabled) { ++_metricAllocations; ++_metricUpdates; }
                return set;
            }
            catch (...) { vmaDestroyBuffer(_device->Allocator, uniform.Buffer, uniform.Allocation); throw; }
        }

        void VulkanCommandList::DrawScene(const SceneDraw& draw)
        {
            if (draw.IndexCount == 0) return;
            if (_metricsEnabled) ++_metricDraws;
            RequireRecording();
            if (!_renderingOpen) throw std::logic_error("Vulkan RHI: scene draw outside rendering.");
            if (!_pipeline || !_pipeline->IsDeferred())
                throw std::logic_error("Vulkan RHI: scene draw needs a scene pass pipeline.");
            auto* program = static_cast<VulkanSceneProgram*>(_device->CurrentSceneProgram);
            if (const auto* fragment = dynamic_cast<const VulkanShader*>(_pipeline->Desc().fragmentShader))
                program = static_cast<VulkanSceneProgram*>(fragment->SceneProgram);
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

            VulkanGraphicsPipeline& native = VariantFor(*program, draw.Lines);
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
                compatible = compatible && group < _boundSceneLayout.groups.size()
                    && _boundSceneLayout.groups[group] == groups[group];
                if (!compatible) _boundSceneSets[group] = VK_NULL_HANDLE;
            }
            _boundSceneLayout = native.Desc().pipelineLayout;
            _boundSmallBytes = native.Desc().smallConstantBytes;
            ApplyDynamicState();

            const auto alphaMode = static_cast<std::int32_t>(_pipeline->Desc().alphaTest);
            program->Write("alpha_test", &alphaMode, sizeof(alphaMode), SceneShaderAbi::ValueType::Int);
            if (_setProgram != program)
            {
                _uniformSlices.assign(program->Blocks.size(), {});
                _sets.fill(VK_NULL_HANDLE);
                _setTextures = {};
                _setProgram = program;
            }
            for (const auto& binding : program->Textures)
                if (textures[binding.unit] != _setTextures[binding.unit])
                    _sets[binding.group] = VK_NULL_HANDLE;
            const auto alignment = _device->UniformAlignment;
            for (std::size_t i = 0; i < program->Blocks.size(); ++i)
            {
                const auto& block = program->Blocks[i];
                if (block.Small)
                {
                    if (block.Data.size() != sizeof(SmallDrawConstants))
                        throw std::logic_error("Vulkan RHI: small constant ABI drift.");
                    SmallDrawConstants constants;
                    std::memcpy(&constants, block.Data.data(), sizeof(constants));
                    SetSmallConstants(constants);
                    continue;
                }
                auto& [slice, generation] = _uniformSlices[i];
                if (program->Id == SceneProgram::Main
                    && block.Group == static_cast<std::uint32_t>(SceneShaderAbi::Group::Material))
                {
                    if (_sets[block.Group] && generation == block.Generation) continue;
                    if (const auto persistent = PersistentMaterialSet(*program, textures))
                    {
                        _sets[block.Group] = persistent;
                        generation = block.Generation;
                        slice = {};
                        continue;
                    }
                }
                if (!slice.Buffer || generation != block.Generation)
                {
                    const std::array key{program->Identity, static_cast<std::uint64_t>(i), block.Generation};
                    if (const auto found = _uniformCache.find(key); found != _uniformCache.end()) slice = found->second;
                    else
                    {
                        slice = Allocate(block.Data.size(), alignment);
                        std::memcpy(slice.Data, block.Data.data(), block.Data.size());
                        if (_uniformCache.size() < 16384) _uniformCache.emplace(key, slice);
                    }
                    generation = block.Generation;
                    _sets[block.Group] = VK_NULL_HANDLE;
                }
            }
            for (std::uint32_t group = 0; group < SceneShaderAbi::GroupCount; ++group)
            {
                const auto& layout = *program->Layouts[group];
                if (layout.Desc().entries.empty()) continue;
                if (!_sets[group])
                {
                    DescriptorKey key{};
                    std::size_t k = 0;
                    key.Words[k++] = program->Identity; key.Words[k++] = group;
                    for (std::size_t i = 0; i < program->Blocks.size(); ++i)
                    {
                        const auto& block = program->Blocks[i];
                        if (block.Small || block.Group != group) continue;
                        const auto& slice = _uniformSlices[i].first;
                        key.Words.at(k++) = reinterpret_cast<std::uint64_t>(slice.Buffer);
                        key.Words.at(k++) = slice.Offset; key.Words.at(k++) = block.Data.size();
                    }
                    for (const auto& binding : program->Textures)
                    {
                        if (binding.group != group) continue;
                        const auto& texture = textures[binding.unit];
                        key.Words.at(k++) = texture.Identity; key.Words.at(k++) = texture.Generation;
                        key.Words.at(k++) = texture.SamplerIdentity;
                        key.Words.at(k++) = reinterpret_cast<std::uint64_t>(texture.View);
                        key.Words.at(k++) = reinterpret_cast<std::uint64_t>(texture.Sampler);
                        key.Words.at(k++) = texture.Layout;
                    }
                    if (const auto found = _descriptorCache.find(key); found != _descriptorCache.end())
                    {
                        _sets[group] = found->second;
                        if (_metricsEnabled) ++_metricHits;
                    }
                    else
                    {
                        if (_metricsEnabled) ++_metricMisses;
                        _programIdentities.try_emplace(program, program->Identity);
                        const auto fixedIdentity = (program->Identity << 3) | group;
                        // First use, only this command list's current slot. Small
                        // bounded page; all overflow uses the ordinary allocator.
                        _commandSlots->Descriptors().Preallocate(fixedIdentity, layout.Native(), layout.Desc(), 32);
                        auto set = _commandSlots->Descriptors().AllocateFixed(fixedIdentity);
                        if (set)
                        {
                            if (_metricsEnabled) ++_metricFixedSlots;
                        }
                        else
                        {
                            set = AllocateSet(layout);
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
                            const auto& block = program->Blocks[i];
                            if (block.Small || block.Group != group) continue;
                            const auto& slice = _uniformSlices[i].first;
                            buffers[i] = {slice.Buffer, slice.Offset, block.Data.size()};
                            write(block.Binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER).pBufferInfo = &buffers[i];
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
                        _sets[group] = set;
                        if (_descriptorCache.size() < 16384) _descriptorCache.emplace(key, set);
                    }
                }
                if (_boundSceneSets[group] != _sets[group])
                {
                    vk.vkCmdBindDescriptorSets(_commandSlots->Buffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                        native.Layout(), group, 1, &_sets[group], 0, nullptr);
                    if (_metricsEnabled) ++_metricBinds;
                    _boundSceneSets[group] = _sets[group];
                }
            }
            _setTextures = textures;

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
