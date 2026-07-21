#include "utils.h"
#include "cryptoTools/Common/Timer.h"
#include <cmath>

bool isPrime(int num)
{
    if(num == 1)
        return 0;
    if(num == 2 || num == 3)
        return 1;
    if(num % 6 != 1 && num % 6 != 5)
        return 0;
    int tmp = sqrt(num);
    for(int i=5; i<=tmp; i+=6)
        if(num % i == 0 || num % (i+2) == 0)
            return 0;
    return 1;
}

int getmod(int num) {
    if (num%2 == 0)
        num++;
    while (!isPrime(num)) {
        num += 2;
        if (num % 3 == 0) {
            num += 2;
        }
    }
    return num;
}

coproto::task<> sync(coproto::Socket& chl, Role role)
    {
        MC_BEGIN(coproto::task<>,&chl, role,
            dummy = osuCrypto::u8{},
            timer = std::unique_ptr<osuCrypto::Timer>{new osuCrypto::Timer},
            start = osuCrypto::Timer::timeUnit{},
            mid = osuCrypto::Timer::timeUnit{},
            end = osuCrypto::Timer::timeUnit{},
            ms = osuCrypto::u64{},
            rrt = std::chrono::system_clock::duration{}
        );

        if (role == Role::Receiver)
        {

            MC_AWAIT(chl.recv(dummy));

            start = timer->setTimePoint("");

            MC_AWAIT(chl.send(dummy));
            MC_AWAIT(chl.recv(dummy));

            mid = timer->setTimePoint("");

            MC_AWAIT(chl.send(std::move(dummy)));

            rrt = mid - start;
            ms = std::chrono::duration_cast<std::chrono::milliseconds>(rrt).count();

            // wait for half the round trip time to start both parties at the same time.
            if (ms > 4)
                std::this_thread::sleep_for(rrt / 2);

        }
        else
        {
            MC_AWAIT(chl.send(dummy));
            MC_AWAIT(chl.recv(dummy));
            MC_AWAIT(chl.send(dummy));
            MC_AWAIT(chl.recv(dummy));
        }

        MC_END();
    }
