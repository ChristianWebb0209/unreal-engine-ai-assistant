#pragma once

/**
 * Blueprint layout policy for AI tools (user-owned graphs).
 *
 * Defaults:
 * - Prefer layout_scope "ir_nodes" when patching existing busy graphs so only IR-touched nodes are repositioned.
 * - Use "full_graph", blueprint_format_graph, and blueprint_compile format_graphs only when the user explicitly
 *   wants a whole-graph readability pass—otherwise existing manual layout and comment boxes are preserved outside
 *   the materialized IR set.
 * - auto_layout runs the vendored horizontal layout only when IR positions are all zero (see apply_ir implementation);
 *   non-zero x,y on IR nodes are treated as author intent and skip auto layout for those patterns.
 *
 * Enforcement lives in tool args + prompts; this header documents the contract for maintainers.
 */
