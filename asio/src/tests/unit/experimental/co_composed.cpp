//
// experimental/co_composed.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2022 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Disable autolinking for unit tests.
#if !defined(BOOST_ALL_NO_LIB)
#define BOOST_ALL_NO_LIB 1
#endif // !defined(BOOST_ALL_NO_LIB)

// Prevent link dependency on the Boost.System library.
#if !defined(BOOST_SYSTEM_NO_DEPRECATED)
#define BOOST_SYSTEM_NO_DEPRECATED
#endif // !defined(BOOST_SYSTEM_NO_DEPRECATED)

// Test that header file is self-contained.
#include "asio/experimental/co_composed.hpp"

#include "asio/deferred.hpp"
#include "asio/detached.hpp"
#include "asio/io_context.hpp"
#include "asio/post.hpp"
#include "../unit_test.hpp"

template <typename CompletionToken>
auto async_throw(CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void()>(
      [](auto) { throw 42; }, token);
}

template <typename CompletionToken>
auto throw_first(CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void()>(
      asio::experimental::co_composed(), token,
      [](auto state) -> void
      {
        throw 42;
        co_yield state.complete();
      });
}

void test_throw_first()
{
  try
  {
    throw_first(asio::detached);
    ASIO_CHECK(0);
  }
  catch (int)
  {
  }
}

template <typename CompletionToken>
auto throw_after_await(asio::io_context& ctx, CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void()>(
      asio::experimental::co_composed(), token,
      [](auto state, asio::io_context& ctx) -> void
      {
        co_await asio::post(ctx, asio::deferred);
        throw 42;
        co_yield state.complete();
      }, std::ref(ctx));
}

void test_throw_after_await()
{
  try
  {
    asio::io_context ctx(1);
    throw_after_await(ctx, asio::detached);
    ctx.run();
    ASIO_CHECK(0);
  }
  catch (int)
  {
  }
}

template <typename CompletionToken>
auto throw_in_first_suspend(CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void()>(
      asio::experimental::co_composed(), token,
      [](auto state) -> void
      {
        co_await async_throw(asio::deferred);
        co_yield state.complete();
      });
}

void test_throw_in_first_suspend()
{
  try
  {
    throw_in_first_suspend(asio::detached);
    ASIO_CHECK(0);
  }
  catch (int)
  {
  }
}

template <typename CompletionToken>
auto throw_in_suspend_after_await(asio::io_context& ctx, CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void()>(
      asio::experimental::co_composed(), token,
      [](auto state, asio::io_context& ctx) -> void
      {
        co_await asio::post(ctx, asio::deferred);
        co_await async_throw(asio::deferred);
        co_yield state.complete();
      }, std::ref(ctx));
}

void test_throw_in_suspend_after_await()
{
  try
  {
    asio::io_context ctx(1);
    throw_in_suspend_after_await(ctx, asio::detached);
    ctx.run();
    ASIO_CHECK(0);
  }
  catch (int)
  {
  }
}

template <typename CompletionToken>
auto post_loop(asio::io_context& ctx, CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void(int)>(
      asio::experimental::co_composed(ctx), token,
      [](auto state, asio::io_context& ctx) -> void
      {
        int i = 0;
        for (; i < 100; ++i)
          co_await asio::post(ctx, asio::deferred);
        co_yield state.complete(i);
      }, std::ref(ctx));
}

void test_post_loop()
{
  asio::io_context ctx(1);
  int count = 0;
  post_loop(ctx, [&](int i){ count = i; });
  ctx.run();
  ASIO_CHECK(count == 100);
}

template <typename CompletionToken>
auto nested_post(asio::io_context& ctx, CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void()>(
      asio::experimental::co_composed(ctx), token,
      [](auto state, asio::io_context& ctx) -> void
      {
        co_await asio::post(ctx, asio::deferred);
        co_yield state.complete();
      }, std::ref(ctx));
}

template <typename CompletionToken>
auto nested_post_loop(asio::io_context& ctx, CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void(int)>(
      asio::experimental::co_composed(ctx), token,
      [](auto state, asio::io_context& ctx) -> void
      {
        int i = 0;
        for (; i < 100; ++i)
          co_await nested_post(ctx, asio::deferred);
        co_yield state.complete(i);
      }, std::ref(ctx));
}

void test_nested_post_loop()
{
  asio::io_context ctx(1);
  int count = 0;
  nested_post_loop(ctx, [&](int i){ count = i; });
  ctx.run();
  ASIO_CHECK(count == 100);
}

template <typename CompletionToken>
auto post_loop_return_1_0(asio::io_context& ctx, CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void()>(
      asio::experimental::co_composed(ctx), token,
      [](auto, asio::io_context& ctx)
        -> asio::experimental::co_returns<void()>
      {
        int i = 0;
        for (; i < 100; ++i)
          co_await asio::post(ctx, asio::deferred);
        co_return {};
      }, std::ref(ctx));
}

void test_post_loop_return_1_0()
{
  asio::io_context ctx(1);
  bool done = false;
  post_loop_return_1_0(ctx, [&]{ done = true; });
  ctx.run();
  ASIO_CHECK(done);
}

template <typename CompletionToken>
auto post_loop_return_1_1(asio::io_context& ctx, CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void(int)>(
      asio::experimental::co_composed(ctx), token,
      [](auto, asio::io_context& ctx)
        -> asio::experimental::co_returns<void(int)>
      {
        int i = 0;
        for (; i < 100; ++i)
          co_await asio::post(ctx, asio::deferred);
        co_return {i};
      }, std::ref(ctx));
}

void test_post_loop_return_1_1()
{
  asio::io_context ctx(1);
  int count = 0;
  post_loop_return_1_1(ctx, [&](int i){ count = i; });
  ctx.run();
  ASIO_CHECK(count == 100);
}

template <typename CompletionToken>
auto post_loop_return_1_2(asio::io_context& ctx, CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void(int, char)>(
      asio::experimental::co_composed(ctx), token,
      [](auto, asio::io_context& ctx)
        -> asio::experimental::co_returns<void(int, char)>
      {
        int i = 0;
        for (; i < 100; ++i)
          co_await asio::post(ctx, asio::deferred);
        co_return {i, 'A'};
      }, std::ref(ctx));
}

void test_post_loop_return_1_2()
{
  asio::io_context ctx(1);
  int count = 0;
  char ch = 0;
  post_loop_return_1_2(ctx, [&](int i, char c){ count = i, ch = c; });
  ctx.run();
  ASIO_CHECK(count == 100);
  ASIO_CHECK(ch == 'A');
}

template <typename CompletionToken>
auto post_loop_return_2(asio::io_context& ctx, CompletionToken&& token)
{
  return asio::async_initiate<CompletionToken, void(), void(int)>(
      asio::experimental::co_composed(ctx), token,
      [](auto, asio::io_context& ctx)
        -> asio::experimental::co_returns<void(), void(int)>
      {
        int i = 0;
        for (; i < 100; ++i)
          co_await asio::post(ctx, asio::deferred);
        co_return {i};
      }, std::ref(ctx));
}

void test_post_loop_return_2()
{
  asio::io_context ctx(1);
  int count = 0;
  post_loop_return_2(ctx, [&](int i = 0){ count = i; });
  ctx.run();
  ASIO_CHECK(count == 100);
}

ASIO_TEST_SUITE
(
  "experimental/co_composed",
  ASIO_TEST_CASE(test_throw_first)
  ASIO_TEST_CASE(test_throw_after_await)
  ASIO_TEST_CASE(test_throw_in_first_suspend)
  ASIO_TEST_CASE(test_throw_in_suspend_after_await)
  ASIO_TEST_CASE(test_post_loop)
  ASIO_TEST_CASE(test_nested_post_loop)
  ASIO_TEST_CASE(test_post_loop_return_1_0)
  ASIO_TEST_CASE(test_post_loop_return_1_1)
  ASIO_TEST_CASE(test_post_loop_return_1_2)
  ASIO_TEST_CASE(test_post_loop_return_2)
)
