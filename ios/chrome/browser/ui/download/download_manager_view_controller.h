// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_DOWNLOAD_DOWNLOAD_MANAGER_VIEW_CONTROLLER_H_
#define IOS_CHROME_BROWSER_UI_DOWNLOAD_DOWNLOAD_MANAGER_VIEW_CONTROLLER_H_

#import <UIKit/UIKit.h>

#import "ios/chrome/browser/ui/download/download_manager_consumer.h"

@class DownloadManagerViewController;

@protocol DownloadManagerViewControllerDelegate<NSObject>
@optional
// Called when close button was tapped. Delegate may dismiss presentation.
- (void)downloadManagerViewControllerDidClose:
    (DownloadManagerViewController*)controller;

// Called when Download or Restart button was tapped. Delegate should start the
// download.
- (void)downloadManagerViewControllerDidStartDownload:
    (DownloadManagerViewController*)controller;

// Called when "Open In.." button was tapped. Delegate should present system's
// OpenIn dialog from |layoutGuide|.
- (void)downloadManagerViewController:(DownloadManagerViewController*)controller
     presentOpenInMenuWithLayoutGuide:(UILayoutGuide*)layoutGuide;

@end

// Presents bottom bar UI for a single download task.
@interface DownloadManagerViewController
    : UIViewController<DownloadManagerConsumer>

@property(nonatomic, weak) id<DownloadManagerViewControllerDelegate> delegate;

@end

// All UI elements presend in view controller's view.
@interface DownloadManagerViewController (UIElements)

// Button to dismiss the download toolbar.
@property(nonatomic, readonly) UIButton* closeButton;

// Label that describes the current download status.
@property(nonatomic, readonly) UILabel* statusLabel;

// Button appropriate for the current download status ("Download", "Open In..",
// "Try Again").
@property(nonatomic, readonly) UIButton* actionButton;

@end

#endif  // IOS_CHROME_BROWSER_UI_DOWNLOAD_DOWNLOAD_MANAGER_VIEW_CONTROLLER_H_
