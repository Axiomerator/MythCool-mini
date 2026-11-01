// SPDX-License-Identifier: MPL-2.0
#include "mini.h"
#include <iostream>
#include <psapi.h>
using namespace mini;
int main(int argc, char **argv) {
    auto i = import_official();
    std::string mode = argc > 1 ? argv[1] : "all";
    if (mode == "base")
        i.slots.clear();
    else if (mode == "cpu")
        std::erase_if(i.slots, [](const Slot &s) { return s.metric != Metric::CpuMax; });
    else if (mode == "gpu")
        std::erase_if(i.slots, [](const Slot &s) {
            return s.metric != Metric::GpuLoad && s.metric != Metric::GpuPower && s.metric != Metric::GpuTemp;
        });
    Sensors sensors;
    sensors.initialize(i.slots);
    for (int n = 0; n < 3; ++n) {
        sensors.apply(i.slots, sensors.sample());
        Sleep(1000);
    }
    PROCESS_MEMORY_COUNTERS_EX p{};
    p.cb = sizeof(p);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&p, sizeof(p));
    std::cout << Json{{"backend", mode},
                      {"working_set", p.WorkingSetSize},
                      {"private_commit", p.PrivateUsage}}
                     .dump()
              << '\n';
}
