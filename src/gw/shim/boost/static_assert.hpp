// Shim: Cannonball only uses Boost here to check type sizes at compile time.
#pragma once
#define BOOST_STATIC_ASSERT_MSG(c, m) static_assert(c, m)
#define BOOST_STATIC_ASSERT(c) static_assert(c, #c)
