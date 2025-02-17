// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/page/scrolling/SnapCoordinator.h"

#include "core/dom/Element.h"
#include "core/dom/Node.h"
#include "core/layout/LayoutBlock.h"
#include "core/layout/LayoutBox.h"
#include "core/layout/LayoutView.h"
#include "core/paint/PaintLayerScrollableArea.h"
#include "platform/LengthFunctions.h"
#include "platform/scroll/ScrollSnapData.h"

namespace blink {

SnapCoordinator::SnapCoordinator() : snap_container_map_() {}

SnapCoordinator::~SnapCoordinator() = default;

SnapCoordinator* SnapCoordinator::Create() {
  return new SnapCoordinator();
}

// Returns the scroll container that can be affected by this snap area.
static LayoutBox* FindSnapContainer(const LayoutBox& snap_area) {
  // According to the new spec
  // https://drafts.csswg.org/css-scroll-snap/#snap-model
  // "Snap positions must only affect the nearest ancestor (on the element’s
  // containing block chain) scroll container".
  Element* viewport_defining_element =
      snap_area.GetDocument().ViewportDefiningElement();
  LayoutBox* box = snap_area.ContainingBlock();
  while (box && !box->HasOverflowClip() && !box->IsLayoutView() &&
         box->GetNode() != viewport_defining_element)
    box = box->ContainingBlock();

  // If we reach to viewportDefiningElement then we dispatch to viewport
  if (box && box->GetNode() == viewport_defining_element)
    return snap_area.GetDocument().GetLayoutView();

  return box;
}

void SnapCoordinator::SnapAreaDidChange(LayoutBox& snap_area,
                                        ScrollSnapAlign scroll_snap_align) {
  LayoutBox* old_container = snap_area.SnapContainer();
  if (scroll_snap_align.alignmentX == SnapAlignment::kNone &&
      scroll_snap_align.alignmentY == SnapAlignment::kNone) {
    snap_area.SetSnapContainer(nullptr);
    if (old_container)
      UpdateSnapContainerData(*old_container);
    return;
  }

  if (LayoutBox* new_container = FindSnapContainer(snap_area)) {
    snap_area.SetSnapContainer(new_container);
    // TODO(sunyunjia): consider keep the SnapAreas in a map so it is
    // easier to update.
    // TODO(sunyunjia): Only update when the localframe doesn't need layout.
    UpdateSnapContainerData(*new_container);
    if (old_container && old_container != new_container)
      UpdateSnapContainerData(*old_container);
  } else {
    // TODO(majidvp): keep track of snap areas that do not have any
    // container so that we check them again when a new container is
    // added to the page.
  }
}

void SnapCoordinator::UpdateAllSnapContainerData() {
  for (const auto& entry : snap_container_map_) {
    UpdateSnapContainerData(*entry.key);
  }
}

static ScrollableArea* ScrollableAreaForSnapping(const LayoutBox& layout_box) {
  return layout_box.IsLayoutView()
             ? layout_box.GetFrameView()->LayoutViewportScrollableArea()
             : layout_box.GetScrollableArea();
}

void SnapCoordinator::UpdateSnapContainerData(const LayoutBox& snap_container) {
  if (snap_container.Style()->GetScrollSnapType().is_none)
    return;

  SnapContainerData snap_container_data(
      snap_container.Style()->GetScrollSnapType());

  ScrollableArea* scrollable_area = ScrollableAreaForSnapping(snap_container);
  if (!scrollable_area)
    return;
  FloatPoint max_position = ScrollOffsetToPosition(
      scrollable_area->MaximumScrollOffset(), scrollable_area->ScrollOrigin());
  snap_container_data.set_max_position(
      gfx::ScrollOffset(max_position.X(), max_position.Y()));

  if (SnapAreaSet* snap_areas = snap_container.SnapAreas()) {
    for (const LayoutBox* snap_area : *snap_areas) {
      snap_container_data.AddSnapAreaData(
          CalculateSnapAreaData(*snap_area, snap_container, max_position));
    }
  }
  snap_container_map_.Set(&snap_container, snap_container_data);
}

static float ClipInContainer(LayoutUnit unit, float max) {
  float value = unit.ClampNegativeToZero().ToFloat();
  return value > max ? max : value;
}

// Returns scroll offset at which the snap area and snap containers meet the
// requested snapping alignment on the given axis.
// If the scroll offset required for the alignment is outside the valid range
// then it will be clamped.
// alignment - The scroll-snap-align specified on the snap area.
//    https://www.w3.org/TR/css-scroll-snap-1/#scroll-snap-align
// axis - The axis for which we consider alignment on. Should be either X or Y
// container - The snap_container rect relative to the container_element's
//    boundary. Note that this rect is represented by the dotted box below,
//    which is contracted by the scroll-padding from the element's original
//    boundary.
// max_position - The maximal scrollable offset of the container. The
//    calculated snap_position can not be larger than this value.
// area - The snap area rect relative to the snap container's boundary. Note
//    that this rect is represented by the dotted box below, which is expanded
//    by the scroll-margin from the element's original boundary.
static float CalculateSnapPosition(SnapAlignment alignment,
                                   SnapAxis axis,
                                   const LayoutRect& container,
                                   const FloatPoint& max_position,
                                   const LayoutRect& area) {
  DCHECK(axis == SnapAxis::kX || axis == SnapAxis::kY);
  switch (alignment) {
    /* Start alignment aligns the area's start edge with container's start edge.
       https://www.w3.org/TR/css-scroll-snap-1/#valdef-scroll-snap-align-start
      + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +
      +                   ^                                     +
      +                   |                                     +
      +                   |snap_position                          +
      +                   |                                     +
      +                   v                                     +
      +  \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\  +
      +  \                 scroll-padding                    \  +
      +  \   . . . . . . . . . . . . . . . . . . . . . . .   \  +
      +  \   .       .     scroll-margin     .           .   \  +
      +  \   .       .  |=================|  .           .   \  +
      +  \   .       .  |                 |  .           .   \  +
      +  \   .       .  |    snap_area    |  .           .   \  +
      +  \   .       .  |                 |  .           .   \  +
      +  \   .       .  |=================|  .           .   \  +
      +  \   .       .                       .           .   \  +
      +  \   .       . . . . . . . . . . . . .           .   \  +
      +  \   .                                           .   \  +
      +  \   .                                           .   \  +
      +  \   .                                           .   \  +
      +  \   . . . . . . .snap_container . . . . . . . . .   \  +
      +  \                                                   \  +
      +  \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\  +
      +                                                         +
      +                scrollable_content                       +
      + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +

    */
    case SnapAlignment::kStart:
      if (axis == SnapAxis::kX) {
        return ClipInContainer(area.X() - container.X(), max_position.X());
      }
      return ClipInContainer(area.Y() - container.Y(), max_position.Y());

    /* Center alignment aligns the snap_area(with margin)'s center line with
       snap_container(without padding)'s center line.
       https://www.w3.org/TR/css-scroll-snap-1/#valdef-scroll-snap-align-center
      + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +
      +                    ^                                    +
      +                    | snap_position                        +
      +                    v                                    +
      +  \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\  +
      +  \                 scroll-padding                    \  +
      +  \   . . . . . . . . . . . . . . . . . . . . . . .   \  +
      +  \   .                                           .   \  +
      +  \   .       . . . . . . . . . . . . .           .   \  +
      +  \   .       .      scroll-margin    .           .   \  +
      +  \   .       .  |=================|  .           .   \  +
      +  \   .       .  |    snap_area    |  .           .   \  +
      +  \* *.* * * *.* * * * * * * * * * * *.* * * * * * * * * * Center line
      +  \   .       .  |                 |  .           .   \  +
      +  \   .       .  |=================|  .           .   \  +
      +  \   .       .                       .           .   \  +
      +  \   .       . . . . . . . . . . . . .           .   \  +
      +  \   .                                           .   \  +
      +  \   . . . . . . snap_container. . . . . . . . . .   \  +
      +  \                                                   \  +
      +  \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\  +
      +                                                         +
      +                                                         +
      +                scrollable_content                       +
      +                                                         +
      + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +

    */
    case SnapAlignment::kCenter:
      if (axis == SnapAxis::kX) {
        return ClipInContainer(area.Center().X() - container.Center().X(),
                               max_position.X());
      }
      return ClipInContainer(area.Center().Y() - container.Center().Y(),
                             max_position.Y());

    /* End alignment aligns the snap_area(with margin)'s end edge with
       snap_container(without padding)'s end edge.
       https://www.w3.org/TR/css-scroll-snap-1/#valdef-scroll-snap-align-end
      + + + + + + + + + + + + + + + + + + + + + + + + + + + + . .
      +                    ^                                    +
      +                    | snap_position                        +
      +                    v                                    +
      +  \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\  +
      +  \                                                   \  +
      +  \   . . . . . . . . snap_container. . . . . . . .   \  +
      +  \   .                                           .   \  +
      +  \   .                                           .   \  +
      +  \   .                                           .   \  +
      +  \   .       . . . . . . . . . . . . .           .   \  +
      +  \   .       .      scroll-margin    .           .   \  +
      +  \   .       .  |=================|  .           .   \  +
      +  \   .       .  |                 |  .           .   \  +
      +  \   .       .  |    snap_area    |  .           .   \  +
      +  \   .       .  |                 |  .           .   \  +
      +  \   .       .  |=================|  .           .   \  +
      +  \   .       .                       .           .   \  +
      +  \   . . . . . . . . . . . . . . . . . . . . . . .   \  +
      +  \               scroll-padding                      \  +
      +  \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\  +
      +                                                         +
      +               scrollable_content                        +
      +                                                         +
      + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +

    */
    case SnapAlignment::kEnd:
      if (axis == SnapAxis::kX) {
        return ClipInContainer(area.MaxX() - container.MaxX(),
                               max_position.X());
      }
      return ClipInContainer(area.MaxY() - container.MaxY(), max_position.Y());
    default:
      return LayoutUnit(SnapAreaData::kInvalidScrollPosition);
  }
}

static ScrollSnapAlign GetPhysicalAlignment(
    const ComputedStyle& area_style,
    const ComputedStyle& container_style) {
  ScrollSnapAlign align = area_style.GetScrollSnapAlign();
  if (container_style.IsFlippedBlocksWritingMode()) {
    if (align.alignmentX == SnapAlignment::kStart) {
      align.alignmentX = SnapAlignment::kEnd;
    } else if (align.alignmentX == SnapAlignment::kEnd) {
      align.alignmentX = SnapAlignment::kStart;
    }
  }
  return align;
}

SnapAreaData SnapCoordinator::CalculateSnapAreaData(
    const LayoutBox& snap_area,
    const LayoutBox& snap_container,
    const FloatPoint& max_position) {
  const ComputedStyle* container_style = snap_container.Style();
  const ComputedStyle* area_style = snap_area.Style();
  SnapAreaData snap_area_data;

  // Scroll-padding represents inward offsets from the corresponding edge of the
  // scrollport. https://drafts.csswg.org/css-scroll-snap-1/#scroll-padding
  // Scrollport is the visual vieport of the scroll container (through which the
  // scrollable overflow region can be viewed) coincides with its padding box.
  // https://drafts.csswg.org/css-overflow-3/#scrollport
  // So we use the LayoutRect of the padding box here. The coordinate is based
  // on the container's border box.
  LayoutRect container(snap_container.PaddingBoxRect());

  // We assume that the snap_container is the snap_area's ancestor in layout
  // tree, as the snap_container is found by walking up the layout tree in
  // FindSnapContainer(). Under this assumption,
  // snap_area.LocalToAncestorQuad() returns the snap_area's position relative
  // to its container's border box. And the |area| below represents the
  // snap_area rect in respect to the snap_container.
  LayoutRect area(LayoutPoint(), LayoutSize(snap_area.OffsetWidth(),
                                            snap_area.OffsetHeight()));
  area = EnclosingLayoutRect(
      snap_area
          .LocalToAncestorQuad(FloatRect(area), &snap_container,
                               kUseTransforms | kTraverseDocumentBoundaries)
          .BoundingBox());
  ScrollableArea* scrollable_area = ScrollableAreaForSnapping(snap_container);
  if (scrollable_area) {
    if (snap_container.IsLayoutView())
      area = snap_container.GetFrameView()->AbsoluteToDocument(area);
    else
      area.MoveBy(LayoutPoint(scrollable_area->ScrollPosition()));
  }

  LayoutRectOutsets container_padding(
      // The percentage of scroll-padding is different from that of normal
      // padding, as scroll-padding resolves the percentage against
      // corresponding dimension of the scrollport[1], while the normal padding
      // resolves that against "width".[2,3]
      // We use MinimumValueForLength here to ensure kAuto is resolved to
      // LayoutUnit() which is the correct behavior for padding.
      // [1] https://drafts.csswg.org/css-scroll-snap-1/#scroll-padding
      //     "relative to the corresponding dimension of the scroll container’s
      //      scrollport"
      // [2] https://drafts.csswg.org/css-box/#padding-props
      // [3] See for example LayoutBoxModelObject::ComputedCSSPadding where it
      //     uses |MinimumValueForLength| but against the "width".
      MinimumValueForLength(container_style->ScrollPaddingTop(),
                            container.Height()),
      MinimumValueForLength(container_style->ScrollPaddingRight(),
                            container.Width()),
      MinimumValueForLength(container_style->ScrollPaddingBottom(),
                            container.Height()),
      MinimumValueForLength(container_style->ScrollPaddingLeft(),
                            container.Width()));
  LayoutRectOutsets area_margin(
      area_style->ScrollMarginTop(), area_style->ScrollMarginRight(),
      area_style->ScrollMarginBottom(), area_style->ScrollMarginLeft());
  container.Contract(container_padding);
  area.Expand(area_margin);

  ScrollSnapAlign align = GetPhysicalAlignment(*area_style, *container_style);

  snap_area_data.snap_position.set_x(CalculateSnapPosition(
      align.alignmentX, SnapAxis::kX, container, max_position, area));
  snap_area_data.snap_position.set_y(CalculateSnapPosition(
      align.alignmentY, SnapAxis::kY, container, max_position, area));

  if (align.alignmentX != SnapAlignment::kNone &&
      align.alignmentY != SnapAlignment::kNone) {
    snap_area_data.snap_axis = SnapAxis::kBoth;
  } else if (align.alignmentX != SnapAlignment::kNone &&
             align.alignmentY == SnapAlignment::kNone) {
    snap_area_data.snap_axis = SnapAxis::kX;
  } else {
    snap_area_data.snap_axis = SnapAxis::kY;
  }

  snap_area_data.must_snap =
      (area_style->ScrollSnapStop() == EScrollSnapStop::kAlways);

  return snap_area_data;
}

bool SnapCoordinator::GetSnapPosition(const LayoutBox& snap_container,
                                      bool did_scroll_x,
                                      bool did_scroll_y,
                                      FloatPoint* snap_position) {
  auto iter = snap_container_map_.find(&snap_container);
  if (iter == snap_container_map_.end())
    return false;

  const SnapContainerData& data = iter->value;
  if (!data.size())
    return false;

  ScrollableArea* scrollable_area = ScrollableAreaForSnapping(snap_container);
  if (!scrollable_area)
    return false;

  FloatPoint current_position = scrollable_area->ScrollPosition();

  gfx::ScrollOffset position = data.FindSnapPosition(
      gfx::ScrollOffset(current_position.X(), current_position.Y()),
      did_scroll_x, did_scroll_y);
  snap_position->SetX(position.x());
  snap_position->SetY(position.y());

  return *snap_position != current_position;
}

void SnapCoordinator::PerformSnapping(const LayoutBox& snap_container,
                                      bool did_scroll_x,
                                      bool did_scroll_y) {
  FloatPoint snap_position;
  if (GetSnapPosition(snap_container, did_scroll_x, did_scroll_y,
                      &snap_position)) {
    if (ScrollableArea* scrollable_area =
            ScrollableAreaForSnapping(snap_container)) {
      scrollable_area->SetScrollOffset(
          ScrollPositionToOffset(snap_position,
                                 scrollable_area->ScrollOrigin()),
          kProgrammaticScroll, kScrollBehaviorSmooth);
    }
  }
}

void SnapCoordinator::SnapContainerDidChange(LayoutBox& snap_container,
                                             ScrollSnapType scroll_snap_type) {
  if (scroll_snap_type.is_none) {
    snap_container_map_.erase(&snap_container);
    snap_container.ClearSnapAreas();
  } else {
    if (scroll_snap_type.axis == SnapAxis::kInline) {
      if (snap_container.Style()->IsHorizontalWritingMode())
        scroll_snap_type.axis = SnapAxis::kX;
      else
        scroll_snap_type.axis = SnapAxis::kY;
    }
    if (scroll_snap_type.axis == SnapAxis::kBlock) {
      if (snap_container.Style()->IsHorizontalWritingMode())
        scroll_snap_type.axis = SnapAxis::kY;
      else
        scroll_snap_type.axis = SnapAxis::kX;
    }
    // TODO(sunyunjia): Only update when the localframe doesn't need layout.
    UpdateSnapContainerData(snap_container);
  }

  // TODO(majidvp): Add logic to correctly handle orphaned snap areas here.
  // 1. Removing container: find a new snap container for its orphan snap
  // areas (most likely nearest ancestor of current container) otherwise add
  // them to an orphan list.
  // 2. Adding container: may take over snap areas from nearest ancestor snap
  // container or from existing areas in orphan pool.
}

Optional<SnapContainerData> SnapCoordinator::GetSnapContainerData(
    const LayoutBox& snap_container) const {
  auto iter = snap_container_map_.find(&snap_container);
  if (iter != snap_container_map_.end()) {
    return iter->value;
  }
  return base::nullopt;
}

#ifndef NDEBUG

void SnapCoordinator::ShowSnapAreaMap() {
  for (auto& container : snap_container_map_.Keys())
    ShowSnapAreasFor(container);
}

void SnapCoordinator::ShowSnapAreasFor(const LayoutBox* container) {
  LOG(INFO) << *container->GetNode();
  if (SnapAreaSet* snap_areas = container->SnapAreas()) {
    for (auto& snap_area : *snap_areas) {
      LOG(INFO) << "    " << *snap_area->GetNode();
    }
  }
}

void SnapCoordinator::ShowSnapDataFor(const LayoutBox* snap_container) {
  auto iter = snap_container_map_.find(snap_container);
  if (iter == snap_container_map_.end())
    return;
  LOG(INFO) << iter->value;
}

#endif

}  // namespace blink
