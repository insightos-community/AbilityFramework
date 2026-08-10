import * as path from 'node:path';
import { defineConfig } from '@rspress/core';
import { pluginMermaid } from 'rspress-plugin-mermaid-js';
import readingTime from 'rspress-plugin-reading-time';

export default defineConfig({
  root: path.join(__dirname, 'docs'),
  lang: 'en',
  title: 'AbilityFramework',
  icon: '/logo.svg',
  llms: true,
  locales: [
    {
      lang: 'zh',
      label: '简体中文',
      title: 'AbilityFramework',
    },
    {
      lang: 'en',
      label: 'English',
      title: 'AbilityFramework',
    },
  ],
  themeConfig: {
    socialLinks: [
      {
        icon: 'github',
        mode: 'link',
        content: 'https://github.com/insightos-community/AbilityFramework',
      },
    ],
    llmsUI: {
      viewOptions: ['markdownLink', 'chatgpt'],
    },
  },
  plugins: [
    pluginMermaid(),
    readingTime(),
  ],
});
