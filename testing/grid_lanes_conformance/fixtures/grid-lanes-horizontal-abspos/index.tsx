import { root } from '@lynx-js/react';

import './index.css';

root.render(
  <view className="page" lynx-test-tag="page">
    <view className="lanes" lynx-test-tag="lanes">
      <view className="item first" lynx-test-tag="first" />
      <view className="item second" lynx-test-tag="second" />
      <view className="item explicit" lynx-test-tag="explicit" />
      <view className="absolute" lynx-test-tag="absolute" />
    </view>
  </view>
);
