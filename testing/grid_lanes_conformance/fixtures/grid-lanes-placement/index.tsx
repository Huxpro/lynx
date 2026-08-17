import { root } from '@lynx-js/react';

import './index.css';

root.render(
  <view className="page" lynx-test-tag="page">
    <view className="lanes" lynx-test-tag="lanes">
      <view className="item last" lynx-test-tag="last" />
      <view className="item spanning" lynx-test-tag="spanning" />
      <view className="item automatic" lynx-test-tag="automatic" />
    </view>
  </view>
);
