# Guide: auditing the tool catalog using harness and test results

**Audience:** Humans or coding agents reviewing [`Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`](../../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json) for **redundancy**, **unused / underused** entries, and **inaccurate** (misleading or broken) tools.

**Related:** [`tool-registry.md`](tool-registry.md), [`tests/tool-iteration-log.md`](../../tests/tool-iteration-log.md) (numbered changelog; optional coverage audit notes), [`.cursor/rules/testing-long-running-harness.mdc`](../../.cursor/rules/testing-long-running-harness.mdc).

---

## 1. What harness results can and cannot prove

| Harness results **can** suggest… | Harness results **cannot** prove… |
|----------------------------------|-----------------------------------|
| Which tools the **model actually invoked** when turns completed | That a tool is **useless in production** because it never appeared in a batch |
| **tool_finish success:false** rates per tool (from `run.jsonl`) | Full **real-user** frequency (unless you also have product telemetry) |
| Systematic **transport/policy** failures (429, timeouts, harness policy) | Coverage of **every discipline** (audio, landscape, packaging, C++…) unless scenarios ask for them |
| Whether descriptions/schemas **steer** the model toward the right tool | That a tool is **redundant** only because two tools never appeared in the same run |

**Rule:** Treat **absence of invocations** in long-running tests as **“no opportunity in this batch,”** not **“delete this tool.”** Only treat long test silence as a **merge/demote signal** when **category intent + other signals** agree (see §5.2).

---

## 2. Inputs to collect before auditing

Always prefer the **latest** long-running batch unless the task names a specific run.

1. **Batch pointer:** [`tests/qualitative-tests/last-suite-summary.json`](../../tests/qualitative-tests/last-suite-summary.json) — read `batch_output_folder` / `batch_stamp`.
2. **Classification rollup:** `harness-classification.json` inside that batch folder (from [`tests/classify_harness_run_jsonl.py`](../../tests/classify_harness_run_jsonl.py) if present).
3. **Per-turn artifacts:** under the batch folder, each suite’s `turns/**/run.jsonl` — lines with `"type":"tool_finish"` (and deltas if you need tool names from stream events, depending on logger format).
4. **Catalog:** `UnrealAiToolCatalog.json` — `meta.categories`, per-tool `summary`, `parameters`, `permission`, `side_effects`, `status`, `modes`.
5. **Optional:** static coverage table in [`tests/tool-iteration-log.md`](../../tests/tool-iteration-log.md) — which tools are **high/low/gap/policy** for current vague suites.

**Minimum extraction for an agent script or manual pass:**

- Per `tool_id`: **invoke count**, **tool_finish success:false count**, **unique suites/steps** where it appeared.
- Global batch health: counts for `rate_limit`, `http_timeout`, `harness_policy`, `tool_finish_false` so you do not mis-attribute **infra failures** to **tool quality**.

---

## 3. Audit dimension A — Redundant tools

**Definition:** Two or more tools that achieve the **same user intent** with overlapping parameters, differing only in naming or thin wrappers, *without* a compelling reason (permission tier, safety, or distinct side effects).

**How to detect (use multiple signals):**

1. **Intent pairs:** For each tool, write one sentence: *“User wants: ___ → tool: ___.”* If two sentences are interchangeable, flag **candidate redundancy**.
2. **Catalog metadata:** Compare `permission`, `side_effects`, `category`. If two tools differ only in `summary` but not in safety class, merge is more likely.
3. **Harness behavior:** If the model **alternates** between two tools for the same turn family and **both succeed**, that is **redundancy risk** (or unclear routing). If **one always fails** or is never chosen, prefer fixing **prompting/catalog copy** before splitting tools further.

**Outcomes:**

| Finding | Typical action |
|--------|----------------|
| True duplicate | Merge parameters into one `tool_id`; deprecate alias in dispatch with migration note. |
| “Near duplicate” but different **permission** / **destructive** boundary | **Keep separate**; improve summaries so the model distinguishes read vs write. |
| Redundancy only in tests (model confusion) | Fix **summary + JSON schema**; add harness turn that forces disambiguation. |

---

## 4. Audit dimension B — Unused or underused tools

**Definition:** **Unused** must be split into **expected non-use** vs **suspicious non-use**.

**Buckets:**

| Bucket | Meaning | Action |
|--------|---------|--------|
| **No opportunity** | Scenarios never needed this capability | **No change** to catalog; optionally add a future vague scenario if this category is product-important. |
| **Opportunity but model avoided** | User text implied the capability; model used chat/console or wrong tool | **Inaccuracy / routing** issue (§5); fix docs, schema, or add a smaller stepping-stone tool. |
| **Invoked but always fails** | `tool_finish` success:false dominates | **Inaccuracy / implementation** bug; prioritize fix over removal. |
| **Policy / safety** | Tool is rarely used **on purpose** (e.g. arbitrary exec) | **Document** as intentional; do not score as “unused bad.” |

**How to use test results:**

1. Build **invocation frequency** from `run.jsonl` across the batch.
2. Cross-check **static gap list** in `tests/tool-iteration-log.md`: a tool marked **gap** there is **expected** for current suites.
3. Flag **suspicious** unused only when: **(a)** `summary` claims a common editor action, **and** **(b)** scenarios in that batch should have exercised it, **and** **(c)** the model chose a weaker workaround repeatedly.

**Outcomes:**

| Finding | Typical action |
|--------|----------------|
| Unused, niche discipline, product still wants it | **Tier B** in docs; keep catalog entry; optional `context_selector` deprioritization. |
| Unused, overlaps another tool | Merge (§3). |
| Unused, **status: research**, never implemented | Either implement or mark **`future` / remove** with changelog. |

---

## 5. Audit dimension C — Inaccurate tools

**Definition:** The tool **misrepresents** what it does, accepts parameters it rejects at runtime, or **fails** under valid agent use — including **systematic** `tool_finish` failures not explained by rate limits.

**Subtypes:**

1. **Summary/schema drift:** JSON schema or `summary` does not match dispatch validation (wrong required fields, renamed args).
2. **Implementation bugs:** Handler throws, returns `success:false` with opaque errors for valid inputs.
3. **Routing confusion:** Model picks **wrong** tool because naming overlaps another (fix summaries, not always code).
4. **Harness-only false negatives:** Failure is actually **429 / timeout / policy** — use `harness-classification.json` and `error_message` on `run_finished` to separate from tool logic.

**How to detect from tests:**

1. Filter `tool_finish` where `success:false`; bucket by `tool` name and **result_preview** / error string if logged.
2. For top offenders, open the matching dispatch file under `Plugins/UnrealAiEditor/.../Tools/` and compare to catalog **parameters**.
3. Re-read failing **turn message** (`turn_messages/turn_*.txt` or suite JSON `request`) — was the user intent aligned with the tool?

**Outcomes:**

| Finding | Typical action |
|--------|----------------|
| Schema/summary wrong | Patch catalog + any dispatch coercion docs; run harness again. |
| Handler bug | Fix C++; add regression harness step if cheap. |
| Model routing | Tweak **summaries**, `embedding_hint`, tool ordering in profile — not new tools. |

---

## 6. Recommended workflow for an agent (step-by-step)

1. **Resolve latest batch** from `tests/qualitative-tests/last-suite-summary.json` → set `BATCH_ROOT`.
2. **Load** `BATCH_ROOT/harness-classification.json` and note **global failure mix** (429 vs tool vs policy). If 429 dominates, **down-rank** conclusions about tool accuracy for that batch.
3. **Scan** all `BATCH_ROOT/**/run.jsonl` for `tool_finish` lines; aggregate **per-tool** success/failure and invocation counts.
4. **Join** with full `tools[]` list from `UnrealAiToolCatalog.json` — tools with **zero** invocations get tag **`no_opportunity_in_batch`** unless suite text clearly required them (then tag **`avoidance`** for follow-up).
5. **Classify** each tool into: **redundant_candidate**, **unused_expected**, **unused_suspicious**, **inaccurate_from_failures**, **policy_intentional**, using §§3–5.
6. **Produce** a short report: **Top 5** `tool_finish` failures by count, **Top 5** redundant pairs, **Top 5** “suspicious unused” with **why** tests did not cover them.
7. **Recommend** concrete PRs: catalog-only vs code vs new harness turns — **one category per PR** when possible.

---

## 7. Output template (paste into issues or PRs)

```text
## Batch analyzed
- batch_stamp: …
- batch_root: …

## Health
- rate_limit / http_timeout / harness_policy counts: …
- Interpretation: …

## Redundancy
- Pairs: …
- Recommendation: merge | clarify docs | no change

## Unused
- Expected (no scenario): …
- Suspicious (scenario implied use): …

## Inaccuracy
- tool_id — failure pattern — likely cause — fix owner (catalog vs dispatch)

## Follow-ups
- [ ] Harness scenario additions (if coverage gap)
- [ ] Catalog JSON edits
- [ ] Code fixes
```

---

## 8. When to run this audit

- After **meaningful** changes to the catalog or dispatch layer.
- After a **full** long-running batch (or monthly), even if results are “green,” to catch **low-frequency** wrong-tool patterns.
- Before **removing** or **merging** tools: require **either** telemetry **or** explicit product decision **plus** this checklist.

This guide is intentionally conservative: **tests narrow the opportunity set**; **product judgment** decides what stays in the catalog for users and workflows the harness does not simulate.
