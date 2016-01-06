#include "util/callback.hpp"

#include <cassert>

struct AddTwo {
    static int exec (int i) { return i + 2; };
};

int main () {
    int x;
    util::Callback<int(int)> sig;

    auto addOne = [] (int i) { return i + 1; };
    using Lambda = decltype(addOne);

    sig = BIND_MEM_CB(&Lambda::operator(), &addOne);
    x = sig(1);
    assert(2 == x);

    sig = BIND_FREE_CB(&AddTwo::exec);
    x = sig(1);
    assert(3 == x);

    /* TODO: add some more cases, comment this stuff */
}
