#include "MapData.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace {
    std::string Trim(const std::string& s)
    {
        size_t b = 0;
        size_t e = s.size();
        // 先頭のBOMも取り除く
        if (e >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
            b = 3;
        }
        while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) { ++b; }
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) { --e; }
        return s.substr(b, e - b);
    }

    std::vector<std::string> Split(const std::string& line)
    {
        std::vector<std::string> out;
        std::string cur;
        std::istringstream ss(line);
        while (std::getline(ss, cur, ',')) {
            out.push_back(Trim(cur));
        }
        return out;
    }

    // 文字を整数にする。数字でなければ false
    bool ToInt(const std::string& s, int& out)
    {
        if (s.empty()) {
            return false;
        }
        char* end = nullptr;
        const long v = std::strtol(s.c_str(), &end, 10);
        if (end == s.c_str() || *end != '\0') {
            return false;
        }
        out = static_cast<int>(v);
        return true;
    }

    bool ToFloat(const std::string& s, float& out)
    {
        if (s.empty()) {
            return false;
        }
        char* end = nullptr;
        const float v = std::strtof(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0') {
            return false;
        }
        out = v;
        return true;
    }
}

void MapData::Reset(int w, int h)
{
    width = std::clamp(w, 1, kMaxSize);
    height = std::clamp(h, 1, kMaxSize);
    cells.assign(static_cast<size_t>(width) * height, static_cast<std::uint8_t>(Empty));
    hasPlayerStart = false;
    startX = 0;
    startZ = 0;
    spawns.clear();
    gates.clear();
    buildings.clear();
}

int MapData::CountCells(std::uint8_t kind) const
{
    return static_cast<int>(std::count(cells.begin(), cells.end(), kind));
}

MapData::Gate MapData::MakeGate(int xa, int za, int xb, int zb, int area)
{
    Gate g;
    g.x0 = (std::min)(xa, xb);
    g.x1 = (std::max)(xa, xb);
    g.z0 = (std::min)(za, zb);
    g.z1 = (std::max)(za, zb);
    g.area = area;
    return g;
}

MapData::BuildingRect MapData::MakeBuilding(int xa, int za, int xb, int zb, int kind, int facing)
{
    BuildingRect b;
    b.x0 = (std::min)(xa, xb);
    b.x1 = (std::max)(xa, xb);
    b.z0 = (std::min)(za, zb);
    b.z1 = (std::max)(za, zb);
    b.kind = std::clamp(kind, 0, static_cast<int>(KindCount) - 1);
    b.facing = std::clamp(facing, 0, static_cast<int>(FacingCount) - 1);
    return b;
}

void MapData::FrontCell(const BuildingRect& b, int& x, int& z)
{
    switch (b.facing) {
    case FaceNorth: x = (b.x0 + b.x1) / 2; z = b.z1 + 1; break;
    case FaceWest:  x = b.x0 - 1;          z = (b.z0 + b.z1) / 2; break;
    case FaceEast:  x = b.x1 + 1;          z = (b.z0 + b.z1) / 2; break;
    default:        x = (b.x0 + b.x1) / 2; z = b.z0 - 1; break;
    }
}

bool MapData::IsInFront(const BuildingRect& b, int x, int z)
{
    switch (b.facing) {
    case FaceNorth: return x >= b.x0 && x <= b.x1 && z > b.z1 && z <= b.z1 + 2;
    case FaceWest:  return z >= b.z0 && z <= b.z1 && x < b.x0 && x >= b.x0 - 2;
    case FaceEast:  return z >= b.z0 && z <= b.z1 && x > b.x1 && x <= b.x1 + 2;
    default:        return x >= b.x0 && x <= b.x1 && z < b.z0 && z >= b.z0 - 2;
    }
}

bool MapData::PlaceBuilding(const BuildingRect& rect, std::string& why)
{
    if (static_cast<int>(buildings.size()) >= kMaxBuildings) {
        why = "建物が多すぎます";
        return false;
    }
    if (!InRange(rect.x0, rect.z0) || !InRange(rect.x1, rect.z1)) {
        why = "建物がマップの外に出ています";
        return false;
    }
    for (int z = rect.z0; z <= rect.z1; ++z) {
        for (int x = rect.x0; x <= rect.x1; ++x) {
            if (Get(x, z) != Empty) {
                why = "壁・樽・他の建物と重なっています（先に消してください）";
                return false;
            }
            if (hasPlayerStart && startX == x && startZ == z) {
                why = "自機の開始位置と重なっています";
                return false;
            }
            for (const Spawn& s : spawns) {
                if (s.x == x && s.z == z) {
                    why = "敵の出現点（ドア）と重なっています";
                    return false;
                }
            }
        }
    }
    for (int z = rect.z0; z <= rect.z1; ++z) {
        for (int x = rect.x0; x <= rect.x1; ++x) {
            Set(x, z, Building);
        }
    }
    buildings.push_back(rect);
    return true;
}

int MapData::BuildingAt(int x, int z) const
{
    for (size_t i = 0; i < buildings.size(); ++i) {
        const BuildingRect& b = buildings[i];
        if (x >= b.x0 && x <= b.x1 && z >= b.z0 && z <= b.z1) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MapData::RemoveBuilding(size_t index)
{
    if (index >= buildings.size()) {
        return;
    }
    const BuildingRect b = buildings[index];
    for (int z = b.z0; z <= b.z1; ++z) {
        for (int x = b.x0; x <= b.x1; ++x) {
            if (Get(x, z) == Building) {
                Set(x, z, Empty);
            }
        }
    }
    buildings.erase(buildings.begin() + static_cast<std::ptrdiff_t>(index));
}

bool MapData::RemoveBuildingAt(int x, int z)
{
    const int i = BuildingAt(x, z);
    if (i < 0) {
        return false;
    }
    RemoveBuilding(static_cast<size_t>(i));
    return true;
}

bool MapData::Save(const std::string& path, std::string& error) const
{
    if (width <= 0 || height <= 0) {
        error = "map is empty";
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot open file for writing: " + path;
        return false;
    }
    file << "WSMAP,1\n";
    file << "size," << width << "," << height << "\n";
    file << "cells\n";
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            if (x > 0) { file << ","; }
            file << static_cast<int>(Get(x, z));
        }
        file << "\n";
    }
    file << "end\n";
    if (hasPlayerStart) {
        file << "player," << startX << "," << startZ << "\n";
    }
    for (const Spawn& s : spawns) {
        file << "spawn," << s.x << "," << s.z << "," << s.area << "," << s.count << "," << s.interval << "\n";
    }
    for (const Gate& g : gates) {
        file << "gate," << g.x0 << "," << g.z0 << "," << g.x1 << "," << g.z1 << "," << g.area << "\n";
    }
    for (const BuildingRect& b : buildings) {
        file << "building," << b.x0 << "," << b.z0 << "," << b.x1 << "," << b.z1 << "," << b.kind << "," << b.facing << "\n";
    }
    file.flush();
    if (!file) {
        error = "write failed: " + path;
        return false;
    }
    return true;
}

bool MapData::Load(const std::string& path, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open file: " + path;
        return false;
    }

    // 失敗したときに今のマップを壊さないよう、いったん別のものに読んでから入れ替える
    MapData loaded;
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& message) {
        error = "line " + std::to_string(lineNo) + ": " + message;
        return false;
    };

    if (!std::getline(file, line)) {
        error = "file is empty";
        return false;
    }
    ++lineNo;
    {
        const std::vector<std::string> head = Split(Trim(line));
        int version = 0;
        if (head.size() != 2 || head[0] != "WSMAP" || !ToInt(head[1], version)) {
            return fail("not a WSMAP file");
        }
        if (version != 1) {
            return fail("unsupported version");
        }
    }

    bool haveSize = false;
    bool inCells = false;
    bool cellsDone = false;
    int cellRow = 0;

    while (std::getline(file, line)) {
        ++lineNo;
        const std::string text = Trim(line);
        if (text.empty() || text[0] == '#') {
            continue;
        }
        const std::vector<std::string> f = Split(text);

        if (inCells) {
            if (text == "end") {
                if (cellRow != loaded.height) {
                    return fail("too few cell rows");
                }
                inCells = false;
                cellsDone = true;
                continue;
            }
            if (cellRow >= loaded.height) {
                return fail("too many cell rows");
            }
            if (static_cast<int>(f.size()) != loaded.width) {
                return fail("cell row has wrong width");
            }
            for (int x = 0; x < loaded.width; ++x) {
                int v = 0;
                if (!ToInt(f[x], v) || v < 0 || v > 3) {
                    return fail("bad cell value");
                }
                loaded.cells[static_cast<size_t>(cellRow) * loaded.width + x] = static_cast<std::uint8_t>(v);
            }
            ++cellRow;
            continue;
        }

        if (f[0] == "size") {
            int w = 0, h = 0;
            if (f.size() != 3 || !ToInt(f[1], w) || !ToInt(f[2], h) || w < 1 || h < 1 || w > kMaxSize || h > kMaxSize) {
                return fail("bad size");
            }
            loaded.Reset(w, h);
            haveSize = true;
        }
        else if (f[0] == "cells") {
            if (!haveSize) {
                return fail("cells before size");
            }
            inCells = true;
            cellRow = 0;
        }
        else if (f[0] == "player") {
            int x = 0, z = 0;
            if (!haveSize || f.size() != 3 || !ToInt(f[1], x) || !ToInt(f[2], z) || !loaded.InRange(x, z)) {
                return fail("bad player");
            }
            loaded.hasPlayerStart = true;
            loaded.startX = x;
            loaded.startZ = z;
        }
        else if (f[0] == "spawn") {
            Spawn s;
            if (!haveSize || f.size() != 6 || !ToInt(f[1], s.x) || !ToInt(f[2], s.z) || !ToInt(f[3], s.area)
                || !ToInt(f[4], s.count) || !ToFloat(f[5], s.interval) || !loaded.InRange(s.x, s.z)) {
                return fail("bad spawn");
            }
            if (static_cast<int>(loaded.spawns.size()) >= kMaxSpawns) {
                return fail("too many spawns");
            }
            loaded.spawns.push_back(s);
        }
        else if (f[0] == "gate") {
            int x0 = 0, z0 = 0, x1 = 0, z1 = 0, area = 0;
            if (!haveSize || f.size() != 6 || !ToInt(f[1], x0) || !ToInt(f[2], z0) || !ToInt(f[3], x1)
                || !ToInt(f[4], z1) || !ToInt(f[5], area) || !loaded.InRange(x0, z0) || !loaded.InRange(x1, z1)) {
                return fail("bad gate");
            }
            if (static_cast<int>(loaded.gates.size()) >= kMaxGates) {
                return fail("too many gates");
            }
            loaded.gates.push_back(MakeGate(x0, z0, x1, z1, area));
        }
        else if (f[0] == "building") {
            int x0 = 0, z0 = 0, x1 = 0, z1 = 0, kind = 0, facing = 0;
            if (!haveSize || (f.size() != 6 && f.size() != 7) || !ToInt(f[1], x0) || !ToInt(f[2], z0) || !ToInt(f[3], x1)
                || !ToInt(f[4], z1) || !ToInt(f[5], kind) || !loaded.InRange(x0, z0) || !loaded.InRange(x1, z1)
                || kind < 0 || kind >= KindCount) {
                return fail("bad building");
            }
            if (f.size() == 7 && (!ToInt(f[6], facing) || facing < 0 || facing >= FacingCount)) {
                return fail("bad building facing");
            }
            if (static_cast<int>(loaded.buildings.size()) >= kMaxBuildings) {
                return fail("too many buildings");
            }
            loaded.buildings.push_back(MakeBuilding(x0, z0, x1, z1, kind, facing));
        }
        else {
            return fail("unknown line: " + f[0]);
        }
    }

    if (!haveSize || !cellsDone) {
        error = "file is incomplete";
        return false;
    }

    // 建物の範囲のマスを Building にそろえる。どの建物にも属さない Building のマスは壁にする
    {
        std::vector<char> covered(loaded.cells.size(), 0);
        for (const BuildingRect& b : loaded.buildings) {
            for (int z = b.z0; z <= b.z1; ++z) {
                for (int x = b.x0; x <= b.x1; ++x) {
                    covered[static_cast<size_t>(z) * loaded.width + x] = 1;
                }
            }
        }
        for (size_t i = 0; i < loaded.cells.size(); ++i) {
            if (covered[i]) {
                loaded.cells[i] = static_cast<std::uint8_t>(Building);
            }
            else if (loaded.cells[i] == Building) {
                loaded.cells[i] = static_cast<std::uint8_t>(Wall);
            }
        }
    }

    *this = std::move(loaded);
    return true;
}

std::vector<std::string> MapData::Validate() const
{
    std::vector<std::string> problems;
    if (!hasPlayerStart) {
        problems.push_back("自機の開始位置が置かれていません");
    }
    else if (IsSolid(startX, startZ)) {
        problems.push_back("自機の開始位置が壁・樽の上にあります");
    }
    if (spawns.empty()) {
        problems.push_back("敵の出現点（ドア）が1つもありません");
    }
    for (size_t i = 0; i < spawns.size(); ++i) {
        if (IsSolid(spawns[i].x, spawns[i].z)) {
            problems.push_back("出現点 " + std::to_string(i) + " が壁・樽の上にあります");
        }
        if (spawns[i].count <= 0) {
            problems.push_back("出現点 " + std::to_string(i) + " の出す数が0以下です");
        }
    }
    for (size_t i = 0; i < gates.size(); ++i) {
        bool hasSpawn = false;
        for (const Spawn& s : spawns) {
            if (s.area == gates[i].area) {
                hasSpawn = true;
                break;
            }
        }
        if (!hasSpawn) {
            problems.push_back("区切り " + std::to_string(i) + " の区切り番号に、出現点がありません（開かないままになります）");
        }
    }
    for (size_t i = 0; i < buildings.size(); ++i) {
        const BuildingRect& b = buildings[i];
        if (b.kind != Saloon) {
            continue;
        }
        // 酒場の正面のすぐ前に、出現点（ドア）があるか
        bool hasDoor = false;
        for (const Spawn& s : spawns) {
            if (IsInFront(b, s.x, s.z)) {
                hasDoor = true;
                break;
            }
        }
        if (!hasDoor) {
            problems.push_back("酒場 " + std::to_string(i) + " の正面に、敵の出現点（ドア）がありません");
        }
    }
    return problems;
}
