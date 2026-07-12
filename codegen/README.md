# Vortex RPC Code Generator

The codegen tool compiles `.proto`-compatible IDL files into Vortex C++ stubs and skeletons.

## Usage

```bash
vortex-codegen --lang=cpp --out=generated/ my_service.proto
```

## Architecture

```
.proto file → Parser (ANTLR or hand-written) → IR → C++ Generator
                                                    → Vortex Schema Generator
```

## Status

Phase 3 — not yet implemented. The codegen will share the IDL parser with the schema compiler.
