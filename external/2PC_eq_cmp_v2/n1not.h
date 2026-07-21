#pragma once
#include <cryptoTools/Crypto/PRNG.h>
#include <cryptoTools/Common/block.h>
#include "libOTe/TwoChooseOne/Iknp/IknpOtExtReceiver.h"
#include "libOTe/TwoChooseOne/Iknp/IknpOtExtSender.h"
#include "coproto/Socket/AsioSocket.h"
#include "libOTe/Base/BaseOT.h"
#include "cryptoTools/Common/BitVector.h"
#include "coproto/Common/span.h"

#include <vector>
#include <array>
#include <deque>
#include <cstring>


template<typename T>
class NCN1OT {
public:
    NCN1OT(Role role, int nums, int length, osuCrypto::IknpOtExtSender *sender, osuCrypto::IknpOtExtReceiver *recver, coproto::AsioSocket &chl)
        : role(role), nums(nums), length(length), sender(sender), recver(recver), chl(chl) {
        prg.SetSeed(osuCrypto::sysRandomSeed());
    }

    ~NCN1OT() = default;

    void send(T** data) {
        int depth = static_cast<int>(log2(length)) + 1;
        int numOTs = nums * depth;
        std::vector<std::vector<T>> seeds(nums);
        std::vector<T> str0(nums * depth);
        std::vector<T> str1(nums * depth);

        for (int i = 0; i < nums; ++i) {
            OblivSetup(length, seeds[i], str0.data() + i * depth, str1.data() + i * depth);
        }

        send_ot(str0.data(), str1.data(), numOTs);

        for (int i = 0; i < nums; ++i) {
            Expand(length, static_cast<uint64_t>(-1), seeds[i], data[i]);
        }
    }

    void recv(T **data, uint8_t *epsilon) {
        int depth = static_cast<int>(log2(length)) + 1;
        int numOTs = nums * depth;
        std::vector<std::vector<T>> seeds(nums);
        osuCrypto::BitVector chosen_bit(numOTs);
        std::vector<T> temp(numOTs);

        // Generate unbiased epsilon values via rejection sampling.
        // Simple prg.get + modulo would bias toward smaller values.
        {
            uint8_t threshold = 256 - (256 % length);
            for (int i = 0; i < nums; ++i) {
                uint8_t val;
                do {
                    prg.get(&val, 1);
                } while (val >= threshold);
                epsilon[i] = val % length;
            }
        }

        for (int i = 0; i < nums; ++i) {
            OblivSetup(length, seeds[i], nullptr, nullptr);
            int tmp_i = i * depth;
            uint64_t x = epsilon[i];
            for (int j = depth - 1; j >= 0; --j) {
                chosen_bit[tmp_i + j] = (x & 1) ^ 1;
                x >>= 1;
            }
        }

        recv_ot(temp.data(), chosen_bit, numOTs);

        for (int i = 0; i < nums; ++i) {
            std::memcpy(seeds[i].data(), temp.data() + i * depth, depth * sizeof(T));
            Expand(length, epsilon[i], seeds[i], data[i]);
        }
    }

private:
    Role role;
    coproto::AsioSocket chl;
    osuCrypto::IknpOtExtSender *sender;
    osuCrypto::IknpOtExtReceiver *recver;
    osuCrypto::PRNG prg;

    int nums, length;

    void DLenPRG(T seed, T *out) {
        // Use both halves of the AES key for full 128-bit entropy.
        // toBlock(0, seed) would leave the high 64 bits zero, weakening security.
        osuCrypto::block temp = osuCrypto::toBlock(seed, ~seed);
        osuCrypto::AES aes(temp);
        osuCrypto::block res;

        aes.ecbEncBlocks<1>(&osuCrypto::AllOneBlock, &res);
        std::memcpy(out, &res, 2 * sizeof(T));
    }

    void OblivSetup(uint64_t length, std::vector<T>& seed, T *str0, T *str1) {
        int depth = static_cast<int>(log2(length)) + 1;
        if (role == Role::Sender) {
            seed.resize(1);
            prg.get(seed.data(), 1);
            if (str0 && str1) {
                std::memset(str0, 0x00, depth * sizeof(T));
                std::memset(str1, 0x00, depth * sizeof(T));
                PrepareCorrelation(0, depth, seed[0], str0, str1);
            }
        } else {
            seed.resize(depth);
        }
    }

    void SimpleExpand(int cur_depth, int depth, int index, T seed, T *v) {
        T next_seeds[2];
        DLenPRG(seed, next_seeds);

        if (cur_depth == depth - 1) {
            if (index < static_cast<int>(length))
                v[index] = next_seeds[0];
            if (index + 1 < static_cast<int>(length))
                v[index + 1] = next_seeds[1];
            return;
        }

        int offset = 1 << (depth - cur_depth - 1);
        SimpleExpand(cur_depth + 1, depth, index, next_seeds[0], v);
        SimpleExpand(cur_depth + 1, depth, index + offset, next_seeds[1], v);
    }

    // GGM puncturable PRF tree expansion for the Receiver.
    // Uses a parallel deque<bool> to track punctured (placeholder) nodes instead of
    // relying on a magic sentinel value (0), which is a valid PRG output.
    // (The old CheckOnPath(T x, T y) { return x == y; } was removed as it compared
    // against the 0 sentinel, risking a 1/2^64 collision with real PRG outputs.)
    void PunctureExpand(int depth, uint64_t x, const T *seeds, T *v) {
        int cur_depth = 1;

        std::vector<bool> bits(depth);
        T agg_mask;
        T next_seeds[2];

        // q holds tree node values; punctured[i] is true when q[i] is a placeholder
        // for a punctured node (its value is ignored).
        std::deque<T> q;
        std::deque<bool> punctured;

        for (int i = depth - 1; i >= 0; --i) {
            bits[i] = (x & 1) ^ 1;
            x >>= 1;
        }

        int path = bits[0] ? 1 : 0;

        // Root: one punctured placeholder, one real seed.
        if (bits[0]) {
            q.push_back(0);       punctured.push_back(true);
            q.push_back(seeds[0]); punctured.push_back(false);
        } else {
            q.push_back(seeds[0]); punctured.push_back(false);
            q.push_back(0);        punctured.push_back(true);
        }

        while (cur_depth < depth) {
            std::size_t size = q.size();
            agg_mask = 0;
            for (std::size_t i = 0; i < size; ++i) {
                T seed = q.front();
                bool is_punctured = punctured.front();
                q.pop_front();
                punctured.pop_front();

                if (!is_punctured) {
                    DLenPRG(seed, next_seeds);
                    q.push_back(next_seeds[0]); punctured.push_back(false);
                    q.push_back(next_seeds[1]); punctured.push_back(false);
                    agg_mask ^= next_seeds[bits[cur_depth] ? 1 : 0];
                } else {
                    // Punctured placeholder: insert correction seed and a new placeholder.
                    if (bits[cur_depth]) {
                        q.push_back(0);               punctured.push_back(true);
                        q.push_back(seeds[cur_depth]); punctured.push_back(false);
                    } else {
                        q.push_back(seeds[cur_depth]); punctured.push_back(false);
                        q.push_back(0);                punctured.push_back(true);
                    }
                }
            }
            path = ((path ^ 1) << 1) ^ (bits[cur_depth] ? 1 : 0);
            q[path] ^= agg_mask;
            cur_depth++;
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(length) && i < q.size(); ++i) {
            v[i] = q[i];
        }
    }

    void Expand(uint64_t length, uint64_t x, const std::vector<T>& seeds, T *v) {
        int depth = static_cast<int>(log2(length)) + 1;
        if (role == Role::Sender) {
            SimpleExpand(0, depth, 0, seeds[0], v);
        } else {
            PunctureExpand(depth, x, seeds.data(), v);
        }
    }

    void PrepareCorrelation(int cur_depth, int depth, T seed, T *str0, T *str1) {
        if (cur_depth == depth) {
            return;
        }

        T next_seeds[2];
        DLenPRG(seed, next_seeds);
        str0[cur_depth] ^= next_seeds[0];
        str1[cur_depth] ^= next_seeds[1];
        PrepareCorrelation(cur_depth + 1, depth, next_seeds[0], str0, str1);
        PrepareCorrelation(cur_depth + 1, depth, next_seeds[1], str0, str1);
    }

    void send_ot(const T* data0, const T* data1, int64_t length) {
        osuCrypto::AlignedUnVector<std::array<osuCrypto::block, 2>> data(length);
        cp::sync_wait(sender->send(data, prg, chl));

        std::vector<T> pad1(2 * length);

        for (int64_t i = 0; i < length; ++i) {
            T m0, m1;
            std::memcpy(&m0, data[i][0].data(), sizeof(T));
            std::memcpy(&m1, data[i][1].data(), sizeof(T));
            pad1[2*i] = m0 ^ data0[i];
            pad1[2*i+1] = m1 ^ data1[i];
        }

        cp::sync_wait(sync(chl, role));
        coproto::span<T> a(pad1.data(), 2 * length);
        cp::sync_wait(chl.send(a));
    }

    void recv_ot(T* datar, const osuCrypto::BitVector &r, int64_t length) {
        std::vector<osuCrypto::block> data(length);

        cp::sync_wait(recver->receive(r, data, prg, chl));

        std::vector<T> res(2 * length);
        cp::sync_wait(sync(chl, role));

        coproto::span<T> a(res.data(), 2 * length);
        cp::sync_wait(chl.recv(a));

        for (int64_t i = 0; i < length; ++i) {
            T m;
            std::memcpy(&m, data[i].data(), sizeof(T));
            datar[i] = res[2 * i + r[i]] ^ m;
        }
    }
};
