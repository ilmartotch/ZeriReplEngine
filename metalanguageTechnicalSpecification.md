# Meta-Language Technical Specification

## Overview

This document defines the meta-language used by the project REPL. The REPL is designed as a logical execution engine rather than a general-purpose programming language. Its purpose is to orchestrate commands, expressions, and external scripts through a minimal, shell-like, context-driven DSL.

The language prioritizes clarity over cleverness, experimentation over verbosity, and strict logical flow over syntactic freedom. The REPL is intended to be IDE- and UI-integratable; interactivity is a front-end concern, not a language constraint.

## Language Paradigm

The meta-language follows an imperative paradigm combined with a contextual DSL model. Commands do not merely execute actions; they activate a context. Each context defines which syntax is allowed, which expressions are valid, and which outputs are expected. The language is composed of isolated micro-languages bound to specific command contexts.

## Command Model and Context Activation

Commands are explicit and always start with a reserved symbol. A command occupies a single line, may accept flags, and creates or modifies an execution context. Once a context is active, subsequent input lines are interpreted according to that context’s rules until the context is exited or replaced. Only one command is allowed per line.

## Flags and Command Modifiers

Flags are allowed only on the command invocation line. They configure the behavior of the command or the rules of the context it creates. Flags are never allowed inside interactive input after a context has been entered. This separation simplifies parsing and guarantees predictable execution.

## Syntax Flexibility and Case Sensitivity

The language is permissive in syntax but strict in semantics. Commands and keywords are case-insensitive. Identifiers such as variables, functions, and modules preserve case but are resolved case-insensitively by default.

## Expressions and Evaluation

Expressions may be mixed with commands depending on the active context. Evaluation is implicit: any valid expression is automatically evaluated. Every valid input produces an output, and outputs are never silently discarded.

## Function Invocation and Nesting

The language supports function nesting only. Chaining, piping, and operator overloading are intentionally excluded to preserve readability and debuggability. Function arguments follow a mixed model: positional arguments first, followed by named arguments. Named arguments cannot precede positional arguments.

## Variables and Scope

The language uses dynamic typing. Variables may exist in local, session, or global scope. Promotion from local to global scope is allowed but must be explicit or accompanied by a warning. Variable names cannot shadow function names; such conflicts result in compilation failure.

## Output Model

Every execution produces an output. Outputs may be textual, structured objects, or typed errors with descriptive messages. Empty responses are never allowed.

## Script Execution Model

Scripts are executed as isolated modules following a black-box model. The REPL provides input, the script executes in isolation, and the script returns results or errors. The REPL does not introspect or manipulate the internal state of the script.

## Script Metadata

Scripts may optionally define a metadata header specifying execution context, constraints, or execution hints. Headers are optional and never required for execution.

## Extensibility and Plugins

User extensions may modify the resolver and the executor only. The parser and core syntax are immutable, ensuring structural stability, backward compatibility, and predictable evolution. Extensions can be implemented via scripts, compiled modules, or registered runtime hooks.

## Error Handling

Errors are explicit, typed, and always reported. Each error includes a clear message, contextual information, and optional correction hints. Silent failures are forbidden.

## IDE and UI Integration

The REPL is designed as a logic engine. All language decisions prioritize external IDE integration, UI-driven interaction, and programmatic usage. Autocomplete, suggestions, and validations are implementation concerns and do not affect core language semantics.

## Symbol Semantics

The following symbols have reserved semantic meaning. The list is intentionally minimal and extensible.

- / indicates command invocation and context activation
- -- indicates command flags and modifiers
- () indicates function invocation and expression grouping
- = indicates variable binding
- # indicates inline comments

## Design Constraints Summary

The language explicitly avoids implicit context switching, hidden execution, parser-level extensibility, and syntax overloading. Every feature must preserve linear readability, deterministic parsing, and explicit user intent.

