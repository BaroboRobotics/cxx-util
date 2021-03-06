// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Infrastructure for easily defining composed operations. Here be footguns.
//
// TODO:
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

#ifndef COMPOSED_OP_HPP
#define COMPOSED_OP_HPP

#include <composed/stdlib.hpp>

#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>

#include <boost/assert.hpp>
#include <boost/asio/async_result.hpp>

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace composed {

template <class Task, class... Results>
class op_continuation;

template <class Task>
class op {
    // A non-movable, non-copyable reference to an operation in flight. Op objects are passed as the
    // sole parameter to Task objects. Op objects provide the means to perform three actions:
    //   1. Create copyable continuation handlers which will reinvoke the Task.
    //   2. Complete the Task.

    template <class Task1, class... Results>
    friend class op_continuation;

public:
    template <class Task1, class Handler, class... Args>
    friend void start_op(Handler&&, Args&&...);
    // Create and launch an Op. This is the only other way to create an op.

    op() = delete;
    op(op&&) = delete;
    op(const op&) = delete;
    op& operator=(op&&) = delete;
    op& operator=(const op&) = delete;
    // This class is designed to be a handle that lives on the stack, then gets thrown away when no
    // longer needed. Consequently, it is non-movable, non-copyable.

    ~op() = default;

    template <class... Results>
    op_continuation<Task, Results...> operator()(Results&... results) {
        // Return a continuation handler which will store the results of the suboperation it is
        // passed to in the lvalues referred to by `results`. You can pass `std::ignore` as one of
        // the arguments to this function if you would like to discard some of the results.

        BOOST_ASSERT_MSG(task, "Attempt to continue task already continued/completed! Did you forget a yield macro, or already call op.complete()?");
        return {std::move(task), std::tie(results...), cont};
    }

    template <class... Args>
    void complete (Args&&... args) {
        // Deallocate the task and invoke its completion handler.

        BOOST_ASSERT_MSG(task, "Attempt to complete task already continued/completed! Did you forget a yield macro, or already call op.complete()?");
        // `args` may refer to data owned by the Op's task, but `invoke` will delete the task.
        // Therefore, we need to force a move/copy with std::decay to make sure the args are on the
        // stack.
        task.invoke(decay_copy(std::forward<Args>(args))...);
    }

private:
    using task_ptr = beast::handler_ptr<Task, typename Task::handler_type>;

    task_ptr task;
    bool cont;  // Lives outside task to make uses_allocator propagation easier

    op(task_ptr&& d, bool c): task(std::move(d)), cont(c) {
        (*task)(*this);
        // Invoke the task immediately.
        BOOST_ASSERT_MSG(!task, "Task not continued or completed! Did you forget to schedule the next continuation or call op.complete()?");
    }
};

template <class Task, class... Results>
class op_continuation {
public:
    friend class op<Task>;

    template <class... Args>
    void operator()(Args&&... args) {
        // Invoke the task as a continuation of its previous invocation. You will likely never need
        // to call this function directly, but rather you'll just let Asio's event loop invoke it.
        results = std::forward_as_tuple(args...);
        op<Task>{std::move(task), true};
    }

    using task_type = Task;
    using task_ptr = typename op<task_type>::task_ptr;
    const task_ptr& get_task() const { return *task; }
    // Provides access to the operation's task. Useful to add more Asio/Net.TS-style associated
    // object hooks without modifying this class.

private:
    task_ptr task;
    std::tuple<Results&...> results;
    bool cont;

    op_continuation(task_ptr&& t, std::tuple<Results&...>&& r, bool c)
        : task(std::move(t))
        , results(std::move(r))
        , cont(c)
    {}

    // ===================================================================================
    // Handler hooks

    friend void* asio_handler_allocate(size_t size, op_continuation* self) {
        return beast_asio_helpers::allocate(size, self->task.handler());
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, op_continuation* self) {
        beast_asio_helpers::deallocate(pointer, size, self->task.handler());
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, op_continuation* self) {
        beast_asio_helpers::invoke(std::forward<Function>(f), self->task.handler());
    }

    friend bool asio_handler_is_continuation(op_continuation* self) {
        // Composed operations naturally require their own continuation logic.
        return self->cont;
    }
};

template <class Task, class... Args>
auto async_run(Args&&... args);
// Generic initiating function for any Task type which can successfully undergo the following
// transformation:
//   1. Task must be a class template with bound arguments.
//   2. All of Task's arguments must be type arguments.
//   3. If any argument is of the form void(T0, T1, ...), it will be replaced with the result of
//      boost::asio::handler_type<Token, void(T0, T1, ...)>::type, where Token is the last type in
//      Args... after undergoing std::decay_t.
//
// The resulting type of this transformation, Task1, will be initiated via
// start_op<Task1>(handler, args1...), where handler is the result of constructing
// Task1::handler_type from the passed Token object, and args1... are all the arguments preceding
// the token.
//
// Example:
//   template <class T, class U, class Handler = void(error_code, int)>
//   struct my_op {
//       using handler_type = Handler;
//       my_op(handler_type&);
//       void operator(composed::op<my_op>&);
//   };
//
//   struct my_handler { ... };
//
// async_run<my_op<int, double>>(my_handler{}) would transform
//    my_op<int, double, void(error_code, int)>
// to my_op<int, double, my_handler>
// then call start_op<my_op<int, double, my_handler>>(my_handler{}).
//
// This allows the operation writer to conveniently specify the result signature of an operation
// as close to the operation's data structure as possible.



// =======================================================================================
// Inline implementation

template <class Task, class Handler, class... Args>
void start_op(Handler&& handler, Args&&... args) {
    static_assert(std::is_same<typename Task::handler_type, Handler>::value,
            "Attempt to start op with incompatible handler");
    static_assert(!std::is_same<Task, Handler>::value, "this is a terrible idea");

    auto cont = beast_asio_helpers::is_continuation(handler);

    auto p = beast::make_handler_ptr<Task>(
            std::forward<Handler>(handler), std::forward<Args>(args)...);

    op<Task>{std::move(p), cont};
}

namespace _ {

template <class Token, class T>
struct rewrite_type {
    // Default rewrite rule is the identity metafunction.
    using type = T;
};

template <class Token, class... Ts>
struct rewrite_type<Token, void(Ts...)> {
    // If T is a function signature returning void, rewrite it with handler_type<Token, ...>.
    using type = typename boost::asio::handler_type<Token, void(Ts...)>::type;
};

template <template <class...> class TaskTpl, class Token, class From, class... To>
struct transform_task_impl;
// Requires From to be a std::tuple<Froms...>. Recursively rewrites the head of Froms... and appends
// it to To....

template <template <class...> class TaskTpl, class Token, class... To>
struct transform_task_impl<TaskTpl, Token, std::tuple<>, To...> {
    // Base case, return the new Task type with its rewritten type arguments.
    using type = TaskTpl<To...>;
};

template <template <class...> class TaskTpl, class Token, class From0, class... From, class... To>
struct transform_task_impl<TaskTpl, Token, std::tuple<From0, From...>, To...>
        : transform_task_impl<TaskTpl, Token, std::tuple<From...>,
                To..., typename rewrite_type<Token, From0>::type> {};

template <class Task, class Token>
struct transform_task;
// Requires that Task be a class template with all type arguments. Transform all function signature
// type arguments of the form void(...) by using them as the Signature argument in
// handler_type<Token, Signature>::type.
//
// Returns the Task type with rewritten types in the ::type member typedef.

template <template <class...> class TaskTpl, class... Ts, class Token>
struct transform_task<TaskTpl<Ts...>, Token> {
    using type = typename transform_task_impl<TaskTpl, Token, std::tuple<Ts...>>::type;
};

}  // _

template <class Task, class ArgTuple, size_t... Indices>
auto async_run_impl(ArgTuple&& t, index_sequence<Indices...>) {
    constexpr auto token_idx = std::tuple_size<std::decay_t<ArgTuple>>::value - 1;
    auto&& token = std::get<token_idx>(std::forward<ArgTuple>(t));

    using token_type = std::decay_t<decltype(token)>;
    using task_type = typename _::transform_task<Task, token_type>::type;
    using handler_type = typename task_type::handler_type;

    auto handler = handler_type{std::forward<decltype(token)>(token)};
    auto result = boost::asio::async_result<handler_type>{handler};

    start_op<task_type>(std::move(handler), std::get<Indices>(std::forward<ArgTuple>(t))...);

    return result.get();
}

template <class Task, class... Args>
auto async_run(Args&&... args) {
    static_assert(sizeof...(Args) > 0, "Asynchronous operations need at least one argument");
    using indices = make_index_sequence<sizeof...(Args) - 1>;
    async_run_impl<Task>(std::forward_as_tuple(std::forward<Args>(args)...), indices{});
}

}  // composed

#endif
