module.exports = {
    extends: ['@commitlint/config-conventional'],
    rules: {
        'type-enum': [2, 'always', ['feat', 'fix', 'chore', 'docs', 'test', 'ci', 'build', 'style', 'refactor', 'perf']],
    },
};
