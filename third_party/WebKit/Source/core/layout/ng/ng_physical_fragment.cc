// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_physical_fragment.h"

#include "core/layout/LayoutBlock.h"
#include "core/layout/ng/geometry/ng_border_edges.h"
#include "core/layout/ng/geometry/ng_box_strut.h"
#include "core/layout/ng/inline/ng_physical_line_box_fragment.h"
#include "core/layout/ng/inline/ng_physical_text_fragment.h"
#include "core/layout/ng/ng_break_token.h"
#include "core/layout/ng/ng_physical_box_fragment.h"
#include "core/style/ComputedStyle.h"
#include "platform/wtf/text/StringBuilder.h"

namespace blink {
namespace {

bool AppendFragmentOffsetAndSize(const NGPhysicalFragment* fragment,
                                 StringBuilder* builder,
                                 NGPhysicalFragment::DumpFlags flags,
                                 bool has_content) {
  if (flags & NGPhysicalFragment::DumpOffset) {
    if (has_content)
      builder->Append(" ");
    builder->Append("offset:");
    if (fragment->IsPlaced())
      builder->Append(fragment->Offset().ToString());
    else
      builder->Append("unplaced");
    has_content = true;
  }
  if (flags & NGPhysicalFragment::DumpSize) {
    if (has_content)
      builder->Append(" ");
    builder->Append("size:");
    builder->Append(fragment->Size().ToString());
    has_content = true;
  }
  return has_content;
}

String StringForBoxType(const NGPhysicalFragment& fragment) {
  StringBuilder result;
  switch (fragment.BoxType()) {
    case NGPhysicalFragment::NGBoxType::kNormalBox:
      break;
    case NGPhysicalFragment::NGBoxType::kInlineBox:
      result.Append("inline");
      break;
    case NGPhysicalFragment::NGBoxType::kAtomicInline:
      result.Append("atomic-inline");
      break;
    case NGPhysicalFragment::NGBoxType::kFloating:
      result.Append("floating");
      break;
    case NGPhysicalFragment::NGBoxType::kOutOfFlowPositioned:
      result.Append("out-of-flow-positioned");
      break;
  }
  if (fragment.IsOldLayoutRoot()) {
    if (result.length())
      result.Append(" ");
    result.Append("old-layout-root");
  }
  if (fragment.IsBlockFlow()) {
    if (result.length())
      result.Append(" ");
    result.Append("block-flow");
  }
  if (fragment.IsBox() &&
      static_cast<const NGPhysicalBoxFragment&>(fragment).ChildrenInline()) {
    if (result.length())
      result.Append(" ");
    result.Append("children-inline");
  }

  return result.ToString();
}

void AppendFragmentToString(const NGPhysicalFragment* fragment,
                            StringBuilder* builder,
                            NGPhysicalFragment::DumpFlags flags,
                            unsigned indent = 2) {
  if (flags & NGPhysicalFragment::DumpIndentation) {
    for (unsigned i = 0; i < indent; i++)
      builder->Append(" ");
  }

  bool has_content = false;
  if (fragment->IsBox()) {
    const auto* box = ToNGPhysicalBoxFragment(fragment);
    if (flags & NGPhysicalFragment::DumpType) {
      builder->Append("Box");
      String box_type = StringForBoxType(*fragment);
      has_content = true;
      if (!box_type.IsEmpty()) {
        builder->Append(" (");
        builder->Append(box_type);
        builder->Append(")");
      }
      if (flags & NGPhysicalFragment::DumpSelfPainting &&
          box->HasSelfPaintingLayer()) {
        if (box_type.IsEmpty())
          builder->Append(" ");
        builder->Append("(self paint)");
      }
    }
    has_content =
        AppendFragmentOffsetAndSize(fragment, builder, flags, has_content);

    builder->Append("\n");

    if (flags & NGPhysicalFragment::DumpSubtree) {
      const auto& children = box->Children();
      for (unsigned i = 0; i < children.size(); i++)
        AppendFragmentToString(children[i].get(), builder, flags, indent + 2);
    }
    return;
  }

  if (fragment->IsLineBox()) {
    if (flags & NGPhysicalFragment::DumpType) {
      builder->Append("LineBox");
      has_content = true;
    }
    has_content =
        AppendFragmentOffsetAndSize(fragment, builder, flags, has_content);
    builder->Append("\n");

    if (flags & NGPhysicalFragment::DumpSubtree) {
      const auto* line_box = ToNGPhysicalLineBoxFragment(fragment);
      const auto& children = line_box->Children();
      for (unsigned i = 0; i < children.size(); i++)
        AppendFragmentToString(children[i].get(), builder, flags, indent + 2);
      return;
    }
  }

  if (fragment->IsText()) {
    if (flags & NGPhysicalFragment::DumpType) {
      builder->Append("Text");
      has_content = true;
    }
    has_content =
        AppendFragmentOffsetAndSize(fragment, builder, flags, has_content);

    if (flags & NGPhysicalFragment::DumpTextOffsets) {
      const auto* text = ToNGPhysicalTextFragment(fragment);
      if (has_content)
        builder->Append(" ");
      builder->Append("start: ");
      builder->Append(String::Format("%u", text->StartOffset()));
      builder->Append(" end: ");
      builder->Append(String::Format("%u", text->EndOffset()));
      has_content = true;
    }
    builder->Append("\n");
    return;
  }

  if (flags & NGPhysicalFragment::DumpType) {
    builder->Append("Unknown fragment type");
    has_content = true;
  }
  has_content =
      AppendFragmentOffsetAndSize(fragment, builder, flags, has_content);
  builder->Append("\n");
}

}  // namespace

// static
void NGPhysicalFragmentTraits::Destruct(const NGPhysicalFragment* fragment) {
  fragment->Destroy();
}

NGPhysicalFragment::NGPhysicalFragment(LayoutObject* layout_object,
                                       const ComputedStyle& style,
                                       NGPhysicalSize size,
                                       NGFragmentType type,
                                       scoped_refptr<NGBreakToken> break_token)
    : layout_object_(layout_object),
      style_(&style),
      size_(size),
      break_token_(std::move(break_token)),
      type_(type),
      box_type_(NGBoxType::kNormalBox),
      is_old_layout_root_(false),
      is_placed_(false) {}

// Keep the implementation of the destructor here, to avoid dependencies on
// ComputedStyle in the header file.
NGPhysicalFragment::~NGPhysicalFragment() = default;

void NGPhysicalFragment::Destroy() const {
  switch (Type()) {
    case kFragmentBox:
      delete static_cast<const NGPhysicalBoxFragment*>(this);
      break;
    case kFragmentText:
      delete static_cast<const NGPhysicalTextFragment*>(this);
      break;
    case kFragmentLineBox:
      delete static_cast<const NGPhysicalLineBoxFragment*>(this);
      break;
    default:
      NOTREACHED();
      break;
  }
}

const ComputedStyle& NGPhysicalFragment::Style() const {
  DCHECK(style_);
  return *style_;
}

Node* NGPhysicalFragment::GetNode() const {
  // TODO(layout-dev): This should store the node directly instead of going
  // through LayoutObject.
  return layout_object_ ? layout_object_->GetNode() : nullptr;
}

bool NGPhysicalFragment::HasLayer() const {
  return layout_object_->HasLayer();
}

PaintLayer* NGPhysicalFragment::Layer() const {
  if (!HasLayer())
    return nullptr;

  // If the underlying LayoutObject has a layer it's guranteed to be a
  // LayoutBoxModelObject.
  return static_cast<LayoutBoxModelObject*>(layout_object_)->Layer();
}

bool NGPhysicalFragment::IsBlockFlow() const {
  return layout_object_ && layout_object_->IsLayoutBlockFlow();
}

bool NGPhysicalFragment::IsPlacedByLayoutNG() const {
  // TODO(kojii): Move this to a flag for |LayoutNGBlockFlow::UpdateBlockLayout|
  // to set.
  if (!layout_object_)
    return false;
  const LayoutBlock* container = layout_object_->ContainingBlock();
  return container && container->IsLayoutNGMixin();
}

NGPixelSnappedPhysicalBoxStrut NGPhysicalFragment::BorderWidths() const {
  unsigned edges = BorderEdges();
  NGPhysicalBoxStrut box_strut(
      LayoutUnit((edges & NGBorderEdges::kTop) ? Style().BorderTopWidth()
                                               : .0f),
      LayoutUnit((edges & NGBorderEdges::kRight) ? Style().BorderRightWidth()
                                                 : .0f),
      LayoutUnit((edges & NGBorderEdges::kBottom) ? Style().BorderBottomWidth()
                                                  : .0f),
      LayoutUnit((edges & NGBorderEdges::kLeft) ? Style().BorderLeftWidth()
                                                : .0f));
  return box_strut.SnapToDevicePixels();
}

NGPhysicalOffsetRect NGPhysicalFragment::SelfVisualRect() const {
  switch (Type()) {
    case NGPhysicalFragment::kFragmentBox:
      return ToNGPhysicalBoxFragment(*this).SelfVisualRect();
    case NGPhysicalFragment::kFragmentText:
      return ToNGPhysicalTextFragment(*this).SelfVisualRect();
    case NGPhysicalFragment::kFragmentLineBox:
      return {{}, Size()};
  }
  NOTREACHED();
  return {{}, Size()};
}

NGPhysicalOffsetRect NGPhysicalFragment::VisualRectWithContents() const {
  switch (Type()) {
    case NGPhysicalFragment::kFragmentBox:
      return ToNGPhysicalBoxFragment(*this).VisualRectWithContents();
    case NGPhysicalFragment::kFragmentText:
      return ToNGPhysicalTextFragment(*this).SelfVisualRect();
    case NGPhysicalFragment::kFragmentLineBox:
      return ToNGPhysicalLineBoxFragment(*this).VisualRectWithContents();
  }
  NOTREACHED();
  return {{}, Size()};
}

void NGPhysicalFragment::PropagateContentsVisualRect(
    NGPhysicalOffsetRect* parent_visual_rect) const {
  NGPhysicalOffsetRect visual_rect = VisualRectWithContents();
  visual_rect.offset += Offset();
  parent_visual_rect->Unite(visual_rect);
}

scoped_refptr<NGPhysicalFragment> NGPhysicalFragment::CloneWithoutOffset()
    const {
  switch (Type()) {
    case kFragmentBox:
      return static_cast<const NGPhysicalBoxFragment*>(this)
          ->CloneWithoutOffset();
      break;
    case kFragmentText:
      return static_cast<const NGPhysicalTextFragment*>(this)
          ->CloneWithoutOffset();
      break;
    case kFragmentLineBox:
      return static_cast<const NGPhysicalLineBoxFragment*>(this)
          ->CloneWithoutOffset();
      break;
    default:
      NOTREACHED();
      break;
  }
  return nullptr;
}

String NGPhysicalFragment::ToString() const {
  StringBuilder output;
  output.Append(String::Format(
      "Type: '%d' Size: '%s' Offset: '%s' Placed: '%d'", Type(),
      Size().ToString().Ascii().data(),
      is_placed_ ? Offset().ToString().Ascii().data() : "no offset",
      IsPlaced()));
  switch (Type()) {
    case kFragmentBox:
      output.Append(String::Format(", BoxType: '%s'",
                                   StringForBoxType(*this).Ascii().data()));
      break;
    case kFragmentText: {
      const NGPhysicalTextFragment& text = ToNGPhysicalTextFragment(*this);
      output.Append(String::Format(", Text: (%u,%u) \"", text.StartOffset(),
                                   text.EndOffset()));
      output.Append(text.Text());
      output.Append("\"");
      break;
    }
    case kFragmentLineBox:
      break;
  }
  return output.ToString();
}

String NGPhysicalFragment::DumpFragmentTree(DumpFlags flags,
                                            unsigned indent) const {
  StringBuilder string_builder;
  if (flags & DumpHeaderText)
    string_builder.Append(".:: LayoutNG Physical Fragment Tree ::.\n");
  AppendFragmentToString(this, &string_builder, flags, indent);
  return string_builder.ToString();
}

#ifndef NDEBUG
void NGPhysicalFragment::ShowFragmentTree() const {
  fprintf(stderr, "%s\n", DumpFragmentTree(DumpAll).Utf8().data());
}
#endif  // !NDEBUG

NGPhysicalOffsetRect NGPhysicalFragmentWithOffset::RectInContainerBox() const {
  return {offset_to_container_box, fragment->Size()};
}

}  // namespace blink
