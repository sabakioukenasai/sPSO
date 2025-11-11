// OKVSBK.cpp
#include "okvsbk.h"
#include "utils.h"
#include "blake3.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "cryptoTools/Crypto/PRNG.h"    // osuCrypto::PRNG
#include "cryptoTools/Common/block.h"   // osuCrypto::block

#ifdef HAVE_OPENMP
#include <omp.h>
#endif

using osuCrypto::PRNG;
using osuCrypto::block;

static const uint8_t bitMasks[8] = {
    0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
};

inline bool OKVSBK::getBit(uint8_t b, uint64_t n) { return (b & bitMasks[n]) != 0; }

// // PRF via cryptoTools oc::PRNG seeded by key (16 bytes)
// // key is uint128_t; we serialize big-endian into block seed
// inline std::vector<uint8_t> OKVSBK::HashToFixedSize(size_t bytesize, block key, block seed) {
//     std::vector<uint8_t> out(bytesize);
//     block in = key ^ seed;

//     // PRNG seeded by key ^ seed (AES-CTR based in cryptoTools)
//     PRNG prng(in);
//     prng.get(out.data(), out.size());
//     return out;
// }

inline std::vector<uint8_t> OKVSBK::HashToFixedSize(size_t bytesize, const block key, const block seed) {
    std::vector<uint8_t> out(bytesize);

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, &key, sizeof(key));
    blake3_hasher_update(&hasher, &seed, sizeof(seed));
    blake3_hasher_finalize(&hasher, out.data(), out.size());

    return out;
}

OKVSBK::OKVSBK(uint64_t n, uint64_t w, double e, block seed_w, block seed_pos)
    : n_(n), m_(static_cast<uint64_t>(std::ceil(n * e))), w_(w), r_(0), b_(0), e_(e), seed_w(seed_w), seed_pos(seed_pos) {
    if (w_ <= 0 || (w_ % 8) != 0) {
        std::cerr << "w = " << w << std::endl;
        throw std::invalid_argument("w must be positive and multiple of 8");
    }
    b_ = w_ / 8;
    if (m_ <= w_) {
        throw std::invalid_argument("m must be > w");
    }
    r_ = m_ - w_;
    p_.resize(0);
    // p_.assign(m_, osuCrypto::ZeroBlock);
}

bool OKVSBK::Encode(const std::vector<block>& keys, const std::vector<block>& values) {
    if ((uint64_t)keys.size() != n_ || (uint64_t)values.size() != n_) {
        throw std::invalid_argument("keys/values size mismatch with n");
    }

    std::vector<uint64_t> piv(n_, -1);
    // std::vector<bool> flags(n_, false);
    std::vector<Row> rows(n_);

    // generate rows (parallel with OpenMP if available)
    #ifdef HAVE_OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (uint64_t idx = 0; idx < n_; ++idx) {
        rows[idx].row = HashToFixedSize(static_cast<size_t>(b_), keys[idx], seed_w);
        rows[idx].value = values[idx];
        uint64_t pos64 = blake3_hash64(keys[idx], seed_pos) % r_;
        rows[idx].bpos = pos64 / 8;
        rows[idx].pos = (rows[idx].bpos << 3);

        // if (idx < 3) {
        //     uint64_t lo = keys[idx].get<uint64_t>(0);
        //     uint64_t hi = keys[idx].get<uint64_t>(1);
        //     std::cerr << "row " << idx <<": pos = " << rows[idx].pos << ", key.lo = " << lo << ", key.hi = " << hi << ", seed = " << print_hex16(seed_w) << ", row = " << print_bytes(rows[idx].row.data(), static_cast<size_t>(b_)) << std::endl;
        // }
    }

    // std::cout << "Rows generated" << std::endl;

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.pos < b.pos; });

    // std::cout << "Rows sorted" << std::endl;

    // before elimination
    // std::cout << "=============== before elimination ======================\n";
    // for (uint64_t i = 0; i < n_; ++i) {
    //     std::cerr << "row " << i <<": pos = " << rows[i].pos  << ", row = " << print_bytes(rows[i].row.data(), static_cast<size_t>(b_)) << std::endl;
    // }
    // std::cout <<"In okvs.encode:"<< "n=" << n_ << ",m=" << m_ << ",w=" << w_ << ",r="<<r_<<"\n\n";

    // forward elimination (serial)
    for (uint64_t i = 0; i < n_; ++i) {
        bool found = false;
        for (uint64_t j = 0; j < w_; ++j) {
            uint64_t byteIndex = j >> 3;
            uint64_t bitIndex = j & 0x7;
            if (getBit(rows[i].row[byteIndex], bitIndex)) {
                piv[i] = rows[i].pos + j;
                found = true;

                for (uint64_t k = i + 1; k < n_; ++k) {
                    if (rows[k].pos > piv[i]) break;
                    uint64_t posk = piv[i] - rows[k].pos;
                    if (getBit(rows[k].row[posk >> 3], posk & 0x7)) {
                        uint64_t shiftnum = rows[k].bpos - rows[i].bpos;
                        for (uint64_t bb = 0; bb < b_ - shiftnum; ++bb) {
                            rows[k].row[bb] ^= rows[i].row[bb + shiftnum];
                        }
                        rows[k].value ^= rows[i].value;
                    }
                }

                break;
            }
        }

        // std::cerr << "row " << i <<": pos = " << rows[i].pos  << ", row = " << print_bytes(rows[i].row.data(), static_cast<size_t>(b_)) << std::endl;

        if (!found) {
            throw std::runtime_error("encode failed at row " + std::to_string(i) + " no pivot found");
        }
    }

    // std::cout << "Forward elimination finished" << std::endl;

    PRNG prng(osuCrypto::sysRandomSeed());
    auto seed = prng.get<oc::block>();
    p_.resize(m_);

    #ifdef HAVE_OPENMP
    #pragma omp parallel
    {
        PRNG local(seed ^ oc::toBlock(omp_get_thread_num()));
        #pragma omp for schedule(static)
        for (uint64_t i = 0; i < p_.size(); ++i) {
            p_[i] = local.get<block>();
        }
    }
    #else
    for (auto &x : p_) x = prng.get<block>();
    #endif

    // std::cout << "Randomization of p finished" << std::endl;

    for (uint64_t idx = n_ - 1; ; --idx) {
        block res(0);
        uint64_t basePos = rows[idx].pos;
        uint64_t pivotIndex = piv[idx];
        // if (pivotIndex < 0 || pivotIndex > m_) {
        //     throw std::runtime_error("pivot index out of range during back substitution");
        // }

        std::vector<uint8_t> row = rows[idx].row;
        for (uint64_t j = 0; j < w_; ++j) {
            uint64_t index = basePos + j;
            // if (index < 0 || index > m_ || index == pivotIndex) continue;
            uint64_t byteIndex = j >> 3;
            uint64_t bitIndex = j & 0x7;
            if (getBit(row[byteIndex], bitIndex)) {
                res ^= p_[index];
            }
        }
        res ^= p_[pivotIndex];
        p_[pivotIndex] = res ^ rows[idx].value;
        if (idx == 0) break;
    }

    // std::cout << "Back substitution finished" << std::endl;

    return true;
}

void OKVSBK::Decode(const std::vector<block>& keys, std::vector<block>& values) const {
    const auto& p = this->p_;
    if (keys.size() != values.size()) {
        throw std::invalid_argument("keys size mismatch with values size");
    }

#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (uint64_t idx = 0; idx < n_; ++idx) {
        auto row = HashToFixedSize(static_cast<size_t>(b_), keys[idx], seed_w);
        uint64_t pos64 = blake3_hash64(keys[idx], seed_pos) % r_;
        uint64_t pos = (pos64 / 8) << 3;
        block acc(0);

        for (uint64_t j = pos; j < pos + w_; ++j) {
            uint64_t local = j - pos;
            uint64_t byteIndex = local / 8;
            uint64_t bitIndex = local % 8;
            if (getBit(row[byteIndex], bitIndex)) {
                acc ^= p[j];
            }
        }
        values[idx] = acc;
    }
}

void OKVSBK::DecodeOtherP(const std::vector<block>& keys, std::vector<block>& values,
                          const std::vector<block>& p) const {
    if (keys.size() != values.size()) {
        throw std::invalid_argument("keys size mismatch with values size");
    }
    if ((uint64_t)p.size() != m_) {
        throw std::invalid_argument("p size mismatch with m");
    }

    size_t num = keys.size();
#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (size_t idx = 0; idx < num; ++idx) {
        auto row = HashToFixedSize(static_cast<size_t>(b_), keys[idx], seed_w);
        uint64_t pos64 = blake3_hash64(keys[idx], seed_pos) % r_;
        uint64_t pos = pos64 & 0xFFFFFFFFFFFFFFF8;
        block acc(0);
        for (uint64_t j = pos; j < pos + w_; ++j) {
            uint64_t local = j - pos;
            uint64_t byteIndex = local / 8;
            uint64_t bitIndex = local % 8;
            if (getBit(row[byteIndex], bitIndex)) {
                acc ^= p[j];
            }
        }
        values[idx] = acc;
    }
}

void OKVSBK::Mul(const okvs::Galois128& delta_gf128) {
#ifdef HAVE_OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (uint64_t idx = 0; idx < (uint64_t)p_.size(); ++idx) {
        okvs::Galois128 g(p_[idx]);
        okvs::Galois128 res = delta_gf128 * g;
        p_[idx] = res.get<block>(0);
    }
}
