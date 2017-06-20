#ifndef _UNION_FIND_ADAPTER__
#define _UNION_FIND_ADAPTER__

#if (defined(__GNUC__) && __GNUC__) || (defined(__clang__) && __clang__)
#define PACKED __attribute__((packed))
#else
#define PACKED
#endif

namespace emp { namespace ufa {

// Converts any primitive into
template<typename J>
struct Classified {
    J i;
    operator       J&()       {return i;}
    operator const J&() const {return i;}

    Classified<J>() {}
    template<typename T> Classified<J>(T j): i(j) {}
};

using Double   = Classified<double>;
using Float    = Classified<float>;
using Char     = Classified<char>;
using Short    = Classified<short>;
using Int      = Classified<int>;
using Uint8_t  = Classified<uint8_t>;
using Uint16_t = Classified<uint16_t>;
using Uint32_t = Classified<uint32_t>;
using Uint64_t = Classified<uint64_t>;
using Int8_t   = Classified<int8_t>;
using Int16_t  = Classified<int16_t>;
using Int32_t  = Classified<int32_t>;
using Int64_t  = Classified<int64_t>;
using Uint64_t = Classified<uint64_t>;
using Size_t   = Classified<size_t>;
using Voidptr  = Classified<void*>;

// For composition
template<typename Class, typename size_type=std::uint8_t>
struct uf_adapter: public Class {
    size_type      r_;
    uf_adapter    *p_;

    template<typename... Args>
    uf_adapter(Args&&... args):
        Class(std::forward<Args>(args)...), r_{0}, p_{this} {}
} PACKED;

template<typename T>
T *find(T *node) {
    return node->p_ == node ? node: (node->p_ = find(node->p_));
}

template<typename T>
T *find(T &node) {return find(std::addressof(node));}

template<typename T>
void perform_union(T *a, T *b) {
    if((a = find(a)) == (b = find(b))) return;
    if     (a->r_ < b->r_) b->p_ = a;
    else if(b->r_ < b->r_) a->p_ = b;
    else          b->p_ = a, ++a->r_;
}

template<typename T>
void perform_union(T &a, T &b) {perform_union(std::addressof(a), std::addressof(b));}

}} // namespace emp::ufa

#endif // _UNION_FIND_ADAPTER__
