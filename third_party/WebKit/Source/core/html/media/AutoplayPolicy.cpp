// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/html/media/AutoplayPolicy.h"

#include "core/dom/Document.h"
#include "core/dom/ElementVisibilityObserver.h"
#include "core/frame/ContentSettingsClient.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/html/media/AutoplayUmaHelper.h"
#include "core/html/media/HTMLMediaElement.h"
#include "core/inspector/ConsoleMessage.h"
#include "platform/network/NetworkStateNotifier.h"
#include "platform/runtime_enabled_features.h"
#include "platform/wtf/Assertions.h"
#include "public/platform/WebMediaPlayer.h"
#include "public/web/WebSettings.h"
#include "third_party/WebKit/common/feature_policy/feature_policy.mojom-blink.h"

namespace blink {

namespace {

const char kWarningUnmuteFailed[] =
    "Unmuting failed and the element was paused instead because the user "
    "didn't interact with the document before. https://goo.gl/xX8pDD";
const char kErrorAutoplayFuncUnified[] =
    "play() failed because the user didn't interact with the document first. "
    "https://goo.gl/xX8pDD";
const char kErrorAutoplayFuncMobile[] =
    "play() can only be initiated by a user gesture.";

bool IsDocumentCrossOrigin(const Document& document) {
  const LocalFrame* frame = document.GetFrame();
  return frame && frame->IsCrossOriginSubframe();
}

// Returns whether |document| is whitelisted for autoplay. If true, the user
// gesture lock will be initilized as false, indicating that the element is
// allowed to autoplay unmuted without user gesture.
bool IsDocumentWhitelisted(const Document& document) {
  DCHECK(document.GetSettings());

  const String& whitelist_scope =
      document.GetSettings()->GetMediaPlaybackGestureWhitelistScope();
  if (whitelist_scope.IsNull() || whitelist_scope.IsEmpty())
    return false;

  return document.Url().GetString().StartsWith(whitelist_scope);
}

// Return true if and only if the document settings specifies media playback
// requires user gesture on the element.
bool ComputeLockPendingUserGestureRequired(const Document& document) {
  switch (AutoplayPolicy::GetAutoplayPolicyForDocument(document)) {
    case AutoplayPolicy::Type::kNoUserGestureRequired:
      return false;
    case AutoplayPolicy::Type::kUserGestureRequiredForCrossOrigin:
      return IsDocumentCrossOrigin(document);
    case AutoplayPolicy::Type::kUserGestureRequired:
      return true;
    // kDocumentUserActivationRequired policy does not imply that a user gesture
    // is required on the element but instead requires a user gesture on the
    // document, therefore the element is not locked.
    case AutoplayPolicy::Type::kDocumentUserActivationRequired:
      return false;
  }

  NOTREACHED();
  return true;
}

// Return true if any frame between |frame| and the root has been activated in
// either the current or previous navigation. If the
// FeaturePolicyAutoplayFeature flag is disabled then it will stop at the
// current frame.
bool HasBeenActivated(const Frame& frame) {
  // Check if the current frame has received a user activation.
  if (frame.HasBeenActivated() ||
      frame.HasReceivedUserGestureBeforeNavigation()) {
    return true;
  }

  // Check feature policy before traversing the tree.
  if (!RuntimeEnabledFeatures::FeaturePolicyAutoplayFeatureEnabled() ||
      !frame.IsFeatureEnabled(mojom::FeaturePolicyFeature::kAutoplay)) {
    return false;
  }

  // If there is a parent check if the parent has received a
  // user gesture.
  const Frame* parent = frame.Tree().Parent();
  return parent && HasBeenActivated(*parent);
}

}  // anonymous namespace

// static
AutoplayPolicy::Type AutoplayPolicy::GetAutoplayPolicyForDocument(
    const Document& document) {
  if (!document.GetSettings())
    return Type::kNoUserGestureRequired;

  if (IsDocumentWhitelisted(document))
    return Type::kNoUserGestureRequired;

  if (RuntimeEnabledFeatures::MediaEngagementBypassAutoplayPoliciesEnabled() &&
      document.HasHighMediaEngagement()) {
    return Type::kNoUserGestureRequired;
  }

  return document.GetSettings()->GetAutoplayPolicy();
}

// static
bool AutoplayPolicy::IsDocumentAllowedToPlay(const Document& document) {
  if (!document.GetFrame())
    return false;

  return HasBeenActivated(*document.GetFrame());
}

AutoplayPolicy::AutoplayPolicy(HTMLMediaElement* element)
    : locked_pending_user_gesture_(false),
      locked_pending_user_gesture_if_cross_origin_experiment_enabled_(true),
      element_(element),
      autoplay_visibility_observer_(nullptr),
      autoplay_uma_helper_(AutoplayUmaHelper::Create(element)) {
  locked_pending_user_gesture_ =
      ComputeLockPendingUserGestureRequired(element->GetDocument());
  locked_pending_user_gesture_if_cross_origin_experiment_enabled_ =
      IsDocumentCrossOrigin(element->GetDocument());
}

void AutoplayPolicy::VideoWillBeDrawnToCanvas() const {
  autoplay_uma_helper_->VideoWillBeDrawnToCanvas();
}

void AutoplayPolicy::DidMoveToNewDocument(Document& old_document) {
  // If any experiment is enabled, then we want to enable a user gesture by
  // default, otherwise the experiment does nothing.
  bool old_document_requires_user_gesture =
      ComputeLockPendingUserGestureRequired(old_document);
  bool new_document_requires_user_gesture =
      ComputeLockPendingUserGestureRequired(element_->GetDocument());
  if (new_document_requires_user_gesture && !old_document_requires_user_gesture)
    locked_pending_user_gesture_ = true;

  if (IsDocumentCrossOrigin(element_->GetDocument()) &&
      !IsDocumentCrossOrigin(old_document))
    locked_pending_user_gesture_if_cross_origin_experiment_enabled_ = true;

  autoplay_uma_helper_->DidMoveToNewDocument(old_document);
}

bool AutoplayPolicy::IsEligibleForAutoplayMuted() const {
  return element_->IsHTMLVideoElement() && element_->muted() &&
         RuntimeEnabledFeatures::AutoplayMutedVideosEnabled();
}

void AutoplayPolicy::StartAutoplayMutedWhenVisible() {
  // We might end up in a situation where the previous
  // observer didn't had time to fire yet. We can avoid
  // creating a new one in this case.
  if (autoplay_visibility_observer_)
    return;

  autoplay_visibility_observer_ = new ElementVisibilityObserver(
      element_,
      WTF::BindRepeating(&AutoplayPolicy::OnVisibilityChangedForAutoplay,
                         WrapWeakPersistent(this)));
  autoplay_visibility_observer_->Start();
}

void AutoplayPolicy::StopAutoplayMutedWhenVisible() {
  if (!autoplay_visibility_observer_)
    return;

  autoplay_visibility_observer_->Stop();
  autoplay_visibility_observer_ = nullptr;
}

bool AutoplayPolicy::RequestAutoplayUnmute() {
  DCHECK(!element_->muted());
  bool was_autoplaying_muted = IsAutoplayingMutedInternal(true);

  TryUnlockingUserGesture();

  if (was_autoplaying_muted) {
    if (IsGestureNeededForPlayback()) {
      if (IsUsingDocumentUserActivationRequiredPolicy()) {
        element_->GetDocument().AddConsoleMessage(ConsoleMessage::Create(
            kJSMessageSource, kWarningMessageLevel, kWarningUnmuteFailed));
      }

      autoplay_uma_helper_->RecordAutoplayUnmuteStatus(
          AutoplayUnmuteActionStatus::kFailure);
      return false;
    }
    autoplay_uma_helper_->RecordAutoplayUnmuteStatus(
        AutoplayUnmuteActionStatus::kSuccess);
  }
  return true;
}

bool AutoplayPolicy::RequestAutoplayByAttribute() {
  if (!ShouldAutoplay())
    return false;

  autoplay_uma_helper_->OnAutoplayInitiated(AutoplaySource::kAttribute);

  if (IsGestureNeededForPlayback()) {
    autoplay_uma_helper_->RecordCrossOriginAutoplayResult(
        CrossOriginAutoplayResult::kAutoplayBlocked);
    return false;
  }

  if (IsGestureNeededForPlaybackIfCrossOriginExperimentEnabled()) {
    autoplay_uma_helper_->RecordCrossOriginAutoplayResult(
        CrossOriginAutoplayResult::kAutoplayBlocked);
  } else {
    autoplay_uma_helper_->RecordCrossOriginAutoplayResult(
        CrossOriginAutoplayResult::kAutoplayAllowed);
  }

  // At this point the gesture is not needed for playback per the if statement
  // above.
  if (!IsEligibleForAutoplayMuted())
    return true;

  // Autoplay muted video should be handled by AutoplayPolicy based on the
  // visibily.
  StartAutoplayMutedWhenVisible();
  return false;
}

Optional<ExceptionCode> AutoplayPolicy::RequestPlay() {
  if (!Frame::HasTransientUserActivation(element_->GetDocument().GetFrame())) {
    autoplay_uma_helper_->OnAutoplayInitiated(AutoplaySource::kMethod);
    if (IsGestureNeededForPlayback()) {
      autoplay_uma_helper_->RecordCrossOriginAutoplayResult(
          CrossOriginAutoplayResult::kAutoplayBlocked);
      return kNotAllowedError;
    }

    if (IsGestureNeededForPlaybackIfCrossOriginExperimentEnabled()) {
      autoplay_uma_helper_->RecordCrossOriginAutoplayResult(
          CrossOriginAutoplayResult::kAutoplayBlocked);
    } else {
      autoplay_uma_helper_->RecordCrossOriginAutoplayResult(
          CrossOriginAutoplayResult::kAutoplayAllowed);
    }
  } else {
    autoplay_uma_helper_->RecordCrossOriginAutoplayResult(
        CrossOriginAutoplayResult::kPlayedWithGesture);
    TryUnlockingUserGesture();
  }

  return WTF::nullopt;
}

bool AutoplayPolicy::IsAutoplayingMuted() const {
  return IsAutoplayingMutedInternal(element_->muted());
}

bool AutoplayPolicy::IsAutoplayingMutedInternal(bool muted) const {
  return !element_->paused() && IsOrWillBeAutoplayingMutedInternal(muted);
}

bool AutoplayPolicy::IsOrWillBeAutoplayingMuted() const {
  return IsOrWillBeAutoplayingMutedInternal(element_->muted());
}

bool AutoplayPolicy::IsOrWillBeAutoplayingMutedInternal(bool muted) const {
  if (!element_->IsHTMLVideoElement() ||
      !RuntimeEnabledFeatures::AutoplayMutedVideosEnabled()) {
    return false;
  }

  return muted && IsLockedPendingUserGesture();
}

bool AutoplayPolicy::IsLockedPendingUserGesture() const {
  if (IsUsingDocumentUserActivationRequiredPolicy())
    return !IsDocumentAllowedToPlay(element_->GetDocument());

  return locked_pending_user_gesture_;
}

void AutoplayPolicy::TryUnlockingUserGesture() {
  if (IsLockedPendingUserGesture() &&
      Frame::HasTransientUserActivation(element_->GetDocument().GetFrame())) {
    UnlockUserGesture();
  }
}

void AutoplayPolicy::UnlockUserGesture() {
  locked_pending_user_gesture_ = false;
  locked_pending_user_gesture_if_cross_origin_experiment_enabled_ = false;
}

bool AutoplayPolicy::IsGestureNeededForPlayback() const {
  if (!IsLockedPendingUserGesture())
    return false;

  return IsGestureNeededForPlaybackIfPendingUserGestureIsLocked();
}

String AutoplayPolicy::GetPlayErrorMessage() const {
  return IsUsingDocumentUserActivationRequiredPolicy()
             ? kErrorAutoplayFuncUnified
             : kErrorAutoplayFuncMobile;
}

bool AutoplayPolicy::IsGestureNeededForPlaybackIfPendingUserGestureIsLocked()
    const {
  if (element_->GetLoadType() == WebMediaPlayer::kLoadTypeMediaStream)
    return false;

  // We want to allow muted video to autoplay if:
  // - the flag is enabled;
  // - Data Saver is not enabled;
  // - Preload was not disabled (low end devices);
  // - Autoplay is enabled in settings;
  if (element_->IsHTMLVideoElement() && element_->muted() &&
      RuntimeEnabledFeatures::AutoplayMutedVideosEnabled() &&
      !(element_->GetDocument().GetSettings() &&
        GetNetworkStateNotifier().SaveDataEnabled()) &&
      !(element_->GetDocument().GetSettings() &&
        element_->GetDocument()
            .GetSettings()
            ->GetForcePreloadNoneForMediaElements()) &&
      IsAutoplayAllowedPerSettings()) {
    return false;
  }

  return true;
}

void AutoplayPolicy::OnVisibilityChangedForAutoplay(bool is_visible) {
  if (!is_visible) {
    if (element_->can_autoplay_ && element_->Autoplay()) {
      element_->PauseInternal();
      element_->can_autoplay_ = true;
    }
    return;
  }

  if (ShouldAutoplay()) {
    element_->paused_ = false;
    element_->ScheduleEvent(EventTypeNames::play);
    element_->ScheduleNotifyPlaying();

    element_->UpdatePlayState();
  }
}

bool AutoplayPolicy::IsUsingDocumentUserActivationRequiredPolicy() const {
  return GetAutoplayPolicyForDocument(element_->GetDocument()) ==
         AutoplayPolicy::Type::kDocumentUserActivationRequired;
}

bool AutoplayPolicy::IsGestureNeededForPlaybackIfCrossOriginExperimentEnabled()
    const {
  if (!locked_pending_user_gesture_if_cross_origin_experiment_enabled_)
    return false;

  return IsGestureNeededForPlaybackIfPendingUserGestureIsLocked();
}

bool AutoplayPolicy::IsAutoplayAllowedPerSettings() const {
  LocalFrame* frame = element_->GetDocument().GetFrame();
  if (!frame)
    return false;
  return frame->GetContentSettingsClient()->AllowAutoplay(true);
}

bool AutoplayPolicy::ShouldAutoplay() {
  if (element_->GetDocument().IsSandboxed(kSandboxAutomaticFeatures))
    return false;
  return element_->can_autoplay_ && element_->paused_ && element_->Autoplay();
}

void AutoplayPolicy::Trace(blink::Visitor* visitor) {
  visitor->Trace(element_);
  visitor->Trace(autoplay_visibility_observer_);
  visitor->Trace(autoplay_uma_helper_);
}

STATIC_ASSERT_ENUM(WebSettings::AutoplayPolicy::kNoUserGestureRequired,
                   AutoplayPolicy::Type::kNoUserGestureRequired);
STATIC_ASSERT_ENUM(WebSettings::AutoplayPolicy::kUserGestureRequired,
                   AutoplayPolicy::Type::kUserGestureRequired);
STATIC_ASSERT_ENUM(
    WebSettings::AutoplayPolicy::kUserGestureRequiredForCrossOrigin,
    AutoplayPolicy::Type::kUserGestureRequiredForCrossOrigin);
STATIC_ASSERT_ENUM(WebSettings::AutoplayPolicy::kDocumentUserActivationRequired,
                   AutoplayPolicy::Type::kDocumentUserActivationRequired);

}  // namespace blink
