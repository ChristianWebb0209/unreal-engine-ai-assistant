#pragma once

#include "CoreMinimal.h"

/**
 * Product specialist sub-turn (delegation from the orchestrator).
 * Orthogonal to Blueprint Builder graph work.
 */
enum class EUnrealAiProductSpecialistId : uint8
{
	None = 0,
	/** In-world actors, transforms, scene search, selection helpers for level work. */
	Scene,
	/** Content Browser, asset CRUD, registry, properties on /Game assets. */
	Assets,
	/** Viewport camera, framing, captures, view modes. */
	Viewport,
	/** Logs, snapshots, audit. */
	Diagnostics,
	/** PIE start/stop/status. */
	Playtest,
	/** Sequencer / animation-sequencer category (Level Sequence asset, AnimBP summaries when exposed). */
	Animation,
	/** Project files, source search, C++ compile hook. */
	ProjectIntel,
	/** Editor modes, tabs, menus, allow-listed console (not asset/browser). */
	EditorUi,
	/** Allow-listed settings envelope read/write. */
	Settings,
	/** Material instances + usage (not base material graph — use Blueprint Builder material_graph). */
	Materials,
};

namespace UnrealAiProductSpecialistId
{
	EUnrealAiProductSpecialistId ParseFromString(const FString& In);
	FString ToString(EUnrealAiProductSpecialistId Id);
}
