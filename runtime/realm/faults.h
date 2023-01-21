/* Copyright 2023 Stanford University, NVIDIA Corporation
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

// helper defines/data structures for fault reporting/handling in Realm

#ifndef REALM_FAULTS_H
#define REALM_FAULTS_H

#include "realm/bytearray.h"
#include "realm/event.h"

#include <vector>
#include <iostream>

// we need intptr_t - make it if needed
#if REALM_CXX_STANDARD >= 11
#include <stdint.h>
#else
#include <stddef.h>
typedef ptrdiff_t intptr_t;
#endif

#ifdef REALM_ON_WINDOWS
// winerror.h defines this, polluting all namespaces, so get it out of the way now
#include <winerror.h>
#undef ERROR_CANCELLED
#endif

namespace Realm {

  class ProfilingMeasurementCollection;

  namespace Faults {

    // faults are reported with an integer error code - negative codes are internal to
    //  Realm, while positive ones are reserved for applications
    enum {
      ERROR_POISONED_EVENT = -1000,     // querying a poisoned event without handling poison
      ERROR_POISONED_PRECONDITION,      // precondition to an operation was poisoned
      ERROR_CANCELLED,                  // cancelled by request from application

      // application can use its own error codes too, but start
      //  here so we don't get any conflicts
      ERROR_APPLICATION_DEFINED = 0,
    };

  }; // namespace Faults

  class REALM_PUBLIC_API Backtrace {
  public:
    Backtrace(void);
    ~Backtrace(void);

    Backtrace(const Backtrace& copy_from);
    Backtrace& operator=(const Backtrace& copy_from);

    bool operator==(const Backtrace& rhs) const;

    uintptr_t hash(void) const;

    bool empty(void) const;

    // attempts to prune this backtrace by removing frames that appear
    //  in the other one
    bool prune(const Backtrace &other);

    // captures the current back trace, skipping 'skip' frames, and optionally
    //   limiting the total depth - this isn't as simple as as stack walk any more,
    //   so you probably don't want to ask for these during any normal execution paths
    void capture_backtrace(int skip = 0, int max_depth = 0);

    // attempts to map the pointers in the back trace to symbol names - this can be
    //   much more expensive
    void lookup_symbols(void);

    REALM_PUBLIC_API
    friend std::ostream& operator<<(std::ostream& os, const Backtrace& bt);

  protected:
    uintptr_t compute_hash(int depth = 0) const;

    uintptr_t pc_hash; // used for fast comparisons
    std::vector<uintptr_t> pcs;
    std::vector<std::string> symbols;

    template <typename S>
      friend bool serdez(S& serdez, const Backtrace& b);
  };


  // Realm execution exceptions

  // an abstract intermediate class that captures all Realm-generated exceptions
  class REALM_PUBLIC_API ExecutionException : public std::exception {
  public:
    ExecutionException(int _error_code,
		       const void *_detail_data, size_t _detail_size,
		       bool capture_backtrace = true);
    virtual ~ExecutionException(void) throw();

    virtual const char *what(void) const throw() = 0;

    virtual void populate_profiling_measurements(ProfilingMeasurementCollection& pmc) const;

    int error_code;
    ByteArray details;
    Backtrace backtrace;
  };

  // the result of an explicit application request to cancel the task
  class REALM_PUBLIC_API CancellationException : public ExecutionException {
  public:
    CancellationException(void);

    virtual const char *what(void) const throw();
  };

  // the result of testing a poisoned event
  class REALM_PUBLIC_API PoisonedEventException : public ExecutionException {
  public:
    PoisonedEventException(Event _event);

    virtual const char *what(void) const throw();

  protected:
    Event event;
  };

  // generated by Processor::report_execution_fault()
  class REALM_PUBLIC_API ApplicationException : public ExecutionException {
  public:
    ApplicationException(int _error_code,
			 const void *_detail_data, size_t _detail_size);

    virtual const char *what(void) const throw();
  };

}; // namespace Realm

#include "realm/faults.inl"

#endif // REALM_FAULTS_H
