#ifndef ZUI_HTML_STYLE_MAP_H
#define ZUI_HTML_STYLE_MAP_H

// Computed style is a few dozen properties. `std::unordered_map` allocates a node per property,
// and a flat vector of `std::string` pairs pays for that with a copy per insert under /Od.
// Names and values are interned: one process-lifetime pool, pointer-sized copies, and the same
// find / emplace / operator[] surface the cascade already uses. The table itself is one dense
// vector plus an open-addressed index. Interned text is immutable; assigning a new value points
// at a different pool entry.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {

inline std::uint64_t hash_text(std::string_view key) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

/// A CSS property name or declared value that lives in the process pool. Copying an atom copies
/// a pointer. Two atoms compare equal by pointer when they were interned in this module; compare
/// them to a `std::string` / `string_view` / `const char*` for a content check.
class Atom {
public:
    Atom() : rec_(&empty_rec()) {}
    Atom(const char* text) : Atom(intern(text)) {}
    Atom(std::string_view text) : Atom(intern(text)) {}
    Atom(const std::string& text) : Atom(intern(std::string_view(text))) {}
    Atom(std::string&& text) : Atom(intern(std::string_view(text))) {}

    static Atom intern(std::string_view text);
    static Atom intern(const char* text) {
        return text == nullptr ? Atom() : intern(std::string_view(text));
    }

    const std::string& str() const { return rec_->text; }
    /// Pool storage. Stable for the process, not for the map entry: a later assignment points
    /// the atom at a different record while this pointer still names the old text.
    const std::string* ptr() const { return &rec_->text; }
    std::uint64_t hash() const { return rec_->hash; }
    bool empty() const { return rec_->text.empty(); }
    std::size_t size() const { return rec_->text.size(); }
    const char* data() const { return rec_->text.data(); }
    const char* c_str() const { return rec_->text.c_str(); }

    operator std::string_view() const { return rec_->text; }

    bool operator==(const Atom& other) const { return rec_ == other.rec_; }
    bool operator!=(const Atom& other) const { return rec_ != other.rec_; }
    bool operator==(const char* text) const { return rec_->text == text; }
    bool operator!=(const char* text) const { return rec_->text != text; }
    bool operator==(std::string_view text) const { return rec_->text == text; }
    bool operator!=(std::string_view text) const { return rec_->text != text; }
    bool operator==(const std::string& text) const { return rec_->text == text; }
    bool operator!=(const std::string& text) const { return rec_->text != text; }

    Atom& operator=(const char* text) { return *this = intern(text); }
    Atom& operator=(std::string_view text) { return *this = intern(text); }
    Atom& operator=(const std::string& text) { return *this = intern(std::string_view(text)); }
    Atom& operator=(std::string&& text) { return *this = intern(std::string_view(text)); }

    /// FlatMap's hit path reads `rec_` directly. Under /Ob0 a getter would be a call per probe.
    template <typename V>
    friend class FlatMap;

    /// Pool record. Hot probes read `hash` and `flags` through this pointer. A getter is a
    /// separate call under /Ob0, and the probe runs once per property.
    struct Rec {
        std::string text;
        std::uint64_t hash = 0;
        mutable std::uint8_t flags = 0;
    };

    static constexpr std::uint8_t kInheritKnown = 1;
    static constexpr std::uint8_t kInheritYes = 2;
    static constexpr std::uint8_t kLayoutKnown = 4;
    static constexpr std::uint8_t kLayoutYes = 8;

    const Rec* rec_;

    /// Cached answer of `inheritable()` for this name. The first caller fills it.
    bool inherit_known() const { return (rec_->flags & kInheritKnown) != 0; }
    bool inherit_yes() const { return (rec_->flags & kInheritYes) != 0; }
    void set_inherit(bool yes) const {
        const std::uint8_t rest = static_cast<std::uint8_t>(rec_->flags & ~std::uint8_t{3});
        rec_->flags = static_cast<std::uint8_t>(rest | kInheritKnown | (yes ? kInheritYes : 0));
    }

    /// Cached answer of `affects_layout()` for this property name.
    bool layout_known() const { return (rec_->flags & kLayoutKnown) != 0; }
    bool layout_yes() const { return (rec_->flags & kLayoutYes) != 0; }
    void set_layout(bool yes) const {
        const std::uint8_t rest = static_cast<std::uint8_t>(rec_->flags & ~std::uint8_t{12});
        rec_->flags = static_cast<std::uint8_t>(rest | kLayoutKnown | (yes ? kLayoutYes : 0));
    }

    explicit Atom(const Rec* rec) : rec_(rec) {}

private:
    static const Rec& empty_rec() {
        static const Rec rec{std::string(), hash_text(""), 0};
        return rec;
    }
};

struct AtomHash {
    std::size_t operator()(const Atom& atom) const noexcept { return static_cast<std::size_t>(atom.hash()); }
};

inline Atom Atom::intern(std::string_view text) {
    if (text.empty()) {
        return Atom();
    }
    struct Pool {
        // `deque` keeps `Rec` addresses stable. `ptrs` is the probe target: `deque::operator[]`
        // is a call under /Ob0, and every intern hits it once per slot.
        std::deque<Rec> recs;
        std::vector<const Rec*> ptrs;
        std::vector<std::uint32_t> slots;
    };
    static Pool pool;
    const std::uint64_t h = hash_text(text);
    const std::uint32_t* slots = pool.slots.data();
    const Rec* const* ptrs = pool.ptrs.data();
    const std::size_t nslots = pool.slots.size();
    if (slots != nullptr && ptrs != nullptr && nslots != 0) {
        const std::size_t mask = nslots - 1;
        std::size_t i = static_cast<std::size_t>(h) & mask;
        for (std::size_t n = 0; n < nslots; ++n) {
            const std::uint32_t slot = slots[i];
            if (slot == 0) {
                break;
            }
            const Rec* rec = ptrs[slot - 1];
            if (rec->hash == h && rec->text == text) {
                return Atom(rec);
            }
            i = (i + 1) & mask;
        }
    }
    if (pool.slots.empty() || (pool.recs.size() + 1) * 10 >= pool.slots.size() * 7) {
        const std::size_t cap = pool.slots.empty() ? 16 : pool.slots.size() * 2;
        std::vector<std::uint32_t> next(cap, 0);
        const std::size_t mask = cap - 1;
        for (std::size_t idx = 0; idx < pool.recs.size(); ++idx) {
            std::size_t i = static_cast<std::size_t>(pool.recs[idx].hash) & mask;
            while (next[i] != 0) {
                i = (i + 1) & mask;
            }
            next[i] = static_cast<std::uint32_t>(idx + 1);
        }
        pool.slots.swap(next);
    }
    pool.recs.push_back(Rec{std::string(text), h, 0});
    pool.ptrs.push_back(&pool.recs.back());
    const std::size_t idx = pool.recs.size() - 1;
    const std::size_t mask = pool.slots.size() - 1;
    std::size_t i = static_cast<std::size_t>(h) & mask;
    while (pool.slots[i] != 0) {
        i = (i + 1) & mask;
    }
    pool.slots[i] = static_cast<std::uint32_t>(idx + 1);
    return Atom(&pool.recs[idx]);
}

template <typename V>
class FlatMap {
public:
    using value_type = std::pair<Atom, V>;
    using iterator = typename std::vector<value_type>::iterator;
    using const_iterator = typename std::vector<value_type>::const_iterator;

    iterator begin() { return items_.begin(); }
    iterator end() { return items_.end(); }
    const_iterator begin() const { return items_.begin(); }
    const_iterator end() const { return items_.end(); }
    bool empty() const { return items_.empty(); }
    std::size_t size() const { return items_.size(); }
    /// One call, then the caller indexes the pointer. `operator[]` on the vector is a call
    /// under /Ob0, and a property walk does it once per entry.
    const value_type* items_ptr() const { return items_.data(); }
    value_type* items_ptr() { return items_.data(); }
    void clear() {
        items_.clear();
        slots_.clear();
        tombs_ = 0;
    }
    void reserve(std::size_t n) {
        items_.reserve(n);
        if (slots_.size() < n * 2) {
            rehash(next_pow2(n * 2));
        }
    }

    iterator find(const Atom& key) {
        const std::size_t slot = lookup_atom(key);
        return slot == npos ? items_.end() : items_.begin() + static_cast<std::ptrdiff_t>(slots_[slot] - 1);
    }
    const_iterator find(const Atom& key) const {
        const std::size_t slot = lookup_atom(key);
        return slot == npos ? items_.end() : items_.begin() + static_cast<std::ptrdiff_t>(slots_[slot] - 1);
    }
    iterator find(std::string_view key) {
        const std::size_t slot = lookup_view(key);
        return slot == npos ? items_.end() : items_.begin() + static_cast<std::ptrdiff_t>(slots_[slot] - 1);
    }
    const_iterator find(std::string_view key) const {
        const std::size_t slot = lookup_view(key);
        return slot == npos ? items_.end() : items_.begin() + static_cast<std::ptrdiff_t>(slots_[slot] - 1);
    }
    iterator find(const std::string& key) { return find(std::string_view(key)); }
    const_iterator find(const std::string& key) const { return find(std::string_view(key)); }
    iterator find(const char* key) { return find(std::string_view(key)); }
    const_iterator find(const char* key) const { return find(std::string_view(key)); }

    std::size_t count(const Atom& key) const { return find(key) == end() ? 0 : 1; }
    std::size_t count(std::string_view key) const { return find(key) == end() ? 0 : 1; }
    std::size_t count(const std::string& key) const { return count(std::string_view(key)); }
    std::size_t count(const char* key) const { return count(std::string_view(key)); }

    /// Inserts a default value when @p key is absent. A reference from a previous call is
    /// invalidated if this insert grows the buffer.
    ///
    /// The hit path indexes `slots_.data()` / `items_.data()`. `find` returns a checked iterator,
    /// and that iterator was the declaration-apply cost under /Od.
    V& operator[](const Atom& key) {
        const Atom::Rec* want = key.rec_;
        std::uint32_t* slots = slots_.data();
        value_type* items = items_.data();
        const std::size_t nslots = slots_.size();
        if (slots != nullptr && items != nullptr && nslots != 0 && !items_.empty()) {
            const std::size_t mask = nslots - 1;
            std::size_t i = static_cast<std::size_t>(want->hash) & mask;
            for (std::size_t n = 0; n < nslots; ++n) {
                const std::uint32_t slot = slots[i];
                if (slot == kEmpty) {
                    break;
                }
                if (slot != kTomb && items[slot - 1].first.rec_ == want) {
                    return items[slot - 1].second;
                }
                i = (i + 1) & mask;
            }
        }
        return insert_new(key, V{});
    }
    V& operator[](std::string_view key) { return (*this)[Atom::intern(key)]; }
    V& operator[](const std::string& key) { return (*this)[std::string_view(key)]; }
    V& operator[](const char* key) { return (*this)[std::string_view(key)]; }

    /// The caller already proved @p key is absent. Skipping `find` avoids a checked iterator per
    /// property: under /Ob0 that iterator is a real call, and a cold finalize inserts dozens.
    template <typename U>
    void append_new(const Atom& key, U&& value) {
        insert_new(key, std::forward<U>(value));
    }

    /// Slot index for an inlined probe. `lookup_atom` is a call under /Ob0, so a property walk
    /// that already holds these pointers compares `rec_` in the caller.
    const std::uint32_t* slot_ptr() const { return slots_.data(); }
    std::size_t slot_n() const { return slots_.size(); }
    /// Same values as the private slot sentinels. An inlined probe cannot name those.
    static constexpr std::uint32_t kProbeEmpty = 0;
    static constexpr std::uint32_t kProbeTomb = 0xFFFFFFFFu;

    /// Does not overwrite an existing key. `.first` is the element, `.second` is true when this
    /// call inserted it (`normalize_generated` relies on that unordered_map shape).
    template <typename K, typename U>
    std::pair<iterator, bool> emplace(K&& key, U&& value) {
        const Atom atom = to_atom(std::forward<K>(key));
        const iterator it = find(atom);
        if (it != end()) {
            return {it, false};
        }
        insert_new(atom, std::forward<U>(value));
        return {items_.end() - 1, true};
    }

    std::size_t erase(const Atom& key) { return erase_at(lookup_atom(key)); }
    std::size_t erase(std::string_view key) { return erase_at(lookup_view(key)); }
    std::size_t erase(const std::string& key) { return erase(std::string_view(key)); }
    std::size_t erase(const char* key) { return erase(std::string_view(key)); }

    bool operator==(const FlatMap& other) const {
        if (items_.size() != other.items_.size()) {
            return false;
        }
        for (const value_type& kv : items_) {
            const const_iterator it = other.find(kv.first);
            if (it == other.end() || !(it->second == kv.second)) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const FlatMap& other) const { return !(*this == other); }

private:
    static constexpr std::uint32_t kEmpty = 0;
    static constexpr std::uint32_t kTomb = 0xFFFFFFFFu;
    static_assert(kProbeEmpty == kEmpty && kProbeTomb == kTomb, "probe sentinels");
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    static Atom to_atom(const Atom& key) { return key; }
    static Atom to_atom(std::string_view key) { return Atom::intern(key); }
    static Atom to_atom(const std::string& key) { return Atom::intern(std::string_view(key)); }
    static Atom to_atom(std::string&& key) { return Atom::intern(std::string_view(key)); }
    static Atom to_atom(const char* key) { return Atom::intern(key); }

    static std::size_t next_pow2(std::size_t n) {
        std::size_t cap = 8;
        while (cap < n) {
            cap *= 2;
        }
        return cap;
    }

    std::size_t lookup_atom(const Atom& key) const {
        const std::uint32_t* slots = slots_.data();
        const value_type* items = items_.data();
        const std::size_t nslots = slots_.size();
        if (slots == nullptr || items == nullptr || nslots == 0 || items_.empty()) {
            return npos;
        }
        const Atom::Rec* want = key.rec_;
        const std::size_t mask = nslots - 1;
        std::size_t i = static_cast<std::size_t>(want->hash) & mask;
        for (std::size_t n = 0; n < nslots; ++n) {
            const std::uint32_t slot = slots[i];
            if (slot == kEmpty) {
                return npos;
            }
            if (slot != kTomb && items[slot - 1].first.rec_ == want) {
                return i;
            }
            i = (i + 1) & mask;
        }
        return npos;
    }

    std::size_t lookup_view(std::string_view key) const {
        const std::uint32_t* slots = slots_.data();
        const value_type* items = items_.data();
        const std::size_t nslots = slots_.size();
        if (slots == nullptr || items == nullptr || nslots == 0 || items_.empty()) {
            return npos;
        }
        const std::uint64_t h = hash_text(key);
        const std::size_t mask = nslots - 1;
        std::size_t i = static_cast<std::size_t>(h) & mask;
        for (std::size_t n = 0; n < nslots; ++n) {
            const std::uint32_t slot = slots[i];
            if (slot == kEmpty) {
                return npos;
            }
            if (slot != kTomb) {
                const Atom::Rec* rec = items[slot - 1].first.rec_;
                if (rec->hash == h && rec->text == key) {
                    return i;
                }
            }
            i = (i + 1) & mask;
        }
        return npos;
    }

    void rehash(std::size_t cap) {
        slots_.assign(cap, kEmpty);
        tombs_ = 0;
        std::uint32_t* slots = slots_.data();
        value_type* items = items_.data();
        const std::size_t n = items_.size();
        const std::size_t mask = cap - 1;
        for (std::size_t idx = 0; idx < n; ++idx) {
            std::size_t i = static_cast<std::size_t>(items[idx].first.rec_->hash) & mask;
            while (slots[i] != kEmpty) {
                i = (i + 1) & mask;
            }
            slots[i] = static_cast<std::uint32_t>(idx + 1);
        }
    }

    void grow_if_needed() {
        const std::size_t used = items_.size() + tombs_;
        if (!slots_.empty() && used * 10 < slots_.size() * 7) {
            return;
        }
        const std::size_t cap = slots_.empty() ? 16 : slots_.size() * 2;
        if (items_.capacity() < 32) {
            items_.reserve(32);
        }
        rehash(cap);
    }

    template <typename U>
    V& insert_new(const Atom& key, U&& value) {
        grow_if_needed();
        const std::size_t idx = items_.size();
        items_.emplace_back(key, std::forward<U>(value));
        std::uint32_t* slots = slots_.data();
        const std::size_t mask = slots_.size() - 1;
        std::size_t i = static_cast<std::size_t>(key.rec_->hash) & mask;
        for (;;) {
            const std::uint32_t slot = slots[i];
            if (slot == kEmpty || slot == kTomb) {
                if (slot == kTomb) {
                    --tombs_;
                }
                slots[i] = static_cast<std::uint32_t>(idx + 1);
                return items_.data()[idx].second;
            }
            i = (i + 1) & mask;
        }
    }

    std::size_t erase_at(std::size_t slot) {
        if (slot == npos) {
            return 0;
        }
        const std::size_t idx = slots_[slot] - 1;
        slots_[slot] = kTomb;
        ++tombs_;
        const std::size_t last = items_.size() - 1;
        if (idx != last) {
            retarget(last, idx);
            items_[idx] = std::move(items_[last]);
        }
        items_.pop_back();
        return 1;
    }

    void retarget(std::size_t from, std::size_t to) {
        const std::size_t mask = slots_.size() - 1;
        std::size_t i = static_cast<std::size_t>(items_[from].first.hash()) & mask;
        const std::uint32_t want = static_cast<std::uint32_t>(from + 1);
        for (;;) {
            if (slots_[i] == want) {
                slots_[i] = static_cast<std::uint32_t>(to + 1);
                return;
            }
            i = (i + 1) & mask;
        }
    }

    std::vector<value_type> items_;
    std::vector<std::uint32_t> slots_;
    std::size_t tombs_ = 0;
};

/// A computed style. The stamp distinguishes two maps that reuse one address: the layout intern
/// memo keys `(pointer, stamp)` and must not treat a freed map's address as the same style.
class StyleMap : public FlatMap<Atom> {
public:
    StyleMap() : stamp_(next_stamp()) {}
    StyleMap(const StyleMap& other) : FlatMap<Atom>(other), stamp_(next_stamp()) {}
    StyleMap(StyleMap&& other) noexcept : FlatMap<Atom>(std::move(other)), stamp_(next_stamp()) {}
    StyleMap& operator=(const StyleMap& other) {
        if (this != &other) {
            FlatMap<Atom>::operator=(other);
            stamp_ = next_stamp();
        }
        return *this;
    }
    StyleMap& operator=(StyleMap&& other) noexcept {
        if (this != &other) {
            FlatMap<Atom>::operator=(std::move(other));
            stamp_ = next_stamp();
        }
        return *this;
    }

    std::uint64_t stamp() const { return stamp_; }

private:
    static std::uint64_t next_stamp() {
        static std::atomic<std::uint64_t> next{1};
        return next.fetch_add(1, std::memory_order_relaxed);
    }

    std::uint64_t stamp_ = 0;
};

}  // namespace zui

#endif  // ZUI_HTML_STYLE_MAP_H
