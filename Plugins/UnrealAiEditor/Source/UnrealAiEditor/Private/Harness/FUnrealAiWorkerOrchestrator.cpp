#include "Harness/FUnrealAiWorkerOrchestrator.h"

#include "Templates/SharedPointer.h"

FUnrealAiWorkerResult FUnrealAiWorkerOrchestrator::MergeDeterministic(const TArray<FUnrealAiWorkerResult>& Workers)
{
	FUnrealAiWorkerResult Merged;
	Merged.Status = TEXT("success");
	for (const FUnrealAiWorkerResult& W : Workers)
	{
		if (W.Status == TEXT("failed"))
		{
			Merged.Status = TEXT("partial");
		}
		if (!W.Summary.IsEmpty())
		{
			if (!Merged.Summary.IsEmpty())
			{
				Merged.Summary += TEXT("\n---\n");
			}
			Merged.Summary += W.Summary;
		}
		Merged.Artifacts.Append(W.Artifacts);
		Merged.Errors.Append(W.Errors);
		Merged.FollowUpQuestions.Append(W.FollowUpQuestions);
	}
	return Merged;
}
