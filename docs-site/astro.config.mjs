import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import remarkMath from 'remark-math';
import rehypeKatex from 'rehype-katex';

export default defineConfig({
  site: 'https://stillwater-sc.github.io',
  base: '/mp-dsp',
  markdown: {
    remarkPlugins: [remarkMath],
    rehypePlugins: [rehypeKatex],
  },
  integrations: [
    starlight({
      title: 'Mixed-Precision DSP',
      description: 'Header-only C++20 library for mixed-precision digital signal processing',
      customCss: [
        'katex/dist/katex.min.css',
      ],
      social: [
        { icon: 'github', label: 'GitHub', href: 'https://github.com/stillwater-sc/mp-dsp' },
      ],
      editLink: {
        baseUrl: 'https://github.com/stillwater-sc/mp-dsp/edit/main/docs-site/',
      },
      sidebar: [
        {
          label: 'Getting Started',
          autogenerate: { directory: 'getting-started' },
        },
        {
          label: 'Fundamentals',
          autogenerate: { directory: 'fundamentals' },
        },
        {
          label: 'Signal Conditioning',
          autogenerate: { directory: 'conditioning' },
        },
        {
          label: 'Window Functions',
          autogenerate: { directory: 'windows' },
        },
        {
          label: 'Spectral Analysis',
          autogenerate: { directory: 'spectral' },
        },
        {
          label: 'Image Processing',
          autogenerate: { directory: 'image' },
        },
        {
          label: 'Filter Design',
          autogenerate: { directory: 'filter' },
        },
        {
          label: 'Multirate Signal Processing',
          autogenerate: { directory: 'multirate' },
        },
        {
          // The receiver front-end: IF to baseband. Renamed from
          // 'Software-Defined Radio' when the modulation/demodulation
          // section below was added, so the two SDR halves are
          // distinguishable in the sidebar.
          label: 'SDR Receiver Front-End',
          autogenerate: { directory: 'acquisition' },
        },
        {
          label: 'SDR Modulation & Demodulation',
          autogenerate: { directory: 'sdr' },
        },
        {
          label: 'Instrument Data Acquisition',
          autogenerate: { directory: 'instrument' },
        },
        {
          label: 'Analysis',
          autogenerate: { directory: 'analysis' },
        },
        {
          label: 'Pipeline Probes',
          autogenerate: { directory: 'probe' },
        },
        {
          label: 'Transfer Function Monitor',
          autogenerate: { directory: 'transfer-function' },
        },
        {
          label: 'State Estimation',
          autogenerate: { directory: 'estimation' },
        },
        {
          label: 'Mixed-Precision Arithmetic',
          autogenerate: { directory: 'mixed-precision' },
        },
        {
          label: 'API Reference',
          autogenerate: { directory: 'api' },
        },
      ],
    }),
  ],
});
