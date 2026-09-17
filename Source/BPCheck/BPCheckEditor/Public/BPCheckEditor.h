// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "BlueprintCompilerExtension.h"
#include "Commandlets/Commandlet.h"

#include "BPCheckEditor.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBPCheckEditor, Log, All);

namespace BPCheck::Editor
{
	UEdGraphNode* ResolveNode(const UBlueprint* Blueprint, FGuid NodeGuid);
}

class FBPCheckEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void CreateMenuItem();
	void OnButtonClicked();
};

UCLASS()
class BPCHECKEDITOR_API UBPCompilerExtension : public UBlueprintCompilerExtension
{
	GENERATED_BODY()

protected:
	virtual void ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data) override;
};

UCLASS()
class BPCHECKEDITOR_API UBPCheckCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString&) override;
};
