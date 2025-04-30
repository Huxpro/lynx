// Copyright 2024 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#import <Lynx/LynxGestureDetectorDarwin.h>
#import <Lynx/UIScrollView+LynxGesture.h>
#import <objc/runtime.h>

@implementation LynxGestureConsumer

- (instancetype)init {
  if (self = [super init]) {
    _gestureConsumeStatus = LynxGestureConsumeStatusUndefined;
    _interceptGestureStatus = LynxInterceptGestureStateUnset;
  }
  return self;
}

- (void)consumeGesture:(BOOL)consume {
  _gestureConsumeStatus = consume ? LynxGestureConsumeStatusAllow : LynxGestureConsumeStatusBlock;
}

- (void)interceptGesture:(BOOL)intercept {
  _interceptGestureStatus =
      intercept ? LynxInterceptGestureStateTrue : LynxInterceptGestureStateFalse;
}

@end

@interface UIScrollView (LynxGestureDummy)

- (LynxGestureConsumer *)gestureConsumer;

- (UIPanGestureRecognizer *)lynxNativeGesturePanRecognizer;

- (void)setLynxNativeGesturePanRecognizer:(UIPanGestureRecognizer *)recognizer;

@end

@implementation UIScrollView (LynxGesture)

- (UIPanGestureRecognizer *)nativeGesturePanRecognizer {
  return objc_getAssociatedObject(self, @selector(lynxNativeGesturePanRecognizer));
}

- (void)setNativeGesturePanRecognizer:(UIPanGestureRecognizer *)lynxNativeGesturePanRecognizer {
  objc_setAssociatedObject(self, @selector(lynxNativeGesturePanRecognizer),
                           lynxNativeGesturePanRecognizer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (void)setupNativeGestureRecognizerIfNeeded {
  if (!self.nativeGesturePanRecognizer) {
    self.nativeGesturePanRecognizer =
        [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePanGesture:)];
    self.nativeGesturePanRecognizer.delegate = self;
    [self addGestureRecognizer:self.nativeGesturePanRecognizer];
  }
}

- (void)respondToGestureDidSet:(NSDictionary<NSNumber *, LynxGestureDetectorDarwin *> *)gestureMap {
  for (NSNumber *key in gestureMap) {
    LynxGestureDetectorDarwin *detector = gestureMap[key];
    // Check if a native gesture type exists.
    if (detector.gestureType == LynxGestureTypeNative) {
      // If found, ensure the native pan recognizer is set up on the scrollview.
      [self setupNativeGestureRecognizerIfNeeded];
      break;
    }
  }
}

- (void)handlePanGesture:(UIPanGestureRecognizer *)recognizer {
  LynxGestureConsumer *gestureConsumer = [self tryGetGestureConsumer];
  if (gestureConsumer && gestureConsumer.interceptGestureStatus == LynxInterceptGestureStateTrue) {
    return;
  }
  recognizer.state = UIGestureRecognizerStateCancelled;
}

- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gestureRecognizer {
  if (gestureRecognizer == self.nativeGesturePanRecognizer) {
    LynxGestureConsumer *gestureConsumer = [self tryGetGestureConsumer];
    return gestureConsumer.interceptGestureStatus == LynxInterceptGestureStateTrue;
  }
  return YES;
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer
    shouldRecognizeSimultaneouslyWithGestureRecognizer:
        (UIGestureRecognizer *)otherGestureRecognizer {
  LynxGestureConsumer *gestureConsumer = [self tryGetGestureConsumer];
  if (gestureConsumer && gestureConsumer.interceptGestureStatus == LynxInterceptGestureStateTrue) {
    otherGestureRecognizer.state = UIGestureRecognizerStateFailed;
    return NO;
  }
  return YES;
}

- (BOOL)respondToScrollViewDidScroll:(LynxGestureConsumer *)gestureConsumer {
  if (!gestureConsumer) {
    return YES;
  }

  CGPoint adjustContentOffset = self.contentOffset;

  if (gestureConsumer.adjustingScrollOffset) {
    // The gestureConsumer is adjusting contentOffset, avoid re-entrance
    return NO;
  } else if (gestureConsumer.gestureConsumeStatus == LynxGestureConsumeStatusUndefined) {
    // Do nothing
    gestureConsumer.previousScrollOffset = adjustContentOffset;
    return YES;
  } else if (gestureConsumer.gestureConsumeStatus == LynxGestureConsumeStatusBlock) {
    // Reset the content offset
    adjustContentOffset = gestureConsumer.previousScrollOffset;
    if (!CGPointEqualToPoint(adjustContentOffset, self.contentOffset)) {
      gestureConsumer.adjustingScrollOffset = YES;
      [self setContentOffset:adjustContentOffset];
      gestureConsumer.adjustingScrollOffset = NO;
      return NO;
    }
  } else if (gestureConsumer.gestureConsumeStatus == LynxGestureConsumeStatusAllow) {
    // Do nothing
    gestureConsumer.previousScrollOffset = adjustContentOffset;
    return YES;
  }

  return YES;
}

- (void)disableGesturesRecursivelyIfNecessary:(LynxGestureConsumer *)gestureConsumer {
  if (gestureConsumer && gestureConsumer.gestureConsumeStatus == LynxGestureConsumeStatusBlock) {
    for (UIView *subview in self.subviews) {
      [self disableGesturesRecursively:subview];
    }
  }
}

- (void)disableGesturesRecursively:(UIView *)view {
  // Disable all gesture recognizers for the current view
  for (UIGestureRecognizer *gestureRecognizer in view.gestureRecognizers) {
    gestureRecognizer.state = UIGestureRecognizerStateFailed;
  }
  for (UIView *subview in view.subviews) {
    [self disableGesturesRecursively:subview];
  }
}

- (BOOL)stopDeceleratingIfNecessaryWithTargetContentOffset:(inout CGPoint *)targetContentOffset {
  LynxGestureConsumer *gestureConsumer = [self tryGetGestureConsumer];
  if (gestureConsumer && gestureConsumer.gestureConsumeStatus == LynxGestureConsumeStatusBlock) {
    targetContentOffset->x = self.contentOffset.x;
    targetContentOffset->y = self.contentOffset.y;
    return YES;
  }
  return NO;
}

- (LynxGestureConsumer *)tryGetGestureConsumer {
  if ([self respondsToSelector:@selector(gestureConsumer)]) {
    LynxGestureConsumer *gestureConsumer = [self gestureConsumer];
    if ([gestureConsumer isKindOfClass:LynxGestureConsumer.class]) {
      return gestureConsumer;
    }
  }
  return nil;
}

- (void)dealloc {
  // Remove the gesture recognizer from the view
  if (self.nativeGesturePanRecognizer) {
    [self removeGestureRecognizer:self.nativeGesturePanRecognizer];
    self.nativeGesturePanRecognizer.delegate = nil;
  }
  self.nativeGesturePanRecognizer = nil;
}

@end
