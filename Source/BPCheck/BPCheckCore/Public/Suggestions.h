#pragma once

#include "CoreMinimal.h"

using FSuggestionCode = FString;

enum class ESuggestion
{
	PrintInHotPath
};

struct FSuggestionInfo
{
	FSuggestionCode SuggestionCode;
	FString SuggestionDescription;
};

inline const FSuggestionInfo& GetSuggestionInfo(ESuggestion S)
{
	static const TMap<ESuggestion, FSuggestionInfo> Data = {
		{ ESuggestion::PrintInHotPath, {"P001", TEXT("Print node in hot path can cause performance issues.")} }
	};

	return Data[S];
}