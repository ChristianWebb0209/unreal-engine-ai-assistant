#pragma once

#include "CoreMinimal.h"

/**
 * Validates project-relative paths: must be relative (no drive/root), no ".." segments, and must resolve under
 * FPaths::ProjectDir(). Any subtree (Saved, Intermediate, Content, etc.) is allowed.
 */
bool UnrealAiIsAllowedProjectRelativePath(const FString& RelativePath, FString& OutError);

/** Absolute path under FPaths::ProjectDir() after validation. */
bool UnrealAiResolveProjectFilePath(const FString& RelativePath, FString& OutAbsolute, FString& OutError);
