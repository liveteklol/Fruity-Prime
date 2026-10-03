OpenGlTimestampSet::OpenGlTimestampSet(OpenGlGraphicsDevice& device, std::uint32_t count,
    std::string label, std::shared_ptr<void> reservation)
    : Device(&device), Writes(count), Label(std::move(label)), _properties(device.TimestampInfo()),
      _reservation(std::move(reservation))
{
    device.Api().GenQueries(static_cast<int>(count), Names.data());
    try { device.CheckStorageResult("glGenQueries"); device.Register(*this); }
    catch (...) { device.Api().DeleteQueries(static_cast<int>(count), Names.data()); throw; }
}
OpenGlTimestampSet::~OpenGlTimestampSet() { Close(); }
void OpenGlTimestampSet::Close(bool shutdown) noexcept
{
    if (!Device) return;
    auto& device = *Device;
    for (std::uint32_t i = 0; i < Count(); ++i)
    {
        GlObject object{GlObject::Kind::Query, static_cast<int>(Names[i]), device.Api().DeleteQueries, _reservation};
        if (shutdown) DestroyNative(object); else device.Retire(object);
    }
    device.Unregister(*this); Device = nullptr; _reservation.reset(); Names.fill(0);
}
TimestampStatus OpenGlTimestampSet::ReadResults(std::span<std::uint64_t> destination)
{
    if (destination.size() != Count()) throw std::invalid_argument("GPU timestamp destination count differs.");
    if (!Device) return TimestampStatus::Cancelled;
    if (!Writes.AllWritten()) return TimestampStatus::Pending;
    for (std::uint32_t i = 0; i < Count(); ++i)
    {
        int available = 0; Device->Api().GetQueryObjectiv(Names[i], 0x8867, &available);
        Device->CheckStorageResult("glGetQueryObjectiv(availability)");
        if (!available) return TimestampStatus::Pending;
    }
    std::array<std::uint64_t, 64> values{};
    for (std::uint32_t i = 0; i < Count(); ++i) Device->Api().GetQueryObjectui64v(Names[i], 0x8866, &values[i]);
    Device->CheckStorageResult("glGetQueryObjectui64v");
    std::copy_n(values.begin(), Count(), destination.begin());
    return TimestampStatus::Ready;
}
