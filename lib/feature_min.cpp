#include <cctype>
#include "feature_min.h"
#include <cstring>

namespace emp {


void lca2depth(khash_t(c) *lca_map, const khash_t(p) *tax_map) {
    for(khiter_t ki(kh_begin(lca_map)); ki != kh_end(lca_map); ++ki)
        if(kh_exist(lca_map, ki))
            kh_val(lca_map, ki) = node_depth(tax_map, kh_val(lca_map, ki));
}

khash_t(c) *make_depth_hash(khash_t(c) *lca_map, const khash_t(p) *tax_map) {
    khash_t(c) *ret(kh_init(c));
    kh_resize(c, ret, kh_size(lca_map));
    khiter_t ki1;
    int khr;
    for(khiter_t ki2(kh_begin(lca_map)); ki2 != kh_end(lca_map); ++ki2) {
        if(kh_exist(lca_map, ki2)) {
            ki1 = kh_put(c, ret, kh_key(lca_map, ki2), &khr);
            kh_val(ret, ki1) = node_depth(tax_map, kh_val(lca_map, ki2));
        }
    }
    return ret;
}

void update_feature_counter(khash_t(64) *kc, const khash_t(all) *set, const khash_t(p) *tax, const tax_t taxid) {
    // TODO: make this threadsafe.
    int khr;
    khint_t k2;
    for(khiter_t ki(kh_begin(set)); ki != kh_end(set); ++ki) {
        if(kh_exist(set, ki)) {
           if((k2 = kh_get(64, kc, kh_key(set, ki))) == kh_end(kc)) {
                k2 = kh_put(64, kc, kh_key(set, ki), &khr);
                kh_val(kc, k2) = FMencode(1, node_depth(tax, taxid));
            } else while(!kh_try_set(64, kc, k2, FMencode(FMcount(kh_val(kc, k2)), lca(tax, taxid, kh_val(kc, k2)))));
        }
    }
}

void update_minimized_map(const khash_t(all) *set, const khash_t(64) *full_map, khash_t(c) *ret) {
    khiter_t kif;
    LOG_DEBUG("Size of set: %zu\n", kh_size(set));
    for(khiter_t ki(0); ki != kh_end(set); ++ki) {
        if(!kh_exist(set, ki) || kh_get(c, ret, kh_key(set, ki)) != kh_end(ret))
            continue;
            // If the key is already in the main map, what's the problem?
        if(unlikely((kif = kh_get(64, full_map, kh_key(set, ki))) == kh_end(full_map)))
            LOG_EXIT("Missing kmer from database... Check for matching spacer and kmer size.\n");
        kh_set(c, ret, kh_key(full_map, kif), kh_val(full_map, kif));
        if(unlikely(kh_size(ret) % 1000000 == 0)) LOG_INFO("Final hash size %zu\n", kh_size(ret));
    }
    return;
}

khash_t(64) *make_taxdepth_hash(khash_t(c) *kc, const khash_t(p) *tax) {
    khash_t(64) *ret(kh_init(64));
    int khr;
    khiter_t kir;
    kh_resize(64, ret, kc->n_buckets);
    for(khiter_t ki(0); ki != kh_end(kc); ++ki) {
        if(kh_exist(kc, ki)) {
            kir = kh_put(64, ret, kh_key(kc, ki), &khr);
            kh_val(ret, kir) = TDencode(node_depth(tax, kh_val(kc, ki)), kh_val(kc, ki));
        }
    }
    return ret;
}


void update_lca_map(khash_t(c) *kc, const khash_t(all) *set, const khash_t(p) *tax, tax_t taxid, std::shared_mutex &m) {
    std::unique_lock<std::shared_mutex> lock(m);
    int khr;
    khint_t k2;
    LOG_DEBUG("Adding set of size %zu t total set of current size %zu.\n", kh_size(set), kh_size(kc));
    for(khiter_t ki(kh_begin(set)); ki != kh_end(set); ++ki) {
        if(kh_exist(set, ki)) {
            if((k2 = kh_get(c, kc, kh_key(set, ki))) == kh_end(kc)) {
                k2 = kh_put(c, kc, kh_key(set, ki), &khr);
                kh_val(kc, k2) = taxid;
#if !NDEBUG
                if(unlikely(kh_size(kc) % 1000000 == 0)) LOG_DEBUG("Final hash size %zu\n", kh_size(kc));
#endif
            } else if(kh_val(kc, k2) != taxid) {
                kh_val(kc, k2) = lca(tax, taxid, kh_val(kc, k2));
                if(kh_val(kc, k2) == UINT32_C(-1)) kh_val(kc, k2) = 1, LOG_WARNING("Missing taxid %u. Setting lca to 1\n", taxid);
            }
        }
    }
    LOG_DEBUG("After updating with set of size %zu, total set current size is %zu.\n", kh_size(set), kh_size(kc));
}

void update_td_map(khash_t(64) *kc, const khash_t(all) *set, const khash_t(p) *tax, tax_t taxid) {
    int khr;
    khint_t k2;
    tax_t val;
    LOG_DEBUG("Adding set of size %zu t total set of current size %zu.\n", kh_size(set), kh_size(kc));
    for(khiter_t ki(kh_begin(set)); ki != kh_end(set); ++ki) {
        if(kh_exist(set, ki)) {
            if((k2 = kh_get(64, kc, kh_key(set, ki))) == kh_end(kc)) {
                k2 = kh_put(64, kc, kh_key(set, ki), &khr);
                kh_val(kc, k2) = TDencode(node_depth(tax, kh_val(kc, ki)), kh_val(kc, ki));
                if(unlikely(kh_size(kc) % 1000000 == 0)) LOG_INFO("Final hash size %zu\n", kh_size(kc));
            } else if(kh_val(kc, k2) != taxid) {
                do val = lca(tax, taxid, kh_val(kc, k2));
                while(!kh_try_set(64, kc, k2, val == (tax_t)-1 ? 1: TDencode(node_depth(tax, val), val)));
            }
        }
    }
    LOG_DEBUG("After updating with set of size %zu, total set current size is %zu.\n", kh_size(set), kh_size(kc));
}

} //namespace emp
