
/* See LICENSE file for copyright and license details. */

#pragma once

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <utility>

void die(const char *fmt, ...);

namespace detail::zi
{
void *ecalloc(std::size_t nmemb, std::size_t size);
} // namespace detail::zi

namespace zi
{

template <typename T, typename LowT, typename HighT>
constexpr inline bool cmp_between_inclusive(T x, LowT a, HighT b)
{
    return std::cmp_less_equal(a, x) && std::cmp_less_equal(x, b);
}

template <typename T>
inline T *safe_calloc(std::size_t size)
{
    static_assert(!std::is_const_v<T>);

    auto p = ::detail::zi::ecalloc(size, sizeof(T));

    return reinterpret_cast<T *>(p);
}

using ::die;

} // namespace zi
