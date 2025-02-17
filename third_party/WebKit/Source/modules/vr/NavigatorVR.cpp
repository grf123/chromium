// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/vr/NavigatorVR.h"

#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "core/dom/DOMException.h"
#include "core/dom/Document.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/ExecutionContext.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Navigator.h"
#include "core/frame/UseCounter.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/page/Page.h"
#include "modules/vr/VRController.h"
#include "modules/vr/VRDisplay.h"
#include "modules/vr/VRGetDevicesCallback.h"
#include "modules/vr/VRPose.h"
#include "modules/xr/XR.h"
#include "platform/feature_policy/FeaturePolicy.h"
#include "public/platform/Platform.h"
#include "services/metrics/public/cpp/ukm_builders.h"

namespace blink {

namespace {

const char kFeaturePolicyBlockedMessage[] =
    "Access to the feature \"vr\" is disallowed by feature policy.";

const char kGetVRDisplaysCrossOriginBlockedMessage[] =
    "Access to navigator.getVRDisplays requires a user gesture in cross-origin "
    "embedded frames.";

const char kNotAssociatedWithDocumentMessage[] =
    "The object is no longer associated with a document.";

const char kCannotUseBothNewAndOldAPIMessage[] =
    "Cannot use navigator.getVRDisplays if the XR API is already in "
    "use.";

}  // namespace

NavigatorVR* NavigatorVR::From(Document& document) {
  if (!document.GetFrame() || !document.GetFrame()->DomWindow())
    return nullptr;
  Navigator& navigator = *document.GetFrame()->DomWindow()->navigator();
  return &From(navigator);
}

NavigatorVR& NavigatorVR::From(Navigator& navigator) {
  NavigatorVR* supplement = static_cast<NavigatorVR*>(
      Supplement<Navigator>::From(navigator, SupplementName()));
  if (!supplement) {
    supplement = new NavigatorVR(navigator);
    ProvideTo(navigator, SupplementName(), supplement);
  }
  return *supplement;
}

XR* NavigatorVR::xr(Navigator& navigator) {
  // Always return null when the navigator is detached.
  if (!navigator.GetFrame())
    return nullptr;

  return NavigatorVR::From(navigator).xr();
}

XR* NavigatorVR::xr() {
  LocalFrame* frame = GetSupplementable()->GetFrame();
  // Always return null when the navigator is detached.
  if (!frame)
    return nullptr;

  if (!xr_) {
    // For the sake of simplicity we're going to block developers from using the
    // new API if they've already made calls to the legacy API.
    if (controller_) {
      if (frame->GetDocument()) {
        frame->GetDocument()->AddConsoleMessage(ConsoleMessage::Create(
            kOtherMessageSource, kErrorMessageLevel,
            "Cannot use navigator.xr if the legacy VR API is already in use."));
      }
      return nullptr;
    }

    xr_ = XR::Create(*frame);
  }
  return xr_;
}

ScriptPromise NavigatorVR::getVRDisplays(ScriptState* script_state,
                                         Navigator& navigator) {
  if (!navigator.GetFrame()) {
    return ScriptPromise::RejectWithDOMException(
        script_state, DOMException::Create(kInvalidStateError,
                                           kNotAssociatedWithDocumentMessage));
  }
  return NavigatorVR::From(navigator).getVRDisplays(script_state);
}

ScriptPromise NavigatorVR::getVRDisplays(ScriptState* script_state) {
  if (!GetDocument()) {
    return ScriptPromise::RejectWithDOMException(
        script_state, DOMException::Create(kInvalidStateError,
                                           kNotAssociatedWithDocumentMessage));
  }

  if (!did_log_getVRDisplays_ && GetDocument()->IsInMainFrame()) {
    did_log_getVRDisplays_ = true;

    ukm::builders::XR_WebXR(GetDocument()->UkmSourceID())
        .SetDidRequestAvailableDevices(1)
        .Record(GetDocument()->UkmRecorder());
  }

  LocalFrame* frame = GetDocument()->GetFrame();
  if (!frame) {
    return ScriptPromise::RejectWithDOMException(
        script_state, DOMException::Create(kInvalidStateError,
                                           kNotAssociatedWithDocumentMessage));
  }
  if (IsSupportedInFeaturePolicy(mojom::FeaturePolicyFeature::kWebVr)) {
    if (!frame->IsFeatureEnabled(mojom::FeaturePolicyFeature::kWebVr)) {
      return ScriptPromise::RejectWithDOMException(
          script_state,
          DOMException::Create(kSecurityError, kFeaturePolicyBlockedMessage));
    }
  } else if (!frame->HasBeenActivated() && frame->IsCrossOriginSubframe()) {
    // Before we introduced feature policy, cross-origin iframes had access to
    // WebVR APIs. Ideally, we want to block access to WebVR APIs for
    // cross-origin iframes. To be backward compatible, we changed to require a
    // user gesture for cross-origin iframes.
    return ScriptPromise::RejectWithDOMException(
        script_state,
        DOMException::Create(kSecurityError,
                             kGetVRDisplaysCrossOriginBlockedMessage));
  }

  // Similar to the restriciton above, we're going to block developers from
  // using the legacy API if they've already made calls to the new API.
  if (xr_) {
    return ScriptPromise::RejectWithDOMException(
        script_state, DOMException::Create(kInvalidStateError,
                                           kCannotUseBothNewAndOldAPIMessage));
  }

  UseCounter::Count(*GetDocument(), WebFeature::kVRGetDisplays);
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  if (!execution_context->IsSecureContext())
    UseCounter::Count(*GetDocument(), WebFeature::kVRGetDisplaysInsecureOrigin);

  Platform::Current()->RecordRapporURL("VR.WebVR.GetDisplays",
                                       GetDocument()->Url());

  ScriptPromiseResolver* resolver = ScriptPromiseResolver::Create(script_state);
  ScriptPromise promise = resolver->Promise();
  Controller()->GetDisplays(resolver);

  return promise;
}

VRController* NavigatorVR::Controller() {
  if (!GetSupplementable()->GetFrame())
    return nullptr;

  if (!controller_) {
    controller_ = new VRController(this);
    controller_->SetListeningForActivate(focused_ && listening_for_activate_);
    controller_->FocusChanged();
  }

  return controller_;
}

Document* NavigatorVR::GetDocument() {
  if (!GetSupplementable() || !GetSupplementable()->GetFrame())
    return nullptr;

  return GetSupplementable()->GetFrame()->GetDocument();
}

void NavigatorVR::Trace(blink::Visitor* visitor) {
  visitor->Trace(xr_);
  visitor->Trace(controller_);
  Supplement<Navigator>::Trace(visitor);
}

NavigatorVR::NavigatorVR(Navigator& navigator)
    : Supplement<Navigator>(navigator),
      FocusChangedObserver(navigator.GetFrame()->GetPage()) {
  navigator.GetFrame()->DomWindow()->RegisterEventListenerObserver(this);
  FocusedFrameChanged();
}

NavigatorVR::~NavigatorVR() = default;

const char* NavigatorVR::SupplementName() {
  return "NavigatorVR";
}

void NavigatorVR::EnqueueVREvent(VRDisplayEvent* event) {
  if (!GetSupplementable()->GetFrame())
    return;

  GetSupplementable()->GetFrame()->DomWindow()->EnqueueWindowEvent(event);
}

void NavigatorVR::DispatchVREvent(VRDisplayEvent* event) {
  if (!(GetSupplementable()->GetFrame()))
    return;

  LocalDOMWindow* window = GetSupplementable()->GetFrame()->DomWindow();
  DCHECK(window);
  event->SetTarget(window);
  window->DispatchEvent(event);
}

void NavigatorVR::FocusedFrameChanged() {
  bool focused = IsFrameFocused(GetSupplementable()->GetFrame());
  if (focused == focused_)
    return;
  focused_ = focused;
  if (controller_) {
    controller_->SetListeningForActivate(listening_for_activate_ && focused);
    controller_->FocusChanged();
  }
}

void NavigatorVR::DidAddEventListener(LocalDOMWindow* window,
                                      const AtomicString& event_type) {
  // Don't bother if we're using the newer API
  if (xr_)
    return;

  if (event_type == EventTypeNames::vrdisplayactivate) {
    listening_for_activate_ = true;
    Controller()->SetListeningForActivate(focused_);
  } else if (event_type == EventTypeNames::vrdisplayconnect) {
    // If the page is listening for connection events make sure we've created a
    // controller so that we'll be notified of new devices.
    Controller();
  }
}

void NavigatorVR::DidRemoveEventListener(LocalDOMWindow* window,
                                         const AtomicString& event_type) {
  // Don't bother if we're using the newer API
  if (xr_)
    return;

  if (event_type == EventTypeNames::vrdisplayactivate &&
      !window->HasEventListeners(EventTypeNames::vrdisplayactivate)) {
    listening_for_activate_ = false;
    Controller()->SetListeningForActivate(false);
  }
}

void NavigatorVR::DidRemoveAllEventListeners(LocalDOMWindow* window) {
  if (xr_ || !controller_)
    return;

  controller_->SetListeningForActivate(false);
  listening_for_activate_ = false;
}

}  // namespace blink
