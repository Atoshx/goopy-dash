// cupread: minimal reader for Cuphead's Unity 2017.4 data (UnityFS bundles,
// serialized files with type trees, sprite atlases, BC7 textures, AudioClips).
#include "cupread.hpp"
#include "CommonStrings.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "lz4.h"
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

namespace fs = std::filesystem;

namespace cupread {
namespace {

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };

std::vector<uint8_t> readFile(fs::path const& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Error("cannot open " + p.filename().string());
    f.seekg(0, std::ios::end);
    auto n = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> d(n);
    f.read(reinterpret_cast<char*>(d.data()), n);
    return d;
}

// ---------------------------------------------------------------- reader
struct Reader {
    uint8_t const* data;
    size_t size;
    size_t pos = 0;
    bool big = false;

    Reader(uint8_t const* d, size_t n, bool bigEndian = false) : data(d), size(n), big(bigEndian) {}

    void need(size_t n) const { if (pos + n > size) throw Error("read past end"); }
    template <class T> T raw() {
        need(sizeof(T));
        T v;
        uint8_t b[sizeof(T)];
        std::memcpy(b, data + pos, sizeof(T));
        if (big) std::reverse(b, b + sizeof(T));
        std::memcpy(&v, b, sizeof(T));
        pos += sizeof(T);
        return v;
    }
    uint8_t u8() { need(1); return data[pos++]; }
    int16_t i16() { return raw<int16_t>(); }
    uint16_t u16() { return raw<uint16_t>(); }
    int32_t i32() { return raw<int32_t>(); }
    uint32_t u32() { return raw<uint32_t>(); }
    int64_t i64() { return raw<int64_t>(); }
    uint64_t u64() { return raw<uint64_t>(); }
    float f32() { return raw<float>(); }
    double f64() { return raw<double>(); }
    std::string cstr() {
        size_t e = pos;
        while (e < size && data[e]) ++e;
        if (e >= size) throw Error("unterminated string");
        std::string s(reinterpret_cast<char const*>(data + pos), e - pos);
        pos = e + 1;
        return s;
    }
    void skip(size_t n) { need(n); pos += n; }
    void align(size_t a) { pos = (pos + a - 1) / a * a; }
};

// ---------------------------------------------------------------- UnityFS bundle
std::vector<uint8_t> decompress(uint8_t const* src, size_t csz, size_t usz, uint32_t kind) {
    std::vector<uint8_t> out(usz);
    switch (kind) {
        case 0:
            if (csz != usz) throw Error("bad stored block");
            std::memcpy(out.data(), src, usz);
            break;
        case 2:
        case 3: {
            int r = LZ4_decompress_safe(reinterpret_cast<char const*>(src), reinterpret_cast<char*>(out.data()),
                                        static_cast<int>(csz), static_cast<int>(usz));
            if (r != static_cast<int>(usz)) throw Error("lz4 block failed");
            break;
        }
        default:
            throw Error("unsupported bundle compression " + std::to_string(kind));
    }
    return out;
}

struct Bundle {
    std::vector<uint8_t> data;                 // all data blocks, decompressed
    std::map<std::string, std::pair<size_t, size_t>> files; // path -> (offset,size)

    uint8_t const* file(std::string const& name, size_t& n) const {
        auto it = files.find(name);
        if (it == files.end()) return nullptr;
        n = it->second.second;
        return data.data() + it->second.first;
    }
};

Bundle loadBundle(fs::path const& p) {
    auto raw = readFile(p);
    Reader r(raw.data(), raw.size(), true);
    if (r.cstr() != "UnityFS") throw Error("not a UnityFS bundle");
    uint32_t ver = r.u32();
    r.cstr(); r.cstr();
    int64_t total = r.i64();
    uint32_t ciSize = r.u32(), uiSize = r.u32(), flags = r.u32();
    if (ver >= 7) r.align(16);
    size_t infoPos = (flags & 0x80) ? static_cast<size_t>(total) - ciSize : r.pos;
    if (infoPos + ciSize > raw.size()) throw Error("truncated bundle");
    auto info = decompress(raw.data() + infoPos, ciSize, uiSize, flags & 0x3f);
    size_t dataPos = (flags & 0x80) ? r.pos : infoPos + ciSize;

    Reader ir(info.data(), info.size(), true);
    ir.skip(16);
    int32_t blockCount = ir.i32();
    Bundle b;
    struct Blk { uint32_t u, c; uint16_t f; };
    std::vector<Blk> blocks(blockCount);
    size_t totalU = 0;
    for (auto& bl : blocks) { bl.u = ir.u32(); bl.c = ir.u32(); bl.f = ir.u16(); totalU += bl.u; }
    b.data.resize(totalU);
    size_t src = dataPos, dst = 0;
    for (auto& bl : blocks) {
        if (src + bl.c > raw.size()) throw Error("truncated block");
        auto chunk = decompress(raw.data() + src, bl.c, bl.u, bl.f & 0x3f);
        std::memcpy(b.data.data() + dst, chunk.data(), bl.u);
        src += bl.c; dst += bl.u;
    }
    int32_t nodeCount = ir.i32();
    for (int i = 0; i < nodeCount; ++i) {
        int64_t off = ir.i64(), sz = ir.i64();
        ir.u32();
        std::string path = ir.cstr();
        b.files[path] = {static_cast<size_t>(off), static_cast<size_t>(sz)};
    }
    return b;
}

// ---------------------------------------------------------------- type trees
struct Node {
    std::string type, name;
    int32_t byteSize = 0;
    int32_t metaFlag = 0;
    std::vector<Node> children;
    bool aligns() const { return (metaFlag & 0x4000) != 0; }
};

std::string lookupString(uint32_t off, std::vector<char> const& local) {
    if (off & 0x80000000u) {
        uint32_t o = off & 0x7fffffffu;
        for (auto const& c : kCommonStrings) if (c.offset == o) return c.str;
        return std::to_string(o);
    }
    if (off >= local.size()) return {};
    return std::string(local.data() + off);
}

Node readTypeTreeBlob(Reader& r) {
    int32_t nodeCount = r.i32();
    int32_t strSize = r.i32();
    struct Flat { uint8_t level; uint32_t typeOff, nameOff; int32_t byteSize, meta; };
    std::vector<Flat> flat(nodeCount);
    for (auto& f : flat) {
        r.u16(); f.level = r.u8(); r.u8();
        f.typeOff = r.u32(); f.nameOff = r.u32();
        f.byteSize = r.i32(); r.i32(); f.meta = r.i32();
    }
    std::vector<char> strs(strSize + 1, 0);
    r.need(strSize);
    std::memcpy(strs.data(), r.data + r.pos, strSize);
    r.pos += strSize;

    Node root;
    std::vector<Node*> stack;
    for (int i = 0; i < nodeCount; ++i) {
        Node n;
        n.type = lookupString(flat[i].typeOff, strs);
        n.name = lookupString(flat[i].nameOff, strs);
        n.byteSize = flat[i].byteSize;
        n.metaFlag = flat[i].meta;
        if (i == 0) { root = std::move(n); stack = {&root}; continue; }
        stack.resize(flat[i].level);
        Node* parent = stack.back();
        parent->children.push_back(std::move(n));
        stack.push_back(&parent->children.back());
    }
    return root;
}

struct Value {
    enum Kind { Null, Int, Float, Str, Bytes, Arr, Obj } kind = Null;
    int64_t i = 0;
    double f = 0;
    std::string s;
    std::vector<uint8_t> bytes;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    Value const& operator[](std::string const& k) const {
        static Value none;
        for (auto const& kv : obj) if (kv.first == k) return kv.second;
        return none;
    }
    Value const& operator[](size_t idx) const { return arr.at(idx); }
    double num() const { return kind == Float ? f : static_cast<double>(i); }
    int64_t integer() const { return kind == Float ? static_cast<int64_t>(f) : i; }
};

bool isPrimitive(std::string const& t, size_t& sz, bool& isFloat, bool& isSigned) {
    isFloat = false; isSigned = true;
    static const std::pair<char const*, int> sizes[] = {
        {"bool", 1}, {"char", 1}, {"SInt8", 1}, {"UInt8", 1},
        {"short", 2}, {"SInt16", 2}, {"UInt16", 2}, {"unsigned short", 2},
        {"int", 4}, {"SInt32", 4}, {"UInt32", 4}, {"unsigned int", 4}, {"Type*", 4},
        {"long long", 8}, {"SInt64", 8}, {"UInt64", 8}, {"unsigned long long", 8}, {"FileSize", 8},
        {"float", 4}, {"double", 8},
    };
    for (auto const& [name, n] : sizes) {
        if (t == name) {
            sz = n;
            isFloat = (t == "float" || t == "double");
            isSigned = !(t[0] == 'U' || t.rfind("unsigned", 0) == 0 || t == "bool");
            return true;
        }
    }
    return false;
}

Value readValue(Reader& r, Node const& n);

Value readPrimitive(Reader& r, std::string const& t, size_t sz, bool isFloat, bool isSigned) {
    Value v;
    if (isFloat) {
        v.kind = Value::Float;
        v.f = sz == 4 ? r.f32() : r.f64();
        return v;
    }
    v.kind = Value::Int;
    switch (sz) {
        case 1: v.i = isSigned ? static_cast<int8_t>(r.u8()) : r.u8(); break;
        case 2: v.i = isSigned ? r.i16() : r.u16(); break;
        case 4: v.i = isSigned ? r.i32() : r.u32(); break;
        default: v.i = static_cast<int64_t>(r.u64()); break;
    }
    (void)t;
    return v;
}

Value readArray(Reader& r, Node const& arrayNode) {
    Value v;
    int32_t count = r.i32();
    if (count < 0) throw Error("negative array size");
    Node const& elem = arrayNode.children.at(1);
    size_t sz; bool fl, sg;
    if (elem.children.empty() && isPrimitive(elem.type, sz, fl, sg) && sz == 1) {
        v.kind = Value::Bytes;
        r.need(count);
        v.bytes.assign(r.data + r.pos, r.data + r.pos + count);
        r.pos += count;
    } else {
        v.kind = Value::Arr;
        v.arr.reserve(count);
        for (int32_t i = 0; i < count; ++i) v.arr.push_back(readValue(r, elem));
    }
    if (arrayNode.aligns()) r.align(4);
    return v;
}

Value readValue(Reader& r, Node const& n) {
    Value v;
    size_t sz; bool fl, sg;
    if (n.children.empty() && isPrimitive(n.type, sz, fl, sg)) {
        v = readPrimitive(r, n.type, sz, fl, sg);
    } else if (n.type == "string") {
        int32_t len = r.i32();
        r.need(len);
        v.kind = Value::Str;
        v.s.assign(reinterpret_cast<char const*>(r.data + r.pos), len);
        r.pos += len;
        if (!n.children.empty() && n.children[0].aligns()) r.align(4);
    } else if (n.type == "TypelessData") {
        int32_t len = r.i32();
        r.need(len);
        v.kind = Value::Bytes;
        v.bytes.assign(r.data + r.pos, r.data + r.pos + len);
        r.pos += len;
    } else if (!n.children.empty() && n.children[0].type == "Array") {
        v = readArray(r, n.children[0]);
    } else if (n.type == "Array") {
        v = readArray(r, n);
    } else {
        v.kind = Value::Obj;
        for (auto const& c : n.children) v.obj.emplace_back(c.name, readValue(r, c));
    }
    if (n.aligns()) r.align(4);
    return v;
}

// ---------------------------------------------------------------- serialized files
struct Object {
    int64_t pathId;
    size_t start, size;
    int32_t classId;
    int typeIndex;
};

struct SerializedFile {
    uint8_t const* data = nullptr;
    size_t size = 0;
    bool big = false;
    bool hasTypeTrees = false;
    std::vector<int32_t> typeClass;
    std::vector<Node> typeTrees;
    std::vector<Object> objects;

    Node const* treeFor(Object const& o) const {
        if (!hasTypeTrees || o.typeIndex < 0 || o.typeIndex >= static_cast<int>(typeTrees.size())) return nullptr;
        return &typeTrees[o.typeIndex];
    }
};

SerializedFile parseSerialized(uint8_t const* d, size_t n) {
    SerializedFile sf;
    sf.data = d; sf.size = n;
    Reader h(d, n, true);
    h.u32(); h.u32();
    uint32_t version = h.u32();
    uint32_t dataOffset = h.u32();
    if (version < 14 || version > 21) throw Error("unsupported serialized file version " + std::to_string(version));
    bool big = h.u8() != 0;
    h.skip(3);
    Reader r(d, n, big);
    r.pos = h.pos;
    sf.big = big;
    r.cstr();
    r.i32();
    sf.hasTypeTrees = r.u8() != 0;
    int32_t typeCount = r.i32();
    for (int i = 0; i < typeCount; ++i) {
        int32_t classId = r.i32();
        if (version >= 16) r.u8();
        if (version >= 17) r.i16();
        if ((version < 16 && classId < 0) || (version >= 16 && classId == 114)) r.skip(16);
        r.skip(16);
        sf.typeClass.push_back(classId);
        if (sf.hasTypeTrees) sf.typeTrees.push_back(readTypeTreeBlob(r));
        if (version >= 21) { int32_t deps = r.i32(); r.skip(4 * deps); }
    }
    int32_t objCount = r.i32();
    for (int i = 0; i < objCount; ++i) {
        r.align(4);
        Object o;
        o.pathId = r.i64();
        o.start = r.u32() + static_cast<size_t>(dataOffset);
        o.size = r.u32();
        o.typeIndex = r.i32();
        o.classId = (o.typeIndex >= 0 && o.typeIndex < typeCount) ? sf.typeClass[o.typeIndex] : -1;
        sf.objects.push_back(o);
    }
    return sf;
}

Value readObject(SerializedFile const& sf, Object const& o, Node const& tree) {
    if (o.start + o.size > sf.size) throw Error("object out of range");
    Reader r(sf.data, o.start + o.size, sf.big);
    r.pos = o.start;
    return readValue(r, tree);
}

// ---------------------------------------------------------------- sprites
constexpr int32_t kTexture2D = 28;
constexpr int32_t kSprite = 213;
constexpr int32_t kSpriteAtlas = 687078895;
constexpr int32_t kAudioClip = 83;
constexpr int kFormatBC7 = 25;
constexpr int kFormatRGBA32 = 4;

struct Key {
    uint32_t g[4];
    int64_t second;
    bool operator<(Key const& o) const {
        for (int i = 0; i < 4; ++i) if (g[i] != o.g[i]) return g[i] < o.g[i];
        return second < o.second;
    }
};

Key keyOf(Value const& pair) {
    Key k{};
    Value const& guid = pair["first"];
    for (int i = 0; i < 4; ++i) k.g[i] = static_cast<uint32_t>(guid["data[" + std::to_string(i) + "]"].integer());
    k.second = pair["second"].integer();
    return k;
}

struct Texture {
    int width = 0, height = 0, format = 0;
    uint8_t const* pixels = nullptr; // raw encoded data, rows bottom-up
    size_t size = 0;
};

// Decodes rectangle (x,y,w,h) of a bottom-up texture into RGBA, rows bottom-up.
std::vector<uint8_t> decodeRegion(Texture const& t, int x, int y, int w, int h) {
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4, 0);
    if (t.format == kFormatRGBA32) {
        for (int row = 0; row < h; ++row) {
            size_t src = (static_cast<size_t>(y + row) * t.width + x) * 4;
            if (src + w * 4 > t.size) throw Error("texture data short");
            std::memcpy(out.data() + static_cast<size_t>(row) * w * 4, t.pixels + src, w * 4);
        }
        return out;
    }
    if (t.format != kFormatBC7) throw Error("unsupported texture format " + std::to_string(t.format));
    int bw = (t.width + 3) / 4;
    int bx0 = x / 4, by0 = y / 4, bx1 = (x + w - 1) / 4, by1 = (y + h - 1) / 4;
    uint8_t block[4 * 4 * 4];
    for (int by = by0; by <= by1; ++by) {
        for (int bx = bx0; bx <= bx1; ++bx) {
            size_t off = (static_cast<size_t>(by) * bw + bx) * 16;
            if (off + 16 > t.size) throw Error("texture data short");
            bcdec_bc7(t.pixels + off, block, 4 * 4);
            for (int py = 0; py < 4; ++py) {
                int ty = by * 4 + py - y;
                if (ty < 0 || ty >= h) continue;
                for (int px = 0; px < 4; ++px) {
                    int tx = bx * 4 + px - x;
                    if (tx < 0 || tx >= w) continue;
                    std::memcpy(out.data() + (static_cast<size_t>(ty) * w + tx) * 4, block + (py * 4 + px) * 4, 4);
                }
            }
        }
    }
    return out;
}

void transformPacked(std::vector<uint8_t>& img, int& w, int& h, int rotation) {
    if (rotation == 0) return;
    std::vector<uint8_t> out(img.size());
    int nw = w, nh = h;
    if (rotation == 4) { nw = h; nh = w; }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int dx = x, dy = y;
            switch (rotation) {
                case 1: dx = w - 1 - x; break;                 // flip horizontal
                case 2: dy = h - 1 - y; break;                 // flip vertical
                case 3: dx = w - 1 - x; dy = h - 1 - y; break; // rotate 180
                case 4: dx = y; dy = w - 1 - x; break;         // rotate 90 (as UnityPy ROTATE_270 on bottom-up data)
                default: break;
            }
            std::memcpy(out.data() + (static_cast<size_t>(dy) * nw + dx) * 4, img.data() + (static_cast<size_t>(y) * w + x) * 4, 4);
        }
    }
    img.swap(out); w = nw; h = nh;
}

// Clears every pixel outside the sprite's mesh (tight packing shares rectangles).
void applyMeshMask(std::vector<uint8_t>& img, int w, int h, Value const& rd, double ppu,
                   double rectW, double rectH, double pivX, double pivY, double offX, double offY) {
    Value const& vd = rd["m_VertexData"];
    Value const& channels = vd["m_Channels"];
    std::vector<uint8_t> const& vbytes = vd["m_DataSize"].bytes;
    int vcount = static_cast<int>(vd["m_VertexCount"].integer());
    if (vcount <= 0 || channels.arr.empty()) return;
    static const int formatSize[] = {4, 2, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4};
    int stride = 0;
    for (auto const& c : channels.arr) {
        int dim = static_cast<int>(c["dimension"].integer()) & 0xF;
        if (c["stream"].integer() != 0 || dim == 0) continue;
        int fmt = static_cast<int>(c["format"].integer());
        int fsz = (fmt >= 0 && fmt < 13) ? formatSize[fmt] : 4;
        stride = std::max(stride, static_cast<int>(c["offset"].integer()) + dim * fsz);
    }
    Value const& pos = channels[0];
    if (pos["format"].integer() != 0 || stride <= 0 || static_cast<size_t>(stride) * vcount > vbytes.size()) return;
    int posOff = static_cast<int>(pos["offset"].integer());
    std::vector<float> vx(vcount), vy(vcount);
    for (int i = 0; i < vcount; ++i) {
        float px, py;
        std::memcpy(&px, vbytes.data() + i * stride + posOff, 4);
        std::memcpy(&py, vbytes.data() + i * stride + posOff + 4, 4);
        vx[i] = static_cast<float>(px * ppu + rectW * pivX - offX);
        vy[i] = static_cast<float>(py * ppu + rectH * pivY - offY);
    }
    std::vector<uint8_t> const& ib = rd["m_IndexBuffer"].bytes;
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 0);
    for (auto const& sm : rd["m_SubMeshes"].arr) {
        if (sm["topology"].integer() != 0) continue;
        size_t first = static_cast<size_t>(sm["firstByte"].integer());
        size_t count = static_cast<size_t>(sm["indexCount"].integer());
        for (size_t t = 0; t + 2 < count; t += 3) {
            int idx[3];
            for (int k = 0; k < 3; ++k) {
                size_t o = first + (t + k) * 2;
                if (o + 2 > ib.size()) return;
                idx[k] = ib[o] | (ib[o + 1] << 8);
                if (idx[k] >= vcount) return;
            }
            float ax = vx[idx[0]], ay = vy[idx[0]], bx = vx[idx[1]], by = vy[idx[1]], cx = vx[idx[2]], cy = vy[idx[2]];
            float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (std::fabs(area) < 1e-6f) continue;
            int minX = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))) - 1);
            int maxX = std::min(w - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))) + 1);
            int minY = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}))) - 1);
            int maxY = std::min(h - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}))) + 1);
            float sign = area > 0 ? 1.f : -1.f;
            auto edge = [&](float x0, float y0, float x1, float y1, float x, float y) {
                float ex = x1 - x0, ey = y1 - y0;
                float len = std::sqrt(ex * ex + ey * ey);
                return sign * (ex * (y - y0) - ey * (x - x0)) / (len > 0 ? len : 1.f);
            };
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    float px = x + 0.5f, py = y + 0.5f;
                    const float tol = -0.71f; // keep pixels the outline touches
                    if (edge(ax, ay, bx, by, px, py) >= tol && edge(bx, by, cx, cy, px, py) >= tol &&
                        edge(cx, cy, ax, ay, px, py) >= tol)
                        mask[static_cast<size_t>(y) * w + x] = 1;
                }
            }
        }
    }
    for (size_t i = 0; i < mask.size(); ++i)
        if (!mask[i]) std::memset(img.data() + i * 4, 0, 4);
}

std::string baseName(std::string const& path) {
    auto p = path.find_last_of('/');
    return p == std::string::npos ? path : path.substr(p + 1);
}

} // namespace

LoadResult loadSprites(fs::path const& bundlePath, std::vector<std::string> const& prefixes,
                       std::map<std::string, Frame>& out) {
    LoadResult res;
    try {
        Bundle b = loadBundle(bundlePath);
        // the serialized file is the entry without an extension
        std::string cab;
        for (auto const& [name, _] : b.files)
            if (name.find('.') == std::string::npos) cab = name;
        if (cab.empty()) throw Error("no serialized file in bundle");
        size_t n = 0;
        uint8_t const* sd = b.file(cab, n);
        SerializedFile sf = parseSerialized(sd, n);
        if (!sf.hasTypeTrees) throw Error("bundle has no type trees");

        std::map<Key, Value> renderData;
        std::map<int64_t, Object const*> byPath;
        for (auto const& o : sf.objects) byPath[o.pathId] = &o;
        for (auto const& o : sf.objects) {
            if (o.classId != kSpriteAtlas) continue;
            Value atlas = readObject(sf, o, *sf.treeFor(o));
            for (auto const& kv : atlas["m_RenderDataMap"].arr) renderData[keyOf(kv["first"])] = kv["second"];
        }

        std::map<int64_t, Texture> textures;
        auto texture = [&](int64_t pathId) -> Texture const& {
            auto it = textures.find(pathId);
            if (it != textures.end()) return it->second;
            auto po = byPath.find(pathId);
            if (po == byPath.end()) throw Error("missing texture");
            Value tv = readObject(sf, *po->second, *sf.treeFor(*po->second));
            Texture t;
            t.width = static_cast<int>(tv["m_Width"].integer());
            t.height = static_cast<int>(tv["m_Height"].integer());
            t.format = static_cast<int>(tv["m_TextureFormat"].integer());
            Value const& stream = tv["m_StreamData"];
            if (stream.kind == Value::Obj && stream["size"].integer() > 0) {
                size_t rn = 0;
                uint8_t const* rs = b.file(baseName(stream["path"].s), rn);
                size_t off = static_cast<size_t>(stream["offset"].integer());
                size_t sz = static_cast<size_t>(stream["size"].integer());
                if (!rs || off + sz > rn) throw Error("texture stream missing");
                t.pixels = rs + off; t.size = sz;
            } else {
                throw Error("inline texture data not supported");
            }
            return textures[pathId] = t;
        };

        for (auto const& o : sf.objects) {
            if (o.classId != kSprite) continue;
            Value s = readObject(sf, o, *sf.treeFor(o));
            std::string const& name = s["m_Name"].s;
            bool wanted = false;
            for (auto const& p : prefixes) if (name.rfind(p, 0) == 0) { wanted = true; break; }
            if (!wanted) continue;

            Value const& rd = s["m_RD"];
            auto it = renderData.find(keyOf(s["m_RenderDataKey"]));
            Value const& ad = it != renderData.end() ? it->second : rd;
            Texture const& t = texture(ad["texture"]["m_PathID"].integer());
            Value const& tr = ad["textureRect"];
            int x = static_cast<int>(std::lround(tr["x"].num()));
            int y = static_cast<int>(std::lround(tr["y"].num()));
            int w = static_cast<int>(std::lround(tr["width"].num()));
            int h = static_cast<int>(std::lround(tr["height"].num()));
            if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > t.width || y + h > t.height) continue;

            Frame f;
            f.name = name;
            f.rgba = decodeRegion(t, x, y, w, h);
            int settings = static_cast<int>(ad["settingsRaw"].integer());
            if (settings & 1) transformPacked(f.rgba, w, h, (settings >> 2) & 0xF);

            double ppu = s["m_PixelsToUnits"].num();
            Value const& rect = s["m_Rect"];
            Value const& piv = s["m_Pivot"];
            Value const& off = rd["textureRectOffset"];
            double rw = rect["width"].num(), rh = rect["height"].num();
            double px = piv["x"].num(), py = piv["y"].num();
            double ox = off["x"].num(), oy = off["y"].num();
            if (((settings >> 1) & 1) == 0) applyMeshMask(f.rgba, w, h, rd, ppu, rw, rh, px, py, ox, oy);

            // flip to top-down rows
            std::vector<uint8_t> flipped(f.rgba.size());
            for (int row = 0; row < h; ++row)
                std::memcpy(flipped.data() + static_cast<size_t>(row) * w * 4,
                            f.rgba.data() + static_cast<size_t>(h - 1 - row) * w * 4, w * 4);
            f.rgba.swap(flipped);
            f.width = w; f.height = h;
            f.centerX = static_cast<float>(ox + w / 2.0 - rw * px);
            f.centerY = static_cast<float>(oy + h / 2.0 - rh * py);
            out[name] = std::move(f);
        }
        res.ok = true;
    } catch (std::exception const& e) {
        res.error = e.what();
    }
    return res;
}

LoadResult loadClips(fs::path const& cupheadDataDir, fs::path const& typeSourceBundle,
                     std::vector<std::string> const& names, std::map<std::string, std::vector<uint8_t>>& out) {
    LoadResult res;
    try {
        // Borrow the AudioClip type tree from a bundle built by the same Unity version.
        Bundle tb = loadBundle(typeSourceBundle);
        Node clipTree;
        bool found = false;
        for (auto const& [name, _] : tb.files) {
            if (name.find('.') != std::string::npos) continue;
            size_t n = 0;
            uint8_t const* d = tb.file(name, n);
            SerializedFile sf = parseSerialized(d, n);
            for (size_t i = 0; i < sf.typeClass.size(); ++i)
                if (sf.typeClass[i] == kAudioClip && sf.hasTypeTrees) { clipTree = sf.typeTrees[i]; found = true; }
        }
        if (!found) throw Error("no AudioClip type tree in music bundle");

        fs::path dir = cupheadDataDir;
        std::vector<fs::path> candidates;
        // The Goopy level's shared assets come first; the rest are a fallback for other game builds.
        if (fs::exists(dir / "sharedassets20.assets")) candidates.push_back(dir / "sharedassets20.assets");
        for (auto const& e : fs::directory_iterator(dir)) {
            auto fn = e.path().filename().string();
            if (fn.rfind("sharedassets", 0) == 0 && e.path().extension() == ".assets" && e.path() != dir / "sharedassets20.assets")
                candidates.push_back(e.path());
        }
        std::vector<std::string> remaining = names;
        for (auto const& path : candidates) {
            if (remaining.empty()) break;
            auto data = readFile(path);
            SerializedFile sf = parseSerialized(data.data(), data.size());
            std::map<std::string, std::vector<uint8_t>> resourceCache;
            for (auto const& o : sf.objects) {
                if (o.classId != kAudioClip) continue;
                Value clip = readObject(sf, o, clipTree);
                std::string const& name = clip["m_Name"].s;
                auto it = std::find(remaining.begin(), remaining.end(), name);
                if (it == remaining.end()) continue;
                Value const& rsrc = clip["m_Resource"];
                std::string src = baseName(rsrc["m_Source"].s);
                size_t off = static_cast<size_t>(rsrc["m_Offset"].integer());
                size_t sz = static_cast<size_t>(rsrc["m_Size"].integer());
                auto& buf = resourceCache[src];
                if (buf.empty()) buf = readFile(dir / src);
                if (off + sz > buf.size() || sz < 4 || std::memcmp(buf.data() + off, "FSB5", 4) != 0) continue;
                out[name].assign(buf.begin() + off, buf.begin() + off + sz);
                remaining.erase(it);
            }
        }
        res.ok = true;
        if (!remaining.empty()) res.error = "missing " + std::to_string(remaining.size()) + " clips";
    } catch (std::exception const& e) {
        res.error = e.what();
    }
    return res;
}

} // namespace cupread
