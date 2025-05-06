// Copyright 2023 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#import "LynxScrollFluency.h"
#import "LynxFluencyMonitor.h"

@implementation LynxScrollFluency {
  LynxFluencyMonitor *_fluencyMonitor;
}

- (instancetype)init {
  self = [super init];
  if (self) {
    _fluencyMonitor = [[LynxFluencyMonitor alloc] init];
  }
  return self;
}

+ (LynxFluencyConfig)constructFluencyConfigWithLynxScrollInfo:(LynxScrollInfo *)info {
  LynxFluencyConfig config;
  config.key = info;
  config.lynxView = info.lynxView;
  config.scrollMonitorTagName = info.scrollMonitorTagName;
  config.tagName = info.tagName;
  return config;
}

- (void)scrollerWillBeginDragging:(LynxScrollInfo *)info {
  if (info.lynxView == nil) {
    // This method should be called synchronic when a UIScrollView in LynxView is scrolling. Info's
    // lynxView should not be nil.
    return;
  }
  LynxFluencyConfig config = [LynxScrollFluency constructFluencyConfigWithLynxScrollInfo:info];
  [_fluencyMonitor startWithFluencyConfig:&config];
}

- (void)scrollerDidEndDragging:(LynxScrollInfo *)info willDecelerate:(BOOL)decelerate {
  if (!decelerate) {
    LynxFluencyConfig config = [LynxScrollFluency constructFluencyConfigWithLynxScrollInfo:info];
    [_fluencyMonitor stopWithFluencyConfig:&config];
  }
}

- (void)scrollerDidEndDecelerating:(LynxScrollInfo *)info {
  LynxFluencyConfig config = [LynxScrollFluency constructFluencyConfigWithLynxScrollInfo:info];
  [_fluencyMonitor stopWithFluencyConfig:&config];
}

- (void)setEnabledBySampling:(LynxBooleanOption)enabledBySampling {
  [_fluencyMonitor setEnabledBySampling:enabledBySampling];
}

- (void)setPageConfigProbability:(CGFloat)probability {
  [_fluencyMonitor setPageConfigProbability:probability];
}

- (BOOL)shouldSendAllScrollEvent {
  return [_fluencyMonitor shouldSendAllScrollEvent];
}

@end
