import { root } from '@lynx-js/react';

import './index.css';

root.render(
  <view className="page" lynx-test-tag="page">
    <view className="lanes" lynx-test-tag="lanes">
      <view className="item item1" lynx-test-tag="item1" />
      <view className="item item2" lynx-test-tag="item2" />
    </view>
  </view>
);
