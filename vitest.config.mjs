import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    environment: "node",
    pool: "forks",
    fileParallelism: false,
    testTimeout: 30000,
    hookTimeout: 30000,
    setupFiles: ["./test/setup.mjs"],
    include: ["test/**/*.test.mjs"],
    server: {
      deps: {
        external: [/nwscript-compiler/],
      },
    },
  },
});
