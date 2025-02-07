#pragma once
#include "util.h"
#include <climits>
#include <sys/stat.h>
#include <sys/mman.h>

namespace ba {
using bns::khash_t(64);
using bns::tax_t;

inline void allocate_file(std::FILE *fp, size_t nbytes) {
    std::fseek(fp, 0, SEEK_END);
#if !NDEBUG
    auto npos(std::ftell(fp));
    std::fseek(fp, 0, SEEK_SET); // Ensure we're at the beginning.
    LOG_DEBUG("Size of file before allocation: %s\n", std::to_string(npos).data());
#endif
    static const size_t bufsz(1 << 18);
    std::vector<uint8_t> data(bufsz);
    const int fn(fileno(fp));
    bns::RNGType gen(nbytes);
    while(nbytes >= bufsz) {
        ::write(fn, data.data(), bufsz);
        nbytes -= bufsz;
        for(size_t i(0); i < bufsz / sizeof(u64); ++i)
            *((uint64_t *)data.data() + i) = gen();
    }
    ::write(fn, data.data(), nbytes);
}


class DiskBitArray {
protected:
    std::FILE *fp_; // file pointer
    char      *mm_; // mmap pointer
    const size_t nr_, nc_; // Number of rows, columns
    std::string fpath_;
public:
    DiskBitArray(size_t nfeat, size_t nclades, std::string path="./__file.mm"):
        fp_(std::fopen(path.data(), "w+")), mm_(nullptr), nr_(nfeat), nc_(nclades), fpath_(std::move(path)) {
        if(fp_ == nullptr) throw 1;
        allocate_file(fp_, memsz());
        struct stat sb;
        fstat(fileno(fp_), &sb);
        if((mm_ = (char *)mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(fp_), 0)) == MAP_FAILED) throw 1;
        //madvise(mm_, memsz(), MADV_RANDOM);
        if(madvise(mm_, memsz(), MADV_DONTNEED)) throw 1;
    }
    size_t memsz() const {
        size_t ret(nr_ * nc_);
        ret = (ret >> 3) + !!(ret & 0x7u);
        return ret;
    }
    size_t size() const {return nr_ * nc_;}
    ~DiskBitArray() {
        if(fp_) std::fclose(fp_);
        if(mm_) munmap((void *)mm_, memsz());
    }
    auto set1_ts(size_t index) {
        return __sync_or_and_fetch(mm_ + (index>>3), 1u << (index & 0x7u));
    }
    auto set0_ts(size_t index) {
        return __sync_xor_and_fetch(mm_ + (index>>3), 1u << (index & 0x7u));
    }
    void set1(size_t index) {mm_[index>>3] |= (1u << (index & 0x7u));}
    void set0(size_t index) {mm_[index>>3] &= ~(1u << (index & 0x7u));}
    void set1(size_t row, size_t column) {set1(row * nc_ + column);}
    void set0(size_t row, size_t column) {set0(row * nc_ + column);}
    void set1_ts(size_t row, size_t column) {set1_ts(row * nc_ + column);}
    void set0_ts(size_t row, size_t column) {set0_ts(row * nc_ + column);}
    bool operator[](size_t pos) const {return mm_[pos>>3] & (1u << (pos & 0x7u));}
    bool operator()(size_t row, size_t column) const {return operator[](row * nc_ + column);}
    size_t popcount() const {
        size_t ret, i;
        for(i = ret = 0; i < memsz(); ret += pop::popcount(mm_[i++]));
        return ret;
    }
    void fill_row(std::vector<uint8_t> &data, size_t row) const {
        if(data.size() != nc_) data.resize(nc_);
        std::copy(data.begin(), data.end(), mm_ + row * nc_);
    }
    std::vector<uint8_t> get_row(size_t row) const {
        std::vector<uint8_t> ret;
        fill_row(ret, row);
        return ret;
    }
};

class MMapTaxonomyBitmap: public DiskBitArray {
public:
    MMapTaxonomyBitmap(size_t nkmers, size_t ntax): DiskBitArray(nkmers, ntax) {}
    void set_kmer(const khash_t(64) *map, u64 kmer, tax_t tax) {
        khint_t ki(kh_get(64, map, kmer));
        if(ki == kh_end(map)) throw 1;
        set1(kh_val(map, ki), tax);
    }
    void set_kmer_ts(const khash_t(64) *map, u64 kmer, tax_t tax) {
        khint_t ki(kh_get(64, map, kmer));
        if(ki == kh_end(map)) throw 1;
        DiskBitArray::set1_ts(kh_val(map, ki), tax);
    }
    bool contains_kmer(const khash_t(64) *map, u64 kmer, tax_t tax) const {
        khint_t ki(kh_get(64, map, kmer));
        if(ki == kh_end(map)) throw 1;
        return operator()(kh_val(map, ki), tax);
    }
    void fill_kmer_bitmap(std::vector<uint8_t> &data, const khash_t(64) *map, u64 kmer) {
        if(data.size() != (nc_ + 7)>>3) data.resize((nc_ + 7)>>3);
        khint_t ki(kh_get(64, map, kmer));
        if(ki == kh_end(map)) throw 2;
        std::memcpy(data.data(), mm_ + ((kh_val(map, ki) * nc_)>>3), data.size());
    }
    std::vector<uint8_t> kmer_bitmap(const khash_t(64) *map, u64 kmer) {
        std::vector<uint8_t> ret;
        fill_kmer_bitmap(ret, map, kmer);
        return ret;
    }
};

} // namespace ba
