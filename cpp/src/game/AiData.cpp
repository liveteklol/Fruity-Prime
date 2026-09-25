#include "AiData.h"

#include "formats/Model.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <queue>
#include <stdexcept>

namespace fp {

namespace {

template <typename T>
T readAt(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset + sizeof(T) > bytes.size()) {
        throw std::runtime_error("read past the end of the file");
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

// AiPersonalityData1.GetLabel: Root, A, B, ... Z, AA, AB, ...
std::string label(int id)
{
    std::string result = "Root";
    if (--id >= 0) {
        result.clear();
        while (id >= 0) {
            result.insert(result.begin(), "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[id % 26]);
            id = id / 26 - 1;
        }
    }
    return result;
}

} // namespace

std::unique_ptr<NodeData> NodeData::load(const std::filesystem::path& file)
{
    const std::vector<uint8_t> bytes = readFile(file);
    const uint16_t version = readAt<uint16_t>(bytes, 0);
    if (version != 6) {
        throw std::runtime_error("unexpected node data version " + std::to_string(version));
    }
    // NodeDataHeader (packed to 2)
    const uint16_t dataCount = readAt<uint16_t>(bytes, 2);
    const uint32_t indexOffset = readAt<uint32_t>(bytes, 4);
    const uint32_t dataOffset = readAt<uint32_t>(bytes, 8);
    const uint16_t indexCount = readAt<uint16_t>(bytes, 12);
    auto data = std::make_unique<NodeData>();
    for (uint16_t i = 0; i < indexCount; i++) {
        data->SetIndices.push_back(readAt<uint16_t>(bytes, indexOffset + i * 2u));
    }
    struct Raw3 {
        uint16_t nodeType, id, field4, count2;
        int32_t x, y, z, maxDistance;
        uint32_t offset1, offset2;
    };
    std::vector<std::vector<std::vector<Raw3>>> raw;
    uint32_t min = std::numeric_limits<uint32_t>::max();
    for (uint16_t i = 0; i < dataCount; i++) {
        const size_t str1 = dataOffset + i * 8u; // NodeDataStruct1
        const uint32_t offset2 = readAt<uint32_t>(bytes, str1);
        const uint16_t count2 = readAt<uint16_t>(bytes, str1 + 4);
        auto& sub = raw.emplace_back();
        for (uint16_t j = 0; j < count2; j++) {
            const size_t str2 = offset2 + j * 8u; // NodeDataStruct2
            const uint32_t offset3 = readAt<uint32_t>(bytes, str2);
            const uint16_t count3 = readAt<uint16_t>(bytes, str2 + 4);
            auto& list = sub.emplace_back();
            for (uint16_t k = 0; k < count3; k++) {
                const size_t str3 = offset3 + k * 36u; // NodeDataStruct3
                Raw3 r{};
                r.nodeType = readAt<uint16_t>(bytes, str3);
                r.id = readAt<uint16_t>(bytes, str3 + 2);
                r.field4 = readAt<uint16_t>(bytes, str3 + 4);
                r.count2 = readAt<uint16_t>(bytes, str3 + 6);
                r.x = readAt<int32_t>(bytes, str3 + 8);
                r.y = readAt<int32_t>(bytes, str3 + 12);
                r.z = readAt<int32_t>(bytes, str3 + 16);
                r.maxDistance = readAt<int32_t>(bytes, str3 + 20);
                r.offset1 = readAt<uint32_t>(bytes, str3 + 24);
                r.offset2 = readAt<uint32_t>(bytes, str3 + 28);
                min = std::min(min, r.offset1);
                list.push_back(r);
            }
        }
    }
    if (min < bytes.size()) {
        const size_t valueCount = (bytes.size() - min) / 2;
        data->Values.resize(valueCount);
        for (size_t i = 0; i < valueCount; i++) {
            data->Values[i] = readAt<uint16_t>(bytes, min + i * 2);
        }
    }
    for (const auto& sub : raw) {
        auto& newSub = data->Data.emplace_back();
        for (const auto& list : sub) {
            auto& newList = newSub.emplace_back();
            for (const Raw3& r : list) {
                NodeData3 node;
                node.NodeType = static_cast<fp::NodeType>(r.nodeType);
                node.Id = r.id;
                node.Field4 = r.field4;
                node.Count2 = r.count2;
                node.Position = {r.x / 4096.0f, r.y / 4096.0f, r.z / 4096.0f};
                node.MaxDistance = r.maxDistance / 4096.0f;
                node.Index1 = static_cast<int>((r.offset1 - min) / 2);
                node.Index2 = static_cast<int>((r.offset2 - min) / 2);
                node.Values = &data->Values;
                newList.push_back(node);
            }
        }
    }
    return data;
}

const NodeData3* NodeData::findClosestNode(const Vec3& position, bool useMaxDist) const
{
    if (Data.empty() || Data[0].empty()) {
        return nullptr;
    }
    const NodeData3* result = nullptr;
    float minDist = std::numeric_limits<float>::max();
    for (const NodeData3& node : Data[0][0]) {
        const Vec3 between = position - node.Position;
        const float dist = dot(between, between);
        if (dist < minDist) {
            result = &node;
            minDist = dist;
        }
    }
    if (result != nullptr && useMaxDist && minDist > result->MaxDistance * result->MaxDistance) {
        result = nullptr;
    }
    return result;
}

AiPersonality::AiPersonality(const std::filesystem::path& root)
    : m_bytes(readFile(resolveCaseInsensitive(root, "aiPersonalityData/aiPersonalityData.bin")))
{
}

int32_t AiPersonality::readInt(int offset) const { return readAt<int32_t>(m_bytes, static_cast<size_t>(offset)); }

const AiPersonalityData1* AiPersonality::load(int offset)
{
    AiPersonalityData1* data = parseData1(offset, 1).at(0);
    // SetLabels: breadth first, Root, A, B...
    int id = 0;
    std::queue<const AiPersonalityData1*> queue;
    queue.push(data);
    while (!queue.empty()) {
        const AiPersonalityData1* node = queue.front();
        queue.pop();
        const_cast<AiPersonalityData1*>(node)->Label = label(id++);
        for (const AiPersonalityData1* child : node->Data1) {
            queue.push(child);
        }
    }
    return data;
}

const std::vector<AiPersonalityData1*>& AiPersonality::parseData1(int offset, int count)
{
    if (auto it = m_data1Cache.find(offset); it != m_data1Cache.end()) {
        return it->second;
    }
    std::vector<AiPersonalityData1*> results;
    for (int i = 0; i < count; i++) {
        const int base = offset + i * 36; // AiData1
        const int field0 = readInt(base);
        const int data1Count = readInt(base + 4), data1Offset = readInt(base + 8);
        const int data2Count = readInt(base + 12), data2Offset = readInt(base + 16);
        const int data3aCount = readInt(base + 20), data3aOffset = readInt(base + 24);
        const int data3bCount = readInt(base + 28), data3bOffset = readInt(base + 32);
        AiPersonalityData1& node = m_data1.emplace_back();
        node.Func24Id = field0;
        if (data1Count > 0 && data1Offset != offset) {
            for (AiPersonalityData1* child : parseData1(data1Offset, data1Count)) {
                child->Parent = &node;
                node.Data1.push_back(child);
            }
        }
        if (data2Count > 0) {
            node.Data2 = parseData2(data2Offset, data2Count);
        }
        if (data3aCount > 0) {
            node.Data3a = parseData3(data3aOffset, data3aCount);
        }
        if (data3bCount > 0) {
            node.Data3b = parseData3(data3bOffset, data3bCount);
        }
        results.push_back(&node);
    }
    return m_data1Cache.emplace(offset, std::move(results)).first->second;
}

const std::vector<int>& AiPersonality::parseData3(int offset, int count)
{
    if (auto it = m_data3Cache.find(offset); it != m_data3Cache.end()) {
        return it->second;
    }
    std::vector<int> values;
    for (int i = 0; i < count; i++) {
        values.push_back(readInt(offset + i * 4));
    }
    return m_data3Cache.emplace(offset, std::move(values)).first->second;
}

const std::vector<const AiPersonalityData2*>& AiPersonality::parseData2(int offset, int count)
{
    if (auto it = m_data2Cache.find(offset); it != m_data2Cache.end()) {
        return it->second;
    }
    std::vector<const AiPersonalityData2*> results;
    for (int i = 0; i < count; i++) {
        const int base = offset + i * 24; // AiData2
        const int data5Type = readInt(base);
        const int data4Count = readInt(base + 4), data4Offset = readInt(base + 8);
        const int fieldC = readInt(base + 12), field10 = readInt(base + 16), data5Offset = readInt(base + 20);
        AiPersonalityData2& data2 = m_data2.emplace_back();
        data2.Data1SelectIndex = fieldC;
        data2.Weight = field10;
        if (data4Count > 0) {
            data2.Data4 = parseData4(data4Offset, data4Count);
        }
        data2.Func3Id = data5Type;
        data2.Parameters = data5Offset != 0 ? parseData5(data5Type, data5Offset) : &m_emptyParams;
        results.push_back(&data2);
    }
    return m_data2Cache.emplace(offset, std::move(results)).first->second;
}

const std::vector<const AiPersonalityData4*>& AiPersonality::parseData4(int offset, int count)
{
    if (auto it = m_data4Cache.find(offset); it != m_data4Cache.end()) {
        return it->second;
    }
    std::vector<const AiPersonalityData4*> results;
    for (int i = 0; i < count; i++) {
        const int base = offset + i * 8; // AiData4
        AiPersonalityData4& data4 = m_data4.emplace_back();
        data4.Func3Id = readInt(base);
        const int data5Offset = readInt(base + 4);
        data4.Parameters = data5Offset != 0 ? parseData5(data4.Func3Id, data5Offset) : &m_emptyParams;
        results.push_back(&data4);
    }
    return m_data4Cache.emplace(offset, std::move(results)).first->second;
}

const AiPersonalityData5* AiPersonality::parseData5(int type, int offset)
{
    // Not cached, as the C# does not: Param2 depends on the type.
    AiPersonalityData5& data5 = m_data5.emplace_back();
    data5.Param1 = readInt(offset);
    data5.Param2 = type == 210 ? readInt(offset + 4) : 0;
    data5.IsEmpty = false;
    return &data5;
}

} // namespace fp
