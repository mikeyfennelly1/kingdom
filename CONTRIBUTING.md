# Contribution guidelines
## Setup your environment 

1. Use Linux/macOS.

This is to ensure that there isn't environmental/platform specific conflictions relating to conventional binary paths in build-tools, scripts etc.

2. [https://www.jetify.com/docs/devbox/installing-devbox][Install devbox.]

3. [https://linear.app/mikey-fennelly/project/kingdom-64173990f5c0/overview][Join the Linear Project.]

## Development workflow
### Static analysis
#### Linting
This project uses the `.clang-format` tool to standardise style conventions. We have arbitrarily chosen to enforce the Google Style Guide as the best-practice for this project.

#### Formatting/automatic refactoring
We also use `.clang-tidy` to clean up repositories. This will help prevent against memory issues.

### Precommit hooks
### CI/CD
