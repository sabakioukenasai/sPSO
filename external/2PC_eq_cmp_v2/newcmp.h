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
#include "libOTe/NChooseOne/Kkrt/KkrtNcoOtReceiver.h"
#include "libOTe/NChooseOne/Kkrt/KkrtNcoOtSender.h"
#include "libOTe/Vole/Silent/SilentVoleReceiver.h"
#include "libOTe/Vole/Silent/SilentVoleSender.h"
#include "n1not.h"
#include <cstdint>
#include <macoro/sync_wait.h>
#include <vector>
#include <array>
#include <random>
#include <numeric>
#include <algorithm>
#include <string>
#include <cstring>
#include "coeff.h"


template<typename T>
class cmp1{
private:
    osuCrypto::BitVector bits;
    std::vector<T> m;
    std::vector<T> s;
    std::vector<T> idx;
    std::vector<T> a;
    std::vector<T> c;
    std::vector<T> w;
    osuCrypto::BitVector r;
    std::vector<T> t;
    T p;

    Role role;
    coproto::AsioSocket chl;
    osuCrypto::IknpOtExtSender *sender;
    osuCrypto::IknpOtExtReceiver *recver;
    osuCrypto::KkrtNcoOtSender *nsender;
    osuCrypto::KkrtNcoOtReceiver *nrecver;
    osuCrypto::SilentVoleReceiver<Zp, Zp, MyCoeffCtx> *volereceiver;
    osuCrypto::SilentVoleSender<Zp, Zp, MyCoeffCtx> *volesender;
    osuCrypto::PRNG prg;
    int n, ell;

    void initOT();
    void convert_offline(T p, int size);
    void convert_online(T p, int size, osuCrypto::BitVector bits, T* output);
    void OLE(T* a1, T* c1);

    void permute(std::vector<T> pi);

    void oss_offline(std::vector<T> &pi, std::vector<T> &pi1);
    void oss_online(std::vector<T> &pi1, osuCrypto::BitVector &res);
    void pre_sum(int p);
    void get_index(int n, uint32_t ell, osuCrypto::BitVector &res);
public:
    cmp1(Role role, std::string ip, int n, int ell);
    void run(std::vector<osuCrypto::block> data, osuCrypto::BitVector &output, uint32_t ell, int numThreads = 1, bool random = false);
};

template<typename T>
cmp1<T>::cmp1(Role role, std::string ip, int n, int ell) {
    this->role = role;
    this->n = n;
    this->ell = ell;
    p = static_cast<T>(getmod(ell));
    Zp::p = p;
    chl = coproto::asioConnect(ip, role == Role::Sender);
    prg.SetSeed(osuCrypto::sysRandomSeed());
    initOT();
}

template<typename T>
void cmp1<T>::run(std::vector<osuCrypto::block> data, osuCrypto::BitVector &output, uint32_t ell, int numThreads, bool random) {
    (void)numThreads;

    if (random)
        prg.get(data.data(), data.size());
    n = static_cast<int>(data.size());

    int numsize = n * ell;
    int sumsize = n * (ell + 2);
    int allsize = n * (ell + 2 + ell);

    bits.reset(0);
    for (int i = 0; i < n; ++i) {
        osuCrypto::BitVector temp(reinterpret_cast<const osuCrypto::u8*>(data[i].data()), ell);
        for (int j = ell - 1; j >= 0; --j) {
            bits.pushBack(temp[j]);
        }
    }

    std::vector<T> pi(allsize), pi1(allsize);

    m.assign(numsize, 0);
    s.assign(sumsize, 0);
    a.assign(allsize, 0);
    c.assign(allsize, 0);
    w.assign(allsize, 0);
    idx.assign(n * (ell + 1), 0);
    r.reset(numsize);
    t.assign(numsize, 0);

    osuCrypto::Timer timer;
    osuCrypto::Timer::timeUnit offline_start, offline_end, online_start, online_end;

    std::cout << "=====offline start=====" << std::endl;
    osuCrypto::u64 com_off_begin = chl.bytesReceived() + chl.bytesSent();
    offline_start = timer.setTimePoint("offline_start");

    convert_offline(p, numsize);
    oss_offline(pi, pi1);

    offline_end = timer.setTimePoint("offline_end");
    osuCrypto::u64 com_off_end = chl.bytesReceived() + chl.bytesSent();

    std::cout << "=====offline end=====" << std::endl;
    cp::sync_wait(sync(chl, role));

    std::cout << "=====online start=====" << std::endl;
    osuCrypto::u64 com_begin = chl.bytesReceived() + chl.bytesSent();
    online_start = timer.setTimePoint("online_start");

    convert_online(p, numsize, bits, m.data());
    pre_sum(p);

    if (role == Role::Sender) {
        get_index(n, ell, output);
    }
    oss_online(pi1, output);

    online_end = timer.setTimePoint("online_end");

    std::cout << "=====online end=====" << std::endl;

    (void)offline_start;
    (void)offline_end;
    (void)online_start;
    (void)online_end;
    (void)com_off_begin;
    (void)com_off_end;
    (void)com_begin;
}

template<typename T>
void cmp1<T>::pre_sum(int p) {
    int ell2 = ell + 2;
    if (role == Role::Sender) {
        for (int i = 0; i < n; ++i) {
            int tmpi = i * ell2;
            int tmpii = i * ell;
            int pres = m[tmpii];
            s[tmpi] = (1 - pres + p) % p;
            for (int j = 1; j < ell; ++j) {
                s[tmpi + j] = (pres + 1 - m[tmpii+j] + p) % p;
                pres = (pres + m[tmpii+j]) % p;
            }
            s[tmpi + ell] = (pres + 1) % p;
            s[tmpi + ell + 1] = 1;
        }
    } else {
        for (int i = 0; i < n; ++i) {
            int tmpi = i * ell2;
            int tmpii = i * ell;
            auto pres = m[tmpii];
            s[tmpi] = (p - pres) % p;
            for (int j = 1; j < ell; ++j) {
                s[tmpi + j] = (pres + p - m[tmpii+j]) % p;
                pres = (pres + m[tmpii+j]) % p;
            }
            s[tmpi + ell] = (pres - 1 + p) % p;
            s[tmpi + ell + 1] = 1;
        }
    }
}

template<typename T>
void cmp1<T>::get_index(int n, uint32_t ell, osuCrypto::BitVector &res) {
    for (int i = 0; i < n; ++i) {
        int tmp_i = i * ell;
        int tmp_ii = i * (ell + 1);
        int mxii = tmp_ii + (ell + 1);

        uint8_t randomchoose = prg.get<uint8_t>() % 2;
        res[i] = randomchoose;
        for (int j = 0; j < static_cast<int>(ell); ++j) {
            if (bits[tmp_i + j] == res[i]) {
                idx[tmp_ii++] = j;
            }
        }
        while (tmp_ii < mxii) {
            idx[tmp_ii++] = ell + 1;
        }
    }
}

template<typename T>
void cmp1<T>::initOT() {
    bool maliciousSecure = false;
    uint64_t statSecParam = 40;
    uint64_t inputBitCount = static_cast<uint64_t>(log2(ell + 2 + ell)) + 1;
    if (role == Role::Sender) {
        this->sender = new osuCrypto::IknpOtExtSender();

        osuCrypto::DefaultBaseOT base;
        osuCrypto::BitVector bv(sender->baseOtCount());
        std::vector<osuCrypto::block> base_msg(sender->baseOtCount());

        bv.randomize(prg);
        cp::sync_wait(base.receive(bv, base_msg, prg, chl));
        sender->setBaseOts(base_msg, bv);

        this->volereceiver = new osuCrypto::SilentVoleReceiver<Zp, Zp, MyCoeffCtx>();
        volereceiver->mMultType = osuCrypto::DefaultMultType;
        volereceiver->configure(n * (ell + 1));

        nrecver = new osuCrypto::KkrtNcoOtReceiver;
        nrecver->configure(maliciousSecure, statSecParam, inputBitCount);
        cp::sync_wait(nrecver->genBaseOts(prg, chl));
    } else {
        this->recver = new osuCrypto::IknpOtExtReceiver();
        osuCrypto::DefaultBaseOT base;
        std::vector<std::array<osuCrypto::block, 2>> base_msg(recver->baseOtCount());
        cp::sync_wait(base.send(base_msg, prg, chl));

        recver->setBaseOts(base_msg);

        this->volesender = new osuCrypto::SilentVoleSender<Zp, Zp, MyCoeffCtx>();
        volesender->mMultType = osuCrypto::DefaultMultType;
        volesender->configure(n * (ell + 1));

        nsender = new osuCrypto::KkrtNcoOtSender();
        nsender->configure(maliciousSecure, statSecParam, inputBitCount);

        cp::sync_wait(nsender->genBaseOts(prg, chl));
    }
}


template<typename T>
void cmp1<T>::convert_offline(T p, int size) {
    r.randomize(prg);
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
void cmp1<T>::convert_online(T p, int size, osuCrypto::BitVector bits, T* output) {
    osuCrypto::BitVector w(size), tmp(size);

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

template<typename T>
void cmp1<T>::OLE(T* a1, T* c1) {
    int ellp = static_cast<int>(log2(ell)) + 1;

    int ell2 = ell + 1;
    int numOTs = n * ell2 * ellp;
    int numsize = n * ell2;
    prg.get<T>(a1, numsize);
    for (int i = 0; i < numsize; ++i) {
        a1[i] = a1[i] % p;
        if (role == Role::Sender && a1[i] == 0) {
            do {
                prg.get<T>(a1 + i, 1);
                a1[i] = a1[i] % p;
            } while (a1[i] == 0);
        }
    }
    if (role == Role::Sender) {
        std::vector<T> r(numOTs);
        prg.get<T>(r.data(), numOTs);
        for (int i = 0; i < numOTs; ++i) {
            r[i] = r[i] % p;
        }
        std::vector<std::array<osuCrypto::block, 2>> messages(numOTs);
        std::vector<T> pad(numOTs * 2, 0);
        sender->send(messages, prg, chl);

        for (int i = 0; i < numOTs; ++i) {
            int tmpi = i / ellp;
            T m;
            std::memcpy(&m, messages[i].data(), sizeof(T));
            pad[2*i] = m ^ r[i];
            pad[2*i+1] = m ^ static_cast<T>(((static_cast<uint16_t>(a1[tmpi]) * (1 << (i % ellp)) + r[i]) % p));
            c1[tmpi] = (c1[tmpi] + p - r[i]) % p;
        }
        coproto::span<T> pat(pad.data(), numOTs * 2);
        cp::sync_wait(chl.send(pat));
    } else {
        osuCrypto::BitVector choice;
        std::vector<osuCrypto::block> message(numOTs);
        std::vector<T> pad(numOTs * 2, 0);
        for (int i = 0; i < numsize; ++i) {
            choice.append(a1 + i, ellp);
        }
        recver->receive(choice, message, prg, chl);
        coproto::span<T> pat(pad.data(), numOTs * 2);
        cp::sync_wait(chl.recv(pat));

        for (int i = 0; i < numOTs; ++i) {
            int tmpi = i / ellp;
            T m;
            std::memcpy(&m, message[i].data(), sizeof(T));
            c1[tmpi] = (c1[tmpi] + (m ^ pad[2*i + choice[i]])) % p;
        }
    }
}

template<typename T>
void cmp1<T>::permute(std::vector<T> pi) {
    if (role == Role::Sender) {
        int inum = ell + 2 + ell;
        int sumsize = static_cast<int>(pi.size());
        osuCrypto::Matrix<T> messages(sumsize, ell + 2 + ell);
        std::vector<osuCrypto::block> message(sumsize);

        cp::sync_wait(nrecver->init(sumsize, prg, chl));

        for(int i = 0; i < sumsize; ++i) {
            osuCrypto::block tmp = osuCrypto::toBlock(0, pi[i]);
            nrecver->encode(i, &tmp, message.data() + i, sizeof(T));
        }

        cp::sync_wait(nrecver->sendCorrection(chl, sumsize));
        cp::sync_wait(chl.recv(messages));

        for (int i = 0; i < sumsize; ++i) {
            int tmpi = (i / inum);
            tmpi *= inum;
            T m;
            std::memcpy(&m, message[i].data(), sizeof(T));
            w[i] = (c[tmpi + pi[i]] + (m ^ messages(i, pi[i]))) % p;
        }

    } else {
        int inum = ell + 2 + ell;
        int sumsize = n * inum;
        prg.get<T>(w.data(), sumsize);
        for (int i = 0; i < sumsize; ++i) {
            w[i] = w[i] % p;
        }
        osuCrypto::Matrix<T> messages(sumsize, inum);

        cp::sync_wait(nsender->init(sumsize, prg, chl));
        cp::sync_wait(nsender->recvCorrection(chl, sumsize));

        osuCrypto::block tmpm;
        for (int i = 0; i < sumsize; ++i) {
            int tmpi = i / inum;
            tmpi *= inum;
            for (int j = 0; j < inum; ++j) {
                T tmpv = (c[tmpi + j] + p - w[i]) % p;
                nsender->encode(i, &j, &tmpm, sizeof(T));
                T encoded;
                std::memcpy(&encoded, tmpm.data(), sizeof(T));
                messages(i, j) = encoded ^ tmpv;
            }
        }

        cp::sync_wait(chl.send(messages));
    }
}


template<typename T>
void cmp1<T>::oss_offline(std::vector<T> &pi, std::vector<T> &pi1) {
    int ell2 = ell + 1;
    int ellh = ell + 1;
    int numVole = n * ellh;
    int single = ell2 + ellh;
    std::vector<T> a1(n * ell2, 0);
    std::vector<T> c1(n * ell2, 0);
    if (role == Role::Sender) {
        pi.resize(n * single);
        pi1.resize(n * single);
        // Create RNG once outside the loop — constructing mt19937 per iteration is expensive.
        std::random_device rd;
        std::mt19937 gen(rd());
        for (int i = 0; i < n; ++i) {
            int tmpi = i * single;
            int tmpii = tmpi + single;
            std::iota(pi.begin() + tmpi, pi.begin() + tmpii, 0);
            std::shuffle(pi.begin() + tmpi, pi.begin() + tmpii, gen);
            for (int j = 0; j < single; ++j) {
                pi1[tmpi + pi[tmpi + j]] = j;
            }
        }
    }

    OLE(a1.data(), c1.data());

    if (role == Role::Sender) {
        osuCrypto::AlignedUnVector<Zp> a2(numVole), c2(numVole);
        cp::sync_wait(volereceiver->silentReceive(a2, c2, prg, chl));

        std::vector<T> ap(numVole);
        prg.get<T>(ap.data(), numVole);
        for (int i = 0; i < numVole; ++i) {
            ap[i] = ap[i] % p;
            if (ap[i] == 0) {
                do {
                    prg.get<T>(ap.data() + i, 1);
                    ap[i] = ap[i] % p;
                } while (ap[i] == 0);
            }
            a2[i].value = (p - ap[i] + a2[i].value) % p;
        }
        cp::sync_wait(chl.send(a2));

        for (int i = 0; i < n; ++i) {
            std::memcpy(a.data() + i * single, a1.data() + i * (ell + 1), ell2 * sizeof(T));
            std::memcpy(c.data() + i * single, c1.data() + i * (ell + 1), ell2 * sizeof(T));
            std::memcpy(a.data() + i * single + ell2, &ap[i * ellh], ellh * sizeof(T));
            std::memcpy(c.data() + i * single + ell2, &c2[i * ellh], ellh * sizeof(T));
        }
    } else {
        osuCrypto::AlignedUnVector<Zp> ap(numVole), c2(numVole);
        T a2_v = prg.get<T>();
        while (a2_v % p == 0) {
            a2_v = prg.get<T>();
        }
        Zp a2{a2_v % p};

        cp::sync_wait(volesender->silentSend(a2, c2, prg, chl));
        cp::sync_wait(chl.recv(ap));

        for (int i = 0; i < n; ++i) {
            int tmpi = i * single;
            int tmpi1 = tmpi + ell2;
            std::memcpy(a.data() + tmpi, a1.data() + i * (ell + 1), ell2 * sizeof(T));
            std::memcpy(c.data() + tmpi, c1.data() + i * (ell + 1), ell2 * sizeof(T));

            int tmpj = i * ellh;
            for (int j = 0; j < ellh; ++j) {
                T val = (c2[tmpj + j].value + ap[tmpj + j].value * a2.value) % p;
                c[tmpi1 + j] = (p - val) % p;
                a[tmpi1 + j] = a2.value;
            }
        }
    }

    permute(pi);
}

template<typename T>
void cmp1<T>::oss_online(std::vector<T> &pi1, osuCrypto::BitVector &res) {
    int ell2 = ell + 2;
    int ellh = ell + 1;
    int single = ell + 1 + ellh;
    int sumsize = n * ell2, halfsize = n * ellh;
    std::vector<T> d(halfsize, 0);
    std::vector<T> index(halfsize);
    coproto::span<T> dp(d.data(), halfsize), ip(index.data(), halfsize);

    if (role == Role::Sender) {
        std::vector<T> a2(n * single);
        std::vector<T> w2(n * single);
        std::vector<T> s2(n * (ell + 2));
        coproto::span<T> a2p(a2.data(), n * single);
        coproto::span<T> w2p(w2.data(), n * single);
        coproto::span<T> s2p(s2.data(), n * (ell + 2));
        cp::sync_wait(chl.recv(a2p));
        cp::sync_wait(chl.recv(w2p));
        cp::sync_wait(chl.recv(s2p));

        std::vector<T> Y(sumsize, 0);
        coproto::span<T> Yp(Y.data(), sumsize);
        cp::sync_wait(chl.recv(Yp));

        for (int i = 0; i < n; ++i) {
            int firstj = ell + 1 + ell;
            int tmpi = i * ell2;
            int tmphi = i * (ell + 1);
            int tmpsi = i * single;
            for (int j = 0; j < ell + 1; ++j) {
                if (idx[tmphi + j] != ell + 1) {
                    auto tmpsij = tmpsi + idx[tmphi + j];
                    auto tmpij = tmpi + idx[tmphi + j];

                    index[tmphi + j] = pi1[tmpsi + idx[tmphi + j]];
                    d[tmphi + j] = ((a[tmpsij] * (s[tmpij] + Y[tmpij])) % p + p - w[tmpsi + pi1[tmpsij]]) % p;
                } else {
                    firstj = std::min(firstj, j);

                    auto tmpsij = tmpsi + j - firstj + ell + 1;
                    auto tmpij = tmpi + ell2 - 1;

                    index[tmphi + j] = pi1[tmpsij];
                    d[tmphi + j] = ((a[tmpsij] * (s[tmpij] + Y[tmpij])) % p + p - w[tmpsi + pi1[tmpsij]]) % p;
                }
            }
        }
        cp::sync_wait(chl.send(dp));
        cp::sync_wait(chl.send(ip));
    } else {
        coproto::span<T> a2p(a.data(), n * single);
        coproto::span<T> w2p(w.data(), n * single);
        coproto::span<T> s2p(s.data(), n * (ell + 2));
        cp::sync_wait(chl.send(a2p));
        cp::sync_wait(chl.send(w2p));
        cp::sync_wait(chl.send(s2p));
        for (int i = 0; i < n; ++i) {
            int tmpi = i * ell2;
            int tmpa = i * single;
            for (int j = 0; j < ell + 2; ++j) {
                s[tmpi + j] = (s[tmpi + j] + a[tmpa + j]) % p;
            }
        }
        coproto::span<T> sp(s.data(), sumsize);
        cp::sync_wait(chl.send(sp));

        cp::sync_wait(chl.recv(dp));
        cp::sync_wait(chl.recv(ip));

        for (int i = 0; i < n; ++i) {
            int tmphi = i * (ell + 1);
            int tmpsi = i * single;
            for (int j = 0; j < ell + 1; ++j) {
                int tmpj = tmphi + j;
                d[tmpj] = (d[tmpj] + p - w[tmpsi + ip[tmpj]]) % p;
                if (d[tmpj] == 0) {
                    res[i] = 1;
                    break;
                }
            }
            res[i] ^= 1;
        }
    }
}
