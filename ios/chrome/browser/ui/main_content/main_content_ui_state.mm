// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/main_content/main_content_ui_state.h"

#include "base/logging.h"
#include "ios/chrome/browser/ui/ui_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface MainContentUIState ()
// Redefine broadcast properties as readwrite.
@property(nonatomic, assign) CGSize scrollViewSize;
@property(nonatomic, assign) CGSize contentSize;
@property(nonatomic, assign) UIEdgeInsets contentInset;
@property(nonatomic, assign) CGFloat yContentOffset;
@property(nonatomic, assign, getter=isScrolling) BOOL scrolling;
@property(nonatomic, assign, getter=isZooming) BOOL zooming;
@property(nonatomic, assign, getter=isDragging) BOOL dragging;
// Whether the scroll view is decelerating.
@property(nonatomic, assign, getter=isDecelerating) BOOL decelerating;

// Updates |scrolling| based |dragging| and |decelerating|.
- (void)updateIsScrolling;

@end

@implementation MainContentUIState
@synthesize scrollViewSize = _scrollViewSize;
@synthesize contentSize = _contentSize;
@synthesize contentInset = _contentInset;
@synthesize yContentOffset = _yContentOffset;
@synthesize scrolling = _scrolling;
@synthesize zooming = _zooming;
@synthesize dragging = _dragging;
@synthesize decelerating = _decelerating;

- (void)setDragging:(BOOL)dragging {
  if (_dragging == dragging)
    return;
  _dragging = dragging;
  // When a scroll view is being dragged, its contents are tracking the pan
  // gesture, and previous deceleration is cancelled.
  if (_dragging)
    _decelerating = NO;
  [self updateIsScrolling];
}

- (void)setDecelerating:(BOOL)decelerating {
  if (_decelerating == decelerating)
    return;
  // If the scroll view is starting to decelerate after a drag, it is expected
  // that this property is set before |dragging| is reset to NO.  This ensures
  // that the broadcasted |scrolling| property does not quickly flip to NO when
  // drag events finish.
  DCHECK(!decelerating || self.dragging);
  _decelerating = decelerating;
  [self updateIsScrolling];
}

#pragma mark Private

- (void)updateIsScrolling {
  self.scrolling = self.dragging || self.decelerating;
}

@end

@interface MainContentUIStateUpdater ()
// The pan gesture driving the current scroll event.
// TODO(crbug.com/785508): Use this gesture recognizer to broadcast the scroll
// touch location.
@property(nonatomic, weak) UIPanGestureRecognizer* panGesture;
@end

@implementation MainContentUIStateUpdater
@synthesize state = _state;
@synthesize panGesture = _panGesture;

- (instancetype)initWithState:(MainContentUIState*)state {
  if (self = [super init]) {
    _state = state;
    DCHECK(_state);
  }
  return self;
}

#pragma mark Public

- (void)scrollViewSizeDidChange:(CGSize)scrollViewSize {
  self.state.scrollViewSize = scrollViewSize;
}

- (void)scrollViewDidResetContentSize:(CGSize)contentSize {
  self.state.contentSize = contentSize;
}

- (void)scrollViewDidResetContentInset:(UIEdgeInsets)contentInset {
  self.state.contentInset = contentInset;
}

- (void)scrollViewDidScrollToOffset:(CGPoint)offset {
  self.state.yContentOffset = offset.y;
}

- (void)scrollViewWillBeginDraggingWithGesture:
    (UIPanGestureRecognizer*)panGesture {
  self.state.dragging = YES;
  self.panGesture = panGesture;
}

- (void)scrollViewDidEndDraggingWithGesture:(UIPanGestureRecognizer*)panGesture
                           residualVelocity:(CGPoint)velocity {
  // It's possible during the side-swipe gesture for a drag to end on the scroll
  // view without a corresponding begin dragging call.  Early return if there
  // is no pan gesture from the begin call.
  if (!self.panGesture)
    return;
  DCHECK_EQ(panGesture, self.panGesture);
  if (!AreCGFloatsEqual(velocity.y, 0.0))
    self.state.decelerating = YES;
  self.state.dragging = NO;
  self.panGesture = nil;
}

- (void)scrollViewDidEndDecelerating {
  self.state.decelerating = NO;
}

- (void)scrollViewDidStartZooming {
  self.state.zooming = YES;
}

- (void)scrollViewDidEndZooming {
  self.state.zooming = NO;
}

- (void)scrollWasInterrupted {
  self.state.scrolling = NO;
  self.state.dragging = NO;
  self.state.decelerating = NO;
  self.state.zooming = NO;
}

@end
