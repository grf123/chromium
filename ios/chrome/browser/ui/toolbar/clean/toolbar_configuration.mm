// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/toolbar/clean/toolbar_configuration.h"

#import "base/logging.h"
#import "ios/chrome/browser/ui/toolbar/clean/toolbar_constants.h"
#import "ios/chrome/browser/ui/toolbar/public/web_toolbar_controller_constants.h"
#include "ios/chrome/browser/ui/ui_util.h"
#import "ios/chrome/browser/ui/uikit_ui_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@implementation ToolbarConfiguration

@synthesize style = _style;

- (instancetype)initWithStyle:(ToolbarStyle)style {
  self = [super init];
  if (self) {
    _style = style;
  }
  return self;
}

- (UIBlurEffect*)blurEffect {
  switch (self.style) {
    case NORMAL:
      return [UIBlurEffect effectWithStyle:UIBlurEffectStyleProminent];
    case INCOGNITO:
      return [UIBlurEffect effectWithStyle:UIBlurEffectStyleDark];
  }
}

- (UIColor*)NTPBackgroundColor {
  switch (self.style) {
    case NORMAL:
      return [UIColor whiteColor];
    case INCOGNITO:
      return [UIColor colorWithWhite:kNTPBackgroundColorBrightnessIncognito
                               alpha:1.0];
  }
}

- (UIColor*)backgroundColor {
  if (IsUIRefreshPhase1Enabled()) {
    NOTREACHED();
    return nil;
  } else {
    switch (self.style) {
      case NORMAL:
        return UIColorFromRGB(kToolbarBackgroundColor);
      case INCOGNITO:
        return UIColorFromRGB(kIncognitoToolbarBackgroundColor);
    }
  }
}

- (UIColor*)omniboxBackgroundColor {
  if (IsUIRefreshPhase1Enabled()) {
    NOTREACHED();
    return nil;
  } else {
    switch (self.style) {
      case NORMAL:
        return [UIColor whiteColor];
      case INCOGNITO:
        return UIColorFromRGB(kIcongnitoLocationBackgroundColor);
    }
  }
}

- (UIColor*)omniboxBorderColor {
  if (IsUIRefreshPhase1Enabled()) {
    NOTREACHED();
    return nil;
  } else {
    switch (self.style) {
      case NORMAL:
        return UIColorFromRGB(kLocationBarBorderColor);
      case INCOGNITO:
        return UIColorFromRGB(kIncognitoLocationBarBorderColor);
    }
  }
}

- (UIColor*)buttonTitleNormalColor {
  switch (self.style) {
    case NORMAL:
      return UIColorFromRGB(kToolbarButtonTitleNormalColor);
    case INCOGNITO:
      return UIColorFromRGB(kIncognitoToolbarButtonTitleNormalColor);
  }
}

- (UIColor*)buttonTitleHighlightedColor {
  switch (self.style) {
    case NORMAL:
      return UIColorFromRGB(kToolbarButtonTitleHighlightedColor);
    case INCOGNITO:
      return UIColorFromRGB(kIncognitoToolbarButtonTitleHighlightedColor);
  }
}

- (UIColor*)locationBarBackgroundColorWithVisibility:(CGFloat)visibilityFactor {
  switch (self.style) {
    case NORMAL:
      return [UIColor colorWithWhite:0
                               alpha:kAdaptiveLocationBarBackgroundAlpha *
                                     visibilityFactor];
    case INCOGNITO:
      return [UIColor colorWithWhite:1
                               alpha:kAdaptiveLocationBarBackgroundAlpha *
                                     visibilityFactor];
  }
}

@end
