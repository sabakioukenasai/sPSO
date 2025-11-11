#pragma once
// OKVSBK.h
// OKVS BK-style near-banded Gauss solver
// PRF implemented using cryptoTools oc::PRNG (AES-CTR based), parallel loops via OpenMP macro HAVE_OPENMP

#include <cstdint>
#include <vector>

#include "galois128.h"   // your Galois128 implementation (uses oc::block/block)
#include "cryptoTools/Crypto/PRNG.h"    // osuCrypto::PRNG
#include "cryptoTools/Common/block.h"   // osuCrypto::block

// Use GCC/Clang builtin 128-bit integer
// using block = unsigned __int128;
using block = osuCrypto::block;

struct Row {
    uint64_t pos;                 // bit index (aligned to byte boundary: bpos<<3)
    uint64_t bpos;                // byte position (pos / 8)
    std::vector<std::uint8_t> row; // raw bytes produced by PRF (length = b = w/8)
    block value;
};

struct OKVSParam {
    uint64_t n;
    uint64_t w;
    double e;
};

class OKVSBK {
public:
    OKVSBK(uint64_t n, uint64_t w, double e, block seed_w, block seed_pos);
    // explicit OKVSBK(OKVSParam param, block seed);
    OKVSBK() = delete;

    uint64_t getN() const { return n_; }
    uint64_t getM() const { return m_; }
    uint64_t getW() const { return w_; }
    uint64_t getR() const { return r_; }
    uint64_t getB() const { return b_; }
    double getE() const { return e_; }

    // Encode keys->values into internal p_. Returns true on success; throws on failure.
    // Caller should guarantees there are no identical keys.
    bool Encode(const std::vector<block>& keys, const std::vector<block>& values);

    // Decode into `values` (values must be sized to n_)
    void Decode(const std::vector<block>& keys, std::vector<block>& values) const;

    // Decode into `values` using externally provided p vector (const-ref to avoid copy)
    void DecodeOtherP(const std::vector<block>& keys, std::vector<block>& values,
                        const std::vector<block>& p) const;

    // Multiply p vector by delta in GF(2^128)
    void Mul(const okvs::Galois128& delta_gf128);

    // expose p
    const std::vector<block>& getP() const { return p_; }

private:
    uint64_t n_;  // number of kv pairs
    uint64_t m_;  // length of p vector
    uint64_t w_;  // width in bits (multiple of 8)
    uint64_t r_;  // r = m - w
    uint64_t b_;  // bytes = w / 8
    double e_;
    block seed_w;
    block seed_pos;
    std::vector<block> p_;  // length m_

    // internal helpers
    static inline std::vector<uint8_t> HashToFixedSize(size_t bytesize, block key, block seed);
    static inline uint64_t BytesToUint64(const std::vector<uint8_t>& bytes);
    static inline bool getBit(uint8_t byte, uint64_t idx);
};
