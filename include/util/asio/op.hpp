// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Infrastructure for easily defining composed operations. Here be footguns.
//
// TODO:
// - Create OpPtr so Op doesn't have to know anything about the Handler type
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
//   2. require that yield is used with op()
//
// - EBO for Continuation::Data members
// - standardese
// - Hunter

#ifndef UTIL_ASIO_OP_HPP
#define UTIL_ASIO_OP_HPP

#include <util/asio/associatedlogger.hpp>
#include <util/asio/handler_hooks.hpp>
#include <util/asio/handlerptr.hpp>
#include <util/index_sequence.hpp>

#include <boost/scope_exit.hpp>

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace util { namespace asio {

inline namespace v2 {

template <class Task>
class Op {
    // A non-movable, non-copyable reference to an operation in flight. Op objects are passed as the
    // sole parameter to Task objects. Op objects provide the means to perform three actions:
    //   1. Create copyable continuation handlers which will reinvoke the Task.
    //   2. Complete the Task.
    //   3. Access the Logger object associated with the operation.

public:
    template <class Task_, class ArgTuple, size_t... NMinusOne>
    friend void startOpImpl(ArgTuple&&, util::index_sequence<NMinusOne...>&&);
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
        return {std::move(data), std::tie(results...), cont};
    }

    template <class... Args>
    void complete (Args&&... args) {
        // Deallocate the task and invoke its completion handler.

        // `args` may refer to data owned by the Op's task, but `invoke` will delete the task.
        // Therefore, we need to force a move/copy with std::decay to make sure the args are on the
        // stack. std::decay_copy, where you at? :(
        data->invoke(std::decay_t<Args>{std::forward<Args>(args)}...);
    }

    log::Logger& log () const {
        return ::util::asio::getAssociatedLogger(data->handler());
    }

private:
    using Handler = typename Task::HandlerType;
    using TaskPtr = HandlerPtr<Task, Handler>;
    std::shared_ptr<TaskPtr> data;
    bool cont;  // Outside data to make uses_allocator propagation easier
    explicit Op(std::shared_ptr<TaskPtr>&& d, bool c = true)
        : data(std::move(d)), cont(c)
    {
        (**data)(*this);
    }
};

template <class Task>
template <class... Results>
class Op<Task>::Continuation {
public:
    friend class Op<Task>;

    template <class... Args>
    void operator()(Args&&... args) {
        // Invoke the task as a continuation of its previous invocation. You will likely never need
        // to call this function directly, but rather you'll just let Asio's event loop invoke it.
        results = std::forward_as_tuple(args...);
        Op{std::move(data)};
    }

private:
    using TaskPtr = typename Op<Task>::TaskPtr;
    std::shared_ptr<TaskPtr> data;
    std::tuple<Results&...> results;
    bool cont;
    Continuation(std::shared_ptr<TaskPtr>&& p, std::tuple<Results&...>&& r, bool c)
        : data(std::move(p))
        , results(std::move(r))
        , cont(c)
    {}

    // ===================================================================================
    // Handler hooks

    friend log::Logger& getAssociatedLogger(const Continuation& self) {
        return ::util::asio::getAssociatedLogger(self.data->handler());
    }

    friend void* asio_handler_allocate(size_t size, Continuation* self) {
        return handler_hooks::allocate(size, self->data->handler());
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, Continuation* self) {
        handler_hooks::deallocate(pointer, size, self->data->handler());
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, Continuation* self) {
        handler_hooks::invoke(std::forward<Function>(f), self->data->handler());
    }

    friend bool asio_handler_is_continuation(Continuation* self) {
        // Composed operations naturally require their own continuation logic.
        return self->cont;
    }
};

// =======================================================================================
// Inline implementation

namespace _ {

template <class Task, class Handler>
struct TaskWrapper: Task {
    using HandlerType = Handler;
    using Task::operator();
    template <class... Args>
    TaskWrapper(Args&&... args): Task(std::forward<Args>(args)...) {}
};

template <class Task, class Handler, class = void>
struct ChooseTaskType_ {
    using Type = TaskWrapper<Task, Handler>;
};

template <class...> using ToVoid = void;

template <class Task, class Handler>
struct ChooseTaskType_<Task, Handler, ToVoid<typename Task::HandlerType>> {
    static_assert(std::is_same<typename Task::HandlerType, Handler>::value,
            "Attempted to start Op with incompatible handler type");
    using Type = Task;
};

template <class Task, class Handler>
using ChooseTaskType = typename ChooseTaskType_<Task, Handler>::Type;
// If Task has a member typedef HandlerType, this alias evaluates to Task, after static_asserting
// HandlerType equals Handler. Otherwise, this alias evaluates to a TaskWrapper, which has the
// necessary member typedef. This allows Op<Task> to take a single template parameter rather than
// a redundant two, which makes compiler errors far, far, far, far more legible.

}  // _

template <class Task, class ArgTuple, size_t... NMinusOne>
void startOpImpl(ArgTuple&& t, util::index_sequence<NMinusOne...>&&) {
    constexpr auto handlerIndex = std::tuple_size<std::decay_t<ArgTuple>>::value - 1;
    auto&& handler = std::get<handlerIndex>(std::forward<ArgTuple>(t));
    using Handler = std::decay_t<decltype(handler)>;

    static_assert(!std::is_same<Task, Handler>::value, "this is a terrible idea");

    using ActualTask = _::ChooseTaskType<Task, Handler>;
    using Op = Op<ActualTask>;
    using TaskPtr = typename Op::TaskPtr;

    auto cont = handler_hooks::is_continuation(handler);

    auto p = std::make_shared<TaskPtr>(
            std::forward<decltype(handler)>(handler),
            std::get<NMinusOne>(std::forward<ArgTuple>(t))...);

    Op{std::move(p), cont};
}

template <class Task, class... Args>
void startOp(Args&&... args) {
    static_assert(sizeof...(Args) > 0, "Asynchronous operations need at least one argument");
    using Indices = util::make_index_sequence_t<sizeof...(Args) - 1>;
    startOpImpl<Task>(std::forward_as_tuple(std::forward<Args>(args)...), Indices{});
}

template <class DeducedTask, class DeducedHandler>
void startSimpleOp(DeducedTask&& task, DeducedHandler&& handler) {
    using Task = std::decay_t<DeducedTask>;
    startOp<Task>(std::forward<DeducedTask>(task), std::forward<DeducedHandler>(handler));
}

}}}  // util::asio::v2

#endif
