// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#include	"NavigatorPalette.hpp"

#include	"DGModule.hpp"
#include	"Array.hpp"
#include	"APIdefs_Database.h"
#include	"Resources.hpp"


// ============================================================================
// ============================================================================
static GSErrCode APINavigatorPaletteAPIControlCallBack (Int32 referenceID, API_PaletteMessageID messageID, GS::IntPtr /*param*/)
{
	short navigatorDialID = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorDialID ();
	if (referenceID == navigatorDialID) {
		switch (messageID) {
		case APIPalMsg_ClosePalette:		DGModelessClose (navigatorDialID);
			break;
		case APIPalMsg_HidePalette_Begin:	DGHideModelessDialog (navigatorDialID);
			break;
		case APIPalMsg_HidePalette_End:		DGShowModelessDialog (navigatorDialID, DG_DF_FIRST);
			break;
		case APIPalMsg_DisableItems_Begin:
		case APIPalMsg_DisableItems_End:	// actually do nothing, because the input finish functionality the buttons have to stay enabled
		case APIPalMsg_IsPaletteVisible:
		case APIPalMsg_OpenPalette:			break;
		}
	}

	return NoError;
}		/* APINavigatorPaletteAPIControlCallBack */


		// -----------------------------------------------------------------------------
		// -----------------------------------------------------------------------------
static void DelTreeData (short dialogID, short	treeItem, Int32 parent)
{
	Int32 child = DGTreeViewGetItem (dialogID, treeItem, parent, DG_TVG_CHILD);
	while (child != DG_TVI_NONE) {
		DelTreeData (dialogID, treeItem, child);
		child = DGTreeViewGetItem (dialogID, treeItem, child, DG_TVG_NEXT);
	}

	if (parent != DG_TVI_ROOT) {
		UIndex itemIndex = (UIndex)DGTreeViewGetItemUserData (dialogID, NavTreeView, parent);
		NavigatorPaletteTestGlobals::GetInstance ().DeleteNavigatorItem (itemIndex);
	}
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
static Int32 InsertTreeItem (short dialogID, short	treeItem, Int32 parent, const char* name, char* uiId, const API_Guid& guid, API_NavigatorItemTypeID itemType,
							 API_DatabaseInfo* dbUnId = nullptr)
{
	Int32 newItem = DGTreeViewInsertItem (dialogID, treeItem, parent, DG_TVI_BOTTOM);

	char buffer[1024] = {};
	if (uiId == nullptr || uiId[0] == '\0')
		sprintf (buffer, "%s", name);
	else
		sprintf (buffer, "%s %s", uiId, name);
	if (itemType == API_DocumentFrom3DNavItem && dbUnId != nullptr) {
		API_DocumentFrom3DType	d3dt = {};
		ACAPI_View_GetDocumentFrom3DSettings (&dbUnId->databaseUnId, &d3dt);
		if (d3dt.docSrc == API_FloorPlan)
			sprintf (buffer, "%s %s (%hd)", uiId, name, d3dt.skeletonOptions.floorIdx);
	}

	DGTreeViewSetItemText (dialogID, treeItem, newItem, buffer);

	short  iconId = OtherIcon;
	switch (itemType) {
		case API_ProjectNavItem:			iconId = ProjectLinkIcon;		break;
		case API_StoryNavItem:				iconId = StoryLinkIcon;			break;
		case API_SectionNavItem:			iconId = SectionLinkIcon;		break;
		case API_DetailDrawingNavItem:		iconId = DetailLinkIcon;		break;
		case API_PerspectiveNavItem:		iconId = Persp3DLinkIcon;		break;
		case API_AxonometryNavItem:			iconId = Axon3DLinkIcon;		break;
		case API_ListNavItem:				iconId = ListIcon;				break;
		case API_ScheduleNavItem:			iconId = ScheduleLinkIcon;		break;
		case API_TocNavItem:				iconId = TOCIcon;				break;
		case API_CameraNavItem:				iconId = CameraIcon;			break;
		case API_CameraSetNavItem:			iconId = CameraSetIcon;			break;
		case API_InfoNavItem:				iconId = InfoIcon;				break;
		case API_HelpNavItem:				iconId = HelpIcon;				break;
		case API_LayoutNavItem:				iconId = LayoutLinkIcon;		break;
		case API_MasterLayoutNavItem:		iconId = MasterLinkIcon;		break;
		case API_BookNavItem:				iconId = BookIcon;				break;
		case API_MasterFolderNavItem:		iconId = MasterFolderIcon;		break;
		case API_SubSetNavItem:				iconId = SubsetIcon;			break;
		case API_TextListNavItem:			iconId = ListIcon;				break;
		case API_ElevationNavItem:			iconId = ElevationIcon;			break;
		case API_InteriorElevationNavItem:	iconId = IntElevationIcon;		break;
		case API_WorksheetDrawingNavItem:	iconId = WorksheetLinkIcon;		break;
		case API_DocumentFrom3DNavItem:		iconId = DocumentFrom3DIcon;	break;
		default:							iconId = OtherIcon;				break;
	}

	DGTreeViewSetItemIcon (dialogID, treeItem, newItem, DG_TVIT_NORMAL, DG::Icon (ACAPI_GetOwnResModule (), iconId));
	DGTreeViewSetItemUserData (dialogID, treeItem, newItem, NavigatorPaletteTestGlobals::GetInstance ().AddNavigatorItem (guid));

	return newItem;
}


// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
static void MakeTree (short dialogID, short	treeItem, Int32 parent, const API_Guid& guid, GS::PagedArray<Int32>& itemsArray, API_NavigatorMapID mapId)
{
	GS::Array<API_NavigatorItem> items;
	API_NavigatorItem item = {};

	item.guid = guid;
	item.mapId = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorMapID ();

	GSErrCode err = ACAPI_Navigator_GetNavigatorChildrenItems (&item, &items);
	if (err != NoError)
		return;

	for (API_NavigatorItem& childItem : items) {
		if (childItem.itemType == API_ScheduleNavItem) {
			GS::Array<API_NavigatorItem> items2;
			ACAPI_WriteReport ("GUID: %s, GUID2: %s, name: %s\n", false,
				APIGuidToString (childItem.guid).ToCStr ().Get (),
				APIGuidToString (childItem.sourceGuid).ToCStr ().Get (),
				GS::UniString (childItem.uName).ToCStr ().Get ());
			childItem.mapId = mapId;
			err = ACAPI_Navigator_SearchNavigatorItem (&childItem, &items2);

			for (const auto& item2 : items2)
				ACAPI_WriteReport ("GUID: %s, Name: %s, \n", false,
					APIGuidToString (item2.guid).ToCStr ().Get (),
					GS::UniString (item2.uName).ToCStr ().Get ());
		}

		Int32 newItem = InsertTreeItem (dialogID, treeItem, parent, GS::UniString (childItem.uName).ToCStr ().Get (), childItem.uiId, childItem.guid, childItem.itemType, &childItem.db);
		if (newItem != 0) {
			itemsArray.Push (newItem);
			MakeTree (dialogID, treeItem, newItem, childItem.guid, itemsArray, mapId);
		}
	}
}


// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
static void UpdateTree (short dialogID)
{
	GS::PagedArray<Int32>	items;

	API_NavigatorSet	set;
	API_NavigatorMapID	navigatorMapID = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorMapID ();

	Int32		nSet = 1;
	Int32		newItem = 0;
	Int32		i = 0;
	GSErrCode	err = NoError;

	DGTreeViewDisableDraw (dialogID, NavTreeView);

	DelTreeData (dialogID, NavTreeView, DG_TVI_ROOT);
	DGTreeViewDeleteItem (dialogID, NavTreeView, DG_ALL_ITEMS);
	BNZeroMemory (&set, sizeof (API_NavigatorSet));

	bool isPublisher = (navigatorMapID == API_PublisherSets);
	bool isItemSelected = (DGTreeViewGetSelected (dialogID, NavTreeView, DG_TVI_ROOT) != DG_TVI_NONE);

	DGSetItemEnable (dialogID, NavPublishButton, isPublisher);
	DGSetItemEnable (dialogID, NavRenameButton, isPublisher && isItemSelected);
	DGSetItemEnable (dialogID, NavCustomNameText, isPublisher && isItemSelected);

	bool deleteButtonEnabled = (navigatorMapID == API_PublicViewMap || navigatorMapID == API_MyViewMap) && isItemSelected;
	DGSetItemEnable (dialogID, NavDeleteButton, deleteButtonEnabled);

	if (navigatorMapID == API_PublisherSets) {
		err = ACAPI_Navigator_GetNavigatorSetNum (&nSet);
		if (err != NoError) {
			DGTreeViewEnableDraw (dialogID, NavTreeView);
			DGRedrawItem (dialogID, NavTreeView);
			return;
		}
	}

	for (i = 0; i < nSet; i++) {
		set.mapId = navigatorMapID;
		set.wantsExtraInfo = (navigatorMapID == API_PublisherSets) ? 1 : 0;
		err = ACAPI_Navigator_GetNavigatorSet (&set, &i);
		if (err != NoError) {
			DGTreeViewEnableDraw (dialogID, NavTreeView);
			DGRedrawItem (dialogID, NavTreeView);
			return;
		}

		if (navigatorMapID == API_PublisherSets) {
			if (set.path != nullptr) {
				DBPrintf ("Navigator set \"%s\" is published to LAN. Path: \"%s\"\n", set.name, set.path->ToDisplayText ().ToCStr ().Get ());
				delete set.path;
			}
			else if (set.host != nullptr) {
				DBPrintf ("Navigator set \"%s\" is published to Internet. Host: \"%s\", directory: \"%s\"\n",
					set.name, (const char *)set.host->ToCStr (), (const char *)set.dirName->ToCStr ());
				delete set.host;
				delete set.dirName;
			}
		}

		if (navigatorMapID == API_PublisherSets && nSet > 1) {
			newItem = InsertTreeItem (dialogID, NavTreeView, DG_TVI_ROOT, set.name, nullptr, set.rootGuid, API_UndefinedNavItem);
			items.Push (newItem);
		}

		MakeTree (dialogID, NavTreeView, newItem, set.rootGuid, items, navigatorMapID);
	}

	// Update expanded/collapsed state
	for (GS::PagedArray<Int32>::ConstIterator ii = items.Enumerate (); ii != nullptr; ++ii) {
		if (NavigatorPaletteTestGlobals::GetInstance ().IsNavigatorItemOpen (TruncateTo32Bit (DGTreeViewGetItemUserData (dialogID, NavTreeView, *ii))))
			DGTreeViewExpandItem (dialogID, NavTreeView, *ii);
	}

	DGTreeViewEnableDraw (dialogID, NavTreeView);
	DGRedrawItem (dialogID, NavTreeView);
}


// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
static void RenamePublisherItem (short dialogID)
{
	Int32 selected = DGTreeViewGetSelected (dialogID, NavTreeView, DG_TVI_ROOT);
	if (DBERROR (selected == DG_TVI_NONE))
		return;

	GS::Array<API_Guid> toRenameItems;
	API_Guid guid = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem ((UIndex)DGTreeViewGetItemUserData (dialogID, NavTreeView, selected));
	toRenameItems.Push (guid);

	for (;;) {
		selected = DGTreeViewGetItem (dialogID, NavTreeView, selected, DG_TVG_NEXTLISTED);
		selected = DGTreeViewGetSelected (dialogID, NavTreeView, selected);
		if (selected == DG_TVI_NONE)
			break;
		guid = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem ((UIndex)DGTreeViewGetItemUserData (dialogID, NavTreeView, selected));
		toRenameItems.Push (guid);
	}

	GS::UniString customName = DGGetItemText (dialogID, NavCustomNameText);
	for (USize i = 0; i < toRenameItems.GetSize (); ++i) {
		API_NavigatorItem navItem;

		BNZeroMemory (&navItem, sizeof (navItem));
		navItem.mapId = API_PublisherSets;
		GSErrCode err = ACAPI_Navigator_GetNavigatorItem (&toRenameItems[i], &navItem);
		if (err != NoError)
			continue;
		if (customName.IsEmpty ())
			navItem.customName = false;
		else {
			GS::ucscpy (navItem.uName, customName.ToUStr ());
			navItem.customName = true;
		}
		ACAPI_Navigator_ChangeNavigatorItem (&navItem);
	}

	UpdateTree (dialogID);
}


// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
static void DeleteView (short dialogID)
{
	Int32 selected = DGTreeViewGetSelected (dialogID, NavTreeView, DG_TVI_ROOT);
	if (DBERROR (selected == DG_TVI_NONE))
		return;

	GS::Array<API_Guid> toDeleteItems;
	API_Guid guid = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem ((UIndex)DGTreeViewGetItemUserData (dialogID, NavTreeView, selected));
	toDeleteItems.Push (guid);

	for (;;) {
		selected = DGTreeViewGetItem (dialogID, NavTreeView, selected, DG_TVG_NEXTLISTED);
		selected = DGTreeViewGetSelected (dialogID, NavTreeView, selected);
		if (selected == DG_TVI_NONE)
			break;
		guid = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem ((UIndex)DGTreeViewGetItemUserData (dialogID, NavTreeView, selected));
		toDeleteItems.Push (guid);
	}

	bool silentMode = true;
	for (USize i = 0; i < toDeleteItems.GetSize (); ++i)
		ACAPI_Navigator_DeleteNavigatorView (&toDeleteItems[i], &silentMode);

	UpdateTree (dialogID);
}


// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
inline bool Change_Layer (const API_AttributeIndex& index, short status)
{
	API_Attribute layer = {};
	layer.header.typeID = API_LayerID;
	layer.header.index = index;
	GSErrCode err = ACAPI_Attribute_Get (&layer);
	if (err == NoError) {
		if ((status & APILay_Hidden) != (layer.layer.head.flags & APILay_Hidden)) {
			//layer.layer.head.flags &= !APILay_Locked;

			layer.layer.head.flags &= !APILay_Hidden;
			layer.layer.head.flags |= (status & APILay_Hidden);

			err = ACAPI_Attribute_Modify (&layer, nullptr);
			return (err == NoError);
		}
	}

	return false;
}


// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
inline bool Change_LayerComb (char* layerCombName)
{
	API_Attribute layerComb = {};
	layerComb.header.typeID = API_LayerCombID;
	CHCopyC (layerCombName, layerComb.header.name);
	GSErrCode err = ACAPI_Attribute_Get (&layerComb);
	if (err != NoError)
		return false;

	API_AttributeDef attrDef = {};
	err = ACAPI_Attribute_GetDef (API_LayerCombID, layerComb.header.index, &attrDef);
	if (err != NoError)
		return false;

	bool change = false;
	for (auto it = attrDef.layer_statItems->BeginPairs (); it != nullptr; ++it)
		change = change || Change_Layer (it->key, it->value.lFlags);

	delete attrDef.layer_statItems;

	return change;
}

//------------------------------------------------------
//------------------------------------------------------
static void PublishTest ()
{
	short dialogID = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorDialID ();
	Int32 selected = DGTreeViewGetSelected (dialogID, NavTreeView, DG_TVI_ROOT);
	if (selected != DG_TVI_NONE) {
		API_Guid guid = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem ((UIndex)DGTreeViewGetItemUserData (dialogID, NavTreeView, selected));
		if (guid != GS::NULLGuid) {
			API_NavigatorItem navItem {};
			navItem.mapId = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorMapID ();
			GSErrCode err = ACAPI_Navigator_GetNavigatorItem (&guid, &navItem);

			if (err == NoError) {
				API_PublishPars	publishPars;
				BNZeroMemory (&publishPars, sizeof (publishPars));
				publishPars.guid = guid;
				// PublishTest #1: publish with default parameters
				err = ACAPI_ProjectOperation_Publish (&publishPars);

				if (err == NoError) {
					GS::Array<API_Guid> toPublishItems;
					for (;;) {
						selected = DGTreeViewGetItem (dialogID, NavTreeView, selected, DG_TVG_NEXTLISTED);
						selected = DGTreeViewGetSelected (dialogID, NavTreeView, selected);
						if (selected == DG_TVI_NONE)
							break;
						API_Guid guid = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem ((UIndex)DGTreeViewGetItemUserData (dialogID, NavTreeView, selected));
						toPublishItems.Push (guid);
					}

					// PublishTest #2: publish to path
					publishPars.path = new IO::Location ("C:\\Work\\PublishTemp");
					err = ACAPI_ProjectOperation_Publish (&publishPars, &toPublishItems);
					delete publishPars.path;
				}
			}
		}
	}
}		// PublishTest


		// -----------------------------------------------------------------------------
		// Navigator Test palette handler (callback)
		// -----------------------------------------------------------------------------
short DGCALLBACK NavigatorPalette::APINavigator_Handler (	short				message,
															short 				dialogID,
															short				item,
															DGUserData			/*userData*/,
															DGMessageData		msgData)
{
	short ret = 0;

	switch (message) {
	case DG_MSG_INIT:
		if (ACAPI_RegisterModelessWindow (dialogID, APINavigatorPaletteAPIControlCallBack,
			API_PalEnabled_FloorPlan + API_PalEnabled_Section + API_PalEnabled_Elevation +
			API_PalEnabled_InteriorElevation + API_PalEnabled_Detail + API_PalEnabled_Worksheet + API_PalEnabled_Layout +
			API_PalEnabled_3D) != NoError) {
			DBPrintf ("Navigator_Test:: ACAPI_RegisterModelessWindow failed\n");
		}

		NavigatorPaletteTestGlobals::GetInstance ().ResetOpened ();
		UpdateTree (dialogID);

		break;

	case DG_MSG_CLICK:
		switch (item) {
		case NavCloseButton:
			ret = item;
			break;

		case NavUpdateButton:
			UpdateTree (dialogID);
			break;

		case NavPublishButton:
			PublishTest ();
			break;

		case NavDeleteButton:
			DeleteView (dialogID);
			break;

		case NavRenameButton:
			RenamePublisherItem (dialogID);
			break;
		}
		break;
	case DG_MSG_DOUBLECLICK:
		if (item == NavTreeView) {
			Int32 selected = DGTreeViewGetSelected (dialogID, NavTreeView, DG_TVI_ROOT);
			API_Guid guid = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem (TruncateTo32Bit (DGTreeViewGetItemUserData (dialogID, NavTreeView, selected)));
			if (guid != GS::NULLGuid) {
				API_NavigatorItem navItem;

				BNZeroMemory (&navItem, sizeof (navItem));
				navItem.mapId = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorMapID ();
				GSErrCode err = ACAPI_Navigator_GetNavigatorItem (&guid, &navItem);


				if (err == NoError  && navItem.db.typeID == APIWind_3DModelID) {
					API_3DProjectionInfo projInfo;
					BNZeroMemory (&projInfo, sizeof (projInfo));
					err = ACAPI_View_Get3DProjectionSets (&projInfo);
					bool switchOnlyAxoOrPersp = true;
					if (navItem.itemType == API_AxonometryNavItem && projInfo.isPersp) {
						// Switch to Axonometry
						projInfo.isPersp = false;
						ACAPI_View_Change3DProjectionSets (&projInfo, &switchOnlyAxoOrPersp);
					}
					else if (navItem.itemType == API_PerspectiveNavItem && !projInfo.isPersp) {
						// Switch to Perspective
						projInfo.isPersp = true;
						ACAPI_View_Change3DProjectionSets (&projInfo, &switchOnlyAxoOrPersp);
					}
				}


				if (err == NoError) {
					err = ACAPI_Window_ChangeWindow (&navItem.db);
				}

				if (err == NoError && navItem.db.typeID == APIWind_FloorPlanID) {
					API_StoryCmdType cmd;

					BNZeroMemory (&cmd, sizeof (cmd));
					cmd.action = APIStory_GoTo;
					cmd.index = navItem.floorNum;
					err = ACAPI_ProjectSetting_ChangeStorySettings (&cmd);
				}


				if (err == NoError) {
					API_NavigatorView	navView = {};
					API_NavigatorItem	navItem = {};

					navItem.guid = guid;
					navItem.mapId = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorMapID ();

					GSErrCode	err = ACAPI_Navigator_GetNavigatorView (&navItem, &navView);
					if (err == NoError) {
						bool needRebuild = false;
						if (navView.saveLaySet) {
							if (navView.layerCombination[0] != 0) {
								needRebuild = needRebuild || Change_LayerComb (navView.layerCombination);
							} else {
								for (auto it = navView.layerStats->BeginPairs (); it != nullptr; ++it)
									needRebuild = needRebuild || Change_Layer (it->key, it->value.lFlags);
							}
						}
						if (navView.saveDispOpt) {
							API_ViewOptions viewOptions;
							BNZeroMemory (&viewOptions, sizeof (API_ViewOptions));
							err = ACAPI_Navigator_GetViewOptions (&viewOptions);
							ACAPI_FreeGDLModelViewOptionsPtr (&viewOptions.modelViewOpt.gdlOptions);
							if (navView.modelViewOpt != nullptr)
								viewOptions.modelViewOpt = *navView.modelViewOpt;
							err = ACAPI_Navigator_ChangeViewOptions (&viewOptions);
						}

						if (navView.modelViewOpt != nullptr) {
							ACAPI_FreeGDLModelViewOptionsPtr (&navView.modelViewOpt->gdlOptions);
							BMKillPtr ((GSPtr*)&navView.modelViewOpt);
						}

						if (err == NoError && navView.saveDScale) {
							double scale = navView.drawingScale;
							err = ACAPI_Drawing_ChangeDrawingScale (&scale,((GS::IntPtr)(!navView.saveZoom)));
						}
						if (err == NoError && navView.saveZoom)
							err = ACAPI_View_Zoom (&navView.zoom);

						if (navView.layerStats != nullptr)
							delete navView.layerStats;
						BMKillPtr ((GSPtr*)&navView.pens);

						if (needRebuild) {
							bool regener = true;
							err = ACAPI_View_Rebuild (&regener);
						}
					}
				}
			}

		}
		break;

	case DG_MSG_CHANGE:
		switch (item) {
		case NavTreeView:
		{
			DGTreeViewChangeData* treeChange = (DGTreeViewChangeData*)msgData;
			if (treeChange->actionFlag == DG_TVCF_SELECTION) {
				Int32 selected = DGTreeViewGetSelected (dialogID, NavTreeView, DG_TVI_ROOT);
				if (selected == DG_TVI_NONE)
					break;

				API_NavigatorMapID	navigatorMapID = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorMapID ();
				DGSetItemEnable (dialogID, NavDeleteButton, navigatorMapID == API_PublicViewMap || navigatorMapID == API_MyViewMap);

				API_Guid guid = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem (TruncateTo32Bit (DGTreeViewGetItemUserData (dialogID, NavTreeView, selected)));
				if (guid != GS::NULLGuid) {
					char msgStr[256];
					API_SubSet subSet;
					BNZeroMemory (&subSet, sizeof (API_SubSet));

					GSErrCode err = ACAPI_Navigator_GetSubSet (&guid, &subSet);
					if (err == NoError) {
						GS::UniString name (subSet.name);
						GS::UniString customNumber (subSet.customNumber);
						GS::UniString autoNumber (subSet.autoNumber);
						GS::UniString ownPrefix (subSet.ownPrefix);
						sprintf (msgStr, "Name: %s, CustomNumber: %s, AutoNumber: %s, Prefix: %s",
							name.ToCStr ().Get (), customNumber.ToCStr ().Get (),
							autoNumber.ToCStr ().Get (), ownPrefix.ToCStr ().Get ());
						ACAPI_WriteReport (msgStr, false);
					}

					API_NavigatorView	navView = {};
					API_NavigatorItem	navItem = {};

					navItem.guid = guid;
					navItem.mapId = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorMapID ();

					bool isPublisher = (navItem.mapId == API_PublisherSets);
					DGSetItemEnable (dialogID, NavRenameButton, isPublisher);
					DGSetItemEnable (dialogID, NavCustomNameText, isPublisher);
					if (isPublisher) {
						err = ACAPI_Navigator_GetNavigatorItem (&guid, &navItem);
						if (err == NoError)
							DGSetItemText (dialogID, NavCustomNameText, GS::UniString (navItem.uAutoTextedName));
					}
					else
						DGSetItemText (dialogID, NavCustomNameText, "");

					err = ACAPI_Navigator_GetNavigatorView (&navItem, &navView);
					if (err == NoError) {
						if (navView.modelViewOpt != nullptr) {
							ACAPI_FreeGDLModelViewOptionsPtr (&navView.modelViewOpt->gdlOptions);
							BMKillPtr ((GSPtr*)&navView.modelViewOpt);
						}
						char str[256] = {};
						if (navView.saveLaySet) {
							if (navView.layerCombination[0] != 0)
								DGSetItemText (dialogID, LayerText, navView.layerCombination);
							else
								DGSetItemText (dialogID, LayerText, "Individual");
						}
						else {
							DGSetItemText (dialogID, LayerText, "(None)");
						}

						if (navView.saveDScale) {
							sprintf (str, "1 : %ld", (GS::LongForStdio) navView.drawingScale);
							DGSetItemText (dialogID, ScaleText, str);
						}
						else {
							DGSetItemText (dialogID, ScaleText, "(None)");
						}
						if (!navView.ignoreSavedZoom) {
							if (navView.saveZoom) {
								DGSetItemText (dialogID, ZoomText, "Zoomed area");
							}
							else {
								DGSetItemText (dialogID, ZoomText, "Fit in window");
							}
						}
						else {
							DGSetItemText (dialogID, ZoomText, "(None)");
						}
						if (navView.saveDispOpt) {
							if (navView.modelViewOptName[0] != 0)
								DGSetItemText (dialogID, DisplayOpText, navView.modelViewOptName);
							else
								DGSetItemText (dialogID, DisplayOpText, "Display Options");
						}
						else {
							DGSetItemText (dialogID, DisplayOpText, "(None)");
						}
						if (navView.d3styleName[0] != 0)
							DGSetItemText (dialogID, D3WindowStyleText, GS::UniString (navView.d3styleName));
						else
							DGSetItemText (dialogID, D3WindowStyleText, "(No 3D Window Style)");
						if (navView.layerStats != nullptr)
							delete navView.layerStats;
						BMKillPtr ((GSPtr*)&navView.pens);
					}
				}
			}
			else if (treeChange->actionFlag == DG_TVCF_COLLAPSE || treeChange->actionFlag == DG_TVCF_EXPAND) {
				NavigatorPaletteTestGlobals::GetInstance ().SetNavigatorItemOpen (TruncateTo32Bit (DGTreeViewGetItemUserData (dialogID, NavTreeView, treeChange->treeItem)),
					treeChange->actionFlag == DG_TVCF_EXPAND);
			}
		}
		break;
		case NavPopUp:
		{
			short sel = DGPopUpGetSelected (dialogID, NavPopUp);
			switch (sel) {
			case PopupMenuProjectMapItemId:
				NavigatorPaletteTestGlobals::GetInstance ().SetNavigatorMapID (API_ProjectMap);
				break;
			case PopupMenuPublicViewItemId:
				NavigatorPaletteTestGlobals::GetInstance ().SetNavigatorMapID (API_PublicViewMap);
				break;
			case PopupMenuLayoutMapItemId:
				NavigatorPaletteTestGlobals::GetInstance ().SetNavigatorMapID (API_LayoutMap);
				break;
			case PopupMenuPublisherSetsItemId:
				NavigatorPaletteTestGlobals::GetInstance ().SetNavigatorMapID (API_PublisherSets);
				break;
			default:
				DBBREAK ();
			}
			NavigatorPaletteTestGlobals::GetInstance ().ResetOpened ();
			UpdateTree (dialogID);
		}
		break;

		default:
			break;
		}
		break;

	case DG_MSG_TRACK:
		break;

	case DG_MSG_DRAGDROP:
	{
		Int32 selected = DGTreeViewGetSelected (dialogID, NavTreeView, DG_TVI_ROOT);
		API_Guid selectedGuid = selected >= 0 ? NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem (TruncateTo32Bit (DGTreeViewGetItemUserData (dialogID, NavTreeView, selected)))
			: APINULLGuid;

		DGDragDropMsgData* ddData = reinterpret_cast<DGDragDropMsgData*> (msgData);

		if (ddData->subMessage == DG_DRAG_STARTING) {
			ret = 1;
		}
		else if (ddData->subMessage == DG_DRAG_START) {
			ddData->rightDragCtxMenu = false;
		}
		else if (ddData->subMessage == DG_DRAG_END) {
		}
		else if (ddData->subMessage == DG_DRAG_SETDELAYEDDATA) {
		}
		else if (ddData->subMessage == DG_DRAG_ENTER) {
			ddData->effect = DG_DRAG_ACCEPT;
		}
		else if (ddData->subMessage == DG_DRAG_LEAVE) {
		}
		else if (ddData->subMessage == DG_DRAG_DROP) {
			GS::Guid parentGuid = GS::NULLGuid;
			GS::Guid childGuid = GS::NULLGuid;
			API_Guid insertionGuid = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem (TruncateTo32Bit (DGTreeViewGetItemUserData (dialogID, NavTreeView, ddData->insertPos)));
			switch (ddData->relativePos) {
			case DG_DRAG_INSERT_ON:
			{
				parentGuid = APIGuid2GSGuid (insertionGuid);
				API_NavigatorItem navItem = {};
				navItem.guid = insertionGuid;
				navItem.mapId = NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorMapID ();
				GS::Array<API_NavigatorItem> navItems;
				ACAPI_Navigator_GetNavigatorChildrenItems (&navItem, &navItems);
				if (navItems.GetSize () > 0) {
					childGuid = APIGuid2GSGuid (navItems.GetLast ().guid);
				}
			}
			break;
			case DG_DRAG_INSERT_BELOW:
			{
				childGuid = APIGuid2GSGuid (insertionGuid);
				Int32 parent = DGTreeViewGetItem (dialogID, NavTreeView, ddData->insertPos, DG_TVG_PARENT);
				parentGuid = APIGuid2GSGuid (NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem (TruncateTo32Bit (DGTreeViewGetItemUserData (dialogID, NavTreeView, parent))));
			}
			break;

			case DG_DRAG_INSERT_ABOVE:
			{
				Int32 parent = DGTreeViewGetItem (dialogID, NavTreeView, ddData->insertPos, DG_TVG_PARENT);
				parentGuid = APIGuid2GSGuid (NavigatorPaletteTestGlobals::GetInstance ().GetNavigatorItem (TruncateTo32Bit (DGTreeViewGetItemUserData (dialogID, NavTreeView, parent))));
			}
			break;
			}
			GS::Guid sourceGuid = APIGuid2GSGuid (selectedGuid);
			if (ACAPI_Navigator_SetNavigatorItemPosition (&sourceGuid, &parentGuid, &childGuid) == NoError) {
				UpdateTree (dialogID);
			}
		}
	}
	break;

	case DG_MSG_GROW:
	{
		short	vgrow = DGGetVGrow (msgData);
		short	hgrow = DGGetHGrow (msgData);
		DGBeginMoveGrowItems (dialogID);

		DGMoveItem (dialogID, NavCloseButton, hgrow, vgrow);
		DGMoveItem (dialogID, NavUpdateButton, hgrow, vgrow);
		DGMoveItem (dialogID, NavDeleteButton, hgrow, vgrow);
		DGMoveItem (dialogID, NavPublishButton, hgrow, vgrow);

		DGGrowItem (dialogID, NavPopUp, hgrow, 0);
		DGGrowItem (dialogID, NavTreeView, hgrow, vgrow);
		DGMoveGrowItem (dialogID, LayerText, 0, vgrow, hgrow, 0);
		DGMoveGrowItem (dialogID, ScaleText, 0, vgrow, hgrow, 0);
		DGMoveGrowItem (dialogID, ZoomText, 0, vgrow, hgrow, 0);
		DGMoveGrowItem (dialogID, DisplayOpText, 0, vgrow, hgrow, 0);
		DGMoveGrowItem (dialogID, D3WindowStyleText, 0, vgrow, hgrow, 0);
		DGMoveGrowItem (dialogID, NavCustomNameText, 0, vgrow, hgrow, 0);
		DGMoveItem (dialogID, NavRenameButton, hgrow, vgrow);

		DGEndMoveGrowItems (dialogID);

	}
	break;

	case DG_MSG_CLOSE:
	{
		DelTreeData (dialogID, NavTreeView, DG_TVI_ROOT);
		ACAPI_UnregisterModelessWindow (dialogID);
	}
	break;
	}

	return ret;
}		// APINavigator_Handler
