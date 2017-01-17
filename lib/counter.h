#ifndef COUNTER_H__
#define COUNTER_H__
#include <unordered_map>
#include <cstddef>
#include <cstdlib>
#include <vector>

namespace count {

template<typename T, class Hash=std::hash<T>>
class Counter {
    std::unordered_map<T, std::size_t, Hash> map_;
    std::size_t n_;
public:
    Counter(): n_(0) {}

    void add(T elem) {
        auto match(map_.find(elem));
        if(match == map_.end()) map_.emplace(elem, 1);
        else ++match->second;
        ++n_;
    }

    template<typename U, typename = std::enable_if<std::is_same<std::string, T>::value>>
    void add(T &elem) {
        auto match(map_.find(elem));
        if(match == map_.end()) map_.emplace(elem, 1);
        else ++match->second;
        ++n_;
    }

    template<typename U, typename = std::enable_if<!std::is_same<std::string, T>::value>>
    void add(T elem) {
        auto match(map_.find(elem));
        if(match == map_.end()) map_.emplace(elem, 1);
        else ++match->second;
        ++n_;
    }

    template<class C>
    void fill(C container) {
        for(auto i: container) add(i);
    }
    std::size_t size()  const {return map_.size();}
    std::size_t total() const {return n_;}
    auto begin()        const {return map_.begin();}
    auto end()          const {return map_.end();}
};

template<typename T>
size_t unique(T &vec, Counter<uint64_t> &c);

} // namespace count

#endif  // COUNTER_H__
