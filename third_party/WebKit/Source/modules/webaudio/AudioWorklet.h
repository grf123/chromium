// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AudioWorklet_h
#define AudioWorklet_h

#include "core/workers/Worklet.h"
#include "modules/ModulesExport.h"
#include "platform/heap/Handle.h"

namespace blink {

class AudioWorkletHandler;
class AudioWorkletMessagingProxy;
class BaseAudioContext;
class CrossThreadAudioParamInfo;
class MessagePortChannel;
class SerializedScriptValue;

class MODULES_EXPORT AudioWorklet final : public Worklet {
  DEFINE_WRAPPERTYPEINFO();
  USING_GARBAGE_COLLECTED_MIXIN(AudioWorklet);
  WTF_MAKE_NONCOPYABLE(AudioWorklet);

 public:
  // When the AudioWorklet runtime flag is not enabled, this constructor returns
  // |nullptr|.
  static AudioWorklet* Create(BaseAudioContext*);

  ~AudioWorklet() = default;

  void CreateProcessor(AudioWorkletHandler*,
                       MessagePortChannel,
                       scoped_refptr<SerializedScriptValue> node_options);

  // Invoked by AudioWorkletMessagingProxy. Notifies |context_| when
  // AudioWorkletGlobalScope finishes the first script evaluation and is ready
  // for the worklet operation. Can be used for other post-evaluation tasks
  // in AudioWorklet or BaseAudioContext.
  void NotifyGlobalScopeIsUpdated();

  WebThread* GetBackingThread();

  const Vector<CrossThreadAudioParamInfo> GetParamInfoListForProcessor(
      const String& name);

  bool IsProcessorRegistered(const String& name);

  // Returns |true| when a AudioWorkletMessagingProxy and a WorkletBackingThread
  // are ready.
  bool IsReady();

  void Trace(blink::Visitor*) override;

 private:
  explicit AudioWorklet(BaseAudioContext*);

  // Implements Worklet
  bool NeedsToCreateGlobalScope() final;
  WorkletGlobalScopeProxy* CreateGlobalScope() final;

  // Returns |nullptr| if there is no active WorkletGlobalScope().
  AudioWorkletMessagingProxy* GetMessagingProxy();

  // To catch the first global scope update and notify the context.
  bool worklet_started_ = false;

  Member<BaseAudioContext> context_;
};

}  // namespace blink

#endif  // AudioWorklet_h
