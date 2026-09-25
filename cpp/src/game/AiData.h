#pragma once

#include "Collision.h"

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The bots' data: the room's node graph (levels/nodeData, Formats/NodeData.cs)
// and their behavior trees (aiPersonalityData.bin, Formats/AiPersonality.cs).
namespace fp {

enum class NodeType : uint16_t { Navigation = 0, Special = 1, Aerial = 2, Vantage = 3, AltForm = 4, Hazard = 5 };

// NodeData3: one node. Index1 and Index2 point into Values, the file's shared
// list of node indices (Index1 has no count of its own, Index2 has Count2).
struct NodeData3 {
    fp::NodeType NodeType = fp::NodeType::Navigation;
    uint16_t Id = 0;
    uint32_t Field4 = 0;
    int Count2 = 0;
    Vec3 Position{};
    float MaxDistance = 0;
    int Index1 = 0, Index2 = 0;
    const std::vector<uint16_t>* Values = nullptr;
};

// NodeData: sets of lists of nodes. Nodes never move once loaded, so the
// bots keep pointers to them.
struct NodeData {
    std::vector<uint16_t> SetIndices;
    std::vector<std::vector<std::vector<NodeData3>>> Data;
    std::vector<uint16_t> Values;
    // Which sets are switched on (the bots switch them); the set index is their bits.
    mutable std::array<bool, 16> SetSelector{};
    bool Simple() const { return Data.size() == 1 && Data[0].size() == 1; }

    // ReadNodeData.ReadData (version 6); throws on anything else.
    static std::unique_ptr<NodeData> load(const std::filesystem::path& file);
    // ReadNodeData.FindClosestNode, over the first list.
    const NodeData3* findClosestNode(const Vec3& position, bool useMaxDist = false) const;
};

struct AiPersonalityData5 {
    int Param1 = 0;
    int Param2 = 0;
    bool IsEmpty = true;
};

struct AiPersonalityData4 {
    int Func3Id = 0;
    const AiPersonalityData5* Parameters = nullptr;
};

struct AiPersonalityData2 {
    int Data1SelectIndex = 0;
    int Weight = 0;
    std::vector<const AiPersonalityData4*> Data4;
    int Func3Id = 0;
    const AiPersonalityData5* Parameters = nullptr;
};

// AiPersonalityData1: a node of a behavior tree.
struct AiPersonalityData1 {
    std::string Label = "?";
    int Func24Id = 0;
    std::vector<const AiPersonalityData1*> Data1;
    std::vector<const AiPersonalityData2*> Data2;
    std::vector<int> Data3a, Data3b;
    const AiPersonalityData1* Parent = nullptr;
};

// AiPersonality.LoadData: the trees, read once per file and shared by every bot.
class AiPersonality {
public:
    explicit AiPersonality(const std::filesystem::path& root);
    // The tree at `offset` (AiPersonality.LoadAll picks it by game mode).
    const AiPersonalityData1* load(int offset);

private:
    const std::vector<AiPersonalityData1*>& parseData1(int offset, int count);
    const std::vector<const AiPersonalityData2*>& parseData2(int offset, int count);
    const std::vector<const AiPersonalityData4*>& parseData4(int offset, int count);
    const AiPersonalityData5* parseData5(int type, int offset);
    const std::vector<int>& parseData3(int offset, int count);
    int32_t readInt(int offset) const;

    std::vector<uint8_t> m_bytes;
    std::deque<AiPersonalityData1> m_data1;
    std::deque<AiPersonalityData2> m_data2;
    std::deque<AiPersonalityData4> m_data4;
    std::deque<AiPersonalityData5> m_data5;
    std::map<int, std::vector<AiPersonalityData1*>> m_data1Cache;
    std::map<int, std::vector<const AiPersonalityData2*>> m_data2Cache;
    std::map<int, std::vector<const AiPersonalityData4*>> m_data4Cache;
    std::map<int, std::vector<int>> m_data3Cache;
    AiPersonalityData5 m_emptyParams;
};

} // namespace fp
