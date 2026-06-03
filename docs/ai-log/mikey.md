# AI Interaction Log — Mikey Fennelly

This file records significant AI-assisted sessions for academic transparency and audit purposes.

---
## Session — 2026-05-17 to 2026-05-19

### What was asked
The user asked the AI to help bootstrap the entire project from scratch: convert the specification PDF into a structured AsciiDoc file, set up the CMake multi-module project, configure devbox and clang tooling, scaffold the REST API with a security filter chain, initialise the libkd shared library, add Ansible infrastructure, and wire up Docker Compose.

### What the AI produced
- `CMakeLists.txt` — multi-module CMake project with `libkd`, `kdctl`, `kds` targets, C++23 standard, Ninja build
- `cmake/` — initial `dependencies.cmake` pulling CLI11, nlohmann/json, cpp-httplib, spdlog, GoogleTest via FetchContent
- `devbox.json` — initial dev environment with OpenSSL, PlantUML, openjdk, clang-tools
- `.clang-format` and `.clang-tidy` — LLVM style, `readability-*`, `bugprone-*`, `modernize-*` checks
- `docs/spec.adoc` — full spec re-typed from PDF, lists numbered, unnecessary sections removed
- `docs/report.adoc` — communal report skeleton
- `kds/src/controller/Controller.cc` and `Controller.hh` — initial HTTP route scaffold using cpp-httplib
- `kds/src/configure.cc` — `kd::configure()` function reading `KD_PORT` and `KD_LOG_LEVEL` from env
- `kds/src/security/SecurityPredicates.hh` — security filter chain using Chain of Responsibility pattern, stub predicates
- `libkd/src/` — `Message.cc`, `Conversation.cc`, `User.cc` with initial plain-old-data structs
- `kdctl/src/main.cc` — CLI11 subcommands for `login`, `logout`, `conversations`; `Client::getConversations` method
- `ansible/` — initial playbooks and inventory for infrastructure configuration and deployment
- `docker-compose.yml` and `Dockerfile` — kds container with Nix build environment
- `README.adoc` — getting started instructions

### Key prompts and responses

**Prompt:** Convert the spec PDF to AsciiDoc and number all the lists properly.
**Response summary:** The AI produced `docs/spec.adoc` by re-reading the raw PDF text and reformatting it as AsciiDoc with numbered lists, section headings matching the original, and unnecessary boilerplate removed. The marking rubric sections were preserved verbatim.

**Prompt:** Bootstrap the CMake multi-module project with kds, kdctl, and libkd.
**Response summary:** The AI generated the top-level `CMakeLists.txt` with `add_subdirectory` calls and a `cmake/dependencies.cmake` using FetchContent for all third-party libraries. It set C++23 as the standard and configured Ninja as the generator. Per-module `CMakeLists.txt` files were written for each of the three targets with correct `target_link_libraries` entries.

**Prompt:** Implement the security filter chain and initial REST endpoints.
**Response summary:** The AI implemented a Chain of Responsibility pattern in `SecurityPredicates.hh` with a `SecurityFilterChain` class accepting a vector of predicate objects, each with a `Validate(request, response)` method. Initial predicates were stubs returning `true`. Route handlers for `GET /users/:userId/conversations`, `GET /api/health`, login, and logout were scaffolded. A `404` handler was added for unregistered routes.

**Prompt:** Add Ansible playbooks and Docker Compose for the infrastructure.
**Response summary:** The AI wrote initial Ansible playbooks covering VM configuration and deployment steps, with separate `configure_vm` and `deployment_vm` inventory files. The Dockerfile was written to use Nix as the build environment, compile the `kds` binary, and copy it into a runtime image. `docker-compose.yml` was updated to name the container `kds` and expose the configured port.

### Design decisions made

- **Chain of Responsibility for security predicates:** Chosen to allow each security concern (auth, rate limit, input validation) to be an independent object that can be inserted or removed without modifying the chain infrastructure. This maps directly to the security filter chain requirement in the spec.
- **libkd as a shared library linked by both binaries:** All domain logic (Message, Conversation, User PODs; Client HTTP wrapper) lives in `libkd` rather than being duplicated between `kdctl` and `kds`. Both executables link against the shared library.
- **`kd::configure()` free function:** Server configuration was extracted from `main.cc` into a separate function in `configure.cc` to keep `main.cc` minimal and testable.
- **C++23 standard:** Set to C++23 rather than C++20 to allow use of the latest STL features. Later revised to C++20 for compatibility with the submission environment.

### Critical evaluation

- **Accepted as-is:** The CMake structure was taken directly and has remained stable throughout the project.
- **Accepted as-is:** The Chain of Responsibility security predicate design — it was the pattern suggested by the spec wording.
- **Modified:** The initial devbox spec only included the clang binary package, not the dev headers. Additional packages had to be added incrementally as build errors surfaced.
- **Rejected:** An early suggestion to put conversation and message models directly in `kds` rather than `libkd` was rejected — moving them to `libkd` from the start avoided duplication once the client needed them too.

### Limitations / what the AI got wrong or missed

- The initial `CMakeLists.txt` used C++23 which later caused compatibility issues in the CI Docker build environment. The standard was quietly revised to C++20 without the AI flagging that this might break certain library headers.
- The initial Dockerfile used devbox as the build environment. This was replaced entirely with Nix in later sessions because devbox was not well-suited to hermetic container builds.
- The AI generated stub security predicates that returned `true` unconditionally, without flagging that this left the server completely unprotected at the time.

---
## Session — 2026-05-27 to 2026-05-29

### What was asked
The user asked the AI to set up a complete CI/CD pipeline: a GitHub Actions build workflow, commit linting with commitlint, a semver tagging workflow on merge to main, a Docker release pipeline triggered by semver tags, Ansible deployment scripts, a suite of dev/test shell scripts, and a full E2E TypeScript test suite with a Vitest runner and accompanying GHA workflow.

### What the AI produced
- `.github/workflows/build.yml` — GHA workflow running cmake + build + unit tests on every PR
- `.github/workflows/release.yml` — semver tag creation on merge to main using conventional commits
- `.github/workflows/docker-release.yml` — Docker image build and push triggered by semver tags via `workflow_run`
- `.commitlintrc` — commitlint config enforcing conventional commit format (`feat`, `fix`, `ci`, `chore`, etc.)
- `.githooks/` — pre-commit and commit-msg hooks wired to commitlint
- `scripts/build.sh` — parameterised build script with `--skip-tests`, `--rebuild`, timeout guards, and fatal error assertions
- `scripts/run-in-dev.sh`, `scripts/teardown.sh`, `scripts/verify-env.sh` — development lifecycle scripts
- `scripts/init.sh` — bootstrap script to set up the project from a fresh checkout
- `tests/e2e-flow-test.ts` — TypeScript E2E test suite using Vitest covering signup, login, message send/receive flow
- `tests/package.json` — Vitest test runner dependencies
- `tests/openapi.yaml` — OpenAPI spec for the `kds` REST API
- `ansible/deploy.yml` — deployment playbook covering cert copy, docker pull, container start
- `ansible/vars/secrets.example.yml` — example secrets file for Ansible vault

### Key prompts and responses

**Prompt:** Set up a GitHub Actions CI pipeline that builds and tests on every PR.
**Response summary:** The AI wrote `build.yml` using the ubuntu-latest runner, checking out the repo, entering the Nix devShell via `nix develop`, running `cmake -B build -GNinja && cmake --build build`, then running the GoogleTest binary. It added `concurrency:` settings to cancel in-progress runs on the same branch.

**Prompt:** Add commit linting and a semver tagging workflow.
**Response summary:** The AI installed `commitlint` and `@commitlint/config-conventional`, wrote `.commitlintrc.json`, and added a `commit-msg` githook that runs `commitlint` on each commit message. It then wrote a separate `release.yml` workflow that, on merge to main, reads the commit history since the last tag and creates a new semver tag based on the highest-level conventional commit type found (`feat` → minor bump, `fix` → patch bump).

**Prompt:** Write the E2E test suite for the API.
**Response summary:** The AI bootstrapped `tests/e2e-flow-test.ts` with Vitest, writing test cases for the full user flow: `POST /users/signup`, `POST /users/login`, `POST /conversations`, `POST /conversations/{id}/messages`, `GET /conversations/{id}/messages`. Each test stored the JWT from the login response and passed it as a Bearer token for authenticated requests. A `KD_HOST` environment variable was used so the same tests could run against localhost or the production VM.

**Prompt:** The Docker release workflow is not triggering on the tag push — the image never gets built.
**Response summary:** The AI identified that a workflow triggered on `push: tags:` cannot be triggered by another workflow pushing a tag due to GitHub's anti-recursion rule. The fix was to switch `docker-release.yml` to trigger on `workflow_run` with the semver tagging workflow as the source, so the Docker build only starts after the tag workflow completes successfully.

### Design decisions made

- **`workflow_run` instead of tag push trigger for Docker release:** GitHub Actions does not re-trigger workflows when a tag is pushed by another workflow using the default `GITHUB_TOKEN`. Using `workflow_run` is the correct workaround for chaining workflows without a PAT.
- **Separate inventories for configure vs deploy:** `configure_vm` and `deployment_vm` Ansible inventories were kept separate because configuration (initial VM setup, firewall, nginx) and deployment (pull image, restart container) are different operations run at different times.
- **`--skip-tests` flag in build.sh:** Provides a fast path for deployment builds where tests have already been run in CI. Making it a flag rather than a separate script keeps the build logic in one place.
- **TypeScript + Vitest for E2E tests:** Chosen over a bash-based test approach because Vitest provides structured test output, `expect()` assertions, and a clear pass/fail report that integrates cleanly with GitHub Actions test result display.

### Critical evaluation

- **Accepted as-is:** The `workflow_run` fix was correct and worked immediately once the trigger type was changed.
- **Accepted as-is:** The commitlint configuration — the conventional commit enforcement was adopted project-wide without modification.
- **Modified:** The initial `build.yml` ran `nix develop` with a subshell invocation that did not correctly inherit the Nix environment for the cmake step. Several iterations were needed to get the correct `nix develop --command` syntax.
- **Modified:** The `init.sh` bootstrap script initially assumed `nix` was installed on the host machine without checking. An assertion checking for the `nix` binary was added after a first-run failure.

### Limitations / what the AI got wrong or missed

- The initial E2E tests did not handle the case where the server was not yet running, producing confusing `ECONNREFUSED` errors rather than a clear "server not reachable" message. A pre-flight connectivity check was added manually.
- The AI generated `.githooks/` scripts without checking whether `git config core.hooksPath` was set or offering a way to install the hooks. A user running `git clone` would not have the hooks active until they ran `init.sh`.
- The semver tagging workflow incremented the minor version on every `feat` commit, including trivial ones. No mechanism was provided for skipping a version bump or for manual override.

---
## Session — 2026-05-30 to 2026-05-31

### What was asked
The user asked the AI to solve a serious Docker image build problem: the container built fine locally but was downloading the entire Nix dependency graph on every cold start in CI, making deployments impractically slow. The session covered Nix flake formalisation, build closure creation, multi-stage Dockerfile construction, Docker image weight reduction, and wiring everything into the GHA pipeline.

### What the AI produced
- `flake.nix` — Nix flake defining a `kds` devShell with all build and runtime dependencies pinned to `nixos-25.11`, plus a separate slim `kds` shell excluding dev-only tools to minimise closure size
- `nix/build.nix` — Nix derivation for the build environment, moved from `config/build.shell.nix`
- `scripts/create-toolchain-closure.sh` — script that calls `nix-store --export` on the full transitive closure of the build devShell and packs it as a tarball
- `scripts/build.sh` — extended with: `--logs` flag to export Docker build logs, auto-entry into the Nix devShell, `construct_runtime_closure_graph` function to produce a closure dependency graph
- `Dockerfile` — rewritten as a multi-stage build: `builder` stage unpacks the pre-built Nix closure tarball, enters the devShell, runs `cmake` and `ninja`; runtime stage uses Alpine and copies only the compiled binary and shared library dependencies
- `.github/workflows/build.yml` — updated to cache the Nix store on the host and mount `/nix` into the Docker builder container
- `docs/nix-store.md`, `docs/docker-closure-size.md` — documentation explaining how to trace flake inputs to download URLs and how to reason about closure size
- `scripts/build-analysis/` — scripts for analysing which Nix store paths are contributing most to closure size

### Key prompts and responses

**Prompt:** The container is downloading gigabytes of Nix packages on every CI run. Fix it.
**Response summary:** The AI identified two separate problems. First, the Dockerfile was running `nix-shell` on each cold start to enter the dev environment, triggering full package downloads every time. Second, the CI workflow was not caching the Nix store between runs. The solution involved two parallel tracks: create a pre-built toolchain closure that could be committed to GHA cache and unpacked into the builder container at the start of each build; and mount the host's `/nix` store into the Docker builder so that cached Nix paths do not need to be re-fetched.

**Prompt:** The Alpine runtime base is crashing because shared libraries are missing.
**Response summary:** The AI diagnosed that the compiled `kds` binary was dynamically linked against `libsodium`, `libssl`, and C++ runtime libraries that were in the Nix store but not copied into the Alpine runtime layer. It produced a `nix-store -q --tree` command to enumerate all runtime dependencies, then updated the Dockerfile's `COPY` instructions to copy each required `.so` path from the builder stage.

**Prompt:** Consolidate the three separate nixpkgs inputs in the flake — libsodium keeps being re-downloaded.
**Response summary:** The AI explained that having three different `nixpkgs` inputs (each pointing to different revisions) forced Nix to treat them as separate registries, causing `libsodium` to be fetched once per input. The fix was to merge all three inputs into a single pinned `nixpkgs` reference and update all `overlays` and `packages` references to use it. After this change, `libsodium` appeared in the closure exactly once.

**Prompt:** Document how to trace a flake input back to the download URL and Nix store path.
**Response summary:** The AI wrote `docs/nix-store.md` explaining the full trace: `nix flake metadata` shows each input's locked `rev`; `nix path-info` with the derivation attribute shows the store path; `nix store make-content-addressed` and `nix store ls` show the contents. The document was aimed at future contributors who need to debug unexpected re-downloads.

### Design decisions made

- **Mount host `/nix` into Docker builder:** Rather than downloading the Nix dependency graph inside each container build, the host's already-populated `/nix` store is bind-mounted into the builder stage. This means CI agents with a warm Nix cache do not perform any network fetches at build time.
- **Toolchain closure tarball committed to GHA cache:** `create-toolchain-closure.sh` packs the build devShell's full transitive closure as a `.tar.zst`. This tarball is uploaded as a GHA cache artifact keyed on the `flake.lock` hash, so it is reused across runs that have not changed the Nix inputs.
- **Multi-stage Dockerfile with Alpine runtime:** The builder stage is Nix-heavy (hundreds of MB); the runtime stage is Alpine with only the compiled binary and its shared library dependencies copied in. This keeps the production image small.
- **Slim `kds` devShell for runtime closure:** A separate Nix shell that excludes compilers, formatters, plantuml, and other dev tools was defined. The runtime closure is derived from this slim shell, not the full dev shell.
- **Single consolidated `nixpkgs` input:** All packages source from one pinned `nixpkgs` to prevent duplicate fetches of the same package at different revisions.

### Critical evaluation

- **Accepted as-is:** The consolidated `nixpkgs` input change — the reasoning was correct and the fix worked immediately.
- **Accepted as-is:** The decision to use a closure tarball rather than a Docker layer cache for Nix dependencies, because Docker layer caches are not portable across GHA runner instances.
- **Modified:** The initial multi-stage Dockerfile `COPY` instructions for shared libraries were incomplete — the AI listed the top-level `.so` files but missed indirect transitive dependencies. Several build-test cycles were needed to identify the full set.
- **Rejected:** A suggestion to use `nix bundle` to produce a self-contained executable was rejected because `nix bundle` with the `bundlers` attribute in flakes was not stable on nixos-25.11 and produced a much larger single binary than the multi-stage approach.

### Limitations / what the AI got wrong or missed

- The AI's initial `--print-dev-env` approach to extract the full toolchain closure did not correctly capture all transitive Nix store paths needed at runtime, requiring a follow-up using `nix-store --query --requisites` instead.
- The first version of `create-toolchain-closure.sh` only accepted a single tarball path argument, causing `unpack-store` to fail when more than one tarball existed. The script was patched to accept multiple tarballs.
- Several debug commits (`chore: debug commit`) were pushed to main during the iteration process. This suggests the AI was guiding changes through trial-and-error without a complete reproducible test setup, relying on CI as the test environment rather than a local equivalent.
- The AI did not flag upfront that mounting `/nix` into a Docker container requires the Docker daemon to have the host `/nix` directory available, which is not the case on GitHub-hosted runners without additional setup.

---
## Session — 2026-06-01

### What was asked
The user asked the AI to diagnose and fix a series of deployment failures: the server was failing to start on the production VM due to TLS certificate issues, the nginx reverse proxy was not forwarding traffic from port 80 to port 8080, Ansible was copying stale files and not picking up new env vars, and the health check script was crashing on startup validation.

### What the AI produced
- `scripts/health-check.sh` — complete rewrite with fail-fast pre-flight checks (connectivity, TLS cert validity, cert path existence), multi-target selection (`--target local|prod`), and meaningful exit codes
- `ansible/deploy.yml` — updated to explicitly copy TLS cert and key to the VM before starting the container; added a step to push the current branch to origin before deploying; added `--skip-tests` flag support
- `ansible/vars/secrets.example.yml` — updated with new required secrets entries
- `kds/src/` — added `fix: add SSL validity check and cert path logging` to diagnose startup failure — the server now logs the resolved cert and key paths at startup before attempting to bind
- CI cert generation scripts — updated to use a CA certificate hierarchy rather than a self-signed leaf cert, and updated `ansible/` to deploy the full CA + leaf pair to the VM
- `fix(certs/ansible): add deployment SANs` — Subject Alternative Name entries for the production VM hostname and IP added to the cert generation script so the cert is valid for both localhost and the remote IP
- `.github/workflows/build.yml` — logging improvements in the build script, network error handling

### Key prompts and responses

**Prompt:** The server is not starting on the VM. SSL_CTX_use_certificate_file is failing.
**Response summary:** The AI first added logging to the server's startup path so the resolved cert and key file paths were printed before the SSL context was created. This revealed that the paths in the deployed container's environment were pointing to a location that did not exist inside the container. The Ansible deploy play was not copying the cert and key to the VM before starting the container, so the bind mount path was empty. Adding an explicit `copy` task to `deploy.yml` before the `docker run` step resolved the failure.

**Prompt:** `curl http://production-ip/api/health` is getting a connection refused. It should go through nginx on port 80.
**Response summary:** The AI identified that the nginx config was directing traffic from port 80 to the wrong direction — the `proxy_pass` directive had the ports inverted (forwarding from 8080 to 80 rather than 80 to 8080). The corrected Ansible playbook updated the nginx config template with `proxy_pass http://localhost:8080` and reloaded nginx. A subsequent CI commit changed the outer Docker port to `80:8080` (host port 80 mapped to container port 8080) after confirming that the container should remain on 8080 internally.

**Prompt:** The health check script is crashing with `stoull: invalid argument` before even reaching the server.
**Response summary:** The AI traced the crash to `std::stoull` being called on `KD_PORT` before the variable had been validated as a non-empty numeric string. The rewritten `health-check.sh` added a pre-flight block that checks: the cert file path is non-empty, the file exists, `openssl x509 -noout -dates` returns exit 0, and the port variable is a valid integer — all before attempting any network connection.

### Design decisions made

- **CA hierarchy for TLS certs:** Moving from a self-signed leaf cert to a CA + leaf hierarchy allows the same CA cert to be distributed to clients once, and leaf certs to be rotated without redistributing the CA. The client's `--ca-cert` flag points to the CA cert rather than the leaf.
- **SANs for prod hostname and IP:** Certificates without a SAN matching the server's actual hostname or IP are rejected by modern TLS clients regardless of the CN field. Adding the prod VM's IP as a SAN in the cert generation script prevents the cert from being rejected when connecting directly by IP.
- **Explicit cert copy step in Ansible deploy:** Rather than assuming the cert was already on the VM from a previous play, the deploy play now explicitly copies the cert and key in every deployment run. This makes the deploy idempotent with respect to cert state.
- **Health check pre-flight before network call:** Structural failures (missing cert file, invalid port env var) are caught before any connection attempt, so the error message is actionable rather than a generic timeout.

### Critical evaluation

- **Accepted as-is:** The nginx port inversion fix — the error was straightforward once identified.
- **Accepted as-is:** The CA hierarchy approach — this is the correct production pattern.
- **Modified:** The initial port forwarding fix set the outer container port to `80:80` (mapping port 80 on the host to port 80 in the container, where nothing was listening). A further commit corrected this to `80:8080`.
- **Modified:** Several CI commits during this session were incremental debugging steps rather than complete solutions, suggesting the remote VM was being used as the test environment. Ideally, the cert and nginx issues would have been reproducible locally.

### Limitations / what the AI got wrong or missed

- The port forwarding direction was initially corrected in the wrong way: the AI fixed `proxy_pass` in nginx but did not simultaneously check that the Docker port binding was consistent, leading to another failing deployment before both were aligned.
- The AI did not initially check whether the TLS cert had the correct SANs by running `openssl x509 -text -noout | grep -A1 'Subject Alternative Name'` before suggesting the deploy. This would have identified the SAN issue without a deployment attempt.
- The `--skip-tests` flag was added to the Ansible deploy play at this stage, bypassing E2E tests that were running against the production server. This was pragmatic given the deadline but leaves the deployment pipeline without a gate on functional correctness.

---
## Session — 2026-06-02

### What was asked
The user asked the AI to clean up the repository for final submission: organise and prune the `docs/` directory, remove unused report files, add production health and version badges to the README, write CI/CD documentation, switch the deploy pipeline to pull the latest GitHub release tag rather than the current commit SHA, and configure a GitHub Actions workflow to automatically trigger deployment when a new release is published.

### What the AI produced
- `docs/` — reorganised: unused report files removed, redundant documentation pruned, remaining docs categorised into `docs/spec/` and `docs/cicd/`
- `docs/cicd/` — new directory with documentation covering the full CI/CD pipeline: build workflow, release tagging, Docker release, Ansible deployment
- `README.adoc` — added production health badge (`/api/health` endpoint) and version badge (GitHub latest release tag)
- `ansible/deploy.yml` — updated to fetch and deploy the latest GitHub release tag rather than a build-time commit SHA; added a step to pull the `docker-compose.yml` from the release tag rather than assuming it is already on the VM
- `.github/workflows/deploy.yml` — new workflow triggering Ansible deployment automatically when a new GitHub release is published
- `docs/spec/qualitative-tests/submission-requirements.md` — document mapping spec requirements to implementation evidence
- `docs/rigorous-critique.md` — self-critique of the codebase covering security, build reproducibility, and CI/CD completeness

### Key prompts and responses

**Prompt:** Organise the docs directory and remove anything unused.
**Response summary:** The AI listed all files in `docs/` and cross-referenced them against references in `README.adoc` and the spec. It identified a set of files that were no longer referenced from anywhere (old report stubs, draft documents superseded by newer versions) and proposed removing them. It also reorganised the remaining docs into logical subdirectories. The operation required two separate PRs because the initial restructure was reverted after breaking an existing doc link, then redone correctly.

**Prompt:** Add health and version badges to the README.
**Response summary:** The AI added a Shields.io badge pointing to the production `/api/health` endpoint (using the JSON response format to extract the `status` field) and a GitHub release badge showing the latest semver tag. Both badges were added to the top of `README.adoc` using AsciiDoc `image::` directives with appropriate `link=` attributes.

**Prompt:** The deploy pipeline is always pulling the image built from the current commit SHA. It should pull the latest release tag.
**Response summary:** The AI identified that `ansible/deploy.yml` was passing the commit SHA as the image tag via a `--build-arg VERSION={{ git_sha }}` pattern. The fix was to add a step that calls the GitHub API to resolve the latest release tag (`gh api repos/:owner/:repo/releases/latest --jq .tag_name`) and use the result as the Docker image tag to pull. The `docker-compose.yml` fetch was also updated to pull from the release artifact rather than the local working tree.

**Prompt:** Set up automatic deployment when a new GitHub release is published.
**Response summary:** The AI wrote `.github/workflows/deploy.yml` triggered on `release: types: [published]`. The workflow SSHes to the production VM using a stored secret, runs the Ansible deploy play, and posts the deployment status back to the release notes. An environment protection rule was noted as a recommended addition to require manual approval before deployment, though it was not configured as it requires GitHub Pro.

### Design decisions made

- **Deploy on GitHub release rather than on every tag:** Tagging and releasing are separate operations — a tag can be created without publishing a release. Triggering on `release: published` ensures deployment only happens for intentional releases, not for automated semver tags that may have been created in error.
- **Fetch `docker-compose.yml` from release artifact:** Rather than assuming the file on the VM is current, the deploy play now downloads `docker-compose.yml` directly from the GitHub release assets. This means the deployed configuration is always consistent with the tagged release.
- **GitHub API tag resolution in Ansible:** Using `gh api` inside the Ansible play to resolve the latest release tag, rather than passing it as an explicit parameter, means the play can be run manually without needing to look up the current version.
- **Submission requirements document:** A dedicated markdown file mapping each spec requirement to the line of code or file that satisfies it, intended to make the examiner's job easier and demonstrate that nothing has been missed.

### Critical evaluation

- **Accepted as-is:** The deploy-on-release workflow trigger — the reasoning for preferring `release: published` over tag push was sound.
- **Accepted as-is:** The badge syntax — both badges rendered correctly in the AsciiDoc preview.
- **Modified:** The initial docs restructure was committed and merged, then reverted (`Revert "chore: restructure scripts dir (#119)"`), then redone correctly in a follow-up PR. The revert was necessary because the first restructure moved files that were referenced by relative paths in other documents, breaking those links. The redo was done with link-checking first.
- **Rejected:** An initial suggestion to add an auto-merge rule so that release PRs would deploy without human review was rejected on the grounds that even at submission time, a broken deployment should require a human to approve before it goes live.

### Limitations / what the AI got wrong or missed

- The docs restructure needed to be reverted and redone, which required three PRs for what should have been one. The AI should have performed a link audit before proposing the restructure rather than after.
- The `deploy.yml` workflow was written without accounting for the Ansible vault password, which is required to decrypt `secrets.yml`. The workflow was later amended to pass the vault password from a GitHub Actions secret.
- The version badge was initially pointed at the wrong endpoint (the GitHub releases API rather than the releases page), meaning clicking the badge navigated to raw JSON rather than the releases UI. This was corrected manually.
- The AI did not flag that the `rigorous-critique.md` document contained opinions about the project's code quality that, if read by an examiner, could negatively frame the submission. The decision to include or remove that document was left to the user.
