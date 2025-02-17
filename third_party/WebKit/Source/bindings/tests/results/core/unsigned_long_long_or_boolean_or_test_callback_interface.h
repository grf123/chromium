// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file has been auto-generated from the Jinja2 template
// third_party/WebKit/Source/bindings/templates/union_container.h.tmpl
// by the script code_generator_v8.py.
// DO NOT MODIFY!

// clang-format off
#ifndef UnsignedLongLongOrBooleanOrTestCallbackInterface_h
#define UnsignedLongLongOrBooleanOrTestCallbackInterface_h

#include "bindings/core/v8/Dictionary.h"
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/NativeValueTraits.h"
#include "bindings/core/v8/V8BindingForCore.h"
#include "core/CoreExport.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Optional.h"

namespace blink {

class V8TestCallbackInterface;

class CORE_EXPORT UnsignedLongLongOrBooleanOrTestCallbackInterface final {
  DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();
 public:
  UnsignedLongLongOrBooleanOrTestCallbackInterface();
  bool IsNull() const { return type_ == SpecificType::kNone; }

  bool IsBoolean() const { return type_ == SpecificType::kBoolean; }
  bool GetAsBoolean() const;
  void SetBoolean(bool);
  static UnsignedLongLongOrBooleanOrTestCallbackInterface FromBoolean(bool);

  bool IsTestCallbackInterface() const { return type_ == SpecificType::kTestCallbackInterface; }
  V8TestCallbackInterface* GetAsTestCallbackInterface() const;
  void SetTestCallbackInterface(V8TestCallbackInterface*);
  static UnsignedLongLongOrBooleanOrTestCallbackInterface FromTestCallbackInterface(V8TestCallbackInterface*);

  bool IsUnsignedLongLong() const { return type_ == SpecificType::kUnsignedLongLong; }
  uint64_t GetAsUnsignedLongLong() const;
  void SetUnsignedLongLong(uint64_t);
  static UnsignedLongLongOrBooleanOrTestCallbackInterface FromUnsignedLongLong(uint64_t);

  UnsignedLongLongOrBooleanOrTestCallbackInterface(const UnsignedLongLongOrBooleanOrTestCallbackInterface&);
  ~UnsignedLongLongOrBooleanOrTestCallbackInterface();
  UnsignedLongLongOrBooleanOrTestCallbackInterface& operator=(const UnsignedLongLongOrBooleanOrTestCallbackInterface&);
  void Trace(blink::Visitor*);

 private:
  enum class SpecificType {
    kNone,
    kBoolean,
    kTestCallbackInterface,
    kUnsignedLongLong,
  };
  SpecificType type_;

  bool boolean_;
  Member<V8TestCallbackInterface> test_callback_interface_;
  uint64_t unsigned_long_long_;

  friend CORE_EXPORT v8::Local<v8::Value> ToV8(const UnsignedLongLongOrBooleanOrTestCallbackInterface&, v8::Local<v8::Object>, v8::Isolate*);
};

class V8UnsignedLongLongOrBooleanOrTestCallbackInterface final {
 public:
  CORE_EXPORT static void ToImpl(v8::Isolate*, v8::Local<v8::Value>, UnsignedLongLongOrBooleanOrTestCallbackInterface&, UnionTypeConversionMode, ExceptionState&);
};

CORE_EXPORT v8::Local<v8::Value> ToV8(const UnsignedLongLongOrBooleanOrTestCallbackInterface&, v8::Local<v8::Object>, v8::Isolate*);

template <class CallbackInfo>
inline void V8SetReturnValue(const CallbackInfo& callbackInfo, UnsignedLongLongOrBooleanOrTestCallbackInterface& impl) {
  V8SetReturnValue(callbackInfo, ToV8(impl, callbackInfo.Holder(), callbackInfo.GetIsolate()));
}

template <class CallbackInfo>
inline void V8SetReturnValue(const CallbackInfo& callbackInfo, UnsignedLongLongOrBooleanOrTestCallbackInterface& impl, v8::Local<v8::Object> creationContext) {
  V8SetReturnValue(callbackInfo, ToV8(impl, creationContext, callbackInfo.GetIsolate()));
}

template <>
struct NativeValueTraits<UnsignedLongLongOrBooleanOrTestCallbackInterface> : public NativeValueTraitsBase<UnsignedLongLongOrBooleanOrTestCallbackInterface> {
  CORE_EXPORT static UnsignedLongLongOrBooleanOrTestCallbackInterface NativeValue(v8::Isolate*, v8::Local<v8::Value>, ExceptionState&);
  CORE_EXPORT static UnsignedLongLongOrBooleanOrTestCallbackInterface NullValue() { return UnsignedLongLongOrBooleanOrTestCallbackInterface(); }
};

template <>
struct V8TypeOf<UnsignedLongLongOrBooleanOrTestCallbackInterface> {
  typedef V8UnsignedLongLongOrBooleanOrTestCallbackInterface Type;
};

}  // namespace blink

// We need to set canInitializeWithMemset=true because HeapVector supports
// items that can initialize with memset or have a vtable. It is safe to
// set canInitializeWithMemset=true for a union type object in practice.
// See https://codereview.chromium.org/1118993002/#msg5 for more details.
WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::UnsignedLongLongOrBooleanOrTestCallbackInterface);

#endif  // UnsignedLongLongOrBooleanOrTestCallbackInterface_h
