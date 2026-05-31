# Automations

- Init environment
- Validate environment
- Full CI

## Configuring environments

### Configuring a dev machine



### Configuring a deployment VM

## Full CI pipeline

1. Run linting & formatting: `./lint.sh`

Checks source code against `.clang-format`, and `.clang-tidy`

2. Construct dependency closure: `./create-closure.sh`

Creates a tarball at `$PROJECT_ROOT/out`, 

3. Build a binary:  `./build.sh`

Runs a build flow with the provided closure dependencies. Nix pkgs only.

4. Generate SBOM/Source dependency report:  `./create-closure.sh`

Creates a report of all dependencies, direct and transitive. Logs nix-store misses.

5. Closure + Binary -> Dockerfile:  `./build.docker.sh`

Sources environment variables from your .env file, packages an image for the build.

6. Test against the Docker artifact:  `./create-closure.sh`


