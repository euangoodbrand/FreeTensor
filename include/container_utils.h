#ifndef CONTAINER_UTILS_H
#define CONTAINER_UTILS_H

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ir {

template <class T, class V1, class V2, class Hash, class KeyEqual>
std::unordered_map<T, std::pair<V1, V2>, Hash, KeyEqual>
intersect(const std::unordered_map<T, V1, Hash, KeyEqual> &lhs,
          const std::unordered_map<T, V2, Hash, KeyEqual> &rhs) {
    std::unordered_map<T, std::pair<V1, V2>, Hash, KeyEqual> ret;
    ret.reserve(std::min(lhs.size(), rhs.size()));
    for (auto &&[key, v1] : lhs) {
        if (rhs.count(key)) {
            ret.emplace(key, std::make_pair(v1, rhs.at(key)));
        }
    }
    return ret;
}

template <class T, class Hash, class KeyEqual>
std::unordered_set<T, Hash, KeyEqual>
intersect(const std::unordered_set<T, Hash, KeyEqual> &lhs,
          const std::unordered_set<T, Hash, KeyEqual> &rhs) {
    std::unordered_set<T, Hash, KeyEqual> ret;
    ret.reserve(std::min(lhs.size(), rhs.size()));
    for (auto &&key : lhs) {
        if (rhs.count(key)) {
            ret.emplace(key);
        }
    }
    return ret;
}

template <class T, class Hash, class KeyEqual>
bool hasIntersect(const std::unordered_set<T, Hash, KeyEqual> &lhs,
                  const std::unordered_set<T, Hash, KeyEqual> &rhs) {
    for (auto &&item : lhs) {
        if (rhs.count(item)) {
            return true;
        }
    }
    return false;
}

template <class T>
std::vector<T> intersect(const std::vector<T> &lhs, const std::vector<T> &rhs) {
    std::vector<T> ret;
    ret.reserve(std::min(lhs.size(), rhs.size()));
    for (auto &&item : lhs) {
        if (std::find(rhs.begin(), rhs.end(), item) != rhs.end()) {
            ret.emplace_back(item);
        }
    }
    return ret;
}

template <class T>
std::vector<T> uni(const std::vector<T> &lhs, const std::vector<T> &rhs) {
    std::vector<T> ret;
    ret.reserve(std::max(lhs.size(), rhs.size()));
    ret.insert(ret.end(), lhs.begin(), lhs.end());
    for (auto &&item : rhs) {
        if (std::find(lhs.begin(), lhs.end(), item) == lhs.end()) {
            ret.emplace_back(item);
        }
    }
    return ret;
}

} // namespace ir

#endif // CONTAINER_UTILS_H
