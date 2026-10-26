#include "undofile.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "editor.hpp"
#include "options.hpp"
#include "piece.hpp"
#include "state.hpp"

namespace {
constexpr char kMagic[8] = {'W', 'N', 'T', 'U', 'N', 'D', 'O', '1'};
constexpr size_t kMaxBytes = 8u << 20;

std::string undo_name(const std::string& file) {
    if(file.empty() || state_dir().empty()) return {};
    char hex[17];
    std::snprintf(
        hex,
        sizeof hex,
        "%016llx",
        static_cast<unsigned long long>(state_hash(state_file_key(file))));
    return std::string("undo/") + hex;
}

void put_u32(std::string& o, uint32_t v) {
    o.append(reinterpret_cast<const char*>(&v), 4);
}
void put_u64(std::string& o, uint64_t v) {
    o.append(reinterpret_cast<const char*>(&v), 8);
}

size_t rev_bytes(const std::vector<PieceTable::SavedEdit>& r) {
    size_t n = 4;
    for(const auto& e : r)
        n += 8 + 4 + 4 + e.removed.size() + e.inserted.size();
    return n;
}

struct Reader {
    std::string_view s;
    size_t i = 0;
    bool bad = false;
    uint64_t u64() {
        uint64_t v = 0;
        if(bad || s.size() - i < 8) {
            bad = true;
            return 0;
        }
        std::memcpy(&v, s.data() + i, 8);
        i += 8;
        return v;
    }
    uint32_t u32() {
        uint32_t v = 0;
        if(bad || s.size() - i < 4) {
            bad = true;
            return 0;
        }
        std::memcpy(&v, s.data() + i, 4);
        i += 4;
        return v;
    }
    std::string str(size_t n) {
        if(bad || s.size() - i < n) {
            bad = true;
            return {};
        }
        std::string r(s.substr(i, n));
        i += n;
        return r;
    }
};

}  // namespace

void undofile_write(const Buffer& b, uint64_t disk_hash, uint64_t disk_size) {
    if(!opts.undofile || b.nofile) return;
    const std::string name = undo_name(b.filename);
    if(name.empty()) return;
    auto h = b.pt.export_history();
    if(h.revs.empty()) {
        state_remove(name);
        return;
    }
    size_t total = 0, drop = 0;
    for(const auto& r : h.revs) total += rev_bytes(r);
    while(total > kMaxBytes && drop < h.head)
        total -= rev_bytes(h.revs[drop++]);
    if(total > kMaxBytes) {
        state_remove(name);
        return;
    }
    h.revs.erase(h.revs.begin(),
                 h.revs.begin() + static_cast<std::ptrdiff_t>(drop));
    h.head -= drop;
    std::string o;
    o.append(kMagic, 8);
    put_u64(o, disk_hash);
    put_u64(o, disk_size);
    put_u32(o, static_cast<uint32_t>(h.head));
    put_u32(o, static_cast<uint32_t>(h.revs.size()));
    for(const auto& rev : h.revs) {
        put_u32(o, static_cast<uint32_t>(rev.size()));
        for(const auto& e : rev) {
            put_u64(o, e.pos);
            put_u32(o, static_cast<uint32_t>(e.removed.size()));
            o += e.removed;
            put_u32(o, static_cast<uint32_t>(e.inserted.size()));
            o += e.inserted;
        }
    }
    put_u64(o, state_hash(o));
    state_write_blob(name, o);
}

void undofile_load(Buffer& b) {
    if(!opts.undofile || b.nofile) return;
    const std::string name = undo_name(b.filename);
    if(name.empty()) return;
    const auto blob = state_read_blob(name);
    if(!blob || blob->size() < 8 + 8 + 8 + 4 + 4 + 8) return;
    const std::string_view all = *blob;
    const std::string_view body = all.substr(0, all.size() - 8);
    uint64_t want = 0;
    std::memcpy(&want, all.data() + body.size(), 8);
    if(state_hash(body) != want) return;
    Reader r{body};
    if(r.str(8) != std::string_view(kMagic, 8)) return;
    const uint64_t hash = r.u64(), size = r.u64();
    if(size != b.orig.size() || hash != state_hash(b.orig)) return;
    PieceTable::SavedHistory h;
    h.head = r.u32();
    const uint32_t nrev = r.u32();
    if(nrev > body.size() / 4) return;
    for(uint32_t i = 0; i < nrev; ++i) {
        const uint32_t ne = r.u32();
        if(r.bad || ne == 0) return;
        auto& rev = h.revs.emplace_back();
        for(uint32_t j = 0; j < ne; ++j) {
            PieceTable::SavedEdit e;
            e.pos = r.u64();
            e.removed = r.str(r.u32());
            e.inserted = r.str(r.u32());
            if(r.bad) return;
            rev.push_back(std::move(e));
        }
    }
    if(r.bad || r.i != body.size()) return;
    b.pt.import_history(h);
}
