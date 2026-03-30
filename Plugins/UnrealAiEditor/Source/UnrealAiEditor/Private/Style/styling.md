# Unreal AI Editor Styling Guide

This repo’s UI is intentionally “gray/black by default” (dark editor theme). The goal of this guide is consistency and maintainability:

1. Prefer shared styling tokens over ad-hoc values.
2. Prefer shared Slate widgets over duplicated UI code.
3. Keep all plugin-specific colors/typography centralized in `FUnrealAiEditorStyle` under this folder.
4. Use Unreal Engine’s built-in widgets and brushes where possible (e.g. group borders, splitter/layout, standard Slate controls).

## Where styling lives

### Plugin-wide style tokens

All plugin-specific colors and typography must come from:

- `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Style/UnrealAiEditorStyle.h/.cpp` (`FUnrealAiEditorStyle`)

Do **not** hardcode `FLinearColor(...)`, custom font sizes, or bespoke brushes inside `Tabs/` (including `SUnrealAiEditorSettingsTab`).

If a new color/token is needed:

1. Add it as a `FSlateColor` (or `FLinearColor` + wrapper) in `UnrealAiEditorStyle.h/.cpp`
2. Reference it from UI code via `FUnrealAiEditorStyle::ColorX()` / `FontX()`

### Engine styling

Engine style can be used ad-hoc when it’s clearly structural (layout grouping) and not plugin-branding:

- `FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder")`
- Standard Slate widget defaults (button/text boxes/etc.)

## Shared UI building blocks

Prefer using existing shared widgets:

- `SUnrealAiLocalJsonInspectorPanel` for “capped file loading + pretty JSON inspection + copy”
- Existing editor list patterns (`SScrollBox` + `SVerticalBox` children, buttons for actions)

If you need a new reusable widget, place it under:

- `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/`

## Required typography/colors for settings pages (recommended)

Use these tokens for common UI roles:

- Panel/section titles: `FUnrealAiEditorStyle::FontSectionHeading()`
- Small labels / helper text: `FUnrealAiEditorStyle::FontBodySmall()` and `FUnrealAiEditorStyle::ColorTextMuted()`
- Primary text in lists: `FUnrealAiEditorStyle::ColorTextPrimary()`
- Emphasis/highlight (e.g., currently open thread): `FUnrealAiEditorStyle::ColorAccent()`
- Footer/lowest-emphasis: `FUnrealAiEditorStyle::ColorTextFooter()`
- Code/JSON wells: mono font via `FUnrealAiEditorStyle::FontMono9()` (or appropriate mono size)

## Implementation checklist

When editing settings UI under `Private/Tabs/`:

- Do you use `FUnrealAiEditorStyle` for fonts/colors instead of inline values?
- Do you reuse `SUnrealAiLocalJsonInspectorPanel` for JSON inspection instead of duplicating inspector UI?
- Do “help” texts and list items use consistent “muted/heading” typography?

## Rationale

The plugin styling is inspired by Cursor’s consistent, low-saturation UI choices and Unreal’s Slate patterns. Keeping tokens centralized prevents UI drift as new pages/controls are added.

## Chat Renderer Surfaces (user messages + tool summaries)

These are the exact “surfaces” you called out. Treat them as contract points: the chat UI must use these tokens and must not introduce new hardcoded `FLinearColor` backgrounds.

### User message background

- Use the shared rounded brush: `FUnrealAiEditorStyle::GetBrush("UnrealAiEditor.UserBubble")`
- This brush provides the rounded “bubble” edges (corner radius is defined in `UnrealAiEditorStyle.cpp`).

UI rule:
- Any user-authored bubble (including harness/system user messages) must be wrapped in an `SBorder` using the `UnrealAiEditor.UserBubble` brush rather than `BorderBackgroundColor`.

### Tool summary / editor-blocking notices / run progress boxes

- `RunProgress` block background must use `UnrealAiEditor.ChatRunProgress` (rounded).
- `Notice` block background must use one of:
  - `UnrealAiEditor.ChatNoticeInfo` (neutral)
  - `UnrealAiEditor.ChatNoticeError` (error)
  - `UnrealAiEditor.ChatNoticeCancelled` (cancelled)

UI rule:
- Tool summaries/notice containers must use `SBorder::BorderImage` with the rounded brush above (no `BorderBackgroundColor` with inline `FLinearColor`).


