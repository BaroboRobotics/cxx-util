// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Infrastructure for easily defining composed operations. Here be footguns.
//
// TODO:
// - Move cont initialization and Task construction into Data ctor.
// - Pass handler as last argument to Task ctor if it wants it. Use forward_as_tuple.
//
// - Implement a fork-join pool of some sort. Thoughts: difficult to fork safely inside a task,
//   because the forked task may need to run on the same execution context, but that is only
//   accessible if you have a copy of the upcall handler, which is released by op.complete().
//   Making op.complete() asynchronous is hacky and annoying: maybe allocate a op::completion struct
//   to store the results of the op and dispatch it when the last fork is done? Might be easier to
//   implement the fork-join infrastructure *outside* the task, as this would cover the timed
//   operation use case described below.
//
// - Generalize timer/signal_set terminator technique, perhaps using the fork-join pool
//
// - Allow Op<Task, Handler = Task::HandlerType>
//   Maybe derive from OpSignature to define both handler_type and signature? This would obviate
//   the need for async_completion. However, it is still difficult for a generic initiating function
//   to deduce the type of handler unless it is passed the Task as a class template. Maybe a
//   boost::mpl::bind-based solution could be used.
//
// - Rewrite addAssociatedLogger to associate references to loggers, rather than add a copy.
//
// - Figure out some way of enforcing op.complete() not called on first invocation. throw if !cont?
//   Maybe a bitmask of cont, first?
//
// - Require Task::operator() to return some sort of failsafe object to:
//   1. require that op.complete() is called at least once
//   2. require that yield is used with op.next()
//
// - EBO for Continuation::Data members
// - standardese
// - Hunter
//
//
// DONE:
// - Replace op.next() with op()
// - Make op() take parameters which dictate where the results of the next suboperation will be
//   stored. This solves two problems:
//   1. The programmer can decide to use a single error_code object to implement exception-like
//      short circuit control flow (the `if (!ec) reenter (op)` idiom), but add a second error_code
//      object if manual error-checking is required in a part of the code.
//   2. The parameters of Task::operator() don't have to be able to handle multiple types. No more
//      variant bullshit.
//

#ifndef UTIL_ASIO_OP_HPP
#define UTIL_ASIO_OP_HPP

#include <util/asio/associatedlogger.hpp>
#include <util/asio/handler_hooks.hpp>
#include <util/index_sequence.hpp>

#include <boost/scope_exit.hpp>

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace util { namespace asio {

inline namespace v2 {

template <class Task, class Handler>
class Op {
    // A non-movable, non-copyable reference to an operation in flight. Op objects are passed as the
    // sole parameter to Task objects. Op objects provide the means to perform three actions:
    //   1. Create copyable continuation handlers which will reinvoke the Task.
    //   2. Complete the Task.
    //   3. Access the Logger object associated with the operation.

public:
    template <class DeducedTask, class DeducedHandler>
    friend void startSimpleOp(DeducedTask&&, DeducedHandler&&);
    // Create and launch an Op. This is the only way to create an Op.

    template <class Task_, class ArgTuple, size_t... NMinusOneIndices>
    friend void startOpImpl(ArgTuple&&, util::index_sequence<NMinusOneIndices...>&&);
    // Create and launch an Op. This is the only other way to create an Op. :)

    Op() = delete;
    Op(Op&&) = delete;
    Op(const Op&) = delete;
    Op& operator=(Op&&) = delete;
    Op& operator=(const Op&) = delete;
    ~Op() = default;

    template <class... Results>
    class Continuation;

    template <class... Results>
    Continuation<Results...> operator()(Results&... results) {
        // Return a continuation handler which will store the results of the suboperation it is
        // passed to in the lvalues referred to by `results`. You can pass `std::ignore` as one of
        // the arguments to this function if you would like to discard some of the results.
        return {std::move(data), std::tie(results...)};
    }

    template <class... Args>
    void complete (Args&&... args) {
        // Deallocate the task and invoke its completion handler.

        // `args` may refer to data owned by the Op's task, but `complete` will delete the task.
        // Therefore, we need to force a move/copy with std::decay to make sure the args are on the
        // stack. std::decay_copy, where you at? :(
        (*data).get_deleter()((*data).release())(std::decay_t<Args>{std::forward<Args>(args)}...);
    }

    log::Logger& log () const {
        return ::util::asio::getAssociatedLogger((*data)->handler);
    }

private:
    struct Data;
    struct Deleter;
    using DataPtr = std::unique_ptr<Data, Deleter>;
    std::shared_ptr<DataPtr> data;
    Op(std::shared_ptr<DataPtr>&& data): data(std::move(data)) {}
};

template <class Task, class Handler>
template <class... Results>
class Op<Task, Handler>::Continuation {
public:
    friend class Op<Task, Handler>;

    template <class... Args>
    void operator()(Args&&... args) {
        // Invoke the task as a continuation of its previous invocation. You will likely never need
        // to call this function directly, but rather you'll just let Asio's event loop invoke it.
        results = std::forward_as_tuple(args...);
        Op op{std::move(data)};
        auto& d = **op.data;
        d.cont = true;
        d.task(op);
    }

private:
    using DataPtr = typename Op<Task, Handler>::DataPtr;
    std::shared_ptr<DataPtr> data;
    std::tuple<Results&...> results;
    Continuation(std::shared_ptr<DataPtr>&& p, std::tuple<Results&...>&& r)
        : data(std::move(p))
        , results(std::move(r)) {}

    // ===================================================================================
    // Handler hooks

    friend log::Logger& getAssociatedLogger(const Continuation& self) {
        return ::util::asio::getAssociatedLogger((*self.data)->handler);
    }

    friend void* asio_handler_allocate(size_t size, Continuation* self) {
        return handler_hooks::allocate(size, (*self->data)->handler);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, Continuation* self) {
        handler_hooks::deallocate(pointer, size, (*self->data)->handler);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, Continuation* self) {
        handler_hooks::invoke(std::forward<Function>(f), (*self->data)->handler);
    }

    friend bool asio_handler_is_continuation(Continuation* self) {
        // Composed operations naturally require their own continuation logic.
        return (*self->data)->cont;
    }
};

// =======================================================================================
// Inline implementation

template <class Task, class ArgTuple, size_t... NMinusOneIndices>
void startOpImpl(ArgTuple&& t, util::index_sequence<NMinusOneIndices...>&&) {
    constexpr auto handlerIndex = std::tuple_size<std::decay_t<ArgTuple>>::value - 1;
    auto&& handler = std::get<handlerIndex>(std::forward<ArgTuple>(t));
    using Handler = std::decay_t<decltype(handler)>;

    static_assert(!std::is_same<Task, Handler>::value, "this is a terrible idea");
    static_assert(std::is_nothrow_move_constructible<Handler>::value,
            "Handler move constructor must be noexcept");

    using Op = Op<Task, Handler>;
    using Data = typename Op::Data;
    using DataPtr = typename Op::DataPtr;

    auto vp = handler_hooks::allocate(sizeof(Data), handler);
    Data* p = nullptr;

    BOOST_SCOPE_EXIT_ALL(&) {
        if (!p) {
            // An exception was thrown constructing Data. `handler` is still safe to use because it
            // comes after `task` in Data's initialization. The Handler type itself is nothrow move
            // constructible, because we assert so above, and the only object that comes after it is
            // a bool, which obviously cannot throw. Therefore, any exception must have originated
            // from `task`'s constructor, in which case our `handler` cannot have been moved-from,
            // or possibly from `handler`'s copy constructor, in which case we obviously still have
            // a copy.
            handler_hooks::deallocate(vp, sizeof(Data), handler);
        }
    };

    p = new (vp) Data{
            std::forward<decltype(handler)>(handler),
            Task{std::get<NMinusOneIndices>(std::forward<ArgTuple>(t))...},
            handler_hooks::is_continuation(handler)};

    Op op{std::make_shared<DataPtr>(p)};
    (*op.data)->task(op);
}

template <class Task, class... Args,
        class Indices = util::make_index_sequence_t<sizeof...(Args) - 1>>
void startOp(Args&&... args) {
    static_assert(sizeof...(Args) > 0, "Asynchronous operations need at least one argument");
    startOpImpl<Task>(std::forward_as_tuple(std::forward<Args>(args)...), Indices{});
}

template <class DeducedTask, class DeducedHandler>
void startSimpleOp(DeducedTask&& task, DeducedHandler&& handler) {
    using Task = std::decay_t<DeducedTask>;
    using Handler = std::decay_t<DeducedHandler>;
    static_assert(!std::is_same<Task, Handler>::value, "this is a terrible idea");
    static_assert(std::is_nothrow_move_constructible<Handler>::value,
            "Handler move constructor must be noexcept");

    using Op = Op<Task, Handler>;
    using Data = typename Op::Data;
    using DataPtr = typename Op::DataPtr;

    auto vp = handler_hooks::allocate(sizeof(Data), handler);
    Data* p = nullptr;

    BOOST_SCOPE_EXIT_ALL(&) {
        if (!p) {
            // An exception was thrown constructing Data. `handler` is still safe to use because it
            // comes after `task` in Data's initialization. The Handler type itself is nothrow move
            // constructible, because we assert so above, and the only object that comes after it is
            // a bool, which obviously cannot throw. Therefore, any exception must have originated
            // from `task`'s constructor, in which case our `handler` cannot have been moved-from,
            // or possibly from `handler`'s copy constructor, in which case we obviously still have
            // a copy.
            handler_hooks::deallocate(vp, sizeof(Data), handler);
        }
    };

    p = new (vp) Data{
            std::forward<DeducedHandler>(handler),
            std::forward<DeducedTask>(task),
            handler_hooks::is_continuation(handler)};

    Op op{std::make_shared<DataPtr>(p)};
    (*op.data)->task(op);
}

template <class Task, class Handler>
struct Op<Task, Handler>::Data {
    Handler handler;
    Task task;
    bool cont;
};

template <class Task, class Handler>
struct Op<Task, Handler>::Deleter {
    Handler operator()(Data* p) const noexcept {
        static_assert(std::is_nothrow_move_constructible<Handler>::value,
                "Handler move constructor must be noexcept");
        auto h = std::move(p->handler);
        p->~Data();
        handler_hooks::deallocate(p, sizeof(Data), h);
        return h;
    }
};

}}}  // util::asio::v2

#endif
