// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/WebKit/common/feature_policy/feature_policy.h"

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"

namespace blink {
namespace {

// Extracts a Whitelist from a ParsedFeaturePolicyDeclaration.
std::unique_ptr<FeaturePolicy::Whitelist> WhitelistFromDeclaration(
    const ParsedFeaturePolicyDeclaration& parsed_declaration) {
  std::unique_ptr<FeaturePolicy::Whitelist> result =
      base::WrapUnique(new FeaturePolicy::Whitelist());
  if (parsed_declaration.matches_all_origins)
    result->AddAll();
  for (const auto& origin : parsed_declaration.origins)
    result->Add(origin);
  return result;
}

}  // namespace

ParsedFeaturePolicyDeclaration::ParsedFeaturePolicyDeclaration()
    : matches_all_origins(false) {}

ParsedFeaturePolicyDeclaration::ParsedFeaturePolicyDeclaration(
    mojom::FeaturePolicyFeature feature,
    bool matches_all_origins,
    std::vector<url::Origin> origins)
    : feature(feature),
      matches_all_origins(matches_all_origins),
      origins(origins) {}

ParsedFeaturePolicyDeclaration::ParsedFeaturePolicyDeclaration(
    const ParsedFeaturePolicyDeclaration& rhs) = default;

ParsedFeaturePolicyDeclaration& ParsedFeaturePolicyDeclaration::operator=(
    const ParsedFeaturePolicyDeclaration& rhs) = default;

ParsedFeaturePolicyDeclaration::~ParsedFeaturePolicyDeclaration() = default;

bool operator==(const ParsedFeaturePolicyDeclaration& lhs,
                const ParsedFeaturePolicyDeclaration& rhs) {
  // This method returns true only when the arguments are actually identical,
  // including the order of elements in the origins vector.
  // TODO(iclelland): Consider making this return true when comparing equal-
  // but-not-identical whitelists, or eliminate those comparisons by maintaining
  // the whiteslists in a normalized form.
  // https://crbug.com/710324
  return std::tie(lhs.feature, lhs.matches_all_origins, lhs.origins) ==
         std::tie(rhs.feature, rhs.matches_all_origins, rhs.origins);
}

FeaturePolicy::Whitelist::Whitelist() : matches_all_origins_(false) {}

FeaturePolicy::Whitelist::Whitelist(const Whitelist& rhs) = default;

FeaturePolicy::Whitelist::~Whitelist() = default;

void FeaturePolicy::Whitelist::Add(const url::Origin& origin) {
  origins_.push_back(origin);
}

void FeaturePolicy::Whitelist::AddAll() {
  matches_all_origins_ = true;
}

bool FeaturePolicy::Whitelist::Contains(const url::Origin& origin) const {
  // This does not handle the case where origin is an opaque origin, which is
  // also supposed to exist in the whitelist. (The identical opaque origins
  // should match in that case)
  // TODO(iclelland): Fix that, possibly by having another flag for
  // 'matches_self', which will explicitly match the policy's origin.
  // https://crbug.com/690520
  if (matches_all_origins_)
    return true;
  for (const auto& targetOrigin : origins_) {
    if (targetOrigin.IsSameOriginWith(origin))
      return true;
  }
  return false;
}

bool FeaturePolicy::Whitelist::MatchesAll() const {
  return matches_all_origins_;
}

const std::vector<url::Origin>& FeaturePolicy::Whitelist::Origins() const {
  return origins_;
}

// static
std::unique_ptr<FeaturePolicy> FeaturePolicy::CreateFromParentPolicy(
    const FeaturePolicy* parent_policy,
    const ParsedFeaturePolicy& container_policy,
    const url::Origin& origin) {
  return CreateFromParentPolicy(parent_policy, container_policy, origin,
                                GetDefaultFeatureList());
}

// static
std::unique_ptr<FeaturePolicy> FeaturePolicy::CreateFromPolicyWithOrigin(
    const FeaturePolicy& policy,
    const url::Origin& origin) {
  std::unique_ptr<FeaturePolicy> new_policy =
      base::WrapUnique(new FeaturePolicy(origin, policy.feature_list_));
  new_policy->inherited_policies_ = policy.inherited_policies_;
  for (const auto& feature : policy.whitelists_) {
    new_policy->whitelists_[feature.first] =
        base::WrapUnique(new Whitelist(*feature.second));
  }
  return new_policy;
}

bool FeaturePolicy::IsFeatureEnabled(
    mojom::FeaturePolicyFeature feature) const {
  return IsFeatureEnabledForOrigin(feature, origin_);
}

bool FeaturePolicy::IsFeatureEnabledForOrigin(
    mojom::FeaturePolicyFeature feature,
    const url::Origin& origin) const {
  DCHECK(base::ContainsKey(feature_list_, feature));
  DCHECK(base::ContainsKey(inherited_policies_, feature));
  if (!inherited_policies_.at(feature))
    return false;
  auto whitelist = whitelists_.find(feature);
  if (whitelist != whitelists_.end())
    return whitelist->second->Contains(origin);

  const FeaturePolicy::FeatureDefault default_policy =
      feature_list_.at(feature);
  if (default_policy == FeaturePolicy::FeatureDefault::EnableForAll)
    return true;
  if (default_policy == FeaturePolicy::FeatureDefault::EnableForSelf) {
    // TODO(iclelland): Remove the pointer equality check once it is possible to
    // compare opaque origins successfully against themselves.
    // https://crbug.com/690520
    return (&origin_ == &origin) || origin_.IsSameOriginWith(origin);
  }
  return false;
}

const FeaturePolicy::Whitelist FeaturePolicy::GetWhitelistForFeature(
    mojom::FeaturePolicyFeature feature) const {
  DCHECK(base::ContainsKey(feature_list_, feature));
  DCHECK(base::ContainsKey(inherited_policies_, feature));
  // Disabled through inheritance.
  if (!inherited_policies_.at(feature))
    return FeaturePolicy::Whitelist();

  // Return defined policy if exists; otherwise return default policy.
  auto whitelist = whitelists_.find(feature);
  if (whitelist != whitelists_.end())
    return FeaturePolicy::Whitelist(*(whitelist->second));

  const FeaturePolicy::FeatureDefault default_policy =
      feature_list_.at(feature);
  FeaturePolicy::Whitelist default_whitelist;

  if (default_policy == FeaturePolicy::FeatureDefault::EnableForAll)
    default_whitelist.AddAll();
  else if (default_policy == FeaturePolicy::FeatureDefault::EnableForSelf)
    default_whitelist.Add(origin_);
  return default_whitelist;
}

void FeaturePolicy::SetHeaderPolicy(const ParsedFeaturePolicy& parsed_header) {
  DCHECK(whitelists_.empty());
  for (const ParsedFeaturePolicyDeclaration& parsed_declaration :
       parsed_header) {
    mojom::FeaturePolicyFeature feature = parsed_declaration.feature;
    DCHECK(feature != mojom::FeaturePolicyFeature::kNotFound);
    whitelists_[feature] = WhitelistFromDeclaration(parsed_declaration);
  }
}

FeaturePolicy::FeaturePolicy(url::Origin origin,
                             const FeatureList& feature_list)
    : origin_(origin), feature_list_(feature_list) {}

FeaturePolicy::FeaturePolicy(url::Origin origin)
    : origin_(origin), feature_list_(GetDefaultFeatureList()) {}

FeaturePolicy::~FeaturePolicy() = default;

// static
std::unique_ptr<FeaturePolicy> FeaturePolicy::CreateFromParentPolicy(
    const FeaturePolicy* parent_policy,
    const ParsedFeaturePolicy& container_policy,
    const url::Origin& origin,
    const FeaturePolicy::FeatureList& features) {
  // If there is a non-empty container policy, then there must also be a parent
  // policy.
  DCHECK(parent_policy || container_policy.empty());

  std::unique_ptr<FeaturePolicy> new_policy =
      base::WrapUnique(new FeaturePolicy(origin, features));
  for (const auto& feature : features) {
    if (!parent_policy ||
        parent_policy->IsFeatureEnabledForOrigin(feature.first, origin)) {
      new_policy->inherited_policies_[feature.first] = true;
    } else {
      new_policy->inherited_policies_[feature.first] = false;
    }
  }
  if (!container_policy.empty())
    new_policy->AddContainerPolicy(container_policy, parent_policy);
  return new_policy;
}

void FeaturePolicy::AddContainerPolicy(
    const ParsedFeaturePolicy& container_policy,
    const FeaturePolicy* parent_policy) {
  DCHECK(parent_policy);
  for (const ParsedFeaturePolicyDeclaration& parsed_declaration :
       container_policy) {
    // If a feature is enabled in the parent frame, and the parent chooses to
    // delegate it to the child frame, using the iframe attribute, then the
    // feature should be enabled in the child frame.
    mojom::FeaturePolicyFeature feature = parsed_declaration.feature;
    if (feature == mojom::FeaturePolicyFeature::kNotFound)
      continue;
    if (WhitelistFromDeclaration(parsed_declaration)->Contains(origin_) &&
        parent_policy->IsFeatureEnabled(feature)) {
      inherited_policies_[feature] = true;
    } else {
      inherited_policies_[feature] = false;
    }
  }
}

// static
// See third_party/WebKit/common/feature_policy/feature_policy.h for status of
// each feature (in spec, implemented, etc).
const FeaturePolicy::FeatureList& FeaturePolicy::GetDefaultFeatureList() {
  CR_DEFINE_STATIC_LOCAL(FeatureList, default_feature_list,
                         ({{mojom::FeaturePolicyFeature::kAutoplay,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kCamera,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kEncryptedMedia,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kFullscreen,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kGeolocation,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kMicrophone,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kMidiFeature,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kPayment,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kSpeaker,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kVibrate,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kDocumentCookie,
                            FeaturePolicy::FeatureDefault::EnableForAll},
                           {mojom::FeaturePolicyFeature::kDocumentDomain,
                            FeaturePolicy::FeatureDefault::EnableForAll},
                           {mojom::FeaturePolicyFeature::kDocumentWrite,
                            FeaturePolicy::FeatureDefault::EnableForAll},
                           {mojom::FeaturePolicyFeature::kSyncScript,
                            FeaturePolicy::FeatureDefault::EnableForAll},
                           {mojom::FeaturePolicyFeature::kSyncXHR,
                            FeaturePolicy::FeatureDefault::EnableForAll},
                           {mojom::FeaturePolicyFeature::kUsb,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kWebVr,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kAccelerometer,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kAmbientLightSensor,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kGyroscope,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kMagnetometer,
                            FeaturePolicy::FeatureDefault::EnableForSelf},
                           {mojom::FeaturePolicyFeature::kUnsizedMedia,
                            FeaturePolicy::FeatureDefault::EnableForAll},
                           {mojom::FeaturePolicyFeature::kPictureInPicture,
                            FeaturePolicy::FeatureDefault::EnableForSelf}}));
  return default_feature_list;
}

}  // namespace blink
