#pragma once

#include "Containers/UnrealString.h"
#include "Containers/Array.h"

namespace UnrealAiToolCatalogMatrixRunner
{
	/**
	 * Invokes each catalog tool (respecting MatrixFilter substring), writes
	 * Saved/UnrealAiEditor/Automation/tool_matrix_last.json.
	 * @return true if there are zero contract violations.
	 */
	bool RunAndWriteJson(const FString& MatrixFilter, TArray<FString>* OutViolationMessages);
}
