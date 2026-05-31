import { defineConfig } from 'vitest/config'

export default defineConfig({
  test: {
    // forks pool: each test file runs in a child process that exits cleanly,
    // closing its HTTPS connections — prevents the process from hanging after
    // the test run completes.
    pool: 'forks',
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
