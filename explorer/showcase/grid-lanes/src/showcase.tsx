import { root, useState } from '@lynx-js/react';

import './style.css';

type Mode = 'compact' | 'horizontal' | 'rtl';

interface ShowcaseProps {
  fixedMode?: Mode;
}

const ITEMS = [
  { id: '01', kind: 'PHOTO', title: 'Coastal light', size: 'tall' },
  { id: '02', kind: 'NOTE', title: 'Field note', size: 'short' },
  { id: '03', kind: 'VIDEO', title: 'City motion', size: 'medium' },
  { id: '04', kind: 'ALBUM', title: 'Weekend frames', size: 'wide' },
  { id: '05', kind: 'QUOTE', title: 'Design follows rhythm', size: 'short' },
  { id: '06', kind: 'PHOTO', title: 'Late afternoon', size: 'tall' },
  { id: '07', kind: 'STORY', title: 'Across the bridge', size: 'medium' },
  { id: '08', kind: 'AUDIO', title: 'Street sounds', size: 'short' },
] as const;

function Control({
  active,
  label,
  onTap,
  tag,
}: {
  active: boolean;
  label: string;
  onTap: () => void;
  tag: string;
}) {
  return (
    <view
      className={`control ${active ? 'control-active' : ''}`}
      bindtap={onTap}
      lynx-test-tag={tag}
    >
      <text className="control-label">{label}</text>
    </view>
  );
}

function App({ fixedMode }: ShowcaseProps) {
  const [infiniteTolerance, setInfiniteTolerance] = useState(false);
  const [rtl, setRtl] = useState(fixedMode === 'rtl');
  const [horizontal, setHorizontal] = useState(fixedMode === 'horizontal');
  const isFixed = fixedMode !== undefined;

  const lanesClass = [
    'lanes',
    infiniteTolerance || fixedMode === 'rtl' ? 'tolerance-infinite' : '',
    rtl ? 'lanes-rtl' : '',
    horizontal ? 'lanes-horizontal' : 'lanes-vertical',
  ]
    .filter(Boolean)
    .join(' ');

  return (
    <scroll-view className="page" scroll-y={!horizontal}>
      <view className="hero">
        <text className="eyebrow">CSS GRID LEVEL 3</text>
        <text className="title">Grid Lanes</text>
        <text className="subtitle">
          Composable masonry for mixed-height native content.
        </text>
      </view>

      {!isFixed && (
        <view className="controls">
          <Control
            active={infiniteTolerance}
            label={`Tolerance: ${infiniteTolerance ? 'infinite' : '0'}`}
            onTap={() => setInfiniteTolerance(!infiniteTolerance)}
            tag="grid-lanes-tolerance-toggle"
          />
          <Control
            active={rtl}
            label={rtl ? 'RTL' : 'LTR'}
            onTap={() => setRtl(!rtl)}
            tag="grid-lanes-rtl-toggle"
          />
          <Control
            active={horizontal}
            label={horizontal ? 'Horizontal' : 'Vertical'}
            onTap={() => setHorizontal(!horizontal)}
            tag="grid-lanes-axis-toggle"
          />
        </view>
      )}

      <view className="status">
        <text className="status-text" lynx-test-tag="grid-lanes-status">
          {horizontal ? 'row lanes · horizontal stacking' : 'column lanes'}
          {' · '}
          {rtl ? 'rtl' : 'ltr'}
          {' · '}
          {infiniteTolerance || fixedMode === 'rtl'
            ? 'flow-tolerance: infinite'
            : 'flow-tolerance: 0'}
        </text>
      </view>

      <scroll-view
        className={`feed-scroll ${horizontal ? 'feed-scroll-horizontal' : ''}`}
        scroll-x={horizontal}
      >
        <view className={lanesClass} lynx-test-tag="grid-lanes-container">
          {ITEMS.map((item, index) => (
            <view
              className={`card card-${item.size} card-${index + 1}`}
              key={item.id}
              lynx-test-tag={`grid-lanes-item-${index + 1}`}
            >
              <view className={`media media-${index + 1}`}>
                <text className="media-kind">{item.kind}</text>
                {item.kind === 'VIDEO' && (
                  <view className="play">
                    <text className="play-icon">▶</text>
                  </view>
                )}
              </view>
              <view className="card-copy">
                <text className="card-number">{item.id}</text>
                <text className="card-title">{item.title}</text>
              </view>
            </view>
          ))}
        </view>
      </scroll-view>
    </scroll-view>
  );
}

export function renderShowcase(fixedMode?: Mode) {
  root.render(<App fixedMode={fixedMode} />);
}
