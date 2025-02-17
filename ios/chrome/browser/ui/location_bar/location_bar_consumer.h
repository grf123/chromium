// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_LOCATION_BAR_LOCATION_BAR_CONSUMER_H_
#define IOS_CHROME_BROWSER_UI_LOCATION_BAR_LOCATION_BAR_CONSUMER_H_

// Consumer for the location bar mediator.
@protocol LocationBarConsumer

// Notifies the consumer to update the omnibox state, including the displayed
// text and the cursor position.
- (void)updateOmniboxState;

@end

#endif  // IOS_CHROME_BROWSER_UI_LOCATION_BAR_LOCATION_BAR_CONSUMER_H_
