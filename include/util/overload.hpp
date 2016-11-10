// Copyright (c) 2015-2016 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0
// http://vittorioromeo.info | vittorio.romeo@outlook.com
//
// Barobo-styled by Harris Hancock.

#include <utility>

namespace util { namespace _ {

template <class...>
struct OverloadSet;

template <class F>
struct OverloadSet<F>: F {
    using CallType = F;
    using CallType::operator();

    OverloadSet(F&& f) noexcept: F(std::forward<decltype(f)>(f)) {}
};

template <class F, class... Fs>
struct OverloadSet<F, Fs...>: F, OverloadSet<Fs...>::CallType {
    using BaseType = typename OverloadSet<Fs...>::CallType;

    using FType = F;
    using CallType = OverloadSet;

    OverloadSet(F&& f, Fs&&... fs) noexcept: FType(std::forward<F>(f)), BaseType(std::forward<Fs>(fs)...) {}

    using FType::operator();
    using BaseType::operator();
};

}  // _

template <class... Fs>
auto overload(Fs&&... fs) noexcept {
    // Merge all given function objects `fs` into a single function object with many overloads.
    return _::OverloadSet<Fs...>{std::forward<Fs>(fs)...};
}

}  // util