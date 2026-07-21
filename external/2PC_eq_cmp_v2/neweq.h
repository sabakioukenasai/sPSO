#pragma once

#include "cryptoTools/Common/Timer.h"
#include "utils.h"
#include "coproto/Common/macoro.h"
#include "coproto/Socket/AsioSocket.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "libOTe/Base/BaseOT.h"
#include "libOTe/TwoChooseOne/Iknp/IknpOtExtReceiver.h"
#include "libOTe/TwoChooseOne/Iknp/IknpOtExtSender.h"
#include "n1not.h"
#include <cstdint>
#include <macoro/sync_wait.h>
#include <vector>
#include <array>
#include <string>


template<typename T>
class eq1{
private:
    osuCrypto::BitVector bits, share;
    std::vector<uint8_t> epsilon;
    Role role;
    coproto::AsioSocket chl;
    osuCrypto::IknpOtExtSender *sender;
    osuCrypto::IknpOtExtReceiver *recver;
    osuCrypto::PRNG prg;
    std::vector<T> w, tmp;

    void vose(osuCrypto::BitVector &bits, uint8_t *delta, osuCrypto::BitVector &output, int num, int size);
    void initOT(osuCrypto::IknpOtExtSender *sender, osuCrypto::IknpOtExtReceiver *recver);
public:
    eq1(Role role, std::string ip, osuCrypto::IknpOtExtSender *sender, osuCrypto::IknpOtExtReceiver *recver);
    eq1(Role role, coproto::AsioSocket &chl, osuCrypto::IknpOtExtSender *sender, osuCrypto::IknpOtExtReceiver *recver);

    void init(int n, uint32_t ell);
    void offline(int n, uint32_t ell);
    void online(int n, uint32_t ell, T* data, osuCrypto::BitVector &output);
    void run(std::vector<T> data, osuCrypto::BitVector &output, uint32_t ell, int numThreads = 1, bool random = true);
};

template<typename T>
class eq2{
    Role role;
    coproto::AsioSocket chl;
    osuCrypto::IknpOtExtSender *sender;
    osuCrypto::IknpOtExtReceiver *recver;
    osuCrypto::PRNG prg;
    osuCrypto::BitVector r;
    std::vector<T> t;

    void initOT(osuCrypto::IknpOtExtSender *sender, osuCrypto::IknpOtExtReceiver *recver);
    void convert_offline(T p, int size);
    void convert_online(T p, int size, osuCrypto::BitVector bits, T* output);
public:
    eq2(Role role, std::string ip, osuCrypto::IknpOtExtSender *sender, osuCrypto::IknpOtExtReceiver *recver);
    void run(std::vector<osuCrypto::block> &data, osuCrypto::BitVector &output, uint32_t ell, int numThreads = 1, bool random = false);
    void online(int size, int len, T* lookupTable, T* delta, T* x, T* output);
};

template<typename T>
eq1<T>::eq1(Role role, std::string ip, osuCrypto::IknpOtExtSender *sender, osuCrypto::IknpOtExtReceiver *recver) {
    this->role = role;
    chl = coproto::asioConnect(ip, role == Role::Sender);
    prg.SetSeed(osuCrypto::sysRandomSeed());
    initOT(sender, recver);
}

template<typename T>
eq1<T>::eq1(Role role, coproto::AsioSocket &chl, osuCrypto::IknpOtExtSender *sender, osuCrypto::IknpOtExtReceiver *recver) {
    this->role = role;
    this->chl = chl;
    prg.SetSeed(osuCrypto::sysRandomSeed());
    initOT(sender, recver);
}

template<typename T>
void eq1<T>::initOT(osuCrypto::IknpOtExtSender *sender_, osuCrypto::IknpOtExtReceiver *recver_) {
    if (role == Role::Sender) {
        if (sender_ != nullptr) {
            this->sender = sender_;
        } else {
            this->sender = new osuCrypto::IknpOtExtSender();

            osuCrypto::DefaultBaseOT base;
            osuCrypto::BitVector bv(sender->baseOtCount());
            std::vector<osuCrypto::block> base_msg(sender->baseOtCount());

            bv.randomize(prg);
            cp::sync_wait(base.receive(bv, base_msg, prg, chl));
            sender->setBaseOts(base_msg, bv);
        }
    } else {
        if (recver_) {
            this->recver = recver_;
        } else {
            this->recver = new osuCrypto::IknpOtExtReceiver();
            osuCrypto::DefaultBaseOT base;
            std::vector<std::array<osuCrypto::block, 2>> base_msg(recver->baseOtCount());
            cp::sync_wait(base.send(base_msg, prg, chl));

            recver->setBaseOts(base_msg);
        }
    }
}

template<typename T>
void eq1<T>::offline(int n, uint32_t ell2) {
    if (role == Role::Sender) {
        prg.get(epsilon.data(), n);

        for (int i = 0; i < n; ++i) {
            epsilon[i] %= ell2;
            bits[i*ell2 + epsilon[i]] = 1;
        }
    }
    vose(bits, epsilon.data(), share, n, ell2);
}

template<typename T>
void eq1<T>::online(int n, uint32_t ell2, T* data, osuCrypto::BitVector &output) {
    coproto::span<T> ws(w.data(), n), tmps(tmp.data(), n);
    if (role == Role::Sender) {
        for (int i = 0; i < n; ++i) {
            w[i] = (data[i] + epsilon[i]) % ell2;
        }
        cp::sync_wait(chl.recv(tmps));
        cp::sync_wait(chl.send(ws));
    } else {
        for (int i = 0; i < n; ++i) {
            ws[i] = (epsilon[i] + ell2 - data[i]) % ell2;
        }
        cp::sync_wait(chl.send(ws));
        cp::sync_wait(chl.recv(tmps));
    }

    for (int i = 0; i < n; ++i) {
        output[i] = share[i*ell2 + ((w[i] + tmp[i]) % ell2)];
    }
}

template<typename T>
void eq1<T>::init(int n, uint32_t ell2) {
    epsilon.assign(n, 0);
    w.assign(n, 0);
    tmp.assign(n, 0);
    bits.reset(n * ell2);
}

template<typename T>
void eq1<T>::vose(osuCrypto::BitVector &bits, uint8_t *delta, osuCrypto::BitVector &output, int nums, int size) {
    std::vector<std::vector<uint64_t>> seed(nums, std::vector<uint64_t>(size, 0));

    NCN1OT<uint64_t> ot(role, nums, size, sender, recver, chl);

    osuCrypto::BitVector u(nums * size);
    output.resize(nums * size);

    if (role == Role::Sender) {
        std::vector<uint64_t*> seed_ptrs(nums);
        for (int i = 0; i < nums; ++i) {
            seed_ptrs[i] = seed[i].data();
        }
        ot.send(seed_ptrs.data());

        for (int i = 0; i < nums; ++i) {
            int tmp_i = i * size;
            osuCrypto::BitVector resu(size), resv(size);
            osuCrypto::BitVector temp0(size);

            for (int j = 0; j < size; ++j) {
                osuCrypto::block seed_one = osuCrypto::toBlock(0, seed[i][j]);

                // Reuse member prg instead of constructing a new PRNG per iteration.
                prg.SetSeed(seed_one);
                prg.get<uint8_t>(temp0.data(), temp0.sizeBytes());

                resu ^= temp0;

                osuCrypto::BitVector temp1(size);
                for (int ti = 0; ti < size; ++ti) {
                    temp1[ti] = temp0[(j + ti) % size];
                }
                resv ^= temp1;
            }

            for (int ti = 0; ti < size; ++ti) {
                output[tmp_i + ti] = resu[ti];
                u[tmp_i + ti] = resv[ti];
            }
        }
        u ^= bits;

        cp::sync_wait(chl.send(u));
        cp::sync_wait(chl.flush());
    } else {
        std::vector<uint64_t*> seed_ptrs(nums);
        for (int i = 0; i < nums; ++i) {
            seed_ptrs[i] = seed[i].data();
        }
        ot.recv(seed_ptrs.data(), delta);

        for (int i = 0; i < nums; ++i) {
            int tmp_i = i * size;
            osuCrypto::BitVector res(size);
            osuCrypto::BitVector temp0(size);

            for (int j = 0; j < size; ++j) {
                if (j == delta[i]) continue;
                osuCrypto::block seed_one = osuCrypto::toBlock(0, seed[i][j]);
                prg.SetSeed(seed_one);
                prg.get<uint8_t>(temp0.data(), temp0.sizeBytes());

                res ^= temp0;

                int tmp_jk = (j + size - delta[i]) % size;

                osuCrypto::BitVector temp1(size);
                for (int ti = 0; ti < size; ++ti) {
                    temp1[ti] = temp0[(tmp_jk + ti) % size];
                }

                res ^= temp1;
            }
            for (int ti = 0; ti < size; ++ti) {
                output[tmp_i + ti] = res[ti];
            }
        }
        u.reset(nums*size);

        cp::sync_wait(chl.recv(u));
        cp::sync_wait(chl.flush());

        for (int i = 0; i < nums; ++i) {
            int tmp_i = i*size;
            for (int j = 0; j < size; ++j) {
                output[tmp_i + j] = u[tmp_i + ((j + size - delta[i])%size)] ^ output[tmp_i + j];
            }
        }
    }
}

template<typename T>
eq2<T>::eq2(Role role, std::string ip, osuCrypto::IknpOtExtSender *sender, osuCrypto::IknpOtExtReceiver *recver) {
    this->role = role;
    chl = coproto::asioConnect(ip, role == Role::Sender);
    prg.SetSeed(osuCrypto::sysRandomSeed());
    initOT(sender, recver);
}

template<typename T>
void eq2<T>::initOT(osuCrypto::IknpOtExtSender *sender_, osuCrypto::IknpOtExtReceiver *recver_) {
    if (role == Role::Sender) {
        if (sender_ != nullptr) {
            this->sender = sender_;
        } else {
            this->sender = new osuCrypto::IknpOtExtSender();

            osuCrypto::DefaultBaseOT base;
            osuCrypto::BitVector bv(sender->baseOtCount());
            std::vector<osuCrypto::block> base_msg(sender->baseOtCount());

            bv.randomize(prg);
            cp::sync_wait(base.receive(bv, base_msg, prg, chl));
            sender->setBaseOts(base_msg, bv);
        }
    } else {
        if (recver_) {
            this->recver = recver_;
        } else {
            this->recver = new osuCrypto::IknpOtExtReceiver();
            osuCrypto::DefaultBaseOT base;
            std::vector<std::array<osuCrypto::block, 2>> base_msg(recver->baseOtCount());
            cp::sync_wait(base.send(base_msg, prg, chl));

            recver->setBaseOts(base_msg);
        }
    }
}

template<typename T>
void eq2<T>::run(std::vector<osuCrypto::block> &data, osuCrypto::BitVector &output, uint32_t ell, int numThreads, bool random) {
    (void)numThreads;

    if (random)
        prg.get(data.data(), data.size());
    int n = static_cast<int>(data.size());
    int size = n * ell;
    int p = getmod(ell);

    osuCrypto::BitVector bits(size);

    t.assign(size, 0);
    r.reset(size);
    std::vector<T> share(size, 0);
    std::vector<T> addshare(n, 0);

    eq1<T> eq11(role, chl, sender, recver);
    eq11.init(n, p);

    osuCrypto::Timer timer;
    osuCrypto::Timer::timeUnit offline_start, offline_end, online_start, online_end;

    std::cout << "=====offline start=====" << std::endl;
    osuCrypto::u64 com_off_begin = chl.bytesReceived() + chl.bytesSent();
    offline_start = timer.setTimePoint("offline_start");

    convert_offline(p, size);
    eq11.offline(n, p);

    offline_end = timer.setTimePoint("offline_end");
    osuCrypto::u64 com_off_end = chl.bytesReceived() + chl.bytesSent();

    std::cout << "=====offline end=====" << std::endl;
    cp::sync_wait(sync(chl, role));

    std::cout << "=====online start=====" << std::endl;
    osuCrypto::u64 com_begin = chl.bytesReceived() + chl.bytesSent();
    online_start = timer.setTimePoint("online_start");

    std::memcpy(bits.data(), data.data(), n * sizeof(osuCrypto::block));

    convert_online(p, size, bits, share.data());

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < ell; ++j) {
            addshare[i] = (addshare[i] + share[i*ell + j]) % p;
        }
    }
    if (role == Role::Receiver) {
        for (int i = 0; i < n; ++i) {
            addshare[i] = (p - addshare[i]) % p;
        }
    }

    eq11.online(n, p, addshare.data(), output);
    std::cout << "=====online end=====" << std::endl;
    osuCrypto::u64 com_end = chl.bytesReceived() + chl.bytesSent();
    online_end = timer.setTimePoint("online_end");
    std::cout << timer << "\n";
    std::cout << "Offline Comm: " << (com_off_end - com_off_begin) / 1024.0 / 1024 << " MB\n";
    std::cout << "Online Comm: " << (com_end - com_begin) / 1024.0 / 1024 << " MB\n";

    (void)offline_start;
    (void)offline_end;
    (void)online_start;
    (void)online_end;
}

template<typename T>
void eq2<T>::convert_offline(T p, int size) {
    prg.get(r.data(), r.sizeBytes());
    if (role == Role::Sender) {
        std::vector<T> pad(2 * size);
        coproto::span<T> pat(pad.data(), 2 * size);
        prg.get(t.data(), size);
        for (int i = 0; i < size; ++i) {
            t[i] %= p;
        }
        std::vector<std::array<osuCrypto::block, 2>> data(size);
        cp::sync_wait(sender->send(data, prg, chl));

        for (int64_t i = 0; i < size; ++i) {
            T m0, m1;
            std::memcpy(&m0, data[i][0].data(), sizeof(T));
            std::memcpy(&m1, data[i][1].data(), sizeof(T));
            pad[2*i] = m0 ^ ((t[i] - r[i] + p) % p);
            pad[2*i+1] = m1 ^ ((t[i] - 1 + r[i] + p) % p);
        }

        cp::sync_wait(chl.send(pat));
    } else {
        std::vector<osuCrypto::block> data(size);
        cp::sync_wait(recver->receive(r, data, prg, chl));

        std::vector<T> pad(2 * size);
        coproto::span<T> pat(pad.data(), 2 * size);
        cp::sync_wait(chl.recv(pat));

        for (int i = 0; i < size; ++i) {
            T m;
            std::memcpy(&m, data[i].data(), sizeof(T));
            t[i] = (p - (m ^ pad[2*i + r[i]])) % p;
        }
    }
}

template<typename T>
void eq2<T>::convert_online(T p, int size, osuCrypto::BitVector bits, T* output) {
    osuCrypto::BitVector w(size), tmp(size);
    const T pp = p * 2;
    (void)pp;

    for (int i = 0; i < size; ++i) {
        w[i] = bits[i] ^ r[i];
    }

    if (role == Role::Sender) {
        cp::sync_wait(chl.send(w));
        cp::sync_wait(chl.recv(tmp));

        for (int i = 0; i < size; ++i) {
            w[i] = w[i] ^ tmp[i];
            output[i] = ((T)w[i] + t[i] - ((2 * w[i] * t[i]) % p) + p) % p;
        }
    } else {
        cp::sync_wait(chl.recv(tmp));
        cp::sync_wait(chl.send(w));

        for (int i = 0; i < size; ++i) {
            output[i] = (t[i] - ((2 * (w[i] ^ tmp[i]) * t[i]) % p) + p) % p;
        }
    }
}
