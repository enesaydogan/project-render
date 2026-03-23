#include "Visibility_Data.hpp"

GS::ClassInfo VisibilityData::classInfo (
	"VisibilityData",
	GS::Guid ("{AB8B4051-98E8-4564-9A7A-B7E4E8D84306}"),
	GS::ClassVersion (1, 0)
);

VisibilityData::VisibilityData () :
	isEnabled (false)
{

}

VisibilityData::~VisibilityData ()
{

}

bool VisibilityData::IsEnabled () const
{
	return isEnabled;
}

void VisibilityData::SetEnabled (bool enabled)
{
	isEnabled = enabled;
}

bool VisibilityData::IsItemVisible (const GS::Guid& guid) const
{
	return !hiddenItems.Contains (guid);
}

void VisibilityData::ShowItem (const GS::Guid& guid)
{
	hiddenItems.Delete (guid);
}

void VisibilityData::HideItem (const GS::Guid& guid)
{
	hiddenItems.Add (guid);
}

void VisibilityData::Clear ()
{
	hiddenItems.Clear ();
}

void VisibilityData::RemoveDeletedItems (const GS::HashSet<GS::Guid>& existingGuids)
{
	GS::HashSet<GS::Guid> guidsToDelete;
	hiddenItems.Enumerate ([&] (const GS::Guid& guid) {
		if (!existingGuids.Contains (guid)) {
			guidsToDelete.Add (guid);
		}
	});
	guidsToDelete.Enumerate ([&] (const GS::Guid& guid) {
		hiddenItems.Delete (guid);
	});
}

GSErrCode VisibilityData::Read (GS::IChannel& ic)
{
	GS::InputFrame frame (ic, classInfo);
	if (frame.GetStatus () != NoError) {
		return frame.GetStatus ();
	}

	GSErrCode err = Error;
	switch (frame.GetMainVersion ()) {
		case 1:	err = ReadVersion1 (ic, frame);	break;
		default:
			DBBREAK_STR ("VisibilityData: Unsupported object version\n");
			break;
	}
	return err;
}

GSErrCode VisibilityData::Write (GS::OChannel& oc) const
{
	GS::OutputFrame frame (oc, classInfo);

	GSErrCode err = Error;
	switch (frame.GetMainVersion ()) {
		case 1: err = WriteVersion1 (oc, frame); break;
		default:
			DBBREAK_STR ("VisibilityData: Unsupported object version\n");
			break;
	}
	return err;
}

GSErrCode VisibilityData::ReadVersion1 (GS::IChannel& ic, const GS::InputFrame& /*frame*/)
{
	ic.Read (isEnabled);
	hiddenItems.Read (ic);
	return ic.GetInputStatus ();
}

GSErrCode VisibilityData::WriteVersion1 (GS::OChannel& oc, const GS::OutputFrame& /*frame*/) const
{
	oc.Write (isEnabled);
	hiddenItems.Write (oc);
	return oc.GetOutputStatus ();
}
