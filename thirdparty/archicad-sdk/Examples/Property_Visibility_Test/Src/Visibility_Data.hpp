#ifndef VISIBILITY_DATA_HPP
#define VISIBILITY_DATA_HPP

#include "HashSet.hpp"
#include "HashTable.hpp"
#include "Channel.hpp"
#include "UniString.hpp"
#include "GSGuid.hpp"

class VisibilityData : public GS::Object
{
	DECLARE_CLASS_INFO;

public:
	VisibilityData ();
	~VisibilityData ();

	bool				IsEnabled () const;
	void				SetEnabled (bool enabled);

	bool				IsItemVisible (const GS::Guid& guid) const;
	void				ShowItem (const GS::Guid& guid);
	void				HideItem (const GS::Guid& guid);
	void				Clear ();

	void				RemoveDeletedItems (const GS::HashSet<GS::Guid>& existingGuids);

	virtual	GSErrCode	Read (GS::IChannel& ic) override;
	virtual	GSErrCode	Write (GS::OChannel& oc) const override;

private:
	GSErrCode			ReadVersion1 (GS::IChannel& ic, const GS::InputFrame& /*frame*/);
	GSErrCode			WriteVersion1 (GS::OChannel& oc, const GS::OutputFrame& /*frame*/) const;

	bool					isEnabled;
	GS::HashSet<GS::Guid>	hiddenItems;
};

#endif
