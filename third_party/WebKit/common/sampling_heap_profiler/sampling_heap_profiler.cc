// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/WebKit/common/sampling_heap_profiler/sampling_heap_profiler.h"

#include <cmath>

#include "base/allocator/allocator_shim.h"
#include "base/allocator/features.h"
#include "base/allocator/partition_allocator/partition_alloc.h"
#include "base/atomicops.h"
#include "base/debug/alias.h"
#include "base/debug/stack_trace.h"
#include "base/memory/singleton.h"
#include "base/rand_util.h"
#include "build/build_config.h"

namespace blink {

using base::allocator::AllocatorDispatch;
using base::subtle::Atomic32;
using base::subtle::AtomicWord;

namespace {

// Control how many top frames to skip when recording call stack.
// These frames correspond to the profiler own frames.
const uint32_t kSkipBaseAllocatorFrames = 4;
const uint32_t kSkipPartitionAllocFrames = 2;

const size_t kDefaultSamplingIntervalBytes = 128 * 1024;

bool g_deterministic;
Atomic32 g_running;
Atomic32 g_operations_in_flight;
Atomic32 g_fast_path_is_closed;
AtomicWord g_bytes_left;
AtomicWord g_current_interval;
AtomicWord g_sampling_interval = kDefaultSamplingIntervalBytes;
uint32_t g_last_sample_ordinal = 0;
SamplingHeapProfiler* g_instance;

void* AllocFn(const AllocatorDispatch* self, size_t size, void* context) {
  void* address = self->next->alloc_function(self->next, size, context);
  SamplingHeapProfiler::MaybeRecordAlloc(address, size,
                                         kSkipBaseAllocatorFrames);
  return address;
}

void* AllocZeroInitializedFn(const AllocatorDispatch* self,
                             size_t n,
                             size_t size,
                             void* context) {
  void* address =
      self->next->alloc_zero_initialized_function(self->next, n, size, context);
  SamplingHeapProfiler::MaybeRecordAlloc(address, n * size,
                                         kSkipBaseAllocatorFrames);
  return address;
}

void* AllocAlignedFn(const AllocatorDispatch* self,
                     size_t alignment,
                     size_t size,
                     void* context) {
  void* address =
      self->next->alloc_aligned_function(self->next, alignment, size, context);
  SamplingHeapProfiler::MaybeRecordAlloc(address, size,
                                         kSkipBaseAllocatorFrames);
  return address;
}

void* ReallocFn(const AllocatorDispatch* self,
                void* address,
                size_t size,
                void* context) {
  // Note: size == 0 actually performs free.
  SamplingHeapProfiler::MaybeRecordFree(address);
  address = self->next->realloc_function(self->next, address, size, context);
  SamplingHeapProfiler::MaybeRecordAlloc(address, size,
                                         kSkipBaseAllocatorFrames);
  return address;
}

void FreeFn(const AllocatorDispatch* self, void* address, void* context) {
  SamplingHeapProfiler::MaybeRecordFree(address);
  self->next->free_function(self->next, address, context);
}

size_t GetSizeEstimateFn(const AllocatorDispatch* self,
                         void* address,
                         void* context) {
  return self->next->get_size_estimate_function(self->next, address, context);
}

unsigned BatchMallocFn(const AllocatorDispatch* self,
                       size_t size,
                       void** results,
                       unsigned num_requested,
                       void* context) {
  unsigned num_allocated = self->next->batch_malloc_function(
      self->next, size, results, num_requested, context);
  for (unsigned i = 0; i < num_allocated; ++i) {
    SamplingHeapProfiler::MaybeRecordAlloc(results[i], size,
                                           kSkipBaseAllocatorFrames);
  }
  return num_allocated;
}

void BatchFreeFn(const AllocatorDispatch* self,
                 void** to_be_freed,
                 unsigned num_to_be_freed,
                 void* context) {
  for (unsigned i = 0; i < num_to_be_freed; ++i)
    SamplingHeapProfiler::MaybeRecordFree(to_be_freed[i]);
  self->next->batch_free_function(self->next, to_be_freed, num_to_be_freed,
                                  context);
}

void FreeDefiniteSizeFn(const AllocatorDispatch* self,
                        void* address,
                        size_t size,
                        void* context) {
  SamplingHeapProfiler::MaybeRecordFree(address);
  self->next->free_definite_size_function(self->next, address, size, context);
}

AllocatorDispatch g_allocator_dispatch = {&AllocFn,
                                          &AllocZeroInitializedFn,
                                          &AllocAlignedFn,
                                          &ReallocFn,
                                          &FreeFn,
                                          &GetSizeEstimateFn,
                                          &BatchMallocFn,
                                          &BatchFreeFn,
                                          &FreeDefiniteSizeFn,
                                          nullptr};

void PartitionAllocHook(void* address, size_t size, const char*) {
  SamplingHeapProfiler::MaybeRecordAlloc(address, size,
                                         kSkipPartitionAllocFrames);
}

void PartitionFreeHook(void* address) {
  SamplingHeapProfiler::MaybeRecordFree(address);
}

}  // namespace

SamplingHeapProfiler::Sample::Sample(size_t size,
                                     size_t count,
                                     uint32_t ordinal)
    : size(size), count(count), ordinal(ordinal) {}

SamplingHeapProfiler::SamplingHeapProfiler() {
  g_instance = this;
}

// static
void SamplingHeapProfiler::InstallAllocatorHooksOnce() {
  static bool hook_installed = InstallAllocatorHooks();
  base::debug::Alias(&hook_installed);
}

// static
bool SamplingHeapProfiler::InstallAllocatorHooks() {
#if BUILDFLAG(USE_ALLOCATOR_SHIM)
  base::allocator::InsertAllocatorDispatch(&g_allocator_dispatch);
#else
  base::debug::Alias(&g_allocator_dispatch);
  DLOG(WARNING)
      << "base::allocator shims are not available for memory sampling.";
#endif  // BUILDFLAG(USE_ALLOCATOR_SHIM)

  base::PartitionAllocHooks::SetAllocationHook(&PartitionAllocHook);
  base::PartitionAllocHooks::SetFreeHook(&PartitionFreeHook);

  return true;
}

uint32_t SamplingHeapProfiler::Start() {
  InstallAllocatorHooksOnce();
  size_t next_interval =
      GetNextSampleInterval(base::subtle::Acquire_Load(&g_sampling_interval));
  base::subtle::Release_Store(&g_current_interval, next_interval);
  base::subtle::Release_Store(&g_bytes_left, next_interval);
  base::subtle::Barrier_AtomicIncrement(&g_running, 1);
  return g_last_sample_ordinal;
}

void SamplingHeapProfiler::Stop() {
  AtomicWord count = base::subtle::Barrier_AtomicIncrement(&g_running, -1);
  CHECK_GE(count, 0);
}

void SamplingHeapProfiler::SetSamplingInterval(size_t sampling_interval) {
  // TODO(alph): Reset the sample being collected if running.
  base::subtle::Release_Store(&g_sampling_interval,
                              static_cast<AtomicWord>(sampling_interval));
}

// static
size_t SamplingHeapProfiler::GetNextSampleInterval(size_t interval) {
  if (UNLIKELY(g_deterministic))
    return interval;

  // We sample with a Poisson process, with constant average sampling
  // interval. This follows the exponential probability distribution with
  // parameter λ = 1/interval where |interval| is the average number of bytes
  // between samples.
  // Let u be a uniformly distributed random number between 0 and 1, then
  // next_sample = -ln(u) / λ
  double uniform = base::RandDouble();
  double value = -log(uniform) * interval;
  size_t min_value = sizeof(intptr_t);
  // We limit the upper bound of a sample interval to make sure we don't have
  // huge gaps in the sampling stream. Probability of the upper bound gets hit
  // is exp(-20) ~ 2e-9, so it should not skew the distibution.
  size_t max_value = interval * 20;
  if (UNLIKELY(value < min_value))
    return min_value;
  if (UNLIKELY(value > max_value))
    return max_value;
  return static_cast<size_t>(value);
}

// static
void SamplingHeapProfiler::MaybeRecordAlloc(void* address,
                                            size_t size,
                                            uint32_t skip_frames) {
  if (UNLIKELY(!base::subtle::NoBarrier_Load(&g_running)))
    return;

  // Lock-free algorithm decreases number of bytes left to form a sample.
  // The thread that makes it to reach zero is responsible for recording
  // a sample.
  AtomicWord bytes_left = base::subtle::NoBarrier_AtomicIncrement(
      &g_bytes_left, -static_cast<AtomicWord>(size));
  if (LIKELY(bytes_left > 0))
    return;

  // Return if g_bytes_left was already zero or below before we decreased it.
  // That basically means that another thread in fact crossed the threshold.
  if (LIKELY(bytes_left + static_cast<AtomicWord>(size) <= 0))
    return;

  // Only one thread that crossed the threshold is running the code below.
  // It is going to be recording the sample.

  size_t accumulated = base::subtle::Acquire_Load(&g_current_interval);
  size_t next_interval =
      GetNextSampleInterval(base::subtle::NoBarrier_Load(&g_sampling_interval));

  // Make sure g_current_interval is set before updating g_bytes_left.
  base::subtle::Release_Store(&g_current_interval, next_interval);

  // Put the next sampling interval to g_bytes_left, thus allowing threads to
  // start accumulating bytes towards the next sample.
  // Simulateneously extract the current value (which is negative or zero)
  // and take it into account when calculating the number of bytes
  // accumulated for the current sample.
  accumulated -=
      base::subtle::NoBarrier_AtomicExchange(&g_bytes_left, next_interval);

  g_instance->RecordAlloc(accumulated, size, address, kSkipBaseAllocatorFrames);
}

void SamplingHeapProfiler::RecordStackTrace(Sample* sample,
                                            uint32_t skip_frames) {
  // TODO(alph): Consider using debug::TraceStackFramePointers. It should be
  // somewhat faster than base::debug::StackTrace.
  base::debug::StackTrace trace;
  size_t count;
  void* const* addresses = const_cast<void* const*>(trace.Addresses(&count));
  // Skip SamplingHeapProfiler frames.
  sample->stack.insert(
      sample->stack.end(), &addresses[skip_frames],
      &addresses[std::max(count, static_cast<size_t>(skip_frames))]);
}

void SamplingHeapProfiler::RecordAlloc(size_t total_allocated,
                                       size_t size,
                                       void* address,
                                       uint32_t skip_frames) {
  // TODO(alph): It's better to use a recursive mutex and move the check
  // inside the critical section.
  if (entered_.Get())
    return;
  base::AutoLock lock(mutex_);
  entered_.Set(true);

  size_t count = std::max<size_t>(1, (total_allocated + size / 2) / size);
  Sample sample(size, count, ++g_last_sample_ordinal);
  RecordStackTrace(&sample, skip_frames);

  // Close the fast-path as inserting an element into samples_ may cause
  // rehashing that invalidates iterators affecting all the concurrent
  // readers.
  base::subtle::Release_Store(&g_fast_path_is_closed, 1);
  while (base::subtle::Acquire_Load(&g_operations_in_flight)) {
    while (base::subtle::NoBarrier_Load(&g_operations_in_flight)) {
    }
  }
  // TODO(alph): We can do better by keeping the fast-path open when
  // we know insert won't cause rehashing.
  samples_.insert(std::make_pair(address, std::move(sample)));
  base::subtle::Release_Store(&g_fast_path_is_closed, 0);

  entered_.Set(false);
}

// static
void SamplingHeapProfiler::MaybeRecordFree(void* address) {
  bool maybe_sampled = true;  // Pessimistically assume allocation was sampled.
  base::subtle::Barrier_AtomicIncrement(&g_operations_in_flight, 1);
  if (LIKELY(!base::subtle::NoBarrier_Load(&g_fast_path_is_closed)))
    maybe_sampled = g_instance->samples_.count(address);
  base::subtle::Barrier_AtomicIncrement(&g_operations_in_flight, -1);
  if (maybe_sampled)
    g_instance->RecordFree(address);
}

void SamplingHeapProfiler::RecordFree(void* address) {
  if (entered_.Get())
    return;
  base::AutoLock lock(mutex_);
  entered_.Set(true);
  samples_.erase(address);
  entered_.Set(false);
}

// static
SamplingHeapProfiler* SamplingHeapProfiler::GetInstance() {
  return base::Singleton<SamplingHeapProfiler>::get();
}

// static
void SamplingHeapProfiler::SuppressRandomnessForTest() {
  g_deterministic = true;
}

std::vector<SamplingHeapProfiler::Sample> SamplingHeapProfiler::GetSamples(
    uint32_t profile_id) {
  base::AutoLock lock(mutex_);
  CHECK(!entered_.Get());
  entered_.Set(true);
  std::vector<Sample> samples;
  for (auto& it : samples_) {
    Sample& sample = it.second;
    if (sample.ordinal > profile_id)
      samples.push_back(sample);
  }
  entered_.Set(false);
  return samples;
}

}  // namespace blink
