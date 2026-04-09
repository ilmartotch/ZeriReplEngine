# Copilot Instructions

## Project Guidelines
- The user expects detailed explanations of the changes made, with a final code snippet and an explanation designed to be understandable by a junior software engineer, as well as adherence to standard implementations.

- Provide detailed change explanations with a final code snippet, and keep code comments as end-of-file /* */ blocks only (no inline comments).

- Use specific formatting rules.

- Follow naming conventions.

- Check references in the project to update all text, commands, and interactions with the project's user interface where necessary.

- Ensure the code is modular and reusable, adhering to object-oriented programming principles where appropriate.

- Extend the context to the necessary files.

- Perform the implementation like a senior C++ engineer and a senior Go engineer.

- Provide a detailed final explanation, checking for structural and syntactic consistency with the source code.

- Code comments must be structured as /* */ blocks placed ONLY at the end of the file. No inline comments (/// @brief, //, etc.) in the code body. The end-of-file comment explains changes, rationale, and integration. Follow the existing style seen in IProcessBridge.h and ProcessBridge.h.

- Always follow the guidelines; don't be vague or redundant in your code; no stupid shortcuts or patchwork. CONCRETE SOLUTIONS.

- When implementation model is unclear, proactively produce a focused set of clarification questions before proceeding to avoid missing references.

- Standardize all user-facing REPL messages in English across global, code, sandbox, and system diagnostics.

- Check and modify Ui Go evry time after a core change to ensure all user-facing interactions are consistent with the new language standardization, color and formatting rules on the output.

- Evry single implementation must be multi platform compatible, this project needs to run on Windows, Linux and MacOS. Always check for platform-specific code and provide cross-platform solutions.