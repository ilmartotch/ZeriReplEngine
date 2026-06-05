# Script editor

The script editor is available inside language contexts (`$lua`, `$python`, `$js`, `$ts`, `$ruby`).

## Write a new script

1. Enter ScriptHub: `$code`
2. Enter a language context, for example: `$python`
3. Open a new script: `/new hello`
4. Type script lines in the editor buffer
5. Save with `/save`

Expected save output:

```text
Script saved: hello
```

## Load and edit an existing script

Run:

```text
/edit hello
```

Expected output:

```text
Python editor opened with script: hello
```

After editing, save with `/save`.

## Run scripts

Run a named script:

```text
/run hello
```

Run the last in-memory buffer (no name):

```text
/run
```

## Show and list scripts

```text
/list
/show hello
```

## Delete scripts

```text
/delete hello
```
