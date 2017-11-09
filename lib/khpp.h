#ifndef KHPP_H__
#define KHPP_H__
#include <functional>
#include "lib/khash64.h"
#include "lib/hash.h"
#include "tinythreadpp/source/fast_mutex.h"
using std::shared_mutex;

#if __GNUC__ >= 7
#  define CONSTEXPR_IF if constexpr
#else
#  define CONSTEXPR_IF if
#endif

#error("Do not use this. It is currently entirely broken>")
 

namespace emp {
namespace kh {

template<typename khkey_t, typename khval_t,
         typename hash_func=std::hash<khkey_t>, class hash_equal=std::equal_to<khkey_t>,
         bool is_map=true, size_t BUCKET_OFFSET=8>
struct khpp_t {
    using index_type = u64;
    index_type n_buckets, size, n_occupied, upper_bound;
    khint32_t *flags;
    khkey_t   *keys;
    khval_t   *vals;
    mutable shared_mutex m;
    hash_equal he;
    hash_func  hf;
    std::vector<tthread::fast_mutex> locks;

    static const size_t LOCK_BUCKET_SIZE = 1ull << BUCKET_OFFSET;
    static constexpr double HASH_UPPER = 0.77;

    using map_type = khpp_t<khkey_t, khval_t, hash_func, hash_equal, is_map, BUCKET_OFFSET>;
    using key_type = khkey_t;
    using val_type = khval_t;

    struct iterator {
        map_type &t_;
        khint_t ki;
        khkey_t &first() {
            return t_.keys[ki];
        }
        khval_t &second() {
            CONSTEXPR_IF(!is_map)
                throw std::runtime_error("Cannot call second for a hash set.");
            return t_.vals[ki];
        }
        iterator &operator++() {
            if(ki < t_.n_buckets) {
                do {++ki;}
                while(ki < t_.n_buckets && !t_.exist(ki));
            }
            return *this;
        }
        iterator operator++(int) {
            iterator ret(*this);
            operator++();
            return ret;
        }
        auto operator*() {
            return std::make_pair(std::ref(t_.keys[ki]),
                                  std::ref(t_.vals[ki]));
        }
        auto operator*() const {
            return std::make_pair(std::cref(t_.keys[ki]),
                                  std::cref(t_.vals[ki]));
        }
        iterator(map_type &map, khint_t ki=0):
            t_(map), ki(ki) {}
        bool operator!=(const iterator &other) {
            return ki != other.ki;
        }
        bool operator==(const iterator &other) {
            return ki == other.ki;
        }
        bool operator<=(const iterator &other) {
            return ki <= other.ki;
        }
        bool operator<(const iterator &other) {
            return ki < other.ki;
        }
        bool operator>(const iterator &other) {
            return ki > other.ki;
        }
        bool operator>=(const iterator &other) {
            return ki >= other.ki;
        }
        iterator &operator-=(index_type diff) {
            if(ki >= diff) {
                ki -= diff;
            } else {
                throw std::runtime_error("Out of range.");
            }
            return *this;
        }
        iterator &operator+=(index_type diff) {
            if(ki <= t_.n_buckets - diff) {
                ki += diff;
            } else {
                throw std::runtime_error("Out of range.");
            }
            return *this;
        }
        iterator operator+(index_type offset) {
            offset += ki;
            if(offset > t_.n_buckets) {
                throw std::runtime_error("Out of range.");
            }
            return iterator(t_, offset);
        }
        iterator operator-(index_type offset) {
            if(offset > ki) {
                throw std::runtime_error("Out of range.");
            }
            offset -= ki;
            return iterator(t_, offset);
        }
    };
    struct const_iterator {
        const map_type &t_;
        khint_t ki;
        const khkey_t &first() const {
            return static_cast<const khkey_t &>(t_.keys[ki]);
        }
        const khval_t &second() const {
            CONSTEXPR_IF(!is_map)
                throw std::runtime_error("Cannot call second for a hash set.");
            return static_cast<const khval_t &>(t_.vals[ki]);
        }
        const_iterator &operator++(){
            if(ki < t_.n_buckets) {
                do {++ki;} while(ki < t_.n_buckets && !t_.exist(ki));
            }
            return *this;
        }
        const_iterator operator++(int) {
            const_iterator ret(*this);
            operator++();
            return ret;
        }
        const_iterator(const map_type &map, khint_t ki=0):
            t_(map), ki(ki) {}
        bool operator!=(const const_iterator &other) {
            return ki != other.ki;
        }
        bool operator==(const const_iterator &other) {
            return ki == other.ki;
        }
        bool operator<=(const const_iterator &other) {
            return ki <= other.ki;
        }
        bool operator<(const const_iterator &other) {
            return ki < other.ki;
        }
        bool operator>(const const_iterator &other) {
            return ki > other.ki;
        }
        bool operator>=(const const_iterator &other) {
            return ki >= other.ki;
        }
        const_iterator &operator-=(index_type diff) {
            if(ki >= diff) {
                ki -= diff;
            } else {
                throw std::runtime_error("Out of range.");
            }
            return *this;
        }
        const_iterator &operator+=(index_type diff) {
            if(ki <= t_.n_buckets - diff) {
                ki += diff;
            } else {
                throw std::runtime_error("Out of range.");
            }
            return *this;
        }
        const_iterator operator+(index_type offset) {
            offset += ki;
            if(offset > t_.n_buckets) {
                throw std::runtime_error("Out of range.");
            }
            return const_iterator(t_, offset);
        }
        const_iterator operator-(index_type offset) {
            if(offset > ki) {
                throw std::runtime_error("Out of range.");
            }
            offset -= ki;
            return const_iterator(t_, offset);
        }
    };

    bool exist(index_type ix) const {return !__ac_iseither(flags, ix);}

    iterator begin()        {return iterator(*this);}
    iterator end()          {return iterator(*this, n_buckets);}
    const_iterator cbegin() {return const_iterator(*this);}
    const_iterator cend()   {return const_iterator(*this, n_buckets);}


    khpp_t(size_t nb=4): n_buckets(0), size(0), n_occupied(0), upper_bound(0), flags(0), keys(0), vals(0), locks(1) {
        resize(nb);
    }
    ~khpp_t() {
        CONSTEXPR_IF(!std::is_trivially_destructible<khval_t>::value) {
            for(khiter_t i(0); i < n_buckets; ++i) if(exist(i)) vals[i].~khval_t();
        }
        std::free(flags);
        CONSTEXPR_IF(is_map) std::free(vals);
        std::free(keys);
    }
    khpp_t(khpp_t &&other) {
        memset(this, 0, sizeof(*this));
        *this = other;
    }
    khpp_t(const khpp_t &other) {
        *this = other;
    }
    
    void clear() {
        if (flags) {
            m.lock();
            std::memset(flags, 0xaa, __ac_fsize(n_buckets) * sizeof(khint32_t));
            m.unlock();
            __sync_fetch_and_sub(&size, size);
            __sync_fetch_and_sub(&n_occupied, n_occupied);
            assert(size == 0);
            assert(n_occupied == 0);
        }
    }
    index_type iget(const khkey_t &key)
    {
        std::shared_lock<shared_mutex> lock(m);
        if (n_buckets) {
            index_type k, i, last, mask, step = 0;
            mask = n_buckets - 1;
            k = hf(key); i = k & mask;
            last = i;
            while (!__ac_isempty(flags, i) && (__ac_isdel(flags, i) || !he(keys[i], key)))
                if((i = (i + (++step)) & mask) == last)
                    return n_buckets;
            return __ac_iseither(flags, i) ? n_buckets : i;
        }
        return 0;
    }
    template<typename... Args>
    index_type iput(const khkey_t &key, int *ret, Args &&... args)
    {
        //std::shared_lock<shared_mutex> lock(m);
        index_type x;
        if (n_occupied >= upper_bound) { /* update the hash table */
            if (n_buckets > (size<<1)) {
                if (resize(n_buckets - 1) < 0) { /* clear "deleted" elements */
                    *ret = -1; return n_buckets;
                }
            } else if (resize(n_buckets + 1) < 0) { /* expand the hash table */
                *ret = -1; return n_buckets;
            }
        } /* TODO: to implement automatically shrinking; resize() already support shrinking */
        {
            index_type k, i, site, last, mask = n_buckets - 1, step = 0;
            x = site = n_buckets; k = hf(key); i = k & mask;
            if (__ac_isempty(flags, i)) x = i; /* for speed up */
            else {
                last = i;
                while (!__ac_isempty(flags, i) && (__ac_isdel(flags, i) || !he(keys[i], key))) {
                    if (__ac_isdel(flags, i)) site = i;
                    if ((i = (i + (++step)) & mask) == last) { x = site; break;}
                }
                if (x == n_buckets) x = (__ac_isempty(flags, i) && site != n_buckets) ? site: i;
            }
        }
        const auto lock_ind(x >> BUCKET_OFFSET);
        tthread::fast_mutex &local_lock(locks[lock_ind]);
        std::cerr << "Trying to obtain lock for index " << (lock_ind) << '\n';
        local_lock.lock();
        assert(locks.size());
        std::cerr << "Obtained lock for index " << lock_ind << '\n';
        switch((flags[x>>4]>>((x&0xfU)<<1))&3) {
            case 2:
                keys[x] = key;
                __ac_set_isboth_false(flags, x);
                ++size, ++n_occupied;
                vals[x] = khval_t(std::forward<Args>(args)...);
                *ret = 1;
                break;
            case 1:
                keys[x] = key;
                __ac_set_isboth_false(flags, x);
                ++size;
                vals[x] = khval_t(std::forward<Args>(args)...);
                *ret = 2;
                break;
            case 0: *ret = 0; break; /* Don't touch keys[x] if present and not deleted */
        }
        local_lock.unlock();
        std::cerr << "Released lock for index " << (x >> BUCKET_OFFSET) << '\n';
        return x;
    }
    index_type nb() const {return n_buckets;}
    khval_t &operator[](const khkey_t &key) {
        std::shared_lock<shared_mutex>(m);
        index_type ki(iget(key));
        int khr;
        if(ki == nb()) ki = iput(key, &khr), vals[ki] = khval_t();
        return vals[ki];
    }
    int resize(index_type new_n_buckets)
    { /* This function uses 0.25*n_buckets bytes of working space instead of [sizeof(key_t+val_t)+.25]*n_buckets. */
        if(new_n_buckets == n_buckets) return n_buckets;
        khint32_t *new_flags = 0;
        index_type j = 1;
        {
            kroundup64(new_n_buckets);
            if (new_n_buckets < 4) new_n_buckets = 4;
            if (size >= (index_type)(new_n_buckets * HASH_UPPER + 0.5)) j = 0;    /* requested size is too small */
            else { /* hash table size to be changed (shrink or expand); rehash */
                std::cerr << "Getting lock_guard on resizing\n";
                std::shared_lock<decltype(m)> lock(m);
                new_flags = (khint32_t*)std::malloc(__ac_fsize(new_n_buckets) * sizeof(khint32_t));
                if (!new_flags) return -1;
                std::memset(new_flags, 0xaa, __ac_fsize(new_n_buckets) * sizeof(khint32_t));
                if (n_buckets < new_n_buckets) {    /* expand */
                    khkey_t *new_keys = static_cast<khkey_t *>(std::realloc((void *)keys, new_n_buckets * sizeof(khkey_t)));
                    if (!new_keys) { std::free(new_flags); return -1; }
                    keys = new_keys;
                    CONSTEXPR_IF(is_map) {
                        khval_t *new_vals = static_cast<khval_t *>(std::realloc((void *)vals, new_n_buckets * sizeof(khval_t)));
                        if (!new_vals) { std::free(new_flags); return -1; }
                        vals = new_vals;
                    }
                } /* otherwise shrink */
            }
        }
        if (j) { /* rehashing is needed */
            std::shared_lock<decltype(m)> lock(m);
            for (j = 0; j != n_buckets; ++j) {
                if (__ac_iseither(flags, j) == 0) {
                    khkey_t key = keys[j];
                    khval_t val;
                    index_type new_mask;
                    new_mask = new_n_buckets - 1;
                    CONSTEXPR_IF(is_map) val = vals[j];
                    __ac_set_isdel_true(flags, j);
                    while (1) { /* kick-out process; sort of like in Cuckoo hashing */
                        index_type k, i, step = 0;
                        k = hf(key);
                        i = k & new_mask;
                        while (!__ac_isempty(new_flags, i)) i = (i + (++step)) & new_mask;
                        __ac_set_isempty_false(new_flags, i);
                        if (i < n_buckets && __ac_iseither(flags, i) == 0) { /* kick out the existing element */
                            { khkey_t tmp = keys[i]; keys[i] = key; key = tmp; }
                            CONSTEXPR_IF(is_map) { khval_t tmp = vals[i]; vals[i] = val; val = tmp; }
                            __ac_set_isdel_true(flags, i); /* mark it as deleted in the old hash table */
                        } else { /* write the element and jump out of the loop */
                            keys[i] = key;
                            CONSTEXPR_IF(is_map) vals[i] = val;
                            break;
                        }
                    }
                }
            }
            if (n_buckets > new_n_buckets) { /* shrink the hash table */
                keys = static_cast<khkey_t *>(std::realloc((void *)keys, new_n_buckets * sizeof(khkey_t)));
                CONSTEXPR_IF(is_map) vals = static_cast<khval_t *>(std::realloc((void *)vals, new_n_buckets * sizeof(khval_t)));
            }
            std::free(flags); /* free the working space */
            flags       = new_flags;
            n_buckets   = new_n_buckets;
            n_occupied  = size;
            upper_bound = static_cast<index_type>(n_buckets * HASH_UPPER + 0.5);
            locks.resize(n_buckets < LOCK_BUCKET_SIZE ? 1: n_buckets >> BUCKET_OFFSET);
            assert(locks.size());
        }
        return 0;
    }
    template<typename T>
    void func_set(T func, khkey_t &key, const khval_t &val) {
        std::shared_lock<shared_mutex> lock(m);
        func_set_impl(func, key, val);
    }
    template<typename T>
    void func_set_impl(T func, khkey_t &key, const khval_t &val) {
        std::shared_lock<shared_mutex> lock(m);
    }
    khpp_t &operator=(const khpp_t &other) {
        if(this == &other) return *this;
        std::memcpy(this, &other, sizeof(*this));
        flags = static_cast<khint32_t *>(std::malloc(__ac_fsize(n_buckets) * sizeof(khint32_t)));
        std::memcpy(flags, other.flags, __ac_fsize(n_buckets) * sizeof(khint32_t));
        vals  = static_cast<khval_t *>(std::malloc(n_buckets * sizeof(khval_t)));
        std::memcpy(vals, other.vals, n_buckets * sizeof(khval_t));
        keys  = static_cast<khkey_t *>(std::malloc(n_buckets * sizeof(khkey_t)));
        std::memcpy(keys, other.keys, n_buckets * sizeof(khkey_t));
        return *this;
    }
    khpp_t &operator=(khpp_t &&other) {
        if(this != &other) {
            std::memcpy(this, &other, sizeof(*this));
            std::memset(&other, 0, sizeof(other));
        }
        return *this;
    }
	void del(khint_t x)
	{
        //TODO: atomicize __ac_set_isdel_true
		if (x != n_buckets && !__ac_iseither(flags, x)) {
            tthread::fast_mutex &local_lock(locks[x >> BUCKET_OFFSET]);
            local_lock.lock();
			__ac_set_isdel_true(flags, x);
            CONSTEXPR_IF(!std::is_trivially_destructible<khval_t>::value) {
                vals[x].~khval_t();
            }
            local_lock.unlock();
			__sync_fetch_and_sub(&size, 1);
		}
	}
    template<typename LambdaType, typename... Args>
    bool upsert(const khkey_t &key, LambdaType lambda, Args &&... args) {
        // Returns 1 if the key was already present, 0 if it was not.
        int khr;
        index_type pos;
        bool ret;
        std::shared_lock<shared_mutex> lock(m);
        if((pos = iget(key)) == n_buckets) {
            pos = iput(key, &khr, std::forward<Args>(args)...), ret = 0;
            std::cerr << "New key at " << pos << " out of " << n_buckets << '\n';
        } ret = 1;
        tthread::fast_mutex &local_lock(locks[pos >> BUCKET_OFFSET]);
        local_lock.lock();
        assert(!__ac_iseither(flags, pos));
        assert(iget(key) != n_buckets);
        lambda(key, vals[pos]);
        local_lock.unlock();
        return ret;
    }
};

} // namespace kh
} // namespace emp
#endif
