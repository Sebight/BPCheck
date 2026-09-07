// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "BlueprintCompilerExtension.h"
#include "BPCheckEditor.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBPCheckEditor, Log, All);

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
