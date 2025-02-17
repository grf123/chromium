/*
 * Copyright (C) 2013 Samsung Electronics. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/svg/SVGGeometryElement.h"

#include "core/layout/HitTestRequest.h"
#include "core/layout/PointerEventsHitRules.h"
#include "core/layout/svg/LayoutSVGPath.h"
#include "core/layout/svg/LayoutSVGShape.h"
#include "core/svg/SVGPointTearOff.h"
#include "core/svg_names.h"

namespace blink {

class SVGAnimatedPathLength final : public SVGAnimatedNumber {
 public:
  static SVGAnimatedPathLength* Create(SVGGeometryElement* context_element) {
    return new SVGAnimatedPathLength(context_element);
  }

  SVGParsingError SetBaseValueAsString(const String& value) override {
    SVGParsingError parse_status =
        SVGAnimatedNumber::SetBaseValueAsString(value);
    if (parse_status == SVGParseStatus::kNoError && BaseValue()->Value() < 0)
      parse_status = SVGParseStatus::kNegativeValue;
    return parse_status;
  }

 private:
  explicit SVGAnimatedPathLength(SVGGeometryElement* context_element)
      : SVGAnimatedNumber(context_element,
                          SVGNames::pathLengthAttr,
                          SVGNumber::Create()) {}
};

SVGGeometryElement::SVGGeometryElement(const QualifiedName& tag_name,
                                       Document& document,
                                       ConstructionType construction_type)
    : SVGGraphicsElement(tag_name, document, construction_type),
      path_length_(SVGAnimatedPathLength::Create(this)) {
  AddToPropertyMap(path_length_);
}

void SVGGeometryElement::Trace(blink::Visitor* visitor) {
  visitor->Trace(path_length_);
  SVGGraphicsElement::Trace(visitor);
}

bool SVGGeometryElement::isPointInFill(SVGPointTearOff* point) const {
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  // FIXME: Eventually we should support isPointInFill for display:none
  // elements.
  if (!GetLayoutObject() || !GetLayoutObject()->IsSVGShape())
    return false;

  HitTestRequest request(HitTestRequest::kReadOnly);
  PointerEventsHitRules hit_rules(
      PointerEventsHitRules::SVG_GEOMETRY_HITTESTING, request,
      GetLayoutObject()->Style()->PointerEvents());
  hit_rules.can_hit_stroke = false;
  return ToLayoutSVGShape(GetLayoutObject())
      ->NodeAtFloatPointInternal(request, point->Target()->Value(), hit_rules);
}

bool SVGGeometryElement::isPointInStroke(SVGPointTearOff* point) const {
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  // FIXME: Eventually we should support isPointInStroke for display:none
  // elements.
  if (!GetLayoutObject() || !GetLayoutObject()->IsSVGShape())
    return false;

  HitTestRequest request(HitTestRequest::kReadOnly);
  PointerEventsHitRules hit_rules(
      PointerEventsHitRules::SVG_GEOMETRY_HITTESTING, request,
      GetLayoutObject()->Style()->PointerEvents());
  hit_rules.can_hit_fill = false;
  return ToLayoutSVGShape(GetLayoutObject())
      ->NodeAtFloatPointInternal(request, point->Target()->Value(), hit_rules);
}

Path SVGGeometryElement::ToClipPath() const {
  Path path = AsPath();
  path.Transform(CalculateTransform(SVGElement::kIncludeMotionTransform));

  DCHECK(GetLayoutObject());
  DCHECK(GetLayoutObject()->Style());
  path.SetWindRule(GetLayoutObject()->Style()->SvgStyle().ClipRule());
  return path;
}

float SVGGeometryElement::getTotalLength() {
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  if (!GetLayoutObject())
    return 0;
  return AsPath().length();
}

SVGPointTearOff* SVGGeometryElement::getPointAtLength(float length) {
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  FloatPoint point;
  if (GetLayoutObject())
    point = AsPath().PointAtLength(length);
  return SVGPointTearOff::CreateDetached(point);
}

float SVGGeometryElement::ComputePathLength() const {
  return AsPath().length();
}

float SVGGeometryElement::AuthorPathLength() const {
  if (!pathLength()->IsSpecified())
    return std::numeric_limits<float>::quiet_NaN();
  float author_path_length = pathLength()->CurrentValue()->Value();
  // https://svgwg.org/svg2-draft/paths.html#PathLengthAttribute
  // "A negative value is an error"
  if (author_path_length < 0)
    return std::numeric_limits<float>::quiet_NaN();
  return author_path_length;
}

float SVGGeometryElement::PathLengthScaleFactor() const {
  float author_path_length = AuthorPathLength();
  if (std::isnan(author_path_length))
    return 1;
  DCHECK(GetLayoutObject());
  return PathLengthScaleFactor(ComputePathLength(), author_path_length);
}

float SVGGeometryElement::PathLengthScaleFactor(float computed_path_length,
                                                float author_path_length) {
  DCHECK(!std::isnan(author_path_length));
  // If the computed path length is zero, then the scale factor will
  // always be zero except if the author path length is also zero - in
  // which case performing the division would yield a NaN. Avoid the
  // division in this case and always return zero.
  if (!computed_path_length)
    return 0;
  // "A value of zero is valid and must be treated as a scaling factor
  //  of infinity. A value of zero scaled infinitely must remain zero,
  //  while any value greater than zero must become +Infinity."
  // However, since 0 * Infinity is not zero (but rather NaN) per
  // IEEE, we need to make sure to clamp the result below - avoiding
  // the actual Infinity (and using max()) instead.
  return clampTo<float>(computed_path_length / author_path_length);
}

LayoutObject* SVGGeometryElement::CreateLayoutObject(const ComputedStyle&) {
  // By default, any subclass is expected to do path-based drawing.
  return new LayoutSVGPath(this);
}

}  // namespace blink
