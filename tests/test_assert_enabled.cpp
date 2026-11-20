// Build-time and runtime proof that CTest does not skip assert checks in Release.
#ifdef NDEBUG
#error "CTest Release assertions are disabled (NDEBUG is defined)"
#endif
#include <cassert>
#include <iostream>
int main() {
    int observed = 0;
    assert(++observed == 1);
    if (observed != 1) return 2;
    std::cout << "Release test assertions are active\n";
    return 0;
}
