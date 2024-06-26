#ifndef CUCKOO_MAP_H
#define CUCKOO_MAP_H 1

#include <iostream>
#include <memory>
#include <mutex>
#include <vector>

#include "InternalCuckooMap.h"

template <class Key, class Value,
          class HashKey1 = HashWithSeed<Key, 0xdeadbeefdeadbeefULL>,
          class HashKey2 = HashWithSeed<Key, 0xabcdefabcdef1234ULL>,
          class CompKey = std::equal_to<Key>,
          class HashShort = HashWithSeed<uint16_t, 0xfedcbafedcba4321ULL>>
class CuckooMap {
  public:
    typedef Key KeyType; // these are for ShardedMap
    typedef Value ValueType;
    typedef HashKey1 HashKey1Type;
    typedef HashKey2 HashKey2Type;
    typedef CompKey CompKeyType;
    typedef InternalCuckooMap<Key, Value, HashKey1, HashKey2, CompKey> Subtable;
    typedef CuckooFilter<Key, HashKey1, HashKey2, HashShort, CompKey> Filter;

  private:
    size_t _firstSize;
    size_t _valueSize;
    size_t _valueAlign;
    CompKey _compKey;

  public:
    CuckooMap(size_t firstSize, size_t valueSize = sizeof(Value),
              size_t valueAlign = alignof(Value), bool useFilters = false)
        : _firstSize(firstSize), _valueSize(valueSize), _valueAlign(valueAlign),
          _randState(0x2636283625154737ULL), _dummyFilter(false, 0), _nrUsed(0),
          _useFilters(useFilters) {
        auto t = new Subtable(false, firstSize, valueSize, valueAlign);
        try {
            _tables.emplace_back(t);
        } catch (...) {
            delete t;
            throw;
        }
        if (_useFilters) {
            auto f = new Filter(false, _tables.back()->capacity());
            try {
                _filters.emplace_back(f);
            } catch (...) {
                delete f;
                throw;
            }
        }
    }

    struct Finding {

        friend class CuckooMap;

        Key *key() const { return _key; }

        Value *value() const { return _value; }

      private:
        Key *_key;
        Value *_value;
        CuckooMap *_map;
        int32_t _layer;

      public:
        Finding(Key *k, Value *v, CuckooMap *m, int32_t l)
            : _key(k), _value(v), _map(m), _layer(l) {}

        constexpr Finding() noexcept
            : _key(nullptr), _value(nullptr), _map(nullptr), _layer(-1) {}

        ~Finding() {
            if (_map != nullptr) {
                _map->release();
            }
        }

        Finding(Finding const &other) = delete;
        Finding &operator=(Finding const &other) = delete;

        // Allow moving, we need this to allow for copy elision where we
        // return by value:
      public:
        Finding(Finding &&other) {
            std::cout << "move" << std::endl;
            if (_map != nullptr) {
                _map->release();
            }
            _map = other._map;
            other._map = nullptr;

            _key = other._key;
            _value = other._value;
            _layer = other._layer;
            other._key = nullptr;
        }

        Finding &operator=(Finding &&other) {
            std::cout << "move assign" << std::endl;
            if (_map != nullptr) {
                _map->release();
            }
            _map = other._map;
            other._map = nullptr;

            _key = other._key;
            _value = other._value;
            _layer = other._layer;
            other._key = nullptr;
            return *this;
        }
        int32_t found() const {
            return (_map != nullptr && _key != nullptr) ? 1 : 0;
        }

        bool next() { return false; }

        bool get(int32_t pos) { return false; }
    };

    Finding lookup(Key const &k) {
        MyMutexGuard guard(_mutex);
        Finding f(nullptr, nullptr, this, -1);
        innerLookup(k, f, true);
        guard.release();
        return f;
    }

    bool lookup(Key const &k, Finding &f) {
        if (f._map != this) {
            f._map->_mutex.unlock();
            f._map = this;
            _mutex.lock();
        }
        f._key = nullptr;
        innerLookup(k, f, true);
        return f.found() > 0;
    }

    bool insert(Key const &k, Value const *v) {
        MyMutexGuard guard(_mutex);
        return innerInsert(k, v, nullptr, -1);
    }

    bool insert(Key const &k, Value const *v, Finding &f) {
        if (f._map != this) {
            f._map->_mutex.unlock();
            f._map = this;
            _mutex.lock();
        }
        bool res = innerInsert(k, v, nullptr, -1);
        f._key = nullptr;
        return res;
    }

    bool remove(Key const &k) {
        MyMutexGuard guard(_mutex);
        Finding f(nullptr, nullptr, this, -1);
        innerLookup(k, f, false);
        guard.release();
        if (f.found() == 0) {
            return false;
        }
        innerRemove(f);
        return true;
    }

    bool remove(Finding &f) {
        if (f._map != this) {
            f._map->_mutex.unlock();
            f._map = this;
            _mutex.lock();
        }
        if (f._key == nullptr) {
            return false;
        }
        innerRemove(f);
        return true;
    }

  private:
    void innerLookup(Key const &k, Finding &f, bool moveToFront) {
        char buffer[_valueSize];
        // f must be initialized with _key == nullptr
        for (int32_t layer = 0; static_cast<uint32_t>(layer) < _tables.size();
             ++layer) {
            Subtable &sub = *_tables[layer];
            Filter &filter = _useFilters ? *_filters[layer] : _dummyFilter;
            Key *key;
            Value *value;
            bool found = _useFilters
                             ? (filter.lookup(k) && sub.lookup(k, key, value))
                             : sub.lookup(k, key, value);
            if (found) {
                f._key = key;
                f._value = value;
                f._layer = layer;
                if (moveToFront && layer > 0) {
                    uint8_t fromBack = _tables.size() - layer;
                    uint8_t denominator =
                        (fromBack >= 6) ? (2 << 6) : (2 << fromBack);
                    uint8_t mask = denominator - 1;
                    uint8_t r = pseudoRandomChoice();
                    if ((r & mask) == 0) {
                        Key kCopy = *key;
                        memcpy(buffer, value, _valueSize);
                        Value *vCopy = reinterpret_cast<Value *>(&buffer);

                        innerRemove(f);
                        innerInsert(kCopy, vCopy, &f, layer - 1);
                    }
                }
                return;
            };
        }
    }

    bool innerInsert(Key const &k, Value const *v, Finding *f, int layerHint) {
        // inserts a pair (k, v) into the table
        // returns true if the insertion took place and false if there was
        // already a pair with the same key k in the table, in which case
        // the table is unchanged.

        Key kCopy = k;
        Key originalKey = k;
        Key originalKeyAtLayer = k;
        char buffer[_valueSize];
        memcpy(buffer, v, _valueSize);
        Value *vCopy = reinterpret_cast<Value *>(&buffer);

        int32_t lastLayer = _tables.size() - 1;
        int32_t layer = (layerHint < 0) ? lastLayer : layerHint;
        int res;
        bool filterRes;
        bool somethingExpunged = true;
        while (static_cast<uint32_t>(layer) < _tables.size()) {
            Subtable &sub = *_tables[layer];
            Filter &filter = _useFilters ? *_filters[layer] : _dummyFilter;
            int maxRounds = (layerHint < 0) ? 128 : 4;
            for (int i = 0; i < maxRounds; ++i) {
                if (f != nullptr && _compKey(originalKey, kCopy)) {
                    res = sub.insert(kCopy, vCopy, &(f->_key), &(f->_value));
                    f->_layer = layer;
                } else {
                    res = sub.insert(kCopy, vCopy, nullptr, nullptr);
                }
                if (res < 0) { // key is already in the table
                    return false;
                } else if (res == 0) {
                    if (_useFilters) {
                        filterRes = filter.insert(originalKeyAtLayer);
                        if (!filterRes) {
                            throw;
                        }
                    }
                    ++_nrUsed;
                    somethingExpunged = false;
                    break;
                }
            }
            // check if table is too full; if so, expunge a random element
            if (!somethingExpunged && sub.overfull()) {
                bool expunged = sub.expungeRandom(kCopy, vCopy);
                if (!expunged) {
                    throw;
                }
                somethingExpunged = true;
            }
            if (somethingExpunged) {
                if (_useFilters && !_compKey(kCopy, originalKeyAtLayer)) {
                    filterRes = filter.remove(kCopy);
                    if (!filterRes) {
                        throw;
                    }
                    filterRes = filter.insert(originalKeyAtLayer);
                    if (!filterRes) {
                        throw;
                    }
                    originalKeyAtLayer = kCopy;
                }
                layer = (layer == lastLayer) ? layer + 1 : lastLayer;
            } else {
                return true;
            }
        }
        // If we get here, then some pair has been expunged from all tables and
        // we have to append a new table:
        uint64_t lastSize = _tables.back()->capacity();
        /*std::cout << "Insertion failure at level " << _tables.size() - 1 << "
           at
           "
                  << 100.0 *
                         (((double)_tables.back()->nrUsed()) /
           ((double)lastSize))
                  << "% capacity with cold " << coldInsert << std::endl;*/
        bool useMmap = (_tables.size() >= 3);
        auto t = new Subtable(useMmap, lastSize * 4, _valueSize, _valueAlign);
        try {
            _tables.emplace_back(t);
        } catch (...) {
            delete t;
            throw;
        }
        if (_useFilters) {
            auto fil = new Filter(useMmap, lastSize * 4);
            try {
                _filters.emplace_back(fil);
            } catch (...) {
                delete f;
                throw;
            }
        }
        originalKeyAtLayer = kCopy;
        while (res > 0) {
            if (f != nullptr && _compKey(originalKey, kCopy)) {
                res = _tables.back()->insert(kCopy, vCopy, &(f->_key),
                                             &(f->_value));
                f->_layer = layer;
            } else {
                res = _tables.back()->insert(kCopy, vCopy, nullptr, nullptr);
            }
        }
        if (_useFilters) {
            filterRes = _filters.back()->insert(originalKeyAtLayer);
            if (!filterRes) {
                throw;
            }
        }
        ++_nrUsed;
        return true;
    }

    uint8_t pseudoRandomChoice() {
        _randState = _randState * 997 + 17; // ignore overflows
        return static_cast<uint8_t>((_randState >> 37) & 0xff);
    }

    void release() { _mutex.unlock(); }

    void innerRemove(Finding &f) {
        if (_useFilters) {
            _filters[f._layer]->remove(*(f._key));
        }
        _tables[f._layer]->remove(f._key, f._value);
        f._key = nullptr;
        --_nrUsed;
    }

    uint64_t _randState; // pseudo random state for move-to-front heuristic
    std::vector<std::unique_ptr<Subtable>> _tables;
    std::vector<std::unique_ptr<Filter>> _filters;
    Filter _dummyFilter;
    std::mutex _mutex;
    uint64_t _nrUsed;
    bool _useFilters;
};

#endif