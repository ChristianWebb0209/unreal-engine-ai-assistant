#include "Tools/UnrealAiBlueprintGraphNodeGuid.h"

#include "Misc/Guid.h"

static void StripMatchingQuotes(FString& S)
{
	S.TrimStartAndEndInline();
	for (int32 Guard = 0; Guard < 8 && S.Len() >= 2; ++Guard)
	{
		const TCHAR A = S[0];
		const TCHAR B = S[S.Len() - 1];
		const bool bPair = (A == TEXT('\'') && B == TEXT('\'')) || (A == TEXT('"') && B == TEXT('"'));
		if (!bPair)
		{
			break;
		}
		S = S.Mid(1, S.Len() - 2);
		S.TrimStartAndEndInline();
	}
}

static bool IsHex(TCHAR C)
{
	return (C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('A') && C <= TEXT('F')) || (C >= TEXT('a') && C <= TEXT('f'));
}

static bool TryBuildDashedFrom32Hex(const FString& Hex32, FString& OutDashed)
{
	if (Hex32.Len() != 32)
	{
		return false;
	}
	for (int32 I = 0; I < 32; ++I)
	{
		if (!IsHex(Hex32[I]))
		{
			return false;
		}
	}
	OutDashed.Reserve(36);
	OutDashed = Hex32.Left(8) + TEXT("-") + Hex32.Mid(8, 4) + TEXT("-") + Hex32.Mid(12, 4) + TEXT("-")
		+ Hex32.Mid(16, 4) + TEXT("-") + Hex32.Mid(20, 12);
	return true;
}

bool UnrealAiTryParseBlueprintGraphNodeGuid(FString In, FGuid& OutGuid, FString* OutCanonicalLex)
{
	OutGuid = FGuid();
	StripMatchingQuotes(In);
	if (In.IsEmpty())
	{
		return false;
	}
	if (In.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
	{
		In = In.Mid(5);
		StripMatchingQuotes(In);
	}
	if (In.Len() >= 2 && In[0] == TEXT('{') && In[In.Len() - 1] == TEXT('}'))
	{
		In = In.Mid(1, In.Len() - 2);
		StripMatchingQuotes(In);
	}
	In.TrimStartAndEndInline();
	if (In.IsEmpty())
	{
		return false;
	}

	FString DashedCandidate;
	{
		FString HexOnly;
		HexOnly.Reserve(32);
		for (int32 I = 0; I < In.Len(); ++I)
		{
			const TCHAR C = In[I];
			if (IsHex(C))
			{
				HexOnly.AppendChar(C);
			}
			else if (C != TEXT('-') && C != TEXT('{') && C != TEXT('}') && !FChar::IsWhitespace(C))
			{
				// stray characters — still try Parse on original below
				HexOnly.Reset();
				break;
			}
		}
		if (HexOnly.Len() == 32 && TryBuildDashedFrom32Hex(HexOnly, DashedCandidate))
		{
			if (FGuid::Parse(DashedCandidate, OutGuid))
			{
				if (OutCanonicalLex)
				{
					*OutCanonicalLex = LexToString(OutGuid);
				}
				return true;
			}
		}
	}

	if (FGuid::Parse(In, OutGuid))
	{
		if (OutCanonicalLex)
		{
			*OutCanonicalLex = LexToString(OutGuid);
		}
		return true;
	}

	return false;
}
