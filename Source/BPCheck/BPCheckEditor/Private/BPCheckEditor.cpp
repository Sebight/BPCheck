// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPCheckEditor.h"
#include "ToolMenus.h"
#include "BPCheckCore.h"
#include "BlueprintCompilationManager.h"
#include "DetailCategoryBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Images/SImage.h"
#include "Styling/StyleColors.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Suggestions.h"

#include "Suggestions.generated.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "FBPCheckModule"

DEFINE_LOG_CATEGORY(LogBPCheckEditor);

static const FName BPCheckTabId = "BPCheckSuggestions";

class SBPCheckSuggestionsPanel;

UEdGraphNode* BPCheck::Editor::ResolveNode(const UBlueprint* Blueprint, FGuid NodeGuid)
{
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);

	for (auto* Graph : Graphs)
	{
		for (auto Node : Graph->Nodes)
		{
			if (Node->NodeGuid == NodeGuid) return Node;
		}
	}

	return nullptr;
}

void FBPCheckEditorModule::StartupModule()
{
	UE_LOG(LogBPCheckEditor, Log, TEXT("BPCheck-Editor started."));
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBPCheckEditorModule::CreateMenuItem));

	UBPCompilerExtension* Extension = NewObject<UBPCompilerExtension>();
	FBlueprintCompilationManager::RegisterCompilerExtension(UBlueprint::StaticClass(), Extension);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(BPCheckTabId, FOnSpawnTab::CreateRaw(this, &FBPCheckEditorModule::CreateSuggestionsTab)).SetDisplayName(LOCTEXT("BPCheckSuggestionsList", "BPCheck Suggestions"));

	FPropertyEditorModule& PE =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PE.RegisterCustomClassLayout(
		UBPCheckSettings::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(
			&FBPCheckSettingsCustomization::MakeInstance));
	PE.NotifyCustomizationModuleChanged();

	SettingsChangedHandle = GetMutableDefault<UBPCheckSettings>()->OnSettingsChanged.AddRaw(this, &FBPCheckEditorModule::OnSettingsChanged);
}

void FBPCheckEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor")
			.UnregisterCustomClassLayout(UBPCheckSettings::StaticClass()->GetFName());
	}

	if (UObjectInitialized())
		GetMutableDefault<UBPCheckSettings>()->OnSettingsChanged.Remove(SettingsChangedHandle);
}

void FBPCheckEditorModule::CreateMenuItem()
{
	FToolMenuOwnerScoped Owner(this);

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	FToolMenuSection& Section = ToolBar->FindOrAddSection("BPCheck");

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"BPCheckButton",
		FUIAction(FExecuteAction::CreateRaw(this, &FBPCheckEditorModule::OnButtonClicked)),
		LOCTEXT("BPCheckLabel", "BPCheck"),
		LOCTEXT("BPCheckTooltip", "See BPCheck Suggestions"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.ViewOptions")
	));

	FNewToolMenuDelegate Delegate = FNewToolMenuDelegate::CreateRaw(this, &FBPCheckEditorModule::BuildNodeSection);

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UK2Node::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			continue;
		}

		const FName MenuName = *FString::Printf(TEXT("GraphEditor.GraphNodeContextMenu.%s"), *It->GetName());
		UToolMenus::Get()->ExtendMenu(MenuName)->AddDynamicSection("BPCheck", Delegate);
	}
}

void FBPCheckEditorModule::OnButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(BPCheckTabId);
}

void FBPCheckEditorModule::OnSettingsChanged()
{
	if (TSharedPtr<SBPCheckSuggestionsPanel> P = SuggestionsPanel.Pin())
	{
		P->SetResults(BuildTree());
	}

	ApplyNodeMarkers();
}

void FBPCheckEditorModule::SetSuggestionsForBlueprint(const UBlueprint* Blueprint, TArray<FSuggestionContext>& NewSuggestions)
{
	Suggestions.Add(Blueprint, MoveTemp(NewSuggestions));

	if (TSharedPtr<SBPCheckSuggestionsPanel> P = SuggestionsPanel.Pin())
	{
		P->SetResults(BuildTree());
	}
}

TSharedRef<SDockTab> FBPCheckEditorModule::CreateSuggestionsTab(const FSpawnTabArgs&)
{
	TSharedRef<SBPCheckSuggestionsPanel> Panel = SNew(SBPCheckSuggestionsPanel);
	SuggestionsPanel = Panel;
	Panel->SetResults(BuildTree());
	return SNew(SDockTab).TabRole(NomadTab)[Panel];
}

namespace ColumnIds
{
	static const FName Severity("Severity");
	static const FName Code("Code");
	static const FName Description("Description");
}

static const FTableRowStyle& GetRowStyle()
{
	static FTableRowStyle Style = FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
	static bool bInit = false;
	if (!bInit)
	{
		static FSlateColorBrush Even(FLinearColor(0.020f, 0.020f, 0.022f, 1.f));
		static FSlateColorBrush Odd(FLinearColor(0.035f, 0.035f, 0.040f, 1.f));
		Style.SetEvenRowBackgroundBrush(Even).SetEvenRowBackgroundHoveredBrush(Even);
		Style.SetOddRowBackgroundBrush(Odd).SetOddRowBackgroundHoveredBrush(Odd);
		bInit = true;
	}
	return Style;
}

// Claude helped with this UI work.
class SSuggestionRow : public SMultiColumnTableRow<TSharedPtr<FResultTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SSuggestionRow) {}
		SLATE_ARGUMENT(TSharedPtr<FResultTreeItem>, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& Owner)
	{
		Item = InArgs._Item;
		FSuperRowType::Construct(FSuperRowType::FArguments().Style(&GetRowStyle()), Owner);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
	{
		if (Column == ColumnIds::Severity)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[SNew(SExpanderArrow, SharedThis(this)).IndentAmount(12)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
				[SNew(SImage).Image(GetIcon()).ColorAndOpacity(GetTint())];
		}
		if (Column == ColumnIds::Code)
		{
			return SNew(STextBlock)
				.Text(FText::FromString(Item->Label))
				.Font(FCoreStyle::GetDefaultFontStyle(Item->bIsGroup ? "Bold" : "Regular", 9));
		}
		if (Column == ColumnIds::Description)
		{
			return SNew(STextBlock)
				.Text(FText::FromString(Item->Description))
				.ColorAndOpacity(FSlateColor(FStyleColors::Foreground));
		}
		return SNullWidget::NullWidget;
	}

private:
	const FSlateBrush* GetIcon() const
	{
		switch (Item->Severity)
		{
		case EItemSeverity::Error:   return FAppStyle::Get().GetBrush("Icons.Error");
		case EItemSeverity::Warning: return FAppStyle::Get().GetBrush("Icons.Warning");
		case EItemSeverity::Group:   return FAppStyle::Get().GetBrush("Icons.FolderClosed");
		default:                     return FAppStyle::Get().GetBrush("Icons.Info");
		}
	}

	FSlateColor GetTint() const
	{
		switch (Item->Severity)
		{
		case EItemSeverity::Error:   return FStyleColors::Error;
		case EItemSeverity::Warning: return FStyleColors::Warning;
		case EItemSeverity::Group:   return FStyleColors::AccentGray;
		default:                     return FStyleColors::AccentBlue;
		}
	}

	TSharedPtr<FResultTreeItem> Item;
};

TArray<TSharedPtr<FResultTreeItem>> FBPCheckEditorModule::BuildTree() const
{
	TArray<TSharedPtr<FResultTreeItem>> Roots;
	const UBPCheckSettings* Settings = GetDefault<UBPCheckSettings>();

	for (const auto& Pair : Suggestions)
	{
		const UBlueprint* BP = Pair.Key;
		if (!BP || Pair.Value.Num() == 0) continue;

		TSharedPtr<FResultTreeItem> Group = MakeShared<FResultTreeItem>();
		Group->bIsGroup = true;
		Group->Severity = EItemSeverity::Group;

		for (const FSuggestionContext& S : Pair.Value)
		{
			if (!Settings->IsSuggestionEnabled(S.Suggestion))
			{
				continue;
			}

			const FSuggestionInfo& Info = GetSuggestionInfo(S.Suggestion);

			TSharedPtr<FResultTreeItem> Leaf = MakeShared<FResultTreeItem>();
			Leaf->Node = BPCheck::Editor::ResolveNode(BP, S.NodeGuid);
			Leaf->Label = FString::Printf(TEXT("%s"), *Info.SuggestionCode);

			FStringFormatNamedArguments Args;
			Args.Add(TEXT("Node"), FStringFormatArg(Leaf->Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
			Leaf->Description = FString::Format(*Info.SuggestionDescription, Args);

			Leaf->Severity = EItemSeverity::Warning;
			Group->Children.Add(Leaf);
		}

		if (Group->Children.Num() == 0) continue;
		Group->Label = FString::Printf(TEXT("%s (%d)"), *BP->GetName(), Group->Children.Num());
		Roots.Add(Group);
	}
	return Roots;
}

void FBPCheckEditorModule::ApplyNodeMarkers()
{
	TSet<UEdGraph*> Dirtied;
	const UBPCheckSettings* Settings = GetDefault<UBPCheckSettings>();

	for (const auto& Pair : Suggestions)
	{
		const UBlueprint* BP = Pair.Key;
		if (!BP) continue;

		for (const auto& Suggestion : Pair.Value)
		{
			UEdGraphNode* Node = BPCheck::Editor::ResolveNode(BP, Suggestion.NodeGuid);

			if (Settings->IsSuggestionEnabled(Suggestion.Suggestion))
			{
				const FSuggestionInfo& Info = GetSuggestionInfo(Suggestion.Suggestion);
				FStringFormatNamedArguments Args;
				Args.Add(TEXT("Node"), FStringFormatArg(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
				const FString Desc = FString::Format(*Info.SuggestionDescription, Args);

				Node->bHasCompilerMessage = true;
				Node->ErrorType = static_cast<int32>(EMessageSeverity::Info);
				Node->ErrorMsg = FString::Printf(TEXT("BPCheck: %s\n%s"), *Info.SuggestionCode, *Desc);
				Dirtied.Add(Node->GetGraph());
			}
			else if (Node->ErrorMsg.StartsWith(TEXT("BPCheck:")))
			{
				Node->bHasCompilerMessage = false;
				Node->ErrorMsg.Empty();
				Dirtied.Add(Node->GetGraph());
			}
		}
	}

	for (UEdGraph* Graph : Dirtied)
	{
		Graph->NotifyGraphChanged();
	}
}

void FBPCheckEditorModule::BuildNodeSection(UToolMenu* InMenu)
{
	if (InMenu->FindSection("BPCheckSection")) return;

	const UGraphNodeContextMenuContext* Ctx = InMenu->FindContext<UGraphNodeContextMenuContext>();
	if (!Ctx || !Ctx->Node) return;

	const UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(Ctx->Node);
	if (!BP) return;

	const FGuid NodeGuid = Ctx->Node->NodeGuid;
	// Suggestions
	TArray<FSuggestionContext> SuggestionsForNode = GetSuggestionsForNode(NodeGuid);

	FToolMenuSection& Section = InMenu->AddSection("BPCheckSection", INVTEXT("BPCheck"));

	for (auto& Suggestion : SuggestionsForNode)
	{
		auto& Info = GetSuggestionInfo(Suggestion.Suggestion);

		Section.AddSubMenu(
			FName(*FString::Printf(TEXT("BPCheck_%s"), *Info.SuggestionCode)),
			FText::FromString(StaticEnum<ESuggestion>()->GetDisplayNameTextByValue(static_cast<int64>(Suggestion.Suggestion)).ToString()),
			FText::GetEmpty(),
			FNewToolMenuDelegate::CreateLambda([BPPath = FSoftObjectPath(BP), NodeGuid](UToolMenu* Sub)
				{
					FToolMenuSection& S = Sub->AddSection("Actions");
					S.AddMenuEntry("SilenceThis", INVTEXT("Silence this one"), INVTEXT("Suppress on this node only"),
						FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([=]()
							{
								UE_LOG(LogBPCheckEditor, Display, TEXT("Silence this"));
							})));
				})
		);
	}
}

TArray<FSuggestionContext> FBPCheckEditorModule::GetSuggestionsForNode(FGuid NodeGuid)
{
	TArray<FSuggestionContext> Out;
	for (auto& Entry : Suggestions)
	{
		for (auto& Value : Entry.Value)
		{
			if (Value.NodeGuid == NodeGuid)
			{
				Out.Push(Value);
			}
		}
	}

	return Out;
}

void SBPCheckSuggestionsPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SAssignNew(TreeView, STreeView<TSharedPtr<FResultTreeItem>>)
				.TreeItemsSource(&RootNodes)
				.OnGenerateRow(this, &SBPCheckSuggestionsPanel::OnGenerateRow)
				.OnGetChildren(this, &SBPCheckSuggestionsPanel::OnGetChildren)
				.SelectionMode(ESelectionMode::Single)
				.OnMouseButtonDoubleClick(this, &SBPCheckSuggestionsPanel::OnItemDoubleClicked)
				.HeaderRow(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(ColumnIds::Severity)
					.DefaultLabel(FText::GetEmpty()).FixedWidth(48.0f)
					+ SHeaderRow::Column(ColumnIds::Code)
					.DefaultLabel(LOCTEXT("ColCode", "Code")).FillWidth(0.25f)
					+ SHeaderRow::Column(ColumnIds::Description)
					.DefaultLabel(LOCTEXT("ColDesc", "Description")).FillWidth(0.75f)
				)
		];
}

void SBPCheckSuggestionsPanel::SetResults(TArray<TSharedPtr<FResultTreeItem>> InNodes)
{
	RootNodes = MoveTemp(InNodes);

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();

		for (const TSharedPtr<FResultTreeItem>& Node : RootNodes)
			TreeView->SetItemExpansion(Node, true);
	}
}

TSharedRef<ITableRow> SBPCheckSuggestionsPanel::OnGenerateRow(TSharedPtr<FResultTreeItem> Node, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(SSuggestionRow, Owner).Item(Node);
}

void SBPCheckSuggestionsPanel::OnGetChildren(TSharedPtr<FResultTreeItem> Node, TArray<TSharedPtr<FResultTreeItem>>& OutChildren)
{
	if (Node.IsValid())
	{
		OutChildren = Node->Children;
	}
}

void SBPCheckSuggestionsPanel::OnItemDoubleClicked(TSharedPtr<FResultTreeItem> Item)
{
	if (!Item.IsValid()) return;

	const UObject* Resolved = Item->Node.Get();
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Resolved);
}

void UBPCompilerExtension::ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	if (!CompilationContext.Blueprint->GetPackage()->GetName().StartsWith("/Game/"))
	{
		// This skips analysis of any engine side Blueprints
		// TODO(Seb): This also skips all plugins, which might be an issue
		return;
	}

	double Seconds = 0.0;
	FAnalyzerResult Result;
	{
		FScopedDurationTimer Timer(Seconds);
		FAnalyzer Analyzer;
		Result = Analyzer.Analyze(CompilationContext, Data);
	}

	const UBPCheckSettings* Settings = GetDefault<UBPCheckSettings>();

	for (auto& Suggestion : Result.Suggestions)
	{
		if (!Settings->IsSuggestionEnabled(Suggestion.Suggestion))
		{
			continue;
		}

		UEdGraphNode* Node = BPCheck::Editor::ResolveNode(CompilationContext.Blueprint, Suggestion.NodeGuid);
		if (!Node)
		{
			// Couldn't properly resolve the reported node? Should probably warn...
		}

		const FSuggestionInfo& Info = GetSuggestionInfo(Suggestion.Suggestion);

		TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning);
		Msg->AddToken(FTextToken::Create(FText::FromString(Info.SuggestionCode)));

		// Note(Seb): I wasn't able to make it work without adding the Callback, which forces the open in the node editor...
		Msg->AddToken(FUObjectToken::Create(Node, Node->GetNodeTitle(ENodeTitleType::ListView))->OnMessageTokenActivated(
			FOnMessageTokenActivated::CreateLambda([](const TSharedRef<IMessageToken>& Token)
				{
					const TSharedRef<FUObjectToken> Obj = StaticCastSharedRef<FUObjectToken>(Token);
					if (UObject* Resolved = Obj->GetObject().Get())
						FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Resolved);
				})));

		FStringFormatNamedArguments Args;
		Args.Add(TEXT("Node"), FStringFormatArg(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
		FString Description = FString::Format(*Info.SuggestionDescription, Args);

		Msg->AddToken(FTextToken::Create(FText::FromString(Description)));

		Node->bHasCompilerMessage = true;
		Node->ErrorType = static_cast<int32>(EMessageSeverity::Info);
		Node->ErrorMsg = FString::Printf(TEXT("BPCheck: %s\n%s"), *Info.SuggestionCode, *Description);

		FMessageLog("BPCheck").AddMessage(Msg);
	}

	FModuleManager::GetModuleChecked<FBPCheckEditorModule>("BPCheckEditor").SetSuggestionsForBlueprint(CompilationContext.Blueprint, Result.Suggestions);

	Super::ProcessBlueprintCompiled(CompilationContext, Data);
}

// Run using "unreal_bin" "project_path" -run=BPCheck -NoShaderCompile -AllowStdOutLogVerbosity 2>&1 | Select-String "BPCheck"
int32 UBPCheckCommandlet::Main(const FString& String)
{
	UE_LOG(LogBPCheckEditor, Log, TEXT("Running BPCheck Commandlet."));

	auto& AssetRegistryMod = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	auto& AssetRegistry = AssetRegistryMod.Get();

	TArray<FAssetData> FoundAssets;
	AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), FoundAssets);

	TArray<FAnalyzerResult> Results;

	for (const auto& AssetData : FoundAssets)
	{
		if (!AssetData.GetPackage()->GetName().StartsWith("/Game/"))
		{
			// Ditto with the TODO above
			continue;
		}

		UBlueprint* AssetObject = Cast<UBlueprint>(AssetData.GetAsset());

		if (!AssetObject)
		{
			continue;
		}

		FAnalyzer Analyzer;
		Results.Push(Analyzer.Analyze(AssetObject));
	}

	FAnalyzerReport Report;
	Report.Results = MoveTemp(Results);

	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(FJsonObjectConverter::UStructToJsonObject(Report).ToSharedRef(), Writer);
	UE_LOG(LogBPCheckEditor, Log, TEXT("%s"), *Json);

	return 0;
}

void FBPCheckSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& B)
{
	TSharedRef<IPropertyHandle> MapHandle =
		B.GetProperty(GET_MEMBER_NAME_CHECKED(UBPCheckSettings, SuggestionEnabled));
	B.HideProperty(MapHandle);

	IDetailCategoryBuilder& Cat = B.EditCategory("Suggestions");
	const UEnum* Enum = StaticEnum<ESuggestion>();

	for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
	{
		const ESuggestion Suggestion = static_cast<ESuggestion>(Enum->GetValueByIndex(i));
		Cat.AddCustomRow(Enum->GetDisplayNameTextByIndex(i))
			.NameContent()[SNew(STextBlock)
			.Text(Enum->GetDisplayNameTextByIndex(i))
			.ToolTipText(FText::FromString(GetSuggestionInfo(Suggestion).SuggestionDescription))]
			.ValueContent()[SNew(SCheckBox)
			.IsChecked_Lambda([Suggestion] { return GetDefault<UBPCheckSettings>()->IsSuggestionEnabled(Suggestion)
				? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Suggestion](ECheckBoxState S)
				{
					UBPCheckSettings* Set = GetMutableDefault<UBPCheckSettings>();
					Set->SuggestionEnabled.Add(Suggestion, S == ECheckBoxState::Checked);
					Set->SaveConfig();
					Set->OnSettingsChanged.Broadcast();
				})];
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPCheckEditorModule, BPCheckEditor)