#include "APIEnvir.h"
#include "ACAPinc.h"

#include "FormatOptionsDialog.hpp"
#include "Resources.hpp"
#include "File.hpp"
#include "MemoryOChannel32.hpp"
#include "MemoryIChannel32.hpp"

#include "DG.h"


static GS::Array<FormatTranslator> translatorList = {
	{ GS::Guid ("BA48DBEA-D1D9-401D-95EB-21515CE00578"), "Default Translator" },
	{ GS::Guid ("74EC730B-DBE2-4DD3-A4E9-78B5C01AD670"), "Translator 1"		  },
	{ GS::Guid ("9B70EC3B-CF02-45C4-A174-B9562BCD998C"), "Translator 2"		  },
	{ GS::Guid ("9C8E03FD-8C9B-4438-9A95-668210CE0828"), "Translator 3"		  },
	{ GS::Guid ("C5D2F332-85EC-40BF-888A-AC823DB0D4B5"), "Translator 4"		  },
	{ GS::Guid ("06F3DE60-7997-4789-A9E0-C703ECA44E12"), "Translator 5"		  }
};


// =============================================================================
//
// Required functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name,        FORMATREGISTER_ADDONINFO_STR_ID, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, FORMATREGISTER_ADDONINFO_STR_ID, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	API_PublishFormatData formatData = {};
	formatData.mimeType = "text/Testxml";
	formatData.popUpText = "Test format";
	formatData.fileExtText2D = "test2D";
	formatData.fileExtText3D = "test3D";
	formatData.windowTypes = { APIWind_AllID };
	formatData.singleFile = false;
	formatData.iconID = DG::Icon::NoIcon;

	GSErrCode err = ACAPI_AddOnIntegration_RegisterPublishFormat (formatData);
	DBASSERT (err == NoError);

	return err;
}		// RegisterInterface


static FormatTranslator GetFormatTranslator (const GSHandle& dataHandle)
{
	FormatTranslator formatTranslator;
	if (dataHandle == nullptr) {
		formatTranslator = translatorList.GetFirst ();
	}
	else {
		GS::MemoryIChannel32 memChannel (*dataHandle, BMGetHandleSize (dataHandle));
		formatTranslator.Read (memChannel);
	}

	return formatTranslator;
}



GSErrCode SaveProcImp (API_SaveMethod /*method*/, const char* path, API_UserData* userData, GS::UniString* reportStr)
{
	FormatTranslator locTranslator = GetFormatTranslator (userData->dataHdl);

	const GS::UniString testText = locTranslator.GetName () + L("\n");
	if (!testText.IsEmpty ()) {
		IO::File testFile (IO::Location (path), IO::File::Create);
		if (DBVERIFY (testFile.Open (IO::File::WriteEmptyMode) == NoError)) {

			for (int i = 0; i < 500; ++i)
				testText.Write (testFile);

			testFile.Close ();
		}
	}
	else {
		*reportStr = "Empty text cannot be exported.";
		return Error;
	}

	return NoError;
}


GSErrCode SaveOptionProcImp (API_SaveMethod /*method*/, API_UserData* userData)
{
	FormatTranslator locTranslator = GetFormatTranslator (userData->dataHdl);

	FormatOptionDialog optionDialog (translatorList, locTranslator, ACAPI_GetOwnResModule ());
	if (optionDialog.Invoke ()) {
		locTranslator = optionDialog.GetFormatTranslator ();

		if (userData->dataHdl != nullptr)
			BMKillHandle (&userData->dataHdl);
		BNZeroMemory (userData, sizeof (API_UserData));

		GS::MemoryOChannel32 memChannel (GS::MemoryOChannel32::BMAllocation);
		locTranslator.Write (memChannel);

		userData->dataVersion = 1;
		userData->platformSign = GS::Act_Platform_Sign;
		userData->dataHdl = BMAllocateHandle (memChannel.GetDataSize (), ALLOCATE_CLEAR, 0);

		BNCopyMemory (*(userData->dataHdl), memChannel.GetDestination (), memChannel.GetDataSize ());
	}

	return NoError;
}


GSErrCode TranslatorGetterProcImp (GS::Array<API_TranslatorNameConfig>& arr) 
{
	arr.Clear ();
	for (const FormatTranslator& translator : translatorList)
		arr.Push ({ GSGuid2APIGuid (translator.GetGuid ()), translator.GetName () });
	return NoError;
}


GSErrCode DefaultTranslatorGetterProcImp (API_TranslatorNameConfig& def) 
{
	def.guid = GSGuid2APIGuid (translatorList.GetFirst ().GetGuid ());
	def.translatorName = translatorList.GetFirst ().GetName ();
	return NoError;
}


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_AddOnIntegration_InstallPublisherSaveMethod ("text/Testxml", SaveProcImp);

	err |= ACAPI_AddOnIntegration_InstallPublisherSaveOption ("text/Testxml", SaveOptionProcImp);
	err |= ACAPI_AddOnIntegration_InstallPublisherTranslatorGetter ("text/Testxml", TranslatorGetterProcImp);
	err |= ACAPI_AddOnIntegration_InstallPublisherDefaultTranslatorGetter ("text/Testxml", DefaultTranslatorGetterProcImp);
	DBASSERT (err == NoError);

	return err;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}		// FreeData
