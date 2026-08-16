import { createRequire } from 'node:module';

import { defineConfig } from '@lynx-js/rspeedy';
import { pluginReactLynx } from '@lynx-js/react-rsbuild-plugin';

const require = createRequire(import.meta.url);
const { getEncodeMode } = require('../../oliver/lynx-tasm');

const entry = process.env.GRID_LANES_FIXTURE_ENTRY;
if (!entry) {
  throw new Error('GRID_LANES_FIXTURE_ENTRY is required');
}

const pluginGridLanes = () => ({
  name: 'grid-lanes-compile-options',
  setup(api) {
    api.modifyRspackConfig((config) => {
      const templatePlugin = config.plugins?.find(
        (plugin) => plugin?.constructor?.name === 'LynxTemplatePlugin'
      );
      if (!templatePlugin) {
        throw new Error('LynxTemplatePlugin is required');
      }
      const LynxTemplatePlugin = templatePlugin.constructor;
      config.plugins ??= [];
      config.plugins.push({
        apply(compiler) {
          compiler.hooks.thisCompilation.tap(
            'grid-lanes-compile-options',
            (compilation) => {
              const hooks =
                LynxTemplatePlugin.getLynxTemplatePluginHooks(compilation);
              hooks.beforeEncode.tapPromise(
                'grid-lanes-compile-options',
                async (args) => {
                  args.encodeData.compilerOptions.enableGridLanes = true;
                  args.encodeData.sourceContent.config.enableGridLanes = true;
                  return args;
                }
              );
              hooks.encode.tapPromise(
                {
                  name: 'grid-lanes-local-encoder',
                  stage: 0,
                },
                async ({ encodeOptions }) => {
                  const encode = getEncodeMode('napi');
                  const { buffer, lepus_debug } = await Promise.resolve(
                    encode(encodeOptions)
                  );
                  return { buffer, debugInfo: lepus_debug };
                }
              );
            }
          );
        },
      });
      return config;
    });
  },
});

export default defineConfig({
  source: {
    entry,
  },
  output: {
    filename: 'main.lynx.bundle',
  },
  plugins: [pluginReactLynx(), pluginGridLanes()],
});
