Read `docs/spec.adoc` in full. Then read the relevant source files in the codebase to understand what has actually been implemented.

The argument passed is: $ARGUMENTS

Based on the argument, generate the corresponding document and write it to the path shown below. If no argument is given, list the available reports and what each covers.

---

## cover → docs/reports/cover.md
The project cover document required for submission. Include:
- Group name and GitHub repository URL
- Full name and student ID of each group member (use placeholders like [STUDENT_NAME], [STUDENT_ID] where you don't know)
- A breakdown of each member's contributions — estimated percentage and specific features worked on (use placeholders where unknown)
- Any design summaries required by the spec

---

## crypto → docs/reports/crypto.md
The cryptographic design document (target 2-6 pages) required by the Cryptography subject. Must cover:
- Threat model: state security properties against (a) passive network attacker, (b) active network attacker, (c) honest-but-curious server, (d) fully compromised server. Properties NOT held in case (d) must be named explicitly.
- Construction walkthrough with diagrams (use ASCII/text diagrams) covering: registration, key publication, send, receive, storage at rest
- Justification of every cryptographic primitive at parameter level — algorithm, parameters, security property relied upon, and why those parameters are appropriate. "It's standard" is not a justification.
- Citations to RFCs or papers with section numbers for any protocol used
- Known limitations stated honestly
Base the document on what is actually implemented or planned in the codebase. Where something is not yet implemented, note it as planned and describe the intended design.

---

## pentest → docs/reports/pentest.md
The penetration testing / vulnerability report required by the Networks & Cybersecurity subject. Structure it as:
- Scope and methodology
- For each vulnerability class from the spec (Improper Input Validation, Broken Authentication, Broken Access Control, Cryptographic Issues, Injection, Security Misconfiguration, Sensitive Data Exposure, Vulnerable Components): what was tested, findings, severity, remediation status
- Summary of findings table
- Conclusion
Base findings on actual analysis of the codebase — identify real issues, don't fabricate findings.

---

## network → docs/reports/network.md
The network architecture documentation required by the Networks & Cybersecurity subject. Include:
- Architecture diagram (ASCII)
- All components and how they connect (kdctl, kds, PostgreSQL, blockchain sidecar, Ethereum Sepolia, verification page)
- All external connections and services documented
- TLS/SSL configuration and certificate verification approach
- Port and protocol summary table

---

Create the `docs/reports/` directory if it doesn't exist. Write in clear, academic English. Be honest about what is implemented vs planned — do not overstate the current state of the project.
