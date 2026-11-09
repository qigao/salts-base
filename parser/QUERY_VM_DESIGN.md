# Shared Query VM Design

## Decision

JSONPath, YPath, XPath, and DSV filter expressions (in
`parser/csv_parser`) share one format-neutral register VM in
`parser/query_vm`. The VM owns bytecode layout, forward-control-flow
validation, register initialization checks, dispatch, and leaf fast paths.
Each query language owns its parser, path traversal, value conversion, and
operator semantics through `qvm_exec_ops_t` callbacks.

No canonical btree is used. JSON, YAML, and XML already own their document
trees and lifetimes; introducing another tree would add copying, ownership
rules, and a second source of structural truth without helping expression
execution.

## Boundaries

- `query_vm`: instruction encoding, verifier, register loop, jumps, and
  single-instruction fast paths.
- JSONPath: lowers compiled filter expressions and retains RFC 9535 comparison,
  regex, UTF-8 length, count, and invalid-value behavior.
- YPath: lowers filter expressions while retaining its native YAML path walker,
  scalar parsing, aliases, slices, and node-set truthiness. A dialect extension
  adds bitwise operators (`&` `|` `^` `<<` `>>` `~`), a ternary
  (`cond ? a : b`, lowered to `QVM_OP_SELECT`), `idiv` (integer division), and
  `matches(str, re)` (bounded re engine, `re.h`) — these make YPath the first
  consumer of the QVM bitwise/select opcodes.
- XPath: lowers binary and unary expression ASTs while retaining cxml's
  node-set coercion, context position/size, function calls, and union resolver.
- DSV filter (`parser/csv_parser`): lowers column comparisons, LHS arithmetic
  (`+ - * /`, unary `-`), and left-associative `and`/`or` joins into one
  verified slice; register 0 carries the clause result. Column resolution and
  numeric coercion (DOM row vs field-view) stay in the DSV frontend through
  `qvm_exec_ops_t`. The native evaluators remain the fallback when lowering
  overflows the register file, and the direct-scan / index-seek fast paths
  keep deriving from the clause model.
  A self-contained dialect (not full XPath 2.0) adds `idiv` (integer division,
  lowered to `QVM_OP_DIV` with `arg == CXML_XP_OP_IDIV`), `if (c) then a else b`
  (lazy: only the selected branch is evaluated; the condition uses truthiness),
  `matches()` and `ends-with()`; `matches()` evaluates the pattern with the
  bounded re engine (`re.h`) and does not accept XSD regex flags or capture
  groups. `if` is a dedicated AST node (`CXML_XP_AST_IF_NODE`) evaluated by the
  cxml layer, so it composes with QVM lowering as an operand.

## Bytecode Contract

`qvm_instruction_t` uses `op/reserved/dst/arg/src1/src2`. Operand indexes are
owned by the frontend and interpreted by its resolver. Jumps are absolute
forward targets within the compiled slice. A frontend must verify a slice
before executing it and must provide the result in register zero.

The verifier rejects unknown opcodes, invalid operands/registers, backward or
out-of-range jumps, uninitialized source registers, and slices whose result
register is not initialized on every reachable exit.

The opcode pool also includes bitwise operations (`BAND` `BOR` `BXOR`
`LSHIFT` `RSHIFT` `BNOT`), string concatenation (`CAT`), and a three-way
select (`SELECT dst, src1=cond, src2=true, arg=false`). As with the other
binary/unary opcodes, the concrete semantics live in the frontend callbacks;
frontends opt in by compiling to these opcodes and handling them in
`qvm_exec_ops_t`.

## Resource And Diagnostic Contract

The compatibility entry points use finite defaults for instruction, operand,
regex, and execution-step counts. Callers that need stricter budgets use
`qvm_verify_slice_ex()` and `qvm_execute_ex()` with `qvm_limits_t`; limits are
checked before verifier allocation or instruction dispatch.

`qvm_diagnostic_t` reports status, bytecode instruction, opcode, relevant
operand, and a stable static message. `qvm_verify_error_t` remains an alias so
existing frontends retain source compatibility. `qvm_disassemble_slice()`
writes deterministic text into caller-owned storage and reports the required
size when the buffer is absent or too small. The VM performs no diagnostic I/O
on query execution paths.

## Migration And Rollback

The public JSONPath, YPath, and XPath APIs remain unchanged. A frontend can
revert its lowering independently to its language-native evaluator while the
shared VM remains an internal target; no document format or node ownership is
changed. Regression coverage must compare query results and errors at each
frontend boundary.
