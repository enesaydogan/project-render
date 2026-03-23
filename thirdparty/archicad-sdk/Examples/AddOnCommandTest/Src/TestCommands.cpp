#include	"TestCommands.hpp"
#include	"ObjectState.hpp"
#include	"StringConversion.hpp"
#include	"TestCommandUtilities.hpp"
#include	"DG.h"


// --- CreateColumnCommand ----------------------------------------------------------------------------------

GS::String CreateColumnCommand::GetName () const
{
	return "CreateColumnCommand";
}


GS::String CreateColumnCommand::GetNamespace () const
{
	return TestCommandUtility::GetCommandNamespace ();
}


GS::Optional<GS::UniString> CreateColumnCommand::GetSchemaDefinitions () const
{
	return R"(
		{
			"Point2D": {
				"type": "object",
				"description" : "Position of a 2D point.",
				"properties" : {
					"x": {
						"type": "number",
						"description" : "X value of the point."
					},
					"y" : {
						"type": "number",
						"description" : "Y value of the point."
					}
				},
				"additionalProperties": false,
				"required" : [
					"x",
					"y"
				]
			}
		}
	)";
}


GS::Optional<GS::UniString> CreateColumnCommand::GetInputParametersSchema () const
{
	return R"(
		{
			"type": "object",
			"properties" : {
				"position": {
					"$ref": "#Point2D"
				}
			},
			"additionalProperties" : false,
			"required" : [
				"position"
			]
		}
	)";
}


GS::Optional<GS::UniString> CreateColumnCommand::GetResponseSchema () const
{
	return R"(
		{ 
			"type": "object",
			"oneOf": [
				{
					"$ref": "APITypes.json#/definitions/ElementId"
				},
				{
					"$ref": "APITypes.json#/definitions/ErrorItem"
				}
			]
		}
	)";
}


GS::ObjectState	CreateColumnCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
	API_Element element = {};
	API_ElementMemo memo = {};

	element.header.type = API_ColumnID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err == NoError) {
		double xPos = 0;
		double yPos = 0;

		if (!parameters.IsEmpty ()) {
			GS::ObjectState position;
			if (parameters.Get ("position", position)) {
				position.Get ("x", xPos);
				position.Get ("y", yPos);
			}
		}

		element.column.origoPos.x = xPos;
		element.column.origoPos.y = yPos;

		err = ACAPI_CallUndoableCommand ("CreateColumn", [&] () -> GSErrCode {
			return ACAPI_Element_Create (&element, &memo);
		});

		ACAPI_DisposeElemMemoHdls (&memo);
	}

	if (err == NoError) {
		return GS::ObjectState ("guid", APIGuidToString (element.header.guid));
	}

	return GS::ObjectState ("error",
				GS::ObjectState ("code", err,
								 "message", "Failed to create column."));
}


void CreateColumnCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}


// --- ProtectionInfoCommand --------------------------------------------------------------------------------

GS::String ProtectionInfoCommand::GetName () const
{
	return "ProtectionInfoCommand";
}


GS::String ProtectionInfoCommand::GetNamespace () const
{
	return TestCommandUtility::GetCommandNamespace ();
}


GS::Optional<GS::UniString> ProtectionInfoCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> ProtectionInfoCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> ProtectionInfoCommand::GetResponseSchema () const
{
	return R"(
		{ 
			"type": "object",
			"oneOf": [
				{
					"properties" : {
						"isSTART": {
							"type": "boolean"
						},
						"isEDU": {
							"type": "boolean"
						},
						"isSOLO": {
							"type": "boolean"
						},
						"isDEMO": {
							"type": "boolean"
						},
						"isTRIAL": {
							"type": "boolean"
						},
						"isBTC": {
							"type": "boolean"
						},
						"isNFR": {
							"type": "boolean"
						},
						"isNetworkLicense": {
							"type": "boolean"
						}
					},
					"additionalProperties" : false,
					"required" : [
						"isSTART",
						"isEDU",
						"isSOLO",
						"isDEMO",
						"isTRIAL",
						"isBTC",
						"isNFR",
						"isNetworkLicense"
					]
				},
				{
					"$ref": "APITypes.json#/definitions/ErrorItem"
				}
			]
		}
	)";
}


GS::ObjectState	ProtectionInfoCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
	const UInt32 protMode = ACAPI_Licensing_GetProtectionMode ();
	GS::ObjectState resultOS;

	resultOS.Add ("isSTART",			(protMode & APIPROT_START_MASK) != 0);
	resultOS.Add ("isEDU",				(protMode & APIPROT_PROTEDU_MASK) != 0 ||
										(protMode & APIPROT_EDU_MASK) != 0);
	resultOS.Add ("isSOLO",				(protMode & APIPROT_SOLO_MASK) != 0);
	resultOS.Add ("isDEMO",				(protMode & APIPROT_DEMO_MASK) != 0);
	resultOS.Add ("isTRIAL",			(protMode & APIPROT_TRIAL_MASK) != 0);
	resultOS.Add ("isBTC",				(protMode & APIPROT_BTC_MASK) != 0);
	resultOS.Add ("isNFR",				(protMode & APIPROT_NFR_MASK) != 0);
	resultOS.Add ("isNetworkLicense",	(protMode & APIPROT_NET_MASK) != 0);

	return resultOS;
}


void ProtectionInfoCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}
