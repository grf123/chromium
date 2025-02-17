/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/style/FilterOperation.h"

#include "core/svg/SVGElementProxy.h"
#include "platform/LengthFunctions.h"
#include "platform/animation/AnimationUtilities.h"
#include "platform/graphics/filters/FEDropShadow.h"
#include "platform/graphics/filters/FEGaussianBlur.h"
#include "platform/graphics/filters/Filter.h"
#include "platform/graphics/filters/FilterEffect.h"

namespace blink {

FilterOperation* FilterOperation::Blend(const FilterOperation* from,
                                        const FilterOperation* to,
                                        double progress) {
  DCHECK(from || to);
  if (to)
    return to->Blend(from, progress);
  return from->Blend(nullptr, 1 - progress);
}

void ReferenceFilterOperation::Trace(blink::Visitor* visitor) {
  visitor->Trace(element_proxy_);
  visitor->Trace(filter_);
  FilterOperation::Trace(visitor);
}

FloatRect ReferenceFilterOperation::MapRect(const FloatRect& rect) const {
  const auto* last_effect = filter_ ? filter_->LastEffect() : nullptr;
  if (!last_effect)
    return rect;
  return last_effect->MapRect(rect);
}

ReferenceFilterOperation::ReferenceFilterOperation(
    const String& url,
    SVGElementProxy& element_proxy)
    : FilterOperation(REFERENCE), url_(url), element_proxy_(&element_proxy) {}

void ReferenceFilterOperation::AddClient(
    SVGResourceClient* client,
    base::SingleThreadTaskRunner* task_runner) {
  element_proxy_->AddClient(client, task_runner);
}

void ReferenceFilterOperation::RemoveClient(SVGResourceClient* client) {
  element_proxy_->RemoveClient(client);
}

bool ReferenceFilterOperation::operator==(const FilterOperation& o) const {
  if (!IsSameType(o))
    return false;
  const ReferenceFilterOperation& other = ToReferenceFilterOperation(o);
  return url_ == other.url_ && element_proxy_ == other.element_proxy_;
}

FilterOperation* BasicColorMatrixFilterOperation::Blend(
    const FilterOperation* from,
    double progress) const {
  double from_amount;
  if (from) {
    SECURITY_DCHECK(from->IsSameType(*this));
    from_amount = ToBasicColorMatrixFilterOperation(from)->Amount();
  } else {
    switch (type_) {
      case GRAYSCALE:
      case SEPIA:
      case HUE_ROTATE:
        from_amount = 0;
        break;
      case SATURATE:
        from_amount = 1;
        break;
      default:
        from_amount = 0;
        NOTREACHED();
    }
  }

  double result = blink::Blend(from_amount, amount_, progress);
  switch (type_) {
    case HUE_ROTATE:
      break;
    case GRAYSCALE:
    case SEPIA:
      result = clampTo<double>(result, 0, 1);
      break;
    case SATURATE:
      result = clampTo<double>(result, 0);
      break;
    default:
      NOTREACHED();
  }
  return BasicColorMatrixFilterOperation::Create(result, type_);
}

FilterOperation* BasicComponentTransferFilterOperation::Blend(
    const FilterOperation* from,
    double progress) const {
  double from_amount;
  if (from) {
    SECURITY_DCHECK(from->IsSameType(*this));
    from_amount = ToBasicComponentTransferFilterOperation(from)->Amount();
  } else {
    switch (type_) {
      case OPACITY:
      case CONTRAST:
      case BRIGHTNESS:
        from_amount = 1;
        break;
      case INVERT:
        from_amount = 0;
        break;
      default:
        from_amount = 0;
        NOTREACHED();
    }
  }

  double result = blink::Blend(from_amount, amount_, progress);
  switch (type_) {
    case BRIGHTNESS:
    case CONTRAST:
      result = clampTo<double>(result, 0);
      break;
    case INVERT:
    case OPACITY:
      result = clampTo<double>(result, 0, 1);
      break;
    default:
      NOTREACHED();
  }
  return BasicComponentTransferFilterOperation::Create(result, type_);
}

FloatRect BlurFilterOperation::MapRect(const FloatRect& rect) const {
  float std_deviation = FloatValueForLength(std_deviation_, 0);
  return FEGaussianBlur::MapEffect(FloatSize(std_deviation, std_deviation),
                                   rect);
}

FilterOperation* BlurFilterOperation::Blend(const FilterOperation* from,
                                            double progress) const {
  LengthType length_type = std_deviation_.GetType();
  if (!from)
    return BlurFilterOperation::Create(std_deviation_.Blend(
        Length(length_type), progress, kValueRangeNonNegative));

  const BlurFilterOperation* from_op = ToBlurFilterOperation(from);
  return BlurFilterOperation::Create(std_deviation_.Blend(
      from_op->std_deviation_, progress, kValueRangeNonNegative));
}

FloatRect DropShadowFilterOperation::MapRect(const FloatRect& rect) const {
  float std_deviation = shadow_.Blur();
  return FEDropShadow::MapEffect(FloatSize(std_deviation, std_deviation),
                                 shadow_.Location(), rect);
}

FilterOperation* DropShadowFilterOperation::Blend(const FilterOperation* from,
                                                  double progress) const {
  if (!from) {
    return Create(shadow_.Blend(ShadowData::NeutralValue(), progress,
                                Color::kTransparent));
  }

  const auto& from_op = ToDropShadowFilterOperation(*from);
  return Create(shadow_.Blend(from_op.shadow_, progress, Color::kTransparent));
}

FloatRect BoxReflectFilterOperation::MapRect(const FloatRect& rect) const {
  return reflection_.MapRect(rect);
}

FilterOperation* BoxReflectFilterOperation::Blend(const FilterOperation* from,
                                                  double progress) const {
  NOTREACHED();
  return nullptr;
}

bool BoxReflectFilterOperation::operator==(const FilterOperation& o) const {
  if (!IsSameType(o))
    return false;
  const auto& other = static_cast<const BoxReflectFilterOperation&>(o);
  return reflection_ == other.reflection_;
}

}  // namespace blink
