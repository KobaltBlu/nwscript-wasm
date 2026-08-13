import { accessSync, constants } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");

try {
  accessSync(resolve(projectRoot, "dist", "nwscript-compiler.wasm"), constants.F_OK);
} catch {
  throw new Error(
    "dist/nwscript-compiler.wasm is missing. Run `npm run build` before `npm test`.",
  );
}
