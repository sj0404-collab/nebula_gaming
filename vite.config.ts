import { defineConfig, loadEnv } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import { VitePWA } from 'vite-plugin-pwa'

// https://vite.dev/config/
export default defineConfig(async ({ mode }) => {
  const plugins = [
    react(),
    tailwindcss(),
    // Installable PWA (Android/iOS/desktop browsers). API calls are never cached.
    VitePWA({
      registerType: 'autoUpdate',
      includeAssets: ['favicon.svg', 'apple-touch-icon.png', 'assets/*.jpg'],
      manifest: {
        name: 'Nebula OS — Big Pickle desktop',
        short_name: 'Nebula OS',
        description: 'Web desktop with Nebula Strike and a real opencode AI agent',
        theme_color: '#0b1220',
        background_color: '#0b1220',
        display: 'standalone',
        orientation: 'any',
        start_url: '/',
        icons: [
          { src: '/pwa-192.png', sizes: '192x192', type: 'image/png' },
          { src: '/pwa-512.png', sizes: '512x512', type: 'image/png' },
          { src: '/pwa-512-maskable.png', sizes: '512x512', type: 'image/png', purpose: 'maskable' },
        ],
      },
      workbox: {
        globPatterns: ['**/*.{js,css,html,svg,png,jpg,wasm,woff2,ttf}'],
        globIgnores: ['downloads/**'],
        maximumFileSizeToCacheInBytes: 6 * 1024 * 1024,
        navigateFallbackDenylist: [/^\/api\//, /^\/downloads\//],
        runtimeCaching: [{ urlPattern: /\/api\/opencode\//, handler: 'NetworkOnly' }],
      },
    }),
  ];
  try {
    // @ts-expect-error optional dev-only helper without type declarations
    const m = await import('./.vite-source-tags.js');
    plugins.push(m.sourceTags());
  } catch { /* optional */ }

  const env = loadEnv(mode, process.cwd(), ['VITE_', 'NEXT_PUBLIC_']);
  const processEnvDefines: Record<string, string> = {};
  for (const [key, value] of Object.entries(env)) {
    processEnvDefines[`process.env.${key}`] = JSON.stringify(value);
  }

  return {
    plugins,
    envPrefix: ['VITE_', 'NEXT_PUBLIC_'],
    define: processEnvDefines,
    // /api/opencode → Bun bridge (opencode/bridge.ts) → real `opencode serve`
    server: { host: true, allowedHosts: true as const, proxy: { '/api/opencode': { target: 'http://127.0.0.1:8787', changeOrigin: true } } },
    preview: { host: true, allowedHosts: true as const, proxy: { '/api/opencode': { target: 'http://127.0.0.1:8787', changeOrigin: true } } },
  };
})
