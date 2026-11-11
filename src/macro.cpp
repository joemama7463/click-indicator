// Kept separate from main.cpp so libGDR's Ok/Err don't clash with Geode's.
#include "macro.hpp"
#include <gdr/gdr.hpp>
#include <matjson.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>

namespace {
    struct RawInput {
        uint64_t frame;
        int button;
        bool player2;
        bool down;
    };

    Holds toHolds(std::vector<RawInput> inputs, double fps) {
        if (fps <= 0) fps = 240.0;
        std::stable_sort(inputs.begin(), inputs.end(),
            [](RawInput const& a, RawInput const& b) { return a.frame < b.frame; });
        Holds holds;
        bool open = false;
        for (auto const& i : inputs) {
            if (i.button != 1 || i.player2) continue; // player 1 jump only
            double t = static_cast<double>(i.frame) / fps;
            if (i.down && !open) {
                holds.push_back({ t, t });
                open = true;
            } else if (!i.down && open) {
                holds.back().second = t;
                open = false;
            }
        }
        if (open) holds.back().second = holds.back().first + 0.05;
        return holds;
    }

    // ---- minimal msgpack -> matjson decoder (enough for GDR v1) ----
    struct MsgPack {
        std::vector<uint8_t> const& d;
        size_t p = 0;
        bool ok = true;

        bool need(size_t n) { if (p + n > d.size()) { ok = false; return false; } return true; }
        uint64_t be(size_t n) {
            if (!need(n)) return 0;
            uint64_t v = 0;
            for (size_t i = 0; i < n; i++) v = (v << 8) | d[p++];
            return v;
        }
        std::string str(size_t n) {
            if (!need(n)) return {};
            std::string s(reinterpret_cast<char const*>(d.data() + p), n);
            p += n;
            return s;
        }
        matjson::Value arr(size_t n, int depth) {
            auto a = matjson::Value::array();
            for (size_t i = 0; i < n && ok; i++) a.push(read(depth + 1));
            return a;
        }
        matjson::Value map(size_t n, int depth) {
            auto o = matjson::Value::object();
            for (size_t i = 0; i < n && ok; i++) {
                auto k = read(depth + 1);
                auto v = read(depth + 1);
                std::string key = k.isString() ? k.asString().unwrapOr("") : k.dump();
                o.set(key, std::move(v));
            }
            return o;
        }
        matjson::Value read(int depth = 0) {
            if (depth > 64 || !need(1)) { ok = false; return nullptr; }
            uint8_t b = d[p++];
            if (b <= 0x7f) return matjson::Value(static_cast<double>(b));
            if (b >= 0xe0) return matjson::Value(static_cast<double>(static_cast<int8_t>(b)));
            if ((b & 0xf0) == 0x80) return map(b & 0x0f, depth);
            if ((b & 0xf0) == 0x90) return arr(b & 0x0f, depth);
            if ((b & 0xe0) == 0xa0) return matjson::Value(str(b & 0x1f));
            switch (b) {
                case 0xc0: return nullptr;
                case 0xc2: return false;
                case 0xc3: return true;
                case 0xc4: return matjson::Value(str(be(1)));
                case 0xc5: return matjson::Value(str(be(2)));
                case 0xc6: return matjson::Value(str(be(4)));
                case 0xca: { uint32_t u = static_cast<uint32_t>(be(4)); float f; std::memcpy(&f, &u, 4); return matjson::Value(static_cast<double>(f)); }
                case 0xcb: { uint64_t u = be(8); double f; std::memcpy(&f, &u, 8); return matjson::Value(f); }
                case 0xcc: return matjson::Value(static_cast<double>(be(1)));
                case 0xcd: return matjson::Value(static_cast<double>(be(2)));
                case 0xce: return matjson::Value(static_cast<double>(be(4)));
                case 0xcf: return matjson::Value(static_cast<double>(be(8)));
                case 0xd0: return matjson::Value(static_cast<double>(static_cast<int8_t>(be(1))));
                case 0xd1: return matjson::Value(static_cast<double>(static_cast<int16_t>(be(2))));
                case 0xd2: return matjson::Value(static_cast<double>(static_cast<int32_t>(be(4))));
                case 0xd3: return matjson::Value(static_cast<double>(static_cast<int64_t>(be(8))));
                case 0xd9: return matjson::Value(str(be(1)));
                case 0xda: return matjson::Value(str(be(2)));
                case 0xdb: return matjson::Value(str(be(4)));
                case 0xdc: return arr(be(2), depth);
                case 0xdd: return arr(be(4), depth);
                case 0xde: return map(be(2), depth);
                case 0xdf: return map(be(4), depth);
                default: ok = false; return nullptr;
            }
        }
    };

    double num(matjson::Value const& v, std::string_view key, double def = 0) {
        if (!v.isObject() || !v.contains(key)) return def;
        auto const& x = v[key];
        if (x.isNumber()) return x.asDouble().unwrapOr(def);
        if (x.isBool()) return x.asBool().unwrapOr(false) ? 1.0 : 0.0;
        return def;
    }

    std::string fromGdr1(matjson::Value const& root, MacroInfo& out) {
        if (!root.isObject() || !root.contains("inputs") || !root["inputs"].isArray())
            return "This file doesn't look like a GDR macro (no inputs found).";
        for (auto key : { "level", "levelInfo" }) {
            if (root.contains(key) && root[key].isObject()) {
                out.levelID = static_cast<uint32_t>(num(root[key], "id"));
                if (root[key].contains("name") && root[key]["name"].isString())
                    out.levelName = root[key]["name"].asString().unwrapOr("");
            }
        }
        std::vector<RawInput> inputs;
        for (auto const& i : root["inputs"].asArray().unwrap()) {
            inputs.push_back({
                static_cast<uint64_t>(num(i, "frame")),
                static_cast<int>(num(i, "btn", 1)),
                num(i, "2p") != 0,
                num(i, "down") != 0,
            });
        }
        out.holds = toHolds(std::move(inputs), num(root, "framerate", 240.0));
        return {};
    }
}

std::string loadMacro(std::filesystem::path const& path, MacroInfo& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "Couldn't open the file.";
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty()) return "The file is empty.";

    // GDR 2 (binary, starts with "GDR")
    if (data.size() >= 3 && std::memcmp(data.data(), "GDR", 3) == 0) {
        auto res = gdr::Replay<>::importData(std::span<uint8_t>(data));
        if (res.isErr()) return "Couldn't read GDR2 macro: " + res.unwrapErr();
        auto const& r = res.unwrap();
        std::vector<RawInput> inputs;
        for (auto const& i : r.inputs) inputs.push_back({ i.frame, i.button, i.player2, i.down });
        out.levelID = r.levelInfo.id;
        out.levelName = r.levelInfo.name;
        out.format = "GDR2";
        out.holds = toHolds(std::move(inputs), r.framerate);
    }
    // GDR 1 JSON
    else if (data[0] == '{') {
        auto res = matjson::parse(std::string_view(reinterpret_cast<char const*>(data.data()), data.size()));
        if (res.isErr()) return "Couldn't read the JSON macro.";
        out.format = "GDR (JSON)";
        auto err = fromGdr1(res.unwrap(), out);
        if (!err.empty()) return err;
    }
    // GDR 1 msgpack
    else {
        MsgPack mp{ data };
        auto root = mp.read();
        if (!mp.ok) return "Unsupported macro format. Use a .gdr or .gdr2 file.";
        out.format = "GDR";
        auto err = fromGdr1(root, out);
        if (!err.empty()) return err;
    }

    if (out.holds.empty()) return "The macro has no player 1 jump inputs.";
    return {};
}
