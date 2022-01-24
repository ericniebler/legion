/* Copyright 2022 Stanford University, NVIDIA Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// configuration settings that control how Realm is built
// this is expected to become an auto-generated file at some point

#ifndef REALM_CONFIG_H
#define REALM_CONFIG_H

// realm_defines.h is auto-generated by both the make and cmake builds
#include "realm_defines.h"

// get macros that spell things the right way for each compiler
#include "realm/compiler_support.h"

// Control the maximum number of dimensions for Realm
#ifndef REALM_MAX_DIM
#define REALM_MAX_DIM 3
#endif

// if set, uses ucontext.h for user level thread switching, otherwise falls
//  back to POSIX threads
// address sanitizer doesn't cope with makecontext/swapcontext either
#if !defined(REALM_USE_NATIVE_THREADS) && !defined(REALM_ON_MACOS) && !defined(ASAN_ENABLED)
// clang on Mac is generating apparently-broken code in the user thread
//  scheduler, so disable this code path for now
#define REALM_USE_USER_THREADS
#endif

// if set, uses Linux's kernel-level io_submit interface, otherwise uses
//  POSIX AIO for async file I/O
#ifdef REALM_ON_LINUX
//define REALM_USE_KERNEL_AIO
#define REALM_USE_LIBAIO
#endif
#if defined(REALM_ON_MACOS) || defined(REALM_ON_FREEBSD)
#define REALM_USE_LIBAIO
#endif

// dynamic loading via dlfcn and a not-completely standard dladdr extension
#ifdef REALM_USE_LIBDL
  #if defined(REALM_ON_LINUX) || defined(REALM_ON_MACOS) || defined(REALM_ON_FREEBSD)
    #define REALM_USE_DLFCN
    #define REALM_USE_DLADDR
  #endif
#endif

// can Realm use exceptions to propagate errors back to the profiling interace?
#define REALM_USE_EXCEPTIONS

// the Realm operation table is needed if you want to be able to cancel operations
#ifndef REALM_NO_USE_OPERATION_TABLE
#define REALM_USE_OPERATION_TABLE
#endif

#ifdef __cplusplus
// runtime configuration settings
namespace Realm {
  namespace Config {
    // if non-zero, eagerly checks deferred user event triggers for loops up to the
    //  specified limit
    extern int event_loop_detection_limit;

    // if true, worker threads that might have used user-level thread switching
    //  fall back to kernel threading
    extern bool force_kernel_threads;
  };
};
#endif

#endif
