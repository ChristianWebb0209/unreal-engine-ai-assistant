# Unreal AI Editor Tooling Architecture Guide

## Overview

This document outlines the core failure modes encountered when building LLM-driven tooling systems for Unreal Engine, and defines a rigorous architecture for designing reliable, scalable AI-assisted editor workflows.

The goal is to transition from fragile, low-level agent control toward a structured system where:

* The LLM plans high-level intent
* Deterministic systems execute graph mutations
* Tools encapsulate meaningful, composable operations

---

## 1. Core Failure Modes

### 1.1 Overly Low-Level Tooling

**Anti-pattern:**

* `AddNode`
* `ConnectPin`
* `SetPinValue`

These require the model to:

* Understand Unreal’s graph semantics
* Track node identities
* Manage execution ordering
* Handle edge cases

**Result:**

* High failure rate
* Compounding errors across multi-step operations
* Non-deterministic outputs

**Conclusion:**
LLMs are not reliable graph construction engines at this level of abstraction.

---

### 1.2 Lack of Deterministic Execution

If tool execution:

* Depends on implicit editor state
* Produces inconsistent outputs
* Lacks validation

Then the system becomes non-recoverable.

**Required properties:**

* Idempotency (where possible)
* Clear success/failure signals
* Structured outputs

---

### 1.3 Missing or Weak Feedback Loops

If the LLM does not receive:

* Execution results
* Error messages
* Updated state

Then it cannot correct itself.

**Failure pattern:**

* Repeated invalid actions
* Hallucinated success

---

### 1.4 Context Overload and Irrelevance

Indexing entire Unreal projects introduces:

* Noise
* Latency
* Reduced retrieval precision

**Key insight:**
The model performs best when given the *minimum sufficient context*.

---

### 1.5 Unreal as a Stateful System

Unreal Engine introduces:

* Hidden editor state
* Object references
* Mutable graphs

This violates assumptions of typical stateless tool-calling systems.

**Implication:**
State must be explicitly modeled and controlled outside the LLM.

---

## 2. Architectural Principles

### 2.1 LLM as Planner, Not Executor

The LLM should:

* Select tools
* Provide high-level parameters

The LLM should NOT:

* Manage node IDs
* Perform graph wiring logic
* Track execution state

---

### 2.2 Deterministic Execution Layer

All Unreal modifications must be handled by a deterministic system that:

* Validates inputs
* Applies changes
* Returns structured outputs

---

### 2.3 Tool Abstraction Layer

Tools must encapsulate:

* Multi-step operations
* Unreal-specific complexity
* Error handling

---

## 3. Tool Design Guidelines

### 3.1 Avoid Low-Level Primitives

**Do NOT expose:**

* Raw node creation
* Manual pin connections

---

### 3.2 Use Parameterized High-Level Tools

**Bad design:**

```
CreateLineTraceFromCamera()
CreateLineTraceFromActor()
```

**Good design:**

```
CreateLineTrace({
  origin: "camera | actor | custom",
  debug: boolean,
  channel: string,
  ignoreActors: Actor[]
})
```

**Benefits:**

* Reduces tool count
* Improves retrieval accuracy
* Simplifies reasoning

---

### 3.3 Atomic but Meaningful Tools

Each tool should:

* Perform a complete logical operation
* Be independently valid
* Avoid partial graph states

**Examples:**

* `CreateLineTrace(...)`
* `BranchOnHitResult(...)`
* `PrintString(...)`

---

### 3.4 Avoid Tool Explosion

Do not create tools for every variant.

Instead:

* Use parameters
* Normalize behavior

---

## 4. Planning and Execution Model

### 4.1 Multi-Step Plans

Complex behavior should be represented as sequences:

```
[
  { tool: "CreateLineTrace", ... },
  { tool: "BranchOnHitResult", ... },
  { tool: "PrintString", ... }
]
```

---

### 4.2 State Ownership

The system (not the LLM) must:

* Track created nodes
* Manage references
* Resolve dependencies

**Example:**

Tool output:

```
{ traceNodeId: "node_12" }
```

Internal system maps and tracks usage.

---

### 4.3 Abstract References

The LLM should refer to outputs abstractly:

* "use previous trace result"

NOT:

* "connect pin X of node_12"

---

## 5. Tool Retrieval Strategy

### 5.1 Embedding-Based Selection

Using vector similarity + top-K retrieval is recommended.

**Requirements:**

* Clear, descriptive tool metadata
* Distinct semantic boundaries between tools

---

### 5.2 Tool Description Quality

**Bad:**

* "Creates a node"

**Good:**

* "Performs a line trace and returns hit result including actor, location, and normal"

---

## 6. Development Strategy

### 6.1 Start Narrow

Do NOT attempt generality early.

Instead:

1. Select a single task
2. Build one high-quality tool
3. Validate reliability

---

### 6.2 Incremental Expansion

Add tools only when:

* Existing tools are stable
* New failure cases are identified

---

### 6.3 Debugging Framework

When failures occur, classify them as:

* Planning failure (wrong tool chosen)
* Execution failure (tool misbehaves)
* State failure (context mismatch)

Address each category separately.

---

## 7. When to Consider Fine-Tuning

Fine-tuning is NOT a first-line solution.

Only consider if:

* Tools are well-designed
* Execution is deterministic
* Failures are due to tool selection ambiguity

Even then, prefer:

* Better tool descriptions
* Few-shot examples

---

## 8. Target System Characteristics

A successful system will exhibit:

* High reliability on constrained tasks
* Predictable execution
* Minimal hallucination
* Clear separation between planning and execution

---

## 9. Key Mindset Shift

Do not build:

> "AI that controls Unreal"

Build:

> "Deterministic Unreal systems that AI can invoke"

This inversion is critical for scalability and reliability.

---

## 10. Expected Scale

A mature system may include:

* 50–150 well-designed tools

However:

* These emerge incrementally
* They are parameterized
* They are semantically distinct

---

## Conclusion

The primary bottleneck in LLM-driven Unreal tooling is not model capability, but system design.

Focusing on:

* abstraction
* determinism
* structured execution

will yield significantly better results than increasing model complexity or fine-tuning prematurely.
