#include "Definitions.hpp"
#include "Array.hpp"

namespace IO {
	class Location;
}

namespace GS {
	class UniString;
}

GSErrCode AddLocalLibrary (const IO::Location& libraryLocation);
GSErrCode AddLocalLibraryList (const GS::Array<IO::Location>& libraryLocations);
GSErrCode AddServerLibrary (const GS::UniString& serverAddress, const GS::UniString& libraryPath);
GSErrCode RemoveAllLibraries ();
GSErrCode DumpLibraryList ();
GSErrCode DumpAllLibParts ();
GSErrCode DuplicateSelectionToEmbeddedLibrary ();
GSErrCode CreateNewObjectFromSelection ();
GSErrCode UploadLibrariesToServer (const GS::Array<IO::Location>& libraryLocationList, const GS::UniString& serverAddress, const GS::UniString& libraryPath);
GSErrCode RemoveLibrariesFromServer (const GS::Array<GS::UniString>& libraryNameList, const GS::UniString& serverAddress, const GS::UniString& libraryPath);
