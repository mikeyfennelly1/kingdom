# e2e user flow

This document goes over instructions to create a test script, with:

- A typescript test flow.

- An environment setup shell script, which serves as the canonical interface to the test bench

- Integrating the script into github actions CI

## Typescript test suite (tests/e2e-flow.test.ts)

- [ ] Create a test flow, that runs from end to end of a user's experience.

- [ ] Split the test into test sections:

    - [ ] Happy paths: Basic user flows, providing valid data types.
    
    - [ ] Unhappy paths: Create requests that have invalid data formats. Look for naivete's in the controller code to think of ways in which you can do this.

    - [ ] Malicious paths: Try take malicious actions such as SQL injection, and other basic security vulnerabilities (integer/buffer overflow etc). Make sure to log what these attacks are, so that if they break a developer has enough information to patch the vulnerability.

- [ ] Ensure that vitest generates a test report - add the directory of generated artifacts to a .gitignore

## test.sh shell script

- [ ] Create a test env init script that does the following:

    - [ ] starts the server and database in docker.

    - [ ] Waits for the health endpoint to become available.

    - [ ] Run the tests from the previous section against the test config.

- [ ] Configurability ensure that the test script is configurable such that default values such as hostname, port, protocol (http/https) are capable of being overridden. This should be done via environment variable, and defaults/overrides set in the test.sh script. Fa

## Integration to GHA, run on merge to `origin/main`
NOTE: The above script should be appropriate to run on both the actions runner and locally. It should be a shell script.

- [ ] into a github action called tests.yml.

- [ ] Cache test dependencies like vitest and certain docker image layers for database etc on the runner.

- [ ] Run the action on merge to main line.

## Dev Documentation

- [ ] Create an openapi.yaml file of the REST interface for the kds server application.
