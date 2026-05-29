import { defineConfig } from 'vitest/config'

export default defineConfig({
  test: {
    reporters: ['verbose', 'html'],
    outputFile: {
      html: 'test-results/index.html',
    },
    testTimeout: 30000,
    sequence: {
      shuffle: false,
    },
  },
})
