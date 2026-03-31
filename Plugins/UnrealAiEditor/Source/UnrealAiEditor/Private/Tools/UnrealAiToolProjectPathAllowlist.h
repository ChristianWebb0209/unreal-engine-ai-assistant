#pragma once

#include "CoreMinimal.h"

/**
 * Validates project-relative paths: must be relative (no drive/root), no ".." segments, and must resolve under
 * FPaths::ProjectDir(). Any subtree (Saved, Intermediate, Content, etc.) is allowed.
 *
 * Additionally, paths starting with harness_step/ (case-insensitive) resolve under the current headed harness
 * turn output directory (the folder containing run.jsonl) when set; see FScopedHarnessStepOutputDir.
 */
bool UnrealAiIsAllowedProjectRelativePath(const FString& RelativePath, FString& OutError);

/** Absolute path after validation (project dir, or harness step dir for harness_step/...). */
bool UnrealAiResolveProjectFilePath(const FString& RelativePath, FString& OutAbsolute, FString& OutError);
