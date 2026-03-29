#include "Tools/UnrealAiToolQueryShaper.h"

namespace UnrealAiToolQueryShaperPriv
{
	static bool ContainsAny(const FString& L, const TArray<const TCHAR*>& Words)
	{
		for (const TCHAR* W : Words)
		{
			if (L.Contains(W))
			{
				return true;
			}
		}
		return false;
	}
} // namespace UnrealAiToolQueryShaperPriv

void UnrealAiToolQueryShaper::ShapeForRetrieval(const FString& RawUserText, FString& OutShaped, EUnrealAiToolQueryShape& OutShapeUsed)
{
	OutShaped.Reset();
	const FString L = RawUserText.ToLower();
	if (L.IsEmpty())
	{
		OutShapeUsed = EUnrealAiToolQueryShape::Raw;
		return;
	}

	// Highest priority: physics / collision / trace queries. Do not fold these into generic "viewport" or "world"
	// (user text often mentions the viewport camera *and* tracing — ObjActor viewport/world was stealing the shape).
	TArray<const TCHAR*> ObjPhysicsTrace = {
		TEXT("collision"),
		TEXT("line trace"),
		TEXT("linetrace"),
		TEXT("raycast"),
		TEXT("sphere trace"),
		TEXT("sweep"),
		TEXT("hit vs"),
		TEXT("hit test"),
		TEXT("trace forward"),
		TEXT("world trace"),
		TEXT("editor world collision"),
		TEXT("line/sweep"),
	};
	if (UnrealAiToolQueryShaperPriv::ContainsAny(L, ObjPhysicsTrace))
	{
		OutShapeUsed = EUnrealAiToolQueryShape::Heuristic;
		OutShaped = FString::Printf(TEXT("query physics_trace %s"), *RawUserText);
		OutShaped.TrimStartAndEndInline();
		return;
	}

	// Viewport *rendering* mode (Lit / Unlit / Wireframe) — rank before generic viewport camera motion.
	TArray<const TCHAR*> ObjViewMode = {
		TEXT("unlit"),
		TEXT("wireframe"),
		TEXT("view mode"),
		TEXT("viewmode"),
		TEXT("rendering mode"),
		TEXT("lit mode"),
	};
	if (UnrealAiToolQueryShaperPriv::ContainsAny(L, ObjViewMode))
	{
		OutShapeUsed = EUnrealAiToolQueryShape::Heuristic;
		OutShaped = FString::Printf(TEXT("modify viewport_rendering %s"), *RawUserText);
		OutShaped.TrimStartAndEndInline();
		return;
	}

	TArray<const TCHAR*> VerbsCreate = {TEXT("create"), TEXT("add"), TEXT("spawn"), TEXT("new "), TEXT("make ")};
	TArray<const TCHAR*> VerbsModify = {TEXT("modify"), TEXT("change"), TEXT("edit"), TEXT("update"), TEXT("set "), TEXT("apply")};
	TArray<const TCHAR*> VerbsFind = {TEXT("find"), TEXT("search"), TEXT("locate"), TEXT("where is"), TEXT("list ")};
	TArray<const TCHAR*> ObjBp = {TEXT("blueprint"), TEXT("bp_"), TEXT("graph"), TEXT("node"), TEXT("variable")};
	TArray<const TCHAR*> ObjMat = {TEXT("material"), TEXT("shader"), TEXT("texture")};
	// Avoid bare "viewport" / "world" here — they appear in unrelated prompts (tracing, collision) and over-weight viewport tools.
	TArray<const TCHAR*> ObjActor = {TEXT("actor"), TEXT("level"), TEXT("scene")};
	TArray<const TCHAR*> ObjViewportCam = {TEXT("orbit"), TEXT("dolly"), TEXT("pan"), TEXT("zoom"), TEXT("pilot"), TEXT("viewport camera")};
	TArray<const TCHAR*> ObjAsset = {TEXT("asset"), TEXT("content"), TEXT("import")};
	TArray<const TCHAR*> ObjPie = {TEXT("play"), TEXT("pie"), TEXT("simulate")};

	FString Verb;
	if (UnrealAiToolQueryShaperPriv::ContainsAny(L, VerbsCreate))
	{
		Verb = TEXT("create");
	}
	else if (UnrealAiToolQueryShaperPriv::ContainsAny(L, VerbsModify))
	{
		Verb = TEXT("modify");
	}
	else if (UnrealAiToolQueryShaperPriv::ContainsAny(L, VerbsFind))
	{
		Verb = TEXT("find");
	}

	FString Object;
	if (UnrealAiToolQueryShaperPriv::ContainsAny(L, ObjBp))
	{
		Object = TEXT("blueprint");
	}
	else if (UnrealAiToolQueryShaperPriv::ContainsAny(L, ObjMat))
	{
		Object = TEXT("material");
	}
	else if (UnrealAiToolQueryShaperPriv::ContainsAny(L, ObjViewportCam))
	{
		Object = TEXT("viewport_camera");
	}
	else if (L.Contains(TEXT("viewport")) || L.Contains(TEXT("level viewport")))
	{
		Object = TEXT("viewport_camera");
	}
	else if (UnrealAiToolQueryShaperPriv::ContainsAny(L, ObjActor))
	{
		Object = TEXT("level_actor");
	}
	else if (UnrealAiToolQueryShaperPriv::ContainsAny(L, ObjAsset))
	{
		Object = TEXT("asset");
	}
	else if (UnrealAiToolQueryShaperPriv::ContainsAny(L, ObjPie))
	{
		Object = TEXT("pie");
	}

	if (!Verb.IsEmpty() || !Object.IsEmpty())
	{
		OutShapeUsed = EUnrealAiToolQueryShape::Heuristic;
		OutShaped = FString::Printf(TEXT("%s %s %s"), *Verb, *Object, *RawUserText);
		OutShaped.TrimStartAndEndInline();
	}
	else
	{
		OutShapeUsed = EUnrealAiToolQueryShape::Raw;
		OutShaped = RawUserText;
	}
}

void UnrealAiToolQueryShaper::BuildHybridQuery(const FString& RawUserText, const FString& Shaped, FString& OutHybrid)
{
	OutHybrid = FString::Printf(TEXT("%s %s"), *RawUserText, *Shaped);
	OutHybrid.TrimStartAndEndInline();
}
