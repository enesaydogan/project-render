// *****************************************************************************
// Source code for the Navigator Test Add-On
// *****************************************************************************

#ifndef	NAVIGATORCALLBACKINTERFACE_HPP
#define	NAVIGATORCALLBACKINTERFACE_HPP

#pragma once

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "API_Guid.hpp"
#include "INavigatorCallbackInterface.hpp"


// -----------------------------------------------------------------------------
// Implementation of INavigatorCallbackInterface
// -----------------------------------------------------------------------------

const	API_Guid RootGuid = APIGuidFromString ("E6CD62CD-0FC0-4FCC-8655-9A2A976C5501");

class NavigatorCallbackInterface : public INavigatorCallbackInterface {
public:
	virtual GSErrCode	OpenView (const API_Guid& /*viewPointId*/, bool /*newWindow*/) const override;
	virtual GSErrCode	OpenSettings (const API_Guid& viewPointID) const override;
	virtual GSErrCode	CreateIDFStore (const API_Guid& viewPointID, double scale, double& clipBoxWidth, double& clipBoxHeight, GSPtr& idfStore, API_Box& boundingBox, double& paddingX, double& paddingY, GS::Array<API_Guid>& elems) const override;
	virtual GSErrCode	GetElemsForDrawingCheck (const API_Guid& viewPointID, GS::Array<API_Guid>& elems) const override;
	virtual GSErrCode	NewItem (const API_Guid& /*viewPointID*/) const override;
	virtual GSErrCode	DeleteItem (const GS::Array<API_Guid>& viewPointIDList) const override;
	virtual GSErrCode	RenameItem (const API_Guid& viewPointID) const override;
	virtual DG::Icon	GetIcon (Int32 /*iconId*/, IconTWMode /*iconTWMode*/, IconLinkness linkness, IconSize /*size*/, IconContext /*isLayout*/) const override;
	virtual bool		GetContextMenuCommandString (ContextMenuCommand cmd, const API_Guid& /*vpId*/, GS::UniString& text) const override;
	virtual bool		IsContextMenuCommandEnabled (ContextMenuCommand /*cmd*/, const API_Guid& /*viewPointId*/) const override;

	virtual bool		GetTWCommandMenuString (TWCommandMenu cmd, const API_Guid& viewPointId, GS::UniString& text) const override;
	virtual bool		GetTWCommandMailString (TWCommandMail cmd, const API_Guid& viewPointId, GS::UniString& text) const override;

	virtual GSErrCode ExecuteMergePostProcess () const override;

};

#endif