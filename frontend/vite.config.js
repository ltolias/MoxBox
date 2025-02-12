import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'


import viteCompression from 'vite-plugin-compression';

import { dependencies } from './package.json';


const reactDeps = Object.keys(dependencies).filter(key => key === 'react' || key.startsWith('react-'))


export default defineConfig({
  plugins: [
    react(),
    viteCompression({
      algorithm: 'gzip',
      threshold: 4096,
      deleteOriginFile: true
    })
  ],
  build: {
    rollupOptions: {
      output: {
        assetFileNames: "[hash:10][extname]",
        chunkFileNames: "[hash:10].js",
        entryFileNames: "[hash:10].js",
        manualChunks: {
          vendor: reactDeps,
          ...Object.keys(dependencies).reduce((chunks, name) => {
            if (!reactDeps.includes(name)) {
              chunks[name] = [name]
            }
            return chunks
          }, {}),
        }
      }
    }
  },
})


