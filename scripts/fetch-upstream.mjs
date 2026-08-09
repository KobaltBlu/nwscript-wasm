// SPDX-License-Identifier: MIT

import { readFile, mkdir, access } from "node:fs/promises";
import { constants } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const config = JSON.parse(await readFile(resolve(projectRoot, "upstream.json"), "utf8"));
const dependencyDir = resolve(
  projectRoot,
  process.env.NEVERWINTER_NIM_DIR ?? config.path,
);
const requestedRef = process.env.NEVERWINTER_NIM_REF ?? config.ref;

function git(args, cwd = projectRoot) {
  // Do not use shell:true here. On Windows it causes cmd.exe to re-parse
  // arguments, breaking URLs and paths containing spaces (for example,
  // C:\\Users\\First Last\\...). git.exe can be spawned directly.
  const result = spawnSync("git", args, {
    cwd,
    stdio: "inherit",
    shell: false,
  });

  if (result.error) {
    throw result.error;
  }
  if (result.status !== 0) {
    throw new Error(`git ${args.join(" ")} failed with exit code ${result.status}`);
  }
}

async function exists(path) {
  try {
    await access(path, constants.F_OK);
    return true;
  } catch {
    return false;
  }
}

await mkdir(dirname(dependencyDir), { recursive: true });

if (await exists(resolve(dependencyDir, ".git"))) {
  console.log(`Updating neverwinter.nim dependency in ${dependencyDir}`);
  git(["fetch", "--tags", "origin"], dependencyDir);
} else if (await exists(resolve(projectRoot, ".git"))) {
  // Prefer the declared submodule when this port itself was cloned with git.
  console.log(`Initializing neverwinter.nim submodule in ${dependencyDir}`);
  const submodule = spawnSync(
    "git",
    ["submodule", "update", "--init", "--recursive", "--", config.path],
    {
      cwd: projectRoot,
      stdio: "inherit",
      shell: false,
    },
  );

  // Archives do not preserve gitlinks, and a consumer may also copy this
  // project into an already-initialized repository. Fall back to a regular
  // dependency clone when there is no usable submodule entry.
  if (submodule.error || submodule.status !== 0 || !(await exists(resolve(dependencyDir, ".git")))) {
    git(["clone", config.repository, dependencyDir]);
  }
} else {
  // Archive distribution fallback: still keep upstream external rather than
  // vendoring compiler source into this package.
  console.log(`Cloning neverwinter.nim dependency into ${dependencyDir}`);
  git(["clone", config.repository, dependencyDir]);
}

console.log(`Checking out neverwinter.nim ${requestedRef}`);
git(["checkout", "--detach", requestedRef], dependencyDir);

const rev = spawnSync("git", ["rev-parse", "HEAD"], {
  cwd: dependencyDir,
  encoding: "utf8",
  shell: false,
});
if (rev.error) {
  throw rev.error;
}
if (rev.status !== 0) {
  throw new Error("Unable to resolve neverwinter.nim dependency revision");
}

console.log(`neverwinter.nim ready at ${rev.stdout.trim()}`);
