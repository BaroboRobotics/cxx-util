// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Specialize associated_logger for op_continuation.

#ifndef COMPOSED_OP_LOGGER_HPP
#define COMPOSED_OP_LOGGER_HPP

#include <composed/associated_logger.hpp>
#include <composed/op.hpp>
#include <composed/stdlib.hpp>

namespace composed {

template <class Task, class... Results, class L>
struct associated_logger<op_continuation<Task, Results...>, L> {
    // Specialization to allow op continuations to forward get_associated_logger calls to their
    // completion handlers.

    using type = associated_logger_t<typename op_continuation<Task, Results...>::task_ptr::handler_type, L>;
    static type get(const op_continuation<Task, Results...>& t, const L& l = L{}) {
        return get_associated_logger(t.get_task().handler(), l);
    }
};

template <class Task, class... Results, class L>
struct associated_logger<op_continuation<Task, Results...>, L,
        void_t<typename op_continuation<Task, Results...>::task_ptr::element_type::logger_type>> {
    // Specialization to allow tasks wrapped in op continuations to manually specify their
    // associated loggers.

    using type = typename op_continuation<Task, Results...>::task_ptr::element_type::logger_type;
    static type get(const op_continuation<Task, Results...>& t, const L& = L{}) {
        return t.get_task()->get_logger();
    }
};

}  // composed

#endif
