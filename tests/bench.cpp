#include "../benches/bench.hpp"

// Exercise benchmark helpers in-process so the test is fast and deterministic
int main() { return hpc::bench::self_test() ? 0 : 1; }
