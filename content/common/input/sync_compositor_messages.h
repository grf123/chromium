// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_SYNC_COMPOSITOR_MESSAGES_H_
#define CONTENT_COMMON_SYNC_COMPOSITOR_MESSAGES_H_

#include <stddef.h>

#include "base/memory/shared_memory_handle.h"
#include "base/optional.h"
#include "components/viz/common/frame_sinks/begin_frame_args.h"
#include "components/viz/common/quads/compositor_frame.h"
#include "content/common/content_export.h"
#include "content/common/content_param_traits.h"
#include "content/public/common/input_event_ack_state.h"
#include "ipc/ipc_message_macros.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/scroll_offset.h"

#ifndef INTERNAL_CONTENT_COMMON_SYNC_COMPOSITOR_MESSAGES_H_
#define INTERNAL_CONTENT_COMMON_SYNC_COMPOSITOR_MESSAGES_H_

namespace content {

struct SyncCompositorDemandDrawHwParams {
  SyncCompositorDemandDrawHwParams();
  SyncCompositorDemandDrawHwParams(
      const gfx::Size& viewport_size,
      const gfx::Rect& viewport_rect_for_tile_priority,
      const gfx::Transform& transform_for_tile_priority);
  ~SyncCompositorDemandDrawHwParams();

  gfx::Size viewport_size;
  gfx::Rect clip;
  gfx::Rect viewport_rect_for_tile_priority;
  gfx::Transform transform_for_tile_priority;
};

struct SyncCompositorSetSharedMemoryParams {
  SyncCompositorSetSharedMemoryParams();

  uint32_t buffer_size;
  base::SharedMemoryHandle shm_handle;
};

struct SyncCompositorDemandDrawSwParams {
  SyncCompositorDemandDrawSwParams();
  ~SyncCompositorDemandDrawSwParams();

  gfx::Size size;
  gfx::Rect clip;
  gfx::Transform transform;
};

struct SyncCompositorCommonRendererParams {
  SyncCompositorCommonRendererParams();
  ~SyncCompositorCommonRendererParams();

  // Allow copy.
  SyncCompositorCommonRendererParams(
      const SyncCompositorCommonRendererParams& other);
  SyncCompositorCommonRendererParams& operator=(
      const SyncCompositorCommonRendererParams& other);

  unsigned int version = 0u;
  gfx::ScrollOffset total_scroll_offset;
  gfx::ScrollOffset max_scroll_offset;
  gfx::SizeF scrollable_size;
  float page_scale_factor = 0.f;
  float min_page_scale_factor = 0.f;
  float max_page_scale_factor = 0.f;
  bool need_animate_scroll = false;
  uint32_t need_invalidate_count = 0u;
  uint32_t did_activate_pending_tree_count = 0u;
};

}  // namespace content

#endif  // INTERNAL_CONTENT_COMMON_SYNC_COMPOSITOR_MESSAGES_H_

#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT CONTENT_EXPORT
#define IPC_MESSAGE_START SyncCompositorMsgStart

IPC_STRUCT_TRAITS_BEGIN(content::SyncCompositorDemandDrawHwParams)
  IPC_STRUCT_TRAITS_MEMBER(viewport_size)
  IPC_STRUCT_TRAITS_MEMBER(viewport_rect_for_tile_priority)
  IPC_STRUCT_TRAITS_MEMBER(transform_for_tile_priority)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(content::SyncCompositorSetSharedMemoryParams)
  IPC_STRUCT_TRAITS_MEMBER(buffer_size)
  IPC_STRUCT_TRAITS_MEMBER(shm_handle)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(content::SyncCompositorDemandDrawSwParams)
  IPC_STRUCT_TRAITS_MEMBER(size)
  IPC_STRUCT_TRAITS_MEMBER(clip)
  IPC_STRUCT_TRAITS_MEMBER(transform)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(content::SyncCompositorCommonRendererParams)
  IPC_STRUCT_TRAITS_MEMBER(version)
  IPC_STRUCT_TRAITS_MEMBER(total_scroll_offset)
  IPC_STRUCT_TRAITS_MEMBER(max_scroll_offset)
  IPC_STRUCT_TRAITS_MEMBER(scrollable_size)
  IPC_STRUCT_TRAITS_MEMBER(page_scale_factor)
  IPC_STRUCT_TRAITS_MEMBER(min_page_scale_factor)
  IPC_STRUCT_TRAITS_MEMBER(max_page_scale_factor)
  IPC_STRUCT_TRAITS_MEMBER(need_animate_scroll)
  IPC_STRUCT_TRAITS_MEMBER(need_invalidate_count)
  IPC_STRUCT_TRAITS_MEMBER(did_activate_pending_tree_count)
IPC_STRUCT_TRAITS_END()

// Messages sent from the browser to the renderer.
// Synchronous IPCs are allowed here to the renderer compositor thread. See
// design doc https://goo.gl/Tn81FW and https://crbug.com/526842 for details.

IPC_MESSAGE_ROUTED1(SyncCompositorMsg_ComputeScroll, base::TimeTicks)

IPC_MESSAGE_ROUTED1(SyncCompositorMsg_DemandDrawHwAsync,
                    content::SyncCompositorDemandDrawHwParams)

IPC_SYNC_MESSAGE_ROUTED1_3(SyncCompositorMsg_DemandDrawHw,
                           content::SyncCompositorDemandDrawHwParams,
                           content::SyncCompositorCommonRendererParams,
                           uint32_t /* layer_tree_frame_sink_id */,
                           base::Optional<viz::CompositorFrame>)

IPC_SYNC_MESSAGE_ROUTED1_2(SyncCompositorMsg_SetSharedMemory,
                           content::SyncCompositorSetSharedMemoryParams,
                           bool /* success */,
                           content::SyncCompositorCommonRendererParams)

IPC_MESSAGE_ROUTED0(SyncCompositorMsg_ZeroSharedMemory)

IPC_SYNC_MESSAGE_ROUTED1_2(SyncCompositorMsg_DemandDrawSw,
                           content::SyncCompositorDemandDrawSwParams,
                           content::SyncCompositorCommonRendererParams,
                           base::Optional<viz::CompositorFrameMetadata>)

IPC_SYNC_MESSAGE_ROUTED2_1(SyncCompositorMsg_ZoomBy,
                           float /* delta */,
                           gfx::Point /* anchor */,
                           content::SyncCompositorCommonRendererParams)

IPC_MESSAGE_ROUTED1(SyncCompositorMsg_SetMemoryPolicy,
                    uint32_t /* bytes_limit */)

IPC_MESSAGE_ROUTED2(SyncCompositorMsg_ReclaimResources,
                    uint32_t /* layer_tree_frame_sink_id */,
                    std::vector<viz::ReturnedResource> /* resources */)

IPC_MESSAGE_ROUTED1(SyncCompositorMsg_SetScroll, gfx::ScrollOffset)

// Let renderer know begin frame messages won't be sent even if requested.
IPC_MESSAGE_ROUTED1(SyncCompositorMsg_SetBeginFramePaused, bool /* paused */)

// Sent by the browser when the renderer should generate a new frame.
IPC_MESSAGE_ROUTED1(SyncCompositorMsg_BeginFrame,
                    viz::BeginFrameArgs /* args */)

// -----------------------------------------------------------------------------
// Messages sent from the renderer to the browser.

IPC_MESSAGE_ROUTED0(SyncCompositorHostMsg_LayerTreeFrameSinkCreated)

IPC_MESSAGE_ROUTED1(SyncCompositorHostMsg_UpdateState,
                    content::SyncCompositorCommonRendererParams)

// Response to a begin frame request.
IPC_MESSAGE_ROUTED1(SyncCompositorHostMsg_BeginFrameResponse,
                    content::SyncCompositorCommonRendererParams)

IPC_MESSAGE_ROUTED2(SyncCompositorHostMsg_ReturnFrame,
                    uint32_t /* layer_tree_frame_sink_id */,
                    base::Optional<viz::CompositorFrame>)

// Sent by renderer to request a SyncCompositorMsg_BeginFrame message for
// upcoming display events. If |enabled| is true, the BeginFrame message will
// continue to be be delivered until the notification is disabled.
IPC_MESSAGE_ROUTED1(SyncCompositorHostMsg_SetNeedsBeginFrames,
                    bool /* enabled */)

#endif  // CONTENT_COMMON_SYNC_COMPOSITOR_MESSAGES_H_
