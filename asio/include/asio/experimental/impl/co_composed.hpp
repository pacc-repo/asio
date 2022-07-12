//
// experimental/impl/co_composed.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2022 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_IMPL_EXPERIMENTAL_CO_COMPOSED_HPP
#define ASIO_IMPL_EXPERIMENTAL_CO_COMPOSED_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"
#include <new>
#include <tuple>
#include <variant>
#include "asio/associated_cancellation_slot.hpp"
#include "asio/associator.hpp"
#include "asio/async_result.hpp"
#include "asio/detail/recycling_allocator.hpp"
#include "asio/detail/throw_error.hpp"
#include "asio/detail/type_traits.hpp"
#include "asio/error.hpp"
#include "asio/experimental/co_returns.hpp"

#if defined(ASIO_HAS_STD_COROUTINE)
# include <coroutine>
#else // defined(ASIO_HAS_STD_COROUTINE)
# include <experimental/coroutine>
#endif // defined(ASIO_HAS_STD_COROUTINE)

#include "asio/detail/push_options.hpp"

namespace asio {
namespace experimental {
namespace detail {

#if defined(ASIO_HAS_STD_COROUTINE)
using std::coroutine_handle;
using std::suspend_always;
using std::suspend_never;
#else // defined(ASIO_HAS_STD_COROUTINE)
using std::experimental::coroutine_handle;
using std::experimental::suspend_always;
using std::experimental::suspend_never;
#endif // defined(ASIO_HAS_STD_COROUTINE)

using asio::detail::composed_io_executors;
using asio::detail::composed_work;
using asio::detail::get_composed_io_executor;
using asio::detail::make_composed_io_executors;
using asio::detail::recycling_allocator;
using asio::detail::throw_error;

template <typename Executors, typename Handler>
class co_composed_state;

template <typename Executors, typename Handler, typename Return>
class co_composed_handler_base;

template <typename Executors, typename Handler, typename Return>
class co_composed_promise;

struct co_composed_on_suspend
{
  void (*fn_)(void*) = nullptr;
  void* arg_ = nullptr;
};

template <typename... Ts>
struct co_composed_completion : std::tuple<Ts&&...>
{
  template <typename... Us>
  co_composed_completion(Us&&... u) noexcept
    : std::tuple<Ts&&...>(std::forward<Us>(u)...)
  {
  }
};

template <typename Executors, typename Handler>
class co_composed_state_cancellation
{
public:
  using cancellation_slot_type = cancellation_slot;

  cancellation_slot_type get_cancellation_slot() const noexcept
  {
    return cancellation_state_.slot();
  }

  cancellation_state get_cancellation_state() const noexcept
  {
    return cancellation_state_;
  }

  void reset_cancellation_state()
  {
    cancellation_state_ = cancellation_state(
        asio::get_associated_cancellation_slot(
          static_cast<co_composed_state<Executors, Handler>*>(
            this)->handler()));
  }

  template <typename Filter>
  void reset_cancellation_state(Filter filter)
  {
    cancellation_state_ = cancellation_state(
        asio::get_associated_cancellation_slot(
          static_cast<co_composed_state<Executors, Handler>*>(
            this)->handler()), filter, filter);
  }

  template <typename InFilter, typename OutFilter>
  void reset_cancellation_state(InFilter&& in_filter, OutFilter&& out_filter)
  {
    cancellation_state_ = cancellation_state(
        asio::get_associated_cancellation_slot(
          static_cast<co_composed_state<Executors, Handler>*>(this)->handler()),
        std::forward<InFilter>(in_filter), std::forward<OutFilter>(out_filter));
  }

  cancellation_type_t cancelled() const noexcept
  {
    return cancellation_state_.cancelled();
  }

  void clear_cancellation_slot() noexcept
  {
    cancellation_state_.slot().clear();
  }

  bool throw_if_cancelled() const noexcept
  {
    return throw_if_cancelled_;
  }

  void throw_if_cancelled(bool b) noexcept
  {
    throw_if_cancelled_ = b;
  }

  void check_for_cancellation()
  {
    if (throw_if_cancelled_ && !!cancelled())
      throw_error(asio::error::operation_aborted, "co_await");
  }

private:
  cancellation_state cancellation_state_;
  bool throw_if_cancelled_ = false;
};

template <typename Executors, typename Handler>
  requires std::same_as<
    typename associated_cancellation_slot<
      Handler, cancellation_slot
    >::asio_associated_cancellation_slot_is_unspecialised,
    void>
class co_composed_state_cancellation<Executors, Handler>
{
public:
  void reset_cancellation_state()
  {
  }

  template <typename Filter>
  void reset_cancellation_state(Filter)
  {
  }

  template <typename InFilter, typename OutFilter>
  void reset_cancellation_state(InFilter&&, OutFilter&&)
  {
  }

  cancellation_type_t cancelled() const noexcept
  {
    return cancellation_type::none;
  }

  void clear_cancellation_slot() noexcept
  {
  }

  bool throw_if_cancelled() const noexcept
  {
    return false;
  }

  void throw_if_cancelled(bool) noexcept
  {
  }

  void check_for_cancellation() noexcept
  {
  }
};

template <typename Executors, typename Handler>
class co_composed_state
  : public co_composed_state_cancellation<Executors, Handler>
{
public:
  template <typename H>
  co_composed_state(composed_io_executors<Executors>&& executors,
      H&& h, co_composed_on_suspend& on_suspend)
    : work_(std::move(executors)),
      handler_(std::forward<H>(h)),
      on_suspend_(&on_suspend)
  {
  }

  template <typename... Ts>
  [[nodiscard]] co_composed_completion<Ts...> complete(Ts&&... ts)
    requires requires { declval<Handler>()(std::forward<Ts>(ts)...); }
  {
    return co_composed_completion<Ts...>(std::forward<Ts>(ts)...);
  }

  const Handler& handler() const noexcept
  {
    return handler_;
  }

private:
  template <typename, typename, typename>
    friend class co_composed_handler_base;
  template <typename, typename, typename>
    friend class co_composed_promise;
  template <typename, typename, typename, typename>
    friend class co_composed_promise_return_overload;

  composed_work<Executors> work_;
  Handler handler_;
  co_composed_on_suspend* on_suspend_;
};

template <typename Executors, typename Handler, typename Return>
class co_composed_handler_cancellation
{
public:
  using cancellation_slot_type = cancellation_slot;

  cancellation_slot_type get_cancellation_slot() const noexcept
  {
    return static_cast<
      const co_composed_handler_base<Executors, Handler, Return>*>(
        this)->promise().state().get_cancellation_slot();
  }
};

template <typename Executors, typename Handler, typename Return>
  requires std::same_as<
    typename associated_cancellation_slot<
      Handler, cancellation_slot
    >::asio_associated_cancellation_slot_is_unspecialised,
    void>
class co_composed_handler_cancellation<Executors, Handler, Return>
{
};

template <typename Executors, typename Handler, typename Return>
class co_composed_handler_base :
  public co_composed_handler_cancellation<Executors, Handler, Return>
{
public:
  co_composed_handler_base(
      co_composed_promise<Executors, Handler, Return>& p) noexcept
    : p_(&p)
  {
  }

  co_composed_handler_base(co_composed_handler_base&& other) noexcept
    : p_(std::exchange(other.p_, nullptr))
  {
  }

  ~co_composed_handler_base()
  {
    if (p_) [[unlikely]]
      p_->destroy();
  }

  co_composed_promise<Executors, Handler, Return>& promise() const noexcept
  {
    return *p_;
  }

protected:
  void resume(void* result)
  {
    co_composed_on_suspend on_suspend{};
    std::exchange(p_, nullptr)->resume(p_, result, on_suspend);
    if (on_suspend.fn_)
      on_suspend.fn_(on_suspend.arg_);
  }

private:
  co_composed_promise<Executors, Handler, Return>* p_;
};

template <typename Executors, typename Handler,
    typename Return, typename Signature>
class co_composed_handler;

template <typename Executors, typename Handler,
    typename Return, typename R, typename... Ts>
class co_composed_handler<Executors, Handler, Return, R(Ts...)>
  : public co_composed_handler_base<Executors, Handler, Return>
{
public:
  using co_composed_handler_base<Executors,
    Handler, Return>::co_composed_handler_base;

  using result_type = std::tuple<typename decay<Ts>::type...>;

  template <typename... Args>
  void operator()(Args&&... args)
  {
    result_type result(std::forward<Args>(args)...);
    this->resume(&result);
  }

  static auto on_resume(void* result)
  {
    auto& t = *static_cast<result_type*>(result);
    if constexpr (sizeof...(Ts) == 0)
      return;
    else if constexpr (sizeof...(Ts) == 1)
      return std::move(std::get<0>(t));
    else
      return std::move(t);
  }
};

template <typename Executors, typename Handler,
    typename Return, typename R, typename... Ts>
class co_composed_handler<Executors, Handler,
    Return, R(asio::error_code, Ts...)>
  : public co_composed_handler_base<Executors, Handler, Return>
{
public:
  using co_composed_handler_base<Executors,
    Handler, Return>::co_composed_handler_base;

  using result_type = std::tuple<asio::error_code,
    std::tuple<typename decay<Ts>::type...>>;

  template <typename... Args>
  void operator()(const asio::error_code& ec, Args&&... args)
  {
    result_type result(ec, {std::forward<Args>(args)...});
    this->resume(&result);
  }

  static auto on_resume(void* result)
  {
    auto& [ec, t] = *static_cast<result_type*>(result);
    throw_error(ec);
    if constexpr (sizeof...(Ts) == 0)
      return;
    else if constexpr (sizeof...(Ts) == 1)
      return std::move(std::get<0>(t));
    else
      return std::move(t);
  }
};

template <typename Executors, typename Handler,
    typename Return, typename R, typename... Ts>
class co_composed_handler<Executors, Handler,
    Return, R(std::exception_ptr, Ts...)>
  : public co_composed_handler_base<Executors, Handler, Return>
{
public:
  using co_composed_handler_base<Executors,
    Handler, Return>::co_composed_handler_base;

  using result_type = std::tuple<std::exception_ptr,
    std::tuple<typename decay<Ts>::type...>>;

  template <typename... Args>
  void operator()(std::exception_ptr ex, Args&&... args)
  {
    result_type result(std::move(ex), {std::forward<Args>(args)...});
    this->resume(&result);
  }

  static auto on_resume(void* result)
  {
    auto& [ex, t] = *static_cast<result_type*>(result);
    if (ex)
      std::rethrow_exception(ex);
    if constexpr (sizeof...(Ts) == 0)
      return;
    else if constexpr (sizeof...(Ts) == 1)
      return std::move(std::get<0>(t));
    else
      return std::move(t);
  }
};

template <typename Executors, typename Handler, typename Return>
class co_composed_promise_return;

template <typename Executors, typename Handler>
class co_composed_promise_return<Executors, Handler, void>
{
public:
  void get_return_object() noexcept
  {
  }

  auto final_suspend() noexcept
  {
    return suspend_never();
  }

  void return_void() noexcept
  {
  }
};

template <typename Executors, typename Handler>
class co_composed_promise_return<Executors, Handler, co_returns<>>
{
public:
  co_returns<> get_return_object() noexcept
  {
    return {};
  }

  auto final_suspend() noexcept
  {
    return suspend_never();
  }

  void return_void() noexcept
  {
  }
};

template <typename Executors, typename Handler,
    typename Return, typename Signature>
class co_composed_promise_return_overload;

template <typename Executors, typename Handler,
    typename Return, typename R, typename... Args>
class co_composed_promise_return_overload<
    Executors, Handler, Return, R(Args...)>
{
public:
  using derived_type = co_composed_promise<Executors, Handler, Return>;
  using return_type = std::tuple<Args...>;

  void return_value(std::tuple<Args...>&& value)
  {
    derived_type& promise = *static_cast<derived_type*>(this);
    promise.return_value_ = std::move(value);
    promise.state().work_.reset();
    promise.state().on_suspend_->arg_ = this;
    promise.state().on_suspend_->fn_ =
      [](void* p)
      {
        auto& promise = *static_cast<derived_type*>(p);

        co_composed_handler_base<Executors, Handler, Return> handler(promise);

        auto invoker =
          [
            handler = std::move(promise.state().handler_),
            result = std::move(std::get<return_type>(promise.return_value_))
          ]
          () mutable
          {
            std::apply(std::move(handler), std::move(result));
          };

        co_composed_handler_base<Executors,
          Handler, Return>(std::move(handler));

        invoker();
      };
  }
};

template <typename Executors, typename Handler, typename... Signatures>
class co_composed_promise_return<Executors, Handler, co_returns<Signatures...>>
  : public co_composed_promise_return_overload<Executors,
      Handler, co_returns<Signatures...>, Signatures>...
{
public:
  co_returns<Signatures...> get_return_object() noexcept
  {
    return {};
  }

  auto final_suspend() noexcept
  {
    return suspend_always();
  }

  using co_composed_promise_return_overload<Executors, Handler,
    co_returns<Signatures...>, Signatures>::return_value...;

private:
  template <typename, typename, typename, typename>
    friend class co_composed_promise_return_overload;

  std::variant<std::monostate,
    typename co_composed_promise_return_overload<
      Executors, Handler, co_returns<Signatures...>,
        Signatures>::return_type...> return_value_;
};

template <typename Executors, typename Handler, typename Return>
class co_composed_promise
  : public co_composed_promise_return<Executors, Handler, Return>
{
public:
  template <typename... Args>
  void* operator new(std::size_t size,
      co_composed_state<Executors, Handler>& state, Args&&...)
  {
    block_allocator_type allocator(
      asio::get_associated_allocator(state.handler_,
        recycling_allocator<void>()));

    block* base_ptr = std::allocator_traits<block_allocator_type>::allocate(
        allocator, blocks(sizeof(allocator_type)) + blocks(size));

    new (static_cast<void*>(base_ptr)) allocator_type(std::move(allocator));

    return base_ptr + blocks(sizeof(allocator_type));
  }

  template <typename C, typename... Args>
  void* operator new(std::size_t size, C&&,
      co_composed_state<Executors, Handler>& state, Args&&...)
  {
    return co_composed_promise::operator new(size, state);
  }

  void operator delete(void* ptr, std::size_t size)
  {
    block* base_ptr = static_cast<block*>(ptr) - blocks(sizeof(allocator_type));

    allocator_type* allocator_ptr = std::launder(
        static_cast<allocator_type*>(static_cast<void*>(base_ptr)));

    block_allocator_type block_allocator(std::move(*allocator_ptr));
    allocator_ptr->~allocator_type();

    std::allocator_traits<block_allocator_type>::deallocate(block_allocator,
        base_ptr, blocks(sizeof(allocator_type)) + blocks(size));
  }

  template <typename... Args>
  co_composed_promise(co_composed_state<Executors, Handler>& state, Args&&...)
    : state_(state)
  {
  }

  template <typename C, typename... Args>
  co_composed_promise(C&&,
      co_composed_state<Executors, Handler>& state, Args&&...)
    : state_(state)
  {
  }

  void destroy() noexcept
  {
    coroutine_handle<co_composed_promise>::from_promise(*this).destroy();
  }

  void resume(co_composed_promise*& owner, void* result,
      co_composed_on_suspend& on_suspend)
  {
    state_.on_suspend_ = &on_suspend;
    state_.clear_cancellation_slot();
    owner_ = &owner;
    result_ = result;
    coroutine_handle<co_composed_promise>::from_promise(*this).resume();
  }

  co_composed_state<Executors, Handler>& state() noexcept
  {
    return state_;
  }

  auto initial_suspend() noexcept
  {
    return suspend_never();
  }

  void unhandled_exception()
  {
    if (owner_)
      *owner_ = this;
    throw;
  }

  template <asio::async_operation Op>
  auto await_transform(Op&& op)
  {
    class [[nodiscard]] awaitable
    {
    public:
      awaitable(Op&& op, co_composed_promise& promise)
        : op_(std::forward<Op>(op)),
          promise_(promise)
      {
      }

      constexpr bool await_ready() const noexcept
      {
        return false;
      }

      void await_suspend(coroutine_handle<co_composed_promise>)
      {
        promise_.state_.on_suspend_->arg_ = this;
        promise_.state_.on_suspend_->fn_ =
          [](void* p)
          {
            std::move(static_cast<awaitable*>(p)->op_)(
                co_composed_handler<Executors, Handler, Return,
                  asio::completion_signature_of_t<Op>>(
                    static_cast<awaitable*>(p)->promise_));
          };
      }

      auto await_resume()
      {
        return co_composed_handler<Executors, Handler, Return,
          asio::completion_signature_of_t<Op>>::on_resume(
            promise_.result_);
      }

    private:
      typename decay<Op>::type op_;
      co_composed_promise& promise_;
    };

    state_.check_for_cancellation();
    return awaitable{std::forward<Op>(op), *this};
  }

  template <typename... Ts>
  auto yield_value(co_composed_completion<Ts...>&& result)
  {
    class [[nodiscard]] awaitable
    {
    public:
      awaitable(co_composed_completion<Ts...>&& result,
          co_composed_promise& promise)
        : result_(std::move(result)),
          promise_(promise)
      {
      }

      constexpr bool await_ready() const noexcept
      {
        return false;
      }

      void await_suspend(coroutine_handle<co_composed_promise>)
      {
        promise_.state_.work_.reset();
        promise_.state_.on_suspend_->arg_ = this;
        promise_.state_.on_suspend_->fn_ =
          [](void* p)
          {
            awaitable& a = *static_cast<awaitable*>(p);

            co_composed_handler_base<Executors,
              Handler, Return> handler(a.promise_);

            auto invoker =
              [
                handler = std::move(a.promise_.state_.handler_),
                result = std::tuple<typename decay<Ts>::type...>(
                    std::move(a.result_))
              ]
              () mutable
              {
                std::apply(std::move(handler), std::move(result));
              };

            co_composed_handler_base<Executors,
              Handler, Return>(std::move(handler));

            invoker();
          };
      }

      void await_resume() noexcept
      {
      }

    private:
      co_composed_completion<Ts...> result_;
      co_composed_promise& promise_;
    };

    return awaitable{std::move(result), *this};
  }

private:
  using allocator_type =
    asio::associated_allocator_t<
      Handler, recycling_allocator<void>>;

  union block
  {
    std::max_align_t max_align;
    alignas(allocator_type) char pad[alignof(allocator_type)];
  };

  using block_allocator_type =
    typename std::allocator_traits<allocator_type>
      ::template rebind_alloc<block>;

  static constexpr std::size_t blocks(std::size_t size)
  {
    return (size + sizeof(block) - 1) / sizeof(block);
  }

  co_composed_state<Executors, Handler>& state_;
  co_composed_promise** owner_ = nullptr;
  void* result_ = nullptr;
};

template <typename Executors>
class initiate_co_composed
{
public:
  using executor_type = typename composed_io_executors<Executors>::head_type;

  explicit initiate_co_composed(composed_io_executors<Executors>&& executors)
    : executors_(std::move(executors))
  {
  }

  executor_type get_executor() const noexcept
  {
    return executors_.head_;
  }

  template <typename Handler, typename Function, typename... InitArgs>
  void operator()(Handler&& handler,
      Function&& function, InitArgs&&... init_args)
  {
    using handler_type = typename decay<Handler>::type;
    co_composed_on_suspend on_suspend{};
    std::forward<Function>(function)(
        co_composed_state<Executors, handler_type>(
          std::move(executors_), std::forward<Handler>(handler), on_suspend),
        std::forward<InitArgs>(init_args)...);
    if (on_suspend.fn_)
      on_suspend.fn_(on_suspend.arg_);
  }

private:
  composed_io_executors<Executors> executors_;
};

template <typename Executors>
inline initiate_co_composed<Executors> make_initiate_co_composed(
    composed_io_executors<Executors>&& executors)
{
  return initiate_co_composed<Executors>(std::move(executors));
}

} // namespace detail

template <typename... IoObjectsOrExecutors>
inline auto co_composed(IoObjectsOrExecutors&&... io_objects_or_executors)
{
  return detail::make_initiate_co_composed(
      detail::make_composed_io_executors(
        detail::get_composed_io_executor(
          std::forward<IoObjectsOrExecutors>(
            io_objects_or_executors))...));
}

} // namespace experimental

#if !defined(GENERATING_DOCUMENTATION)

template <template <typename, typename> class Associator,
    typename Executors, typename Handler, typename Return,
    typename Signature, typename DefaultCandidate>
struct associator<Associator,
    experimental::detail::co_composed_handler<
      Executors, Handler, Return, Signature>,
    DefaultCandidate>
  : Associator<Handler, DefaultCandidate>
{
  static typename Associator<Handler, DefaultCandidate>::type get(
      const experimental::detail::co_composed_handler<
        Executors, Handler, Return, Signature>& h,
      const DefaultCandidate& c = DefaultCandidate()) ASIO_NOEXCEPT
  {
    return Associator<Handler, DefaultCandidate>::get(
        h.promise().state().handler(), c);
  }
};

#endif // !defined(GENERATING_DOCUMENTATION)

} // namespace asio

#if !defined(GENERATING_DOCUMENTATION)
# if defined(ASIO_HAS_STD_COROUTINE)
namespace std {
# else // defined(ASIO_HAS_STD_COROUTINE)
namespace std { namespace experimental {
# endif // defined(ASIO_HAS_STD_COROUTINE)

template <typename C, typename Executors, typename Handler, typename... Args>
struct coroutine_traits<void, C&,
    asio::experimental::detail::co_composed_state<Executors, Handler>,
    Args...>
{
  using promise_type =
    asio::experimental::detail::co_composed_promise<
      Executors, Handler, void>;
};

template <typename C, typename Executors, typename Handler, typename... Args>
struct coroutine_traits<void, C&&,
    asio::experimental::detail::co_composed_state<Executors, Handler>,
    Args...>
{
  using promise_type =
    asio::experimental::detail::co_composed_promise<
      Executors, Handler, void>;
};

template <typename Executors, typename Handler, typename... Args>
struct coroutine_traits<void,
    asio::experimental::detail::co_composed_state<Executors, Handler>,
    Args...>
{
  using promise_type =
    asio::experimental::detail::co_composed_promise<
      Executors, Handler, void>;
};

template <typename... Signatures, typename C,
    typename Executors, typename Handler, typename... Args>
struct coroutine_traits<
    asio::experimental::co_returns<Signatures...>, C&,
    asio::experimental::detail::co_composed_state<Executors, Handler>,
    Args...>
{
  using promise_type =
    asio::experimental::detail::co_composed_promise<Executors,
      Handler, asio::experimental::co_returns<Signatures...>>;
};

template <typename... Signatures, typename C,
    typename Executors, typename Handler, typename... Args>
struct coroutine_traits<
    asio::experimental::co_returns<Signatures...>, C&&,
    asio::experimental::detail::co_composed_state<Executors, Handler>,
    Args...>
{
  using promise_type =
    asio::experimental::detail::co_composed_promise<Executors,
      Handler, asio::experimental::co_returns<Signatures...>>;
};

template <typename... Signatures, typename Executors,
    typename Handler, typename... Args>
struct coroutine_traits<void,
    asio::experimental::co_returns<Signatures...>,
    asio::experimental::detail::co_composed_state<Executors, Handler>,
    Args...>
{
  using promise_type =
    asio::experimental::detail::co_composed_promise<Executors,
      Handler, asio::experimental::co_returns<Signatures...>>;
};

# if defined(ASIO_HAS_STD_COROUTINE)
} // namespace std
# else // defined(ASIO_HAS_STD_COROUTINE)
}} // namespace std::experimental
# endif // defined(ASIO_HAS_STD_COROUTINE)
#endif // !defined(GENERATING_DOCUMENTATION)

#include "asio/detail/pop_options.hpp"

#endif // ASIO_IMPL_EXPERIMENTAL_CO_COMPOSED_HPP
