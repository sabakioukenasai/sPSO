#include <cassert>
#include <iostream>
#include <random>
#include <set>
#include <chrono>
#include <vector>
#include <stdexcept>
#include <thread>
#include <unordered_set>
// #include <span>

// libOTe
// #include <cryptoTools/Common/Aligned.h>
#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Common/block.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Common/macoro.h>
#include <coproto/Socket/AsioSocket.h>
#include <macoro/sync_wait.h>
#include <libOTe/Base/BaseOT.h>
#include <libOTe/Base/SimplestOT.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtSender.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h>


// our lib
#include "oprf.h"
#include "okvsbk.h"
#include "cuckoo3.h"
#include "galois128.h"
#include "utils.h"

#ifdef HAVE_OPENMP
#include <omp.h>
#endif

using namespace std;
using namespace osuCrypto;
using namespace coproto;
using namespace oprf;
using okvs::Galois128;

// generate set
void set_gen(uint64_t n, uint64_t start, vector<uint64_t>& keys, block seed);

// Fake implementation for generating base OT used in otE.
// as it is not the major overhaed
void fakeBase(uint64_t n, uint64_t s, uint64_t threads, PRNG& prng, SilentOtExtReceiver& recver, SilentOtExtSender& sender);

// time formation
static inline double fmt_time(auto start, auto end) {
    using namespace std::chrono;
    return duration_cast<milliseconds>(end - start).count(); // ms
}

// comm formation
static inline double fmt_comm(uint64_t bytes) {
    return bytes / (1024.0 * 1024.0); // MB
}

// ---------------------------- test main --------------------------
int main() {
    // parameter
    uint64_t n          = (1 << 16);        // set size
    uint64_t inter      = (1 << 15);         // intersection size
    // prepare seeds
    PRNG commPrng(block(123456));
    block setGenSeed = commPrng.get();
    block cuckooSeed = commPrng.get();
    uint64_t cuckooDummy = commPrng.get();
    block oprfSeed = commPrng.get();
    block okvsSeed_w = commPrng.get();
    block okvsSeed_pos = commPrng.get();

    // std::cout << "=================== Test start ===================" << std::endl;

    // generate set
    vector<uint64_t> sender_set(n), recver_set(n);
    set_gen(n, (uint64_t)1, sender_set, setGenSeed);
    set_gen(n, n - inter, recver_set, setGenSeed);

    // std::cout << "================= set generated ==================" << std::endl;
    // std::cout << "first 5 items of sender set  : ";
    // for (int i = 0; i < 5; ++i) cout << sender_set[i] << ", ";
    // cout << endl;
    // std::cout << "first 5 items of receiver set: ";
    // for (int i = 0; i < 5; ++i) cout << recver_set[i] << ", ";
    // cout << endl;

    // set parameter
    double cuckoo_exp = CUCKOO_EXPANSION;   // default cuckoo table expansion rate = 1.22
    double okvs_exp   = 1.15;               // defaule okvs structure expansion rate = 1.15
    const uint64_t w  = 152;                // safe okvs row length for items < 2^22 [LTT+25]
    uint64_t cuco_sz  = std::ceil(cuckoo_exp * n);  // cuckoo table size

    // std::cout << "=============== parameters prepared ==============" << std::endl;

    // fake base ot implementation
    PRNG otPrng(sysRandomSeed());
    SilentOtExtSender sender;
    SilentOtExtReceiver recver;
    block delta = otPrng.get();

    // sender.mMultType = MultType::QuasiCyclic;
    // recver.mMultType = MultType::QuasiCyclic;

    // F_2, single thread, no needed as we use SimplestOT instead
    // fakeBase(cuco_sz, 2, 1, otPrng, recver, sender);

    std::cout << "================ Online test parameter ================\nset size="<<n<<", cuckoo expansion rate="<<cuckoo_exp<<", okvs expansion rate="<<okvs_exp<<", okvs w="<<w<< "\n\n";

    // ------------------------ sender thread ------------------------------
    std::thread th_sender([&]() {
        try {
            // std::cout << "====================== sender ======================" << std::endl;
            auto chl = asioConnect("localhost:1213", 0);

            double total_time = 0;
            double total_comm = 0;

            // 1. Cuckoo hashing
            auto t0 = std::chrono::high_resolution_clock::now();
            uint64_t c0 = chl.bytesSent() + chl.bytesReceived();

            CuckooHash3 cuckoo;
            cuckoo.init(n, cuco_sz, cuckooSeed, cuckooDummy);
            cuckoo.insert(sender_set);
            const std::vector<block>& cuco_tab = cuckoo.get_table();

            auto t1 = std::chrono::high_resolution_clock::now();
            uint64_t c1 = chl.bytesSent() + chl.bytesReceived();
            cout << "[sender] cuckoo: " << fmt_time(t0, t1)
                 << " ms, comm = " << fmt_comm(c1 - c0) << " MB" << endl;
            total_time += fmt_time(t0, t1);
            total_comm += fmt_comm(c1 - c0);

            // 2. OPRF (receiver side)
            t0 = std::chrono::high_resolution_clock::now(); 
            c0 = chl.bytesSent() + chl.bytesReceived();
            uint64_t oprf_n = cuco_sz;
            uint64_t oprf_w = w;
            double oprf_e = okvs_exp;

            OprfRecver oprfRecv;
            vector<block> prf_vals(oprf_n);
            oprfRecv.init(oprf_n, oprf_w, oprf_e, std::make_shared<coproto::Socket>(chl), oprfSeed);
            oprfRecv.run(cuco_tab, prf_vals);
            
            t1 = std::chrono::high_resolution_clock::now(); 
            c1 = chl.bytesSent() + chl.bytesReceived();
            cout << "[sender] oprf: " << fmt_time(t0, t1)
                 << " ms, comm = " << fmt_comm(c1 - c0) << " MB" << endl;
            total_time += fmt_time(t0, t1);
            total_comm += fmt_comm(c1 - c0);

            // 3. OKVS decoding
            t0 = std::chrono::high_resolution_clock::now(); 
            c0 = chl.bytesSent() + chl.bytesReceived();

            uint64_t okvs_n = 3*n;
            uint64_t okvs_w = w;
            double okvs_e = okvs_exp;
            uint64_t okvs_m = std::ceil(okvs_e * okvs_n);
            OKVSBK okvs(okvs_n, okvs_w, okvs_e, okvsSeed_w, okvsSeed_pos);
            vector<block> recver_p(okvs_m);
            sync_wait(chl.recv(recver_p));

            // compute r'
            vector<block> rr(cuco_sz);
            okvs.DecodeOtherP(cuco_tab, rr, recver_p);

            t1 = std::chrono::high_resolution_clock::now(); 
            c1 = chl.bytesSent() + chl.bytesReceived();
            cout << "[sender] okvs phase: " << fmt_time(t0, t1)
                 << " ms, comm = " << fmt_comm(c1 - c0) << " MB" << endl;
            total_time += fmt_time(t0, t1);
            total_comm += fmt_comm(c1 - c0);

            // invoke pseqt

            // simulate ot execution
            // 4. OT send
            t0 = std::chrono::high_resolution_clock::now(); 
            c0 = chl.bytesSent() + chl.bytesReceived();

            // base OT
            sender.configure(cuco_sz, 2, 1);    
            auto count = sender.silentBaseOtCount();
            std::vector<std::array<block, 2>> msg2_buf(count);
            std::span<std::array<block, 2>> msg2_base(msg2_buf);

            SimplestOT baseOtSender;
            auto base_ot_task = baseOtSender.send(msg2_base, otPrng, chl);
            sync_wait(base_ot_task);
            sender.setSilentBaseOts(msg2_base);

            std::vector<std::array<block, 2>> msg2(cuco_sz);
            auto ot_task = sender.silentSend(msg2, otPrng, chl);
            // auto ot_task = sender.silentSendInplace(delta, cuco_sz, otPrng, chl);
            sync_wait(ot_task);

            t1 = std::chrono::high_resolution_clock::now(); 
            c1 = chl.bytesSent() + chl.bytesReceived();
            cout << "[sender] OT send: " << fmt_time(t0, t1)
                 << " ms, comm = " << fmt_comm(c1 - c0) << " MB" << endl;
            total_time += fmt_time(t0, t1);
            total_comm += fmt_comm(c1 - c0);

            sync_wait(chl.flush());
            sync_wait(chl.close());

            cout << "\n\n[sender] total running time: " << total_time
                 << " ms, total comm = " << total_comm << " MB" << endl;

        } catch (std::exception& ex) {
            cerr << "Sender exception: " << ex.what() << endl;
        }
    });

    // ----------------------- receiver thread -----------------------------
    std::thread th_recver([&]() {
        try {
            // local socket on "localhost:1213"
            coproto::AsioSocket chl = asioConnect("localhost:1213", 1);

            double total_time = 0;
            double total_comm = 0;

            // 1. simple hash
            auto t0 = std::chrono::high_resolution_clock::now();
            uint64_t c0 = chl.bytesSent() + chl.bytesReceived();
            // simple hashing: generate key set {(y_i||hash(1)),(y_i||hash(2)),(y_i||hash(3)) | i=1..n}
            vector<block> okvs_key(3 * n);
            vector<block> okvs_value(3 * n);
            vector<array<uint64_t, 3>> pos(n);
            #ifdef HAVE_OPENMP
            #pragma omp parallel for schedule(static)
            #endif
            for (uint64_t i = 0; i < n; ++i) {
                pos[i] = get3hash(recver_set[i], cuco_sz, cuckooSeed);
                okvs_key[i * 3 + 0] = block(recver_set[i], blake3_hash64((uint64_t)0x1, cuckooSeed));
                okvs_key[i * 3 + 1] = block(recver_set[i], blake3_hash64((uint64_t)0x2, cuckooSeed));
                okvs_key[i * 3 + 2] = block(recver_set[i], blake3_hash64((uint64_t)0x3, cuckooSeed));
            }
            
            auto t1 = std::chrono::high_resolution_clock::now();
            uint64_t c1 = chl.bytesSent() + chl.bytesReceived();
            cout << "[receiver] simple hash: " << fmt_time(t0, t1)
                 << " ms, comm = " << fmt_comm(c1 - c0) << " MB" << endl;
            total_time += fmt_time(t0, t1);
            total_comm += fmt_comm(c1 - c0);

            // 2. OPRF run + eval (run as oprf sender)
            t0 = std::chrono::high_resolution_clock::now(); 
            c0 = chl.bytesSent() + chl.bytesReceived();
            uint64_t oprf_n = cuco_sz;
            uint64_t oprf_w = w;
            double oprf_e = okvs_exp;

            OprfSender oprfSend;
            oprfSend.init(oprf_n, oprf_w, oprf_e, std::make_shared<coproto::Socket>(chl), oprfSeed);
            oprfSend.run();

            // okvs preprocessing 
            // okvs_value = { F_k(y_i || hash(j)) | i=1..n, j=1..3 }
            oprfSend.eval(okvs_key, okvs_value);
            
            t1 = std::chrono::high_resolution_clock::now(); 
            c1 = chl.bytesSent() + chl.bytesReceived();
            cout << "[receiver] oprf (run+eval): " << fmt_time(t0, t1)
                 << " ms, comm = " << fmt_comm(c1 - c0) << " MB" << endl;
            total_time += fmt_time(t0, t1);
            total_comm += fmt_comm(c1 - c0);

            

            // 3. OKVS encode
            t0 = std::chrono::high_resolution_clock::now(); 
            c0 = chl.bytesSent() + chl.bytesReceived();

            // preprocessing
            vector<block> r(cuco_sz);
            PRNG prng_once(block(233));
            #ifdef HAVE_OPENMP
            #pragma omp parallel for schedule(static)
            #endif
            for (auto &x : r) x = prng_once.get<block>();

            // simple hash: okvs_value = {F_k(y_i || hash(j)) ^ r_{h_j(y_i)} | i=1..n, j=1..3}
            #ifdef HAVE_OPENMP
            #pragma omp parallel for schedule(static)
            #endif
            for (uint64_t i = 0; i < n; ++i) {
                // array<uint64_t, 3> pos = get3hash(recver_set[i], cuco_sz, cuckooSeed);
                okvs_value[i * 3 + 0] ^= r[pos[i][0]];
                okvs_value[i * 3 + 1] ^= r[pos[i][1]];
                okvs_value[i * 3 + 2] ^= r[pos[i][2]];
            }

            uint64_t okvs_n = 3*n;
            uint64_t okvs_w = w;
            double okvs_e = okvs_exp;
            OKVSBK okvs(okvs_n, okvs_w, okvs_e, okvsSeed_w, okvsSeed_pos);
            okvs.Encode(okvs_key, okvs_value);

            vector<block> p = okvs.getP();
            sync_wait(chl.send(p));
            
            t1 = std::chrono::high_resolution_clock::now(); 
            c1 = chl.bytesSent() + chl.bytesReceived();
            cout << "[receiver] okvs encode: " << fmt_time(t0, t1)
                 << " ms, comm = " << fmt_comm(c1 - c0) << " MB" << endl;
            total_time += fmt_time(t0, t1);
            total_comm += fmt_comm(c1 - c0);

            // invoke pseqt

            // 4. OT receive
            t0 = std::chrono::high_resolution_clock::now(); 
            c0 = chl.bytesSent() + chl.bytesReceived();
            // simulate the execution of otE

            // Base OT
            recver.configure(cuco_sz, 2, 1);
            BitVector choices_base = recver.sampleBaseChoiceBits(commPrng);

            std::vector<block> msg_buf(choices_base.size());
            std::span<block> msg_base(msg_buf);
            SimplestOT baseOtRecver;
            auto base_ot_task = baseOtRecver.receive(choices_base, msg_base, otPrng, chl);
            sync_wait(base_ot_task);
            recver.setSilentBaseOts(msg_base);

            std::vector<block> msg(cuco_sz);
            BitVector choice(cuco_sz);
            auto ot_task = recver.silentReceive(choice, msg, otPrng, chl);
            // auto ot_task = recver.silentReceiveInplace(cuco_sz, otPrng, chl);
            sync_wait(ot_task);

            t1 = std::chrono::high_resolution_clock::now(); 
            c1 = chl.bytesSent() + chl.bytesReceived();
            cout << "[receiver] OT recv: " << fmt_time(t0, t1)
                 << " ms, comm = " << fmt_comm(c1 - c0) << " MB" << endl;
            total_time += fmt_time(t0, t1);
            total_comm += fmt_comm(c1 - c0);

            sync_wait(chl.flush());
            sync_wait(chl.close());

            cout << "\n\n[receiver] total running time: " << total_time
                 << " ms, total comm = " << total_comm << " MB" << endl;

        } catch (std::exception& ex) {
            cerr << "Receiver exception: " << ex.what() << endl;
        }
    });

    th_sender.join();
    th_recver.join();

    cout << "online work flow finished." << endl;
    return 0;
}

void set_gen(uint64_t n, uint64_t start, vector<uint64_t>& keys, block seed) {
    if (keys.size() != (size_t)n) {
        throw std::invalid_argument("keys size mismatch with n");
    }

    for (uint64_t i = 0; i < n; ++i) {
        keys[i] = blake3_hash64(i + start, seed);
    }
    
    return ;
}

void fakeBase(uint64_t n, uint64_t s, uint64_t threads, PRNG& prng, SilentOtExtReceiver& recver, SilentOtExtSender& sender) {
    sender.configure(n, s, threads);
    auto count = sender.silentBaseOtCount();
    std::vector<std::array<block, 2>> msg2(count);
    for (u64 i = 0; i < msg2.size(); ++i)
    {
        msg2[i][0] = prng.get();
        msg2[i][1] = prng.get();
    }
    sender.setSilentBaseOts(msg2);

    // fake base OTs.
    {
        recver.configure(n, s, threads);
        BitVector choices = recver.sampleBaseChoiceBits(prng);
        std::vector<block> msg(choices.size());
        for (u64 i = 0; i < msg.size(); ++i)
            msg[i] = msg2[i][choices[i]];
        recver.setSilentBaseOts(msg);
    }
}