// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ScriptWrappableMarkingVisitor_h
#define ScriptWrappableMarkingVisitor_h

#include "platform/PlatformExport.h"
#include "platform/bindings/ScriptWrappableVisitor.h"
#include "platform/heap/HeapPage.h"
#include "platform/heap/ThreadingTraits.h"
#include "platform/heap/VisitorImpl.h"
#include "platform/wtf/Deque.h"
#include "platform/wtf/Vector.h"
#include "v8/include/v8.h"

namespace blink {

template <typename T>
class DOMWrapperMap;
class HeapObjectHeader;
class ScriptWrappable;
class ScriptWrappableVisitor;
template <typename T>
class TraceWrapperV8Reference;

// ScriptWrappableVisitor is used to trace through Blink's heap to find all
// reachable wrappers. V8 calls this visitor during its garbage collection,
// see v8::EmbedderHeapTracer.
class PLATFORM_EXPORT ScriptWrappableMarkingVisitor
    : public v8::EmbedderHeapTracer,
      public ScriptWrappableVisitor {
  DISALLOW_IMPLICIT_CONSTRUCTORS(ScriptWrappableMarkingVisitor);

 public:
  static ScriptWrappableMarkingVisitor* CurrentVisitor(v8::Isolate*);

  bool WrapperTracingInProgress() const { return tracing_in_progress_; }

  // Replace all dead objects in the marking deque with nullptr after Oilpan
  // garbage collection.
  static void InvalidateDeadObjectsInMarkingDeque(v8::Isolate*);

  // Immediately clean up all wrappers.
  static void PerformCleanup(v8::Isolate*);

  // Conservative Dijkstra barrier.
  //
  // On assignment 'x.a = y' during incremental marking the Dijkstra barrier
  // suggests checking the color of 'x' and only mark 'y' if 'x' is marked.

  // Since checking 'x' is expensive in the current setting, as it requires
  // either a back pointer or expensive lookup logic due to large objects and
  // multiple inheritance, just assume that 'x' is black. We assume here that
  // since an object 'x' is referenced for a write, it will generally also be
  // alive in the current GC cycle.
  template <typename T>
  static void WriteBarrier(const T* dst_object) {
    if (!dst_object)
      return;

    const ThreadState* thread_state =
        ThreadStateFor<ThreadingTrait<T>::kAffinity>::GetState();
    DCHECK(thread_state);
    // Bail out if tracing is not in progress.
    if (!thread_state->WrapperTracingInProgress())
      return;

    // If the wrapper is already marked we can bail out here.
    if (TraceTrait<T>::GetHeapObjectHeader(dst_object)->IsWrapperHeaderMarked())
      return;

    CurrentVisitor(thread_state->GetIsolate())
        ->Visit(WrapperDescriptorFor(dst_object));
  }

  static void WriteBarrier(v8::Isolate*,
                           const TraceWrapperV8Reference<v8::Value>&);

  static void WriteBarrier(v8::Isolate*,
                           DOMWrapperMap<ScriptWrappable>*,
                           ScriptWrappable* key);

  ScriptWrappableMarkingVisitor(v8::Isolate* isolate) : isolate_(isolate){};
  ~ScriptWrappableMarkingVisitor() override;

  // v8::EmbedderHeapTracer interface.

  void TracePrologue() override;
  void RegisterV8References(const std::vector<std::pair<void*, void*>>&
                                internal_fields_of_potential_wrappers) override;
  void RegisterV8Reference(const std::pair<void*, void*>& internal_fields);
  bool AdvanceTracing(double deadline_in_ms,
                      v8::EmbedderHeapTracer::AdvanceTracingActions) override;
  void TraceEpilogue() override;
  void AbortTracing() override;
  void EnterFinalPause() override;
  size_t NumberOfWrappersToTrace() override;

 protected:
  // ScriptWrappableVisitor interface.
  void Visit(const TraceWrapperV8Reference<v8::Value>&) const override;
  void Visit(const WrapperDescriptor&) const override;
  void Visit(DOMWrapperMap<ScriptWrappable>*,
             const ScriptWrappable* key) const override;

  v8::Isolate* isolate() const { return isolate_; }

 private:
  class MarkingDequeItem {
   public:
    explicit MarkingDequeItem(const WrapperDescriptor& wrapper_descriptor)
        : trace_wrappers_callback_(wrapper_descriptor.trace_wrappers_callback),
          heap_object_header_callback_(
              wrapper_descriptor.heap_object_header_callback),
          raw_object_pointer_(wrapper_descriptor.traceable) {
      DCHECK(trace_wrappers_callback_);
      DCHECK(heap_object_header_callback_);
      DCHECK(raw_object_pointer_);
    }

    // Traces wrappers if the underlying object has not yet been invalidated.
    inline void TraceWrappers(ScriptWrappableVisitor* visitor) const {
      if (raw_object_pointer_) {
        trace_wrappers_callback_(visitor, raw_object_pointer_);
      }
    }

    inline const void* RawObjectPointer() { return raw_object_pointer_; }

    // Returns true if the object is currently marked in Oilpan and false
    // otherwise.
    inline bool ShouldBeInvalidated() {
      return raw_object_pointer_ && !GetHeapObjectHeader()->IsMarked();
    }

    // Invalidates the current wrapper marking data, i.e., calling TraceWrappers
    // will result in a noop.
    inline void Invalidate() { raw_object_pointer_ = nullptr; }

   private:
    inline const HeapObjectHeader* GetHeapObjectHeader() {
      DCHECK(raw_object_pointer_);
      return heap_object_header_callback_(raw_object_pointer_);
    }

    TraceWrappersCallback trace_wrappers_callback_;
    HeapObjectHeaderCallback heap_object_header_callback_;
    const void* raw_object_pointer_;
  };

  void MarkWrapperHeader(HeapObjectHeader*) const;

  // Schedule an idle task to perform a lazy (incremental) clean up of
  // wrappers.
  void ScheduleIdleLazyCleanup();
  void PerformLazyCleanup(double deadline_seconds);

  void InvalidateDeadObjectsInMarkingDeque();

  // Immediately cleans up all wrappers if necessary.
  void PerformCleanup();

  WTF::Deque<MarkingDequeItem>* MarkingDeque() const { return &marking_deque_; }
  WTF::Vector<HeapObjectHeader*>* HeadersToUnmark() const {
    return &headers_to_unmark_;
  }

  bool MarkingDequeContains(void* needle);

  // Returns true if wrapper tracing is currently in progress, i.e.,
  // TracePrologue has been called, and TraceEpilogue has not yet been called.
  bool tracing_in_progress_ = false;

  // Is AdvanceTracing currently running? If not, we know that all calls of
  // pushToMarkingDeque are from V8 or new wrapper associations. And this
  // information is used by the verifier feature.
  bool advancing_tracing_ = false;

  // Indicates whether an idle task for a lazy cleanup has already been
  // scheduled. The flag is used to avoid scheduling multiple idle tasks for
  // cleaning up.
  bool idle_cleanup_task_scheduled_ = false;

  // Indicates whether cleanup should currently happen. The flag is used to
  // avoid cleaning up in the next GC cycle.
  bool should_cleanup_ = false;

  // Collection of objects we need to trace from. We assume it is safe to hold
  // on to the raw pointers because:
  // - oilpan object cannot move
  // - oilpan gc will call invalidateDeadObjectsInMarkingDeque to delete all
  //   obsolete objects
  mutable WTF::Deque<MarkingDequeItem> marking_deque_;

  // Collection of objects we started tracing from. We assume it is safe to
  // hold on to the raw pointers because:
  // - oilpan object cannot move
  // - oilpan gc will call invalidateDeadObjectsInMarkingDeque to delete
  //   all obsolete objects
  //
  // These objects are used when TraceWrappablesVerifier feature is enabled to
  // verify that all objects reachable in the atomic pause were marked
  // incrementally. If not, there is one or multiple write barriers missing.
  mutable WTF::Deque<MarkingDequeItem> verifier_deque_;

  // Collection of headers we need to unmark after the tracing finished. We
  // assume it is safe to hold on to the headers because:
  // - oilpan objects cannot move
  // - objects this headers belong to are invalidated by the oilpan GC in
  //   invalidateDeadObjectsInMarkingDeque.
  mutable WTF::Vector<HeapObjectHeader*> headers_to_unmark_;
  v8::Isolate* isolate_;

  FRIEND_TEST_ALL_PREFIXES(ScriptWrappableMarkingVisitorTest, MixinTracing);
  FRIEND_TEST_ALL_PREFIXES(ScriptWrappableMarkingVisitorTest,
                           OilpanClearsMarkingDequeWhenObjectDied);
  FRIEND_TEST_ALL_PREFIXES(ScriptWrappableMarkingVisitorTest,
                           ScriptWrappableMarkingVisitorTracesWrappers);
  FRIEND_TEST_ALL_PREFIXES(ScriptWrappableMarkingVisitorTest,
                           OilpanClearsHeadersWhenObjectDied);
  FRIEND_TEST_ALL_PREFIXES(
      ScriptWrappableMarkingVisitorTest,
      MarkedObjectDoesNothingOnWriteBarrierHitWhenDependencyIsMarkedToo);
  FRIEND_TEST_ALL_PREFIXES(
      ScriptWrappableMarkingVisitorTest,
      MarkedObjectMarksDependencyOnWriteBarrierHitWhenNotMarked);
  FRIEND_TEST_ALL_PREFIXES(ScriptWrappableMarkingVisitorTest,
                           WriteBarrierOnHeapVectorSwap1);
  FRIEND_TEST_ALL_PREFIXES(ScriptWrappableMarkingVisitorTest,
                           WriteBarrierOnHeapVectorSwap2);
};

}  // namespace blink

#endif  // ScriptWrappableMarkingVisitor_h
