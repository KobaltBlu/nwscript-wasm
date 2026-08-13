// SPDX-License-Identifier: MIT

import { copyFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");

await copyFile(resolve(projectRoot, "index.mjs"), resolve(projectRoot, "dist", "index.mjs"));
await copyFile(resolve(projectRoot, "index.d.ts"), resolve(projectRoot, "dist", "index.d.ts"));
