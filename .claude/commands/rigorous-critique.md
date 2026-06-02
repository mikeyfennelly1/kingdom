# Rigorous critical analysis of project

This is an instruction set which defines how to orchestrate an analysis
of this codebase, and the quality of the submission.
The test suite is mostly execution of deterministic test suite.
Your role is as a summarizer of the test suite, and you do not have
authority to critique specific aspects unless specifically instructed.

## Natural language test specifications, and assertation schematics

The test names are composed to be answered as either unequivocally true
or false.

Your answer:

1. State how you deduced your answer: Giving reason, critical analysis
of the proposition, and reflective analysis on the rigor of your
logic.
2. Produce a markdown artifact, including test proposition, hypothesis of
answers to these questions, and rationale to suggest that hypothesis.

## Deterministic Tests

The test suite is broken into the following categories:

1. Networking
2. C++
3. Cryptography
4. Blockchain

### For the software

1. Static analysis (linters, shell scripts)

2. Software tests.

3. Runtime tests (e2e tests)

### For documentation, and other ancillary artifacts

Run the following commands in bash:

```bash
./scripts/test.sh
```

