# Security Policy for Aegis

Thank you for your interest in the security of the Aegis programming language. 

Aegis is currently an **experimental / research project**. While we take the safety and stability of the compiler and interpreter seriously, please be aware that the language is in active development and has known architectural limitations.

## Supported Versions

At this stage of development, security updates are only applied to the latest commit on the `main` branch. 

| Version | Supported          |
| ------- | ------------------ |
| `main`  | :white_check_mark: |
| < 0.3   | :x:                |

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

If you discover a security vulnerability within the Aegis compiler, interpreter, or runtime library, please report it via email to: **mradulumrao@gmail.com**

Please include the following in your report:
* A description of the vulnerability and its impact.
* Steps to reproduce the issue (including a minimal `.ae` script if possible).
* Details about your environment (OS, compiler used to build Aegis, etc.).

We will acknowledge receipt of your vulnerability report within 48 hours and strive to send you regular updates about our progress. 

## Scope

### In Scope
We are primarily interested in vulnerabilities in the C++ implementation of the language, such as:
* **Compiler/Interpreter Crashes:** Buffer overflows, use-after-free, or memory corruption in the C++ source (`aegis check`, `aegis run`, or `aegis build`) caused by parsing maliciously crafted `.ae` files.
* **Host Execution:** Arbitrary code execution on the host machine running the compiler during the `parse` or `check` phases.
* **Runtime Escapes:** Flaws in `aegis_runtime.c` that allow an executed Aegis program to break out of expected boundaries or corrupt memory in unintended ways.

### Out of Scope
The following are currently considered out of scope:
* **Known Limitations:** Issues explicitly documented in `DOCUMENTATION.md` or `README.md` (e.g., the lack of a full lifetime/borrow checker, or the fact that lists/maps alias on assignment).
* **Bugs in User Code:** If an Aegis developer writes an insecure program in Aegis, that is an application-level bug, not a language vulnerability.
* **Denial of Service (DoS):** Exhausting memory or CPU by writing infinite loops or massive recursive functions in `.ae` scripts (e.g., intentionally triggering the 500-depth stack guard).
