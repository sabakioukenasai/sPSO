#include <iostream>

#include <cryptoTools/Common/block.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/BufferingSocket.h>
#include <macoro/sync_wait.h>

#include <secure-join/Perm/AltModPerm.h>

using namespace std;
using namespace osuCrypto;
using namespace coproto;
using namespace secJoin;

int main() {
    u64 n = (1 << 10);
    u64 rowSize = 1;

    PRNG prng0(block(0,0));
    PRNG prng1(block(0,1));
    Perm pi(n, prng0);
    AltModPermGenSender genPermSender;
    AltModPermGenReceiver genPermRecver;

    // oc::Matrix<oc::block> aExp(len, oc::divCeil(rowSize, 16));

    auto [sChl, rChl] = LocalAsyncSocket::makePair();
    CorGenerator sOle, rOle;

    sOle.init(sChl.fork(), prng0, 0, 1, 1<<10, 1);
    rOle.init(rChl.fork(), prng1, 1, 1, 1<<10, 1);

    genPermSender.init(n, rowSize, sOle);
    genPermRecver.init(n, rowSize, rOle);

    PermCorSender sPerm;
    PermCorReceiver rPerm;

    auto r = sync_wait(when_all_ready(
        genPermSender.generate(pi, prng0, sChl, sPerm),
        genPermRecver.generate(prng1, rChl, rPerm),
        sOle.start(),
        rOle.start()
    ));

    std::get<0>(r).result();
    std::get<1>(r).result();

    return 0;
}