#include "coproto/Socket/AsioSocket.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Common/CLP.h"
#include "cryptoTools/Common/Matrix.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "libOTe/NChooseOne/Kkrt/KkrtNcoOtReceiver.h"
#include "libOTe/NChooseOne/Kkrt/KkrtNcoOtSender.h"
#include "newcmp.h"
#include "neweq.h"
#include "utils.h"
#include <coproto/Common/macoro.h>
#include <iostream>

uint8_t Zp::p;

int main(int argc, char *argv[]) {
  osuCrypto::CLP cmd;
  cmd.parse(argc, argv);

  Role role;

  if (cmd.isSet("sender")) {
    role = Role::Sender;
  } else if (cmd.isSet("receiver")) {
    role = Role::Receiver;
  } else {
    role = Role::Sender;
  }

  auto ip = cmd.getOr<std::string>("ip", "localhost:1213");

  auto num = cmd.getOr<int>("n", 10000);
  auto ell = cmd.getOr("l", 128);

  auto iscmp = cmd.getOr<int>("c", 0);

  int sameNum = num / 2;
  if (iscmp == 0) {
    std::vector<osuCrypto::block> data(num, osuCrypto::toBlock(0, 0));
    for (int i = 0; i < sameNum; ++i) {
      data[i] = osuCrypto::toBlock(0, i);
    }
    for (int i = sameNum; i < num; ++i) {
      data[i] = osuCrypto::sysRandomSeed();
    }

    osuCrypto::BitVector output(num);
    eq2<uint8_t> e(role, ip, 0, 0);
    e.run(data, output, ell);
  } else if (iscmp == 1) {
    std::vector<osuCrypto::block> data(num, osuCrypto::toBlock(0, 10));
    if (role == Role::Receiver) {
      for (int i = 0; i < num; ++i) {
        data[i] = osuCrypto::toBlock(0, (10 + i) % 16);
      }
    } else {
      for (int i = 0; i < num; ++i) {
        data[i] = osuCrypto::toBlock(0, (11 - i) % 16);
      }
    }
    osuCrypto::BitVector output(num);
    cmp1<uint8_t> c(role, ip, num, ell);
    c.run(data, output, ell);
    for (auto it = output.begin(); it != output.end(); ++it) {
      std::cout << *it << std::endl;
    }
  }

  return 0;
}
