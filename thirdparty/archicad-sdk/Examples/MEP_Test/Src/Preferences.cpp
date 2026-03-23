#include "Preferences.hpp"
#include "Reporter.hpp"

// ACAPI
#include "ACAPI/Result.hpp"

// MEPAPI
#include "ACAPI/MEPUniqueID.hpp"

#include "ACAPI/MEPPreferenceTableContainerBase.hpp"
#include "ACAPI/MEPDuctSegmentPreferenceTableContainer.hpp"
#include "ACAPI/MEPPipeSegmentPreferenceTableContainer.hpp"
#include "ACAPI/MEPCableCarrierSegmentPreferenceTableContainer.hpp"
#include "ACAPI/MEPPipeSegmentPreferenceTable.hpp"
#include "ACAPI/MEPDuctCircularSegmentPreferenceTable.hpp"
#include "ACAPI/MEPDuctRectangularSegmentPreferenceTable.hpp"
#include "ACAPI/MEPCableCarrierSegmentPreferenceTable.hpp"
#include "ACAPI/MEPPipeElbowPreferenceTableContainer.hpp"
#include "ACAPI/MEPPipeElbowPreferenceTable.hpp"
#include "ACAPI/MEPDuctReferenceSet.hpp"
#include "ACAPI/MEPPipeReferenceSet.hpp"
#include "ACAPI/MEPDuctElbowPreferenceTableContainer.hpp"
#include "ACAPI/MEPDuctElbowPreferenceTable.hpp"
#include "ACAPI/MEPPipeBranchPreferenceTableContainer.hpp"
#include "ACAPI/MEPPipeBranchPreferenceTable.hpp"
#include "ACAPI/MEPDuctBranchPreferenceTableContainer.hpp"
#include "ACAPI/MEPDuctBranchPreferenceTable.hpp"
#include "ACAPI/MEPPipeTransitionPreferenceTableContainer.hpp"
#include "ACAPI/MEPPipeTransitionPreferenceTable.hpp"
#include "ACAPI/MEPDuctTransitionPreferenceTableContainer.hpp"
#include "ACAPI/MEPDuctTransitionPreferenceTable.hpp"

using namespace ACAPI::MEP;


namespace MEPExample {


GSErrCode QueryMEPPreferences ()
{
	Reporter preferenceReporter;

	// Duct preferences
	{
		// ! [ReferenceSet-Getters-Example]

		ACAPI::Result<DuctReferenceSet> ductReferenceSet = GetDuctReferenceSet ();
		if (ductReferenceSet.IsErr ())
			return ductReferenceSet.UnwrapErr ().kind;

		preferenceReporter.Add ("Duct Preferences");
		preferenceReporter.AddNewLine ();

		preferenceReporter.SetTabCount (1);
		preferenceReporter.Add ("Duct Reference Set");
		uint32_t referenceIdCount = ductReferenceSet->GetSize ();
		preferenceReporter.SetTabCount (2);
		for (uint32_t i = 0; i < referenceIdCount; ++i) {
			preferenceReporter.Add ("ReferenceId", *ductReferenceSet->GetReferenceId (i));
		}

		// ! [ReferenceSet-Getters-Example]

		preferenceReporter.SetTabCount (1);
		preferenceReporter.AddNewLine ();

		// Segment
		// ! [PreferenceTableContainer-Getters-Example]

		ACAPI::Result<DuctSegmentPreferenceTableContainer> ductSegmentContainer = GetDuctSegmentPreferenceTableContainer ();

		if (ductSegmentContainer.IsErr ())
			return ductSegmentContainer.UnwrapErr ().kind;

		std::vector<UniqueID> ductSegmentTableIds = ductSegmentContainer->GetPreferenceTables ();

		// ! [PreferenceTableContainer-Getters-Example]

		preferenceReporter.SetTabCount (1);
		preferenceReporter.Add ("Segment Preference Tables");

		// ! [DuctSegmentPreferenceTable-Getters-Example]

		for (const UniqueID& ductSegmentTableId : ductSegmentTableIds) {
			ACAPI::Result<DuctRectangularSegmentPreferenceTable> ductRectangularSegmentTable = DuctRectangularSegmentPreferenceTable::Get (ductSegmentTableId);

			if (ductRectangularSegmentTable.IsErr ())
				continue;
			
			preferenceReporter.SetTabCount (2);
			preferenceReporter.Add ("Preference name", ductRectangularSegmentTable->GetName ());
			preferenceReporter.Add ("Rectangular table:");

			for (uint32_t i = 0; i < ductRectangularSegmentTable->GetSize (); ++i) {
				preferenceReporter.SetTabCount (3);
				preferenceReporter.Add ("Value", *ductRectangularSegmentTable->GetValue (i), false);
				preferenceReporter.SetTabCount (0);
				preferenceReporter.Add (", ", false);
				GS::UniString description = *ductRectangularSegmentTable->GetDescription (i);
				preferenceReporter.Add ("Description", description == GS::EmptyUniString ? "Empty" : description);

			}
			preferenceReporter.AddNewLine ();

			ACAPI::Result<DuctCircularSegmentPreferenceTable> ductCircularSegmentTable = DuctCircularSegmentPreferenceTable::Get (ductSegmentTableId);

			if (ductCircularSegmentTable.IsErr ())
				continue;

			preferenceReporter.SetTabCount (2);
			preferenceReporter.Add ("Circular table:");

			for (uint32_t i = 0; i < ductCircularSegmentTable->GetSize (); ++i) {
				preferenceReporter.SetTabCount (3);
				preferenceReporter.Add ("ReferenceId", *ductCircularSegmentTable->GetReferenceId (i), false);
				preferenceReporter.SetTabCount (0);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Diameter", *ductCircularSegmentTable->GetDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Wall Thickness", *ductCircularSegmentTable->GetWallThickness (i), false);
				preferenceReporter.Add (", ", false);
				GS::UniString description = *ductCircularSegmentTable->GetDescription (i);
				preferenceReporter.Add ("Description", description == GS::EmptyUniString ? "Empty" : description, false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add (*ductCircularSegmentTable->IsRowValid (i) ? "Row is valid" : "Row is not valid");
			}

			preferenceReporter.AddNewLine ();
		}
		// ! [DuctSegmentPreferenceTable-Getters-Example]

		preferenceReporter.Write ();


		// Elbow
		ACAPI::Result<DuctElbowPreferenceTableContainer> ductElbowContainer = GetDuctElbowPreferenceTableContainer ();

		if (ductElbowContainer.IsErr ())
			return ductElbowContainer.UnwrapErr ().kind;

		std::vector<UniqueID> ductElbowTableIds = ductElbowContainer->GetPreferenceTables ();
		
		preferenceReporter.SetTabCount (1);
		preferenceReporter.Add ("Elbow Preference Tables");

		// ! [DuctElbowPreferenceTable-Getters-Example]
		for (const UniqueID& ductElbowTableId : ductElbowTableIds) {
			ACAPI::Result<DuctElbowPreferenceTable> ductElbowTable = DuctElbowPreferenceTable::Get (ductElbowTableId);

			if (ductElbowTable.IsErr ())
				continue;

			preferenceReporter.SetTabCount (2);
			preferenceReporter.Add ("Preference name", ductElbowTable->GetName ());
			preferenceReporter.Add ("Circular table:");

			for (uint32_t i = 0; i < ductElbowTable->GetSize (); ++i) {
				preferenceReporter.SetTabCount (3);
				preferenceReporter.Add ("ReferenceId", *ductElbowTable->GetReferenceId (i), false);
				preferenceReporter.SetTabCount (0);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Diameter", *ductElbowTable->GetDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Radius", *ductElbowTable->GetRadius (i), false);
				preferenceReporter.Add (", ", false);
				GS::UniString description = *ductElbowTable->GetDescription (i);
				preferenceReporter.Add ("Description", description == GS::EmptyUniString ? "Empty" : description, false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add (*ductElbowTable->IsRowValid (i) ? "Row is valid" : "Row is not valid");
			}

			preferenceReporter.AddNewLine ();
		}
		// ! [DuctElbowPreferenceTable-Getters-Example]

		preferenceReporter.Write ();

		// Branch
		ACAPI::Result<DuctBranchPreferenceTableContainer> ductBranchContainer = GetDuctBranchPreferenceTableContainer ();

		if (ductBranchContainer.IsErr ())
			return ductBranchContainer.UnwrapErr ().kind;

		std::vector<UniqueID> ductBranchTableIds = ductBranchContainer->GetPreferenceTables ();
		
		preferenceReporter.SetTabCount (1);
		preferenceReporter.Add ("Branch Preference Tables");

		// ! [DuctBranchPreferenceTable-Getters-Example]
		for (const UniqueID& ductBranchTableId : ductBranchTableIds) {
			ACAPI::Result<DuctBranchPreferenceTable> ductBranchTable = DuctBranchPreferenceTable::Get (ductBranchTableId);

			if (ductBranchTable.IsErr ())
				continue;

			preferenceReporter.SetTabCount (2);
			preferenceReporter.Add ("Preference name", ductBranchTable->GetName ());
			preferenceReporter.Add ("Circular table:");

			for (uint32_t i = 0; i < ductBranchTable->GetSize (); ++i) {
				preferenceReporter.SetTabCount (3);
				preferenceReporter.Add ("Main Axis ReferenceId", *ductBranchTable->GetMainAxisReferenceId (i), false);
				preferenceReporter.SetTabCount (0);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Branch Axis ReferenceId", *ductBranchTable->GetBranchAxisReferenceId (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Main Axis Diameter", *ductBranchTable->GetMainAxisDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Branch Axis Diameter", *ductBranchTable->GetBranchAxisDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Length", *ductBranchTable->GetLength (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Branch Offset", *ductBranchTable->GetBranchOffset (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Connection Length", *ductBranchTable->GetConnectionLength (i), false);
				preferenceReporter.Add (", ", false);
				GS::UniString description = *ductBranchTable->GetDescription (i);
				preferenceReporter.Add ("Description", description == GS::EmptyUniString ? "Empty" : description, false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add (*ductBranchTable->IsRowValid (i) ? "Row is valid" : "Row is not valid");
			}

			preferenceReporter.AddNewLine ();
		}

		// ! [DuctBranchPreferenceTable-Getters-Example]
		preferenceReporter.Write ();

		// Transition
		ACAPI::Result<DuctTransitionPreferenceTableContainer> ductTransitionContainer = GetDuctTransitionPreferenceTableContainer ();

		if (ductTransitionContainer.IsErr ())
			return ductTransitionContainer.UnwrapErr ().kind;

		std::vector<UniqueID> ductTransitionTableIds = ductTransitionContainer->GetPreferenceTables ();

		preferenceReporter.SetTabCount (1);
		preferenceReporter.Add ("Transition Preference Tables");

		// ! [DuctTransitionPreferenceTable-Getters-Example]
		for (const UniqueID& ductTransitionTableId : ductTransitionTableIds) {
			ACAPI::Result<DuctTransitionPreferenceTable> ductTransitionTable = DuctTransitionPreferenceTable::Get (ductTransitionTableId);

			if (ductTransitionTable.IsErr ())
				continue;

			preferenceReporter.SetTabCount (2);
			preferenceReporter.Add ("Preference name", ductTransitionTable->GetName ());
			preferenceReporter.Add ("Circular table:");

			for (uint32_t i = 0; i < ductTransitionTable->GetSize (); ++i) {
				preferenceReporter.SetTabCount (3);
				preferenceReporter.Add ("Begin ReferenceId", *ductTransitionTable->GetBeginReferenceId (i), false);
				preferenceReporter.SetTabCount (0);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("End ReferenceId", *ductTransitionTable->GetEndReferenceId (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Begin Diameter", *ductTransitionTable->GetBeginDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("End Diameter", *ductTransitionTable->GetEndDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Angle", *ductTransitionTable->GetAngle (i), false);
				preferenceReporter.Add (", ", false);
				GS::UniString description = *ductTransitionTable->GetDescription (i);
				preferenceReporter.Add ("Description", description == GS::EmptyUniString ? "Empty" : description, false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add (*ductTransitionTable->IsRowValid (i) ? "Row is valid" : "Row is not valid");
			}

			preferenceReporter.AddNewLine ();
		}
		// ! [DuctTransitionPreferenceTable-Getters-Example]

		preferenceReporter.Write ();
	}

	// Pipe preferences
	{
		ACAPI::Result<PipeReferenceSet> pipeReferenceSet = GetPipeReferenceSet ();
		if (pipeReferenceSet.IsErr ())
			return pipeReferenceSet.UnwrapErr ().kind;

		preferenceReporter.SetTabCount (0);
		preferenceReporter.Add ("Pipe Preferences");
		preferenceReporter.AddNewLine ();

		preferenceReporter.SetTabCount (1);
		preferenceReporter.Add ("Pipe Reference Set");
		uint32_t referenceIdCount = pipeReferenceSet->GetSize ();
		preferenceReporter.SetTabCount (2);
		for (uint32_t i = 0; i < referenceIdCount; ++i) {
			preferenceReporter.Add ("ReferenceId", *pipeReferenceSet->GetReferenceId (i));
		}
		preferenceReporter.AddNewLine ();

		// Segment
		ACAPI::Result<PipeSegmentPreferenceTableContainer> pipeSegmentContainer = GetPipeSegmentPreferenceTableContainer ();

		if (pipeSegmentContainer.IsErr ())
			return pipeSegmentContainer.UnwrapErr ().kind;

		std::vector<UniqueID> pipeSegmentTableIds = pipeSegmentContainer->GetPreferenceTables ();

		preferenceReporter.SetTabCount (1);
		preferenceReporter.Add ("Segment Preference Tables");

		// ! [PipeSegmentPreferenceTable-Getters-Example]
		for (const UniqueID& pipeSegmentTableId : pipeSegmentTableIds) {
			ACAPI::Result<PipeSegmentPreferenceTable> pipeSegmentTable = PipeSegmentPreferenceTable::Get (pipeSegmentTableId);

			if (pipeSegmentTable.IsErr ())
				continue;

			preferenceReporter.SetTabCount (2);
			preferenceReporter.Add ("Preference name", pipeSegmentTable->GetName ());
			preferenceReporter.Add ("Circular table:");

			for (uint32_t i = 0; i < pipeSegmentTable->GetSize (); ++i) {
				preferenceReporter.SetTabCount (3);
				preferenceReporter.Add ("ReferenceId", *pipeSegmentTable->GetReferenceId (i), false);
				preferenceReporter.SetTabCount (0);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Diameter", *pipeSegmentTable->GetDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("WallThickness", *pipeSegmentTable->GetWallThickness (i), false);
				preferenceReporter.Add (", ", false);
				GS::UniString description = *pipeSegmentTable->GetDescription (i);
				preferenceReporter.Add ("Description", description == GS::EmptyUniString ? "Empty" : description, false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add (*pipeSegmentTable->IsRowValid (i) ? "Row is valid" : "Row is not valid");
			}

			preferenceReporter.AddNewLine ();
		}
		// ! [PipeSegmentPreferenceTable-Getters-Example]

		preferenceReporter.Write ();

		// Elbow
		ACAPI::Result<PipeElbowPreferenceTableContainer> pipeElbowContainer = GetPipeElbowPreferenceTableContainer ();

		if (pipeElbowContainer.IsErr ())
			return pipeElbowContainer.UnwrapErr ().kind;

		std::vector<UniqueID> pipeElbowTableIds = pipeElbowContainer->GetPreferenceTables ();
		
		preferenceReporter.SetTabCount (1);
		preferenceReporter.Add ("Elbow Preference Tables");

		// ! [PipeElbowPreferenceTable-Getters-Example]
		for (const UniqueID& pipeElbowTableId : pipeElbowTableIds) {
			ACAPI::Result<PipeElbowPreferenceTable> pipeElbowTable = PipeElbowPreferenceTable::Get (pipeElbowTableId);

			if (pipeElbowTable.IsErr ())
				continue;

			preferenceReporter.SetTabCount (2);
			preferenceReporter.Add ("Preference name", pipeElbowTable->GetName ());
			preferenceReporter.Add ("Circular table:");

			for (uint32_t i = 0; i < pipeElbowTable->GetSize (); ++i) {
				preferenceReporter.SetTabCount (3);
				preferenceReporter.Add ("ReferenceId", *pipeElbowTable->GetReferenceId (i), false);
				preferenceReporter.SetTabCount (0);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Diameter", *pipeElbowTable->GetDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Radius", *pipeElbowTable->GetRadius (i), false);
				preferenceReporter.Add (", ", false);
				GS::UniString description = *pipeElbowTable->GetDescription (i);
				preferenceReporter.Add ("Description", description == GS::EmptyUniString ? "Empty" : description, false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add (*pipeElbowTable->IsRowValid (i) ? "Row is valid" : "Row is not valid");
			}

			preferenceReporter.AddNewLine ();
		}
		// ! [PipeElbowPreferenceTable-Getters-Example]

		preferenceReporter.Write ();

		// Branch
		ACAPI::Result<PipeBranchPreferenceTableContainer> pipeBranchContainer = GetPipeBranchPreferenceTableContainer ();

		if (pipeBranchContainer.IsErr ())
			return pipeBranchContainer.UnwrapErr ().kind;

		std::vector<UniqueID> pipeBranchTableIds = pipeBranchContainer->GetPreferenceTables ();
		
		preferenceReporter.SetTabCount (1);
		preferenceReporter.Add ("Branch Preference Tables");

		// ! [PipeBranchPreferenceTable-Getters-Example]
		for (const UniqueID& pipeBranchTableId : pipeBranchTableIds) {
			ACAPI::Result<PipeBranchPreferenceTable> pipeBranchTable = PipeBranchPreferenceTable::Get (pipeBranchTableId);

			if (pipeBranchTable.IsErr ())
				continue;

			preferenceReporter.SetTabCount (2);
			preferenceReporter.Add ("Preference name", pipeBranchTable->GetName ());
			preferenceReporter.Add ("Circular table:");

			for (uint32_t i = 0; i < pipeBranchTable->GetSize (); ++i) {
				preferenceReporter.SetTabCount (3);
				preferenceReporter.Add ("Main Axis ReferenceId", *pipeBranchTable->GetMainAxisReferenceId (i), false);
				preferenceReporter.SetTabCount (0);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Branch Axis ReferenceId", *pipeBranchTable->GetBranchAxisReferenceId (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Main Axis Diameter", *pipeBranchTable->GetMainAxisDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Branch Axis Diameter", *pipeBranchTable->GetBranchAxisDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Length", *pipeBranchTable->GetLength (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Branch Offset", *pipeBranchTable->GetBranchOffset (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Connection Length", *pipeBranchTable->GetConnectionLength (i), false);
				preferenceReporter.Add (", ", false);
				GS::UniString description = *pipeBranchTable->GetDescription (i);
				preferenceReporter.Add ("Description", description == GS::EmptyUniString ? "Empty" : description, false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add (*pipeBranchTable->IsRowValid (i) ? "Row is valid" : "Row is not valid");
			}

			preferenceReporter.AddNewLine ();
		}
		// ! [PipeBranchPreferenceTable-Getters-Example]

		preferenceReporter.Write ();

		// Transition
		ACAPI::Result<PipeTransitionPreferenceTableContainer> pipeTransitionContainer = GetPipeTransitionPreferenceTableContainer ();

		if (pipeTransitionContainer.IsErr ())
			return pipeTransitionContainer.UnwrapErr ().kind;

		std::vector<UniqueID> pipeTransitionTableIds = pipeTransitionContainer->GetPreferenceTables ();

		preferenceReporter.SetTabCount (1);
		preferenceReporter.Add ("Transition Preference Tables");

		// ! [PipeTransitionPreferenceTable-Getters-Example]
		for (const UniqueID& pipeTransitionTableId : pipeTransitionTableIds) {
			ACAPI::Result<PipeTransitionPreferenceTable> pipeTransitionTable = PipeTransitionPreferenceTable::Get (pipeTransitionTableId);

			if (pipeTransitionTable.IsErr ())
				continue;

			preferenceReporter.SetTabCount (2);
			preferenceReporter.Add ("Preference name", pipeTransitionTable->GetName ());
			preferenceReporter.Add ("Circular table:");

			for (uint32_t i = 0; i < pipeTransitionTable->GetSize (); ++i) {
				preferenceReporter.SetTabCount (3);
				preferenceReporter.Add ("Begin ReferenceId", *pipeTransitionTable->GetBeginReferenceId (i), false);
				preferenceReporter.SetTabCount (0);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("End ReferenceId", *pipeTransitionTable->GetEndReferenceId (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Begin Diameter", *pipeTransitionTable->GetBeginDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("End Diameter", *pipeTransitionTable->GetEndDiameter (i), false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add ("Length", *pipeTransitionTable->GetLength (i), false);
				preferenceReporter.Add (", ", false);
				GS::UniString description = *pipeTransitionTable->GetDescription (i);
				preferenceReporter.Add ("Description", description == GS::EmptyUniString ? "Empty" : description, false);
				preferenceReporter.Add (", ", false);
				preferenceReporter.Add (*pipeTransitionTable->IsRowValid (i) ? "Row is valid" : "Row is not valid");
			}

			preferenceReporter.AddNewLine ();

		}
		// ! [PipeTransitionPreferenceTable-Getters-Example]

		preferenceReporter.Write ();
	}

	// Cable carrier preferences
	{
		ACAPI::Result<CableCarrierSegmentPreferenceTableContainer> cableCarrierSegmentContainer = GetCableCarrierSegmentPreferenceTableContainer ();

		if (cableCarrierSegmentContainer.IsErr ())
			return cableCarrierSegmentContainer.UnwrapErr ().kind;

		std::vector<UniqueID> cableCarrierSegmentTableIds = cableCarrierSegmentContainer->GetPreferenceTables ();

		preferenceReporter.Add ("Cable Carrier Preferences");
		// ! [CableCarrierSegmentPreferenceTable-Getters-Example]
		for (const UniqueID& cableCarrierSegmentTableId : cableCarrierSegmentTableIds) {
			ACAPI::Result<CableCarrierSegmentPreferenceTable> cableCarrierSegmentTable = CableCarrierSegmentPreferenceTable::Get (cableCarrierSegmentTableId);

			if (cableCarrierSegmentTable.IsErr ())
				continue;

			preferenceReporter.SetTabCount (1);
			preferenceReporter.Add ("Preference name", cableCarrierSegmentTable->GetName ());
			preferenceReporter.Add ("Rectangular table:");

			for (uint32_t i = 0; i < cableCarrierSegmentTable->GetSize (); ++i) {
				preferenceReporter.SetTabCount (2);
				preferenceReporter.Add ("Value", *cableCarrierSegmentTable->GetValue (i), false);
				preferenceReporter.SetTabCount (0);
				preferenceReporter.Add (", ", false);
				GS::UniString description = *cableCarrierSegmentTable->GetDescription (i);
				preferenceReporter.Add ("Description", description == GS::EmptyUniString ? "Empty" : description);
			}

			preferenceReporter.AddNewLine ();
		}
		// ! [CableCarrierSegmentPreferenceTable-Getters-Example]

		preferenceReporter.Write ();
	}


	return NoError;
}


GSErrCode ModifyDuctSegmentPreferences () 	
{
	// ! [PreferenceTableContainer-Modification-Example]
	Reporter preferenceReporter;

	ACAPI::Result<DuctSegmentPreferenceTableContainer> container = GetDuctSegmentPreferenceTableContainer ();

	if (container.IsErr ())
		return container.UnwrapErr ().kind;

	preferenceReporter.Add ("Adding a new table to the duct container...");
	ACAPI::Result<void> modifyResult = container->Modify ([&](PreferenceTableContainerBase::Modifier& modifier) -> GSErrCode {
		ACAPI::Result<UniqueID> tableId = modifier.AddNewTable ("Duct sizes - added from MEP_Test");

		if (tableId.IsErr ())
			return tableId.UnwrapErr ().kind;

		return NoError;
	}, "Modify the content of duct container.");

	if (modifyResult.IsErr ())
		return modifyResult.UnwrapErr ().kind;

	preferenceReporter.Add ("New table successfully added.");
	// ! [PreferenceTableContainer-Modification-Example]

	preferenceReporter.AddNewLine ();

	// ! [DuctSegmentPreferenceTable-Modification-Example]

	std::vector<UniqueID> tableIds = container->GetPreferenceTables ();

	preferenceReporter.Add ("Deleting the first row of every shape of every duct table.");
	for (const UniqueID& uniqueId : tableIds) {
		ACAPI::Result<DuctRectangularSegmentPreferenceTable> rectangularTable = DuctRectangularSegmentPreferenceTable::Get (uniqueId);

		if (rectangularTable.IsErr ())
			continue;

		preferenceReporter.Add ("Trying to delete from", rectangularTable->GetName ());
		rectangularTable->Modify ([&](DuctRectangularSegmentPreferenceTable::Modifier& modifier) {
			preferenceReporter.SetTabCount (1);
			ACAPI::Result<void> rDeleteResult = modifier.Delete (0);

			if (rDeleteResult.IsOk ())
				preferenceReporter.Add ("Deletion of the first element from rectangular sizes was successful.");
			else
				preferenceReporter.Add ("Deletion unsuccessful", GS::UniString (rDeleteResult.UnwrapErr ().text));
		}, "Delete from rectangular preference table.");

		ACAPI::Result<DuctCircularSegmentPreferenceTable> circularTable = DuctCircularSegmentPreferenceTable::Get (uniqueId);

		if (circularTable.IsErr ())
			continue;

		uint32_t firstValidRowIndex = 0;
		for (uint32_t i = 0; i < circularTable->GetSize (); ++i) {
			if (*circularTable->IsRowValid (i)) {
				firstValidRowIndex = i;
				break;
			}
		}

		preferenceReporter.Add ("Trying to empty the first valid row of", circularTable->GetName ());
		circularTable->Modify ([&](DuctCircularSegmentPreferenceTable::Modifier& modifier) {
			preferenceReporter.SetTabCount (1);
			ACAPI::Result<void> cDeleteResult = modifier.EmptyRow (firstValidRowIndex);
			
			if (cDeleteResult.IsOk ())
				preferenceReporter.Add ("Deletion of the first element from circular sizes was successful.");
			else
				preferenceReporter.Add ("Deletion unsuccessful", GS::UniString (cDeleteResult.UnwrapErr ().text));
		}, "Delete from circular preference table.");
		preferenceReporter.SetTabCount (0);
	}

	// ! [DuctSegmentPreferenceTable-Modification-Example]

	preferenceReporter.Write ();

	return NoError;
}


GSErrCode ModifyPipeElbowPreferences () 	
{
	Reporter preferenceReporter;

	ACAPI::Result<PipeElbowPreferenceTableContainer> container = GetPipeElbowPreferenceTableContainer ();

	if (container.IsErr ())
		return container.UnwrapErr ().kind;

	std::optional<UniqueID> newTableId;

	preferenceReporter.Add ("Adding a new table to the pipe elbow container...");
	ACAPI::Result<void> modifyResult = container->Modify ([&] (PreferenceTableContainerBase::Modifier& modifier) -> GSErrCode {
		ACAPI::Result<UniqueID> newTableIdResult = modifier.AddNewTable ("Pipe Elbow Table - added from MEP_Test");

		if (newTableIdResult.IsErr ())
			return newTableIdResult.UnwrapErr ().kind;

		newTableId = *newTableIdResult;

		return NoError;
	}, "Modify the content of pipe elbow container.");

	if (modifyResult.IsErr ())
		return modifyResult.UnwrapErr ().kind;

	preferenceReporter.Add ("New table successfully added.");
	preferenceReporter.AddNewLine ();
	
	// ! [PipeElbowPreferenceTable-Modification-Example]

	ACAPI::Result<PipeElbowPreferenceTable> newTable = PipeElbowPreferenceTable::Get (*newTableId);
	if (newTable.IsErr ())
		return newTable.UnwrapErr ().kind;

	ACAPI::Result<PipeReferenceSet> referenceSet = GetPipeReferenceSet ();
	if (referenceSet.IsErr ())
		return referenceSet.UnwrapErr ().kind;

	newTable->Modify ([&] (PipeElbowPreferenceTable::Modifier& modifier) -> GSErrCode {

		for (uint32_t i = 0; i < referenceSet->GetSize (); ++i) {
			ACAPI::Result<uint32_t> referenceId = referenceSet->GetReferenceId (i);

			double value = static_cast<double> (*referenceId / 1000.0);
			modifier.SetDiameterByReferenceId (*referenceId, value);
			modifier.SetRadiusByReferenceId (*referenceId, value);
		}

		return NoError;

	}, "Fill the new table with values");

	preferenceReporter.Add ("Empty the first row of the new pipe elbow table.");

	newTable->Modify ([&] (PipeElbowPreferenceTable::Modifier& modifier) {
		ACAPI::Result<void> emptyRowResult = modifier.EmptyRow (0);

		if (emptyRowResult.IsOk ())
			preferenceReporter.Add ("Empty row was successful.");
		else
			preferenceReporter.Add ("Empty row unsuccessful", GS::UniString (emptyRowResult.UnwrapErr ().text));

	}, "Empty Row from preference table.");

	preferenceReporter.Add ("Set diameter for row identified by ReferenceId.");

	newTable->Modify ([&] (PipeElbowPreferenceTable::Modifier& modifier) {
		ACAPI::Result<uint32_t> referenceId = newTable->GetReferenceId (1);
		ACAPI::Result<double> currentDiameter = newTable->GetDiameter (1);

		modifier.SetDiameterByReferenceId (*referenceId, *currentDiameter * 2.0);

	}, "Set diameter for row identified by ReferenceId.");

	preferenceReporter.Write ();

	// ! [PipeElbowPreferenceTable-Modification-Example]

	return NoError;
}


GSErrCode ModifyPipeBranchPreferences () 	
{
	Reporter preferenceReporter;

	ACAPI::Result<PipeBranchPreferenceTableContainer> container = GetPipeBranchPreferenceTableContainer ();
	if (container.IsErr ())
		return container.UnwrapErr ().kind;

	std::optional<UniqueID> newTableId;

	preferenceReporter.Add ("Adding a new table to the Pipe Branch Container...");
	ACAPI::Result<void> modifyResult = container->Modify ([&] (PreferenceTableContainerBase::Modifier& modifier) -> GSErrCode {
		ACAPI::Result<UniqueID> newTableIdResult = modifier.AddNewTable ("Pipe Branch Table - added from MEP_Test");

		if (newTableIdResult.IsErr ())
			return newTableIdResult.UnwrapErr ().kind;

		newTableId = *newTableIdResult;

		return NoError;
	}, "Modify the content of Pipe Branch Container.");

	if (modifyResult.IsErr ())
		return modifyResult.UnwrapErr ().kind;

	preferenceReporter.Add ("New table successfully added.");
	preferenceReporter.AddNewLine ();
	
	// ! [PipeBranchPreferenceTable-Modification-Example]

	ACAPI::Result<PipeBranchPreferenceTable> newTable = PipeBranchPreferenceTable::Get (*newTableId);
	if (newTable.IsErr ())
		return newTable.UnwrapErr ().kind;

	ACAPI::Result<PipeReferenceSet> referenceSet = GetPipeReferenceSet ();
	if (referenceSet.IsErr ())
		return referenceSet.UnwrapErr ().kind;

	newTable->Modify ([&] (PipeBranchPreferenceTable::Modifier& modifier) -> GSErrCode {

		for (uint32_t i = 0; i < referenceSet->GetSize (); ++i) {
			ACAPI::Result<uint32_t> referenceId = referenceSet->GetReferenceId (i);
			ACAPI::Result<uint32_t> newRowIndex = modifier.AddNewRow (*referenceId, *referenceId);
			if (newRowIndex.IsErr ())
				continue;

			double value = static_cast<double> (*referenceId / 1000.0);
			modifier.SetMainAxisDiameter (*newRowIndex, value);
			modifier.SetBranchAxisDiameter (*newRowIndex, value);
			modifier.SetLength (*newRowIndex, 3 * value);
			modifier.SetBranchOffset (*newRowIndex, 3 * value / 2);
			modifier.SetConnectionLength (*newRowIndex, 0.005);
		}

		return NoError;

	}, "Fill the new table with values");

	preferenceReporter.Add ("Delete the first row of the new Pipe Branch table.");

	newTable->Modify ([&] (PipeBranchPreferenceTable::Modifier& modifier) {
		ACAPI::Result<void> deleteRowResult = modifier.DeleteRow (0);

		if (deleteRowResult.IsOk ())
			preferenceReporter.Add ("Delete row was successful.");
		else
			preferenceReporter.Add ("Delete row unsuccessful", GS::UniString (deleteRowResult.UnwrapErr ().text));

	}, "Delete Row from preference table.");

	preferenceReporter.Add ("Modify row identified by ReferenceIds.");

	newTable->Modify ([&] (PipeBranchPreferenceTable::Modifier& modifier) {
		ACAPI::Result<uint32_t> mainAxisReferenceId = newTable->GetMainAxisReferenceId (1);
		ACAPI::Result<uint32_t> branchAxisReferenceId = newTable->GetBranchAxisReferenceId (1);

		ACAPI::Result<double> currentMainAxisDiameter = newTable->GetMainAxisDiameterByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId);
		modifier.SetMainAxisDiameterByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId, *currentMainAxisDiameter * 2.0);

		ACAPI::Result<double> currentBranchAxisDiameter = newTable->GetBranchAxisDiameterByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId);
		modifier.SetBranchAxisDiameterByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId, *currentBranchAxisDiameter * 2.0);

		ACAPI::Result<double> currentLength = newTable->GetLengthByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId);
		modifier.SetLengthByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId, *currentLength * 2.0);

		ACAPI::Result<double> currentBranchOffset = newTable->GetBranchOffsetByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId);
		modifier.SetBranchOffsetByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId, *currentBranchOffset * 2.0);

		ACAPI::Result<double> currentConnectionLength = newTable->GetConnectionLengthByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId);
		modifier.SetConnectionLengthByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId, *currentConnectionLength * 2.0);

		modifier.SetDescriptionByReferenceIds (*mainAxisReferenceId, *branchAxisReferenceId, "Doubled!");

	}, "Modify row identified by ReferenceIds.");

	preferenceReporter.Write ();

	// ! [PipeBranchPreferenceTable-Modification-Example]

	return NoError;
}


GSErrCode ModifyDuctTransitionPreferences ()
{ 
	Reporter preferenceReporter;

	ACAPI::Result<DuctTransitionPreferenceTableContainer> container = GetDuctTransitionPreferenceTableContainer ();
	if (container.IsErr ())
		return container.UnwrapErr ().kind;

	std::optional<UniqueID> newTableId;

	preferenceReporter.Add ("Adding a new table to the Duct Transition Container...");
	ACAPI::Result<void> modifyResult = container->Modify ([&] (PreferenceTableContainerBase::Modifier& modifier) -> GSErrCode {
		ACAPI::Result<UniqueID> newTableIdResult = modifier.AddNewTable ("Duct Transition Table - added from MEP_Test");

		if (newTableIdResult.IsErr ())
			return newTableIdResult.UnwrapErr ().kind;

		newTableId = *newTableIdResult;

		return NoError;
	}, "Modify the content of Duct Transition Container.");

	if (modifyResult.IsErr ())
		return modifyResult.UnwrapErr ().kind;

	preferenceReporter.Add ("New table successfully added.");
	preferenceReporter.AddNewLine ();
	
	// ! [DuctTransitionPreferenceTable-Modification-Example]

	ACAPI::Result<DuctTransitionPreferenceTable> newTable = DuctTransitionPreferenceTable::Get (*newTableId);
	if (newTable.IsErr ())
		return newTable.UnwrapErr ().kind;

	ACAPI::Result<DuctReferenceSet> referenceSet = GetDuctReferenceSet ();
	if (referenceSet.IsErr ())
		return referenceSet.UnwrapErr ().kind;

	newTable->Modify ([&] (DuctTransitionPreferenceTable::Modifier& modifier) -> GSErrCode {

		for (uint32_t i = 1; i < referenceSet->GetSize (); ++i) {
			ACAPI::Result<uint32_t> prevReferenceId = referenceSet->GetReferenceId (i - 1);
			ACAPI::Result<uint32_t> referenceId = referenceSet->GetReferenceId (i);
			ACAPI::Result<uint32_t> newRowIndex = modifier.AddNewRow (*referenceId, *prevReferenceId);
			if (newRowIndex.IsErr ())
				continue;

			modifier.SetBeginDiameter (*newRowIndex, static_cast<double> (*referenceId / 1000.0));
			modifier.SetEndDiameter (*newRowIndex, static_cast<double> (*prevReferenceId / 1000.0));
			modifier.SetAngle (*newRowIndex, 60.0);
		}

		return NoError;

	}, "Fill the new table with values");

	preferenceReporter.Add ("Delete the first row of the new Duct Transition table.");

	newTable->Modify ([&] (DuctTransitionPreferenceTable::Modifier& modifier) {
		ACAPI::Result<void> deleteRowResult = modifier.DeleteRow (0);

		if (deleteRowResult.IsOk ())
			preferenceReporter.Add ("Delete row was successful.");
		else
			preferenceReporter.Add ("Delete row unsuccessful", GS::UniString (deleteRowResult.UnwrapErr ().text));

	}, "Delete Row from preference table.");

	preferenceReporter.Add ("Modify row identified by ReferenceIds.");

	newTable->Modify ([&] (DuctTransitionPreferenceTable::Modifier& modifier) {
		ACAPI::Result<uint32_t> beginReferenceId = newTable->GetBeginReferenceId (1);
		ACAPI::Result<uint32_t> endReferenceId = newTable->GetEndReferenceId (1);

		ACAPI::Result<double> currentAngle = newTable->GetAngleByReferenceIds (*beginReferenceId, *endReferenceId);
		modifier.SetAngleByReferenceIds (*beginReferenceId, *endReferenceId, *currentAngle / 2.0);

		modifier.SetDescriptionByReferenceIds (*beginReferenceId, *endReferenceId, "Halved!");

	}, "Modify row identified by ReferenceIds.");

	preferenceReporter.Write ();

	// ! [DuctTransitionPreferenceTable-Modification-Example]

	return NoError;
}


GSErrCode ModifyPipeReferenceSet () 	
{
	// ! [ReferenceSet-Modification-Example]
	Reporter preferenceReporter;

	ACAPI::Result<PipeReferenceSet> referenceSet = GetPipeReferenceSet ();
	if (referenceSet.IsErr ())
		return referenceSet.UnwrapErr ().kind;

	// SetName
	preferenceReporter.Add ("Renaming the Reference Set...");
	referenceSet->Modify ([&] (ACAPI::MEP::ReferenceSetBase::Modifier& modifier) {
		modifier.SetName ("Not DN");
	}, "Modify the name of Reference Set.");

	// Addition
	preferenceReporter.Add ("Adding new ReferenceIds to the Reference Set...");
	referenceSet->Modify ([&] (ReferenceSetBase::Modifier& modifier) {
		const auto& Add = ([&] (UInt32 referenceId)
		{
			preferenceReporter.Add ("Trying to add " + GS::ValueToUniString (referenceId));
			ACAPI::Result<uint32_t> addRes = modifier.Add (referenceId);
			if (addRes.IsOk ())
				preferenceReporter.Add ("ReferenceId successfully added!");
			else
				preferenceReporter.Add (GS::UniString (addRes.UnwrapErr ().text));
		});

		Add (110);
		Add (700);
		Add (1000);
	}, "Add new values to the Reference Set.");

	// Deletion
	const auto ReportDeletion = [&] (const ACAPI::Result<void>& deletionResult) {
		if (deletionResult.IsOk ())
			preferenceReporter.Add ("Deletion was successful.");
		else
			preferenceReporter.Add ("Deletion unsuccessful", GS::UniString (deletionResult.UnwrapErr ().text));
	};

	// Deleting an unused ReferenceId
	preferenceReporter.Add ("Deleting ReferenceId 400, which is unused");
	referenceSet->Modify ([&] (ACAPI::MEP::ReferenceSetBase::Modifier& modifier) {
		ACAPI::Result<void> res = modifier.Delete (400);
		ReportDeletion (res);
	}, "Delete an unused value from the Reference Set.");


	// ! [ReferenceSet-Modification-Example]

	// Deleting a used ReferenceId
	ACAPI::Result<PipeSegmentPreferenceTableContainer> segmentContainer = GetPipeSegmentPreferenceTableContainer ();
	ACAPI::Result<PipeSegmentPreferenceTable> segmentTable = PipeSegmentPreferenceTable::Get (segmentContainer->GetPreferenceTables ().front ());
	uint32_t usedReferenceId;
	for (uint32_t i = 0; i < segmentTable->GetSize (); ++i) {
		if (*segmentTable->IsRowValid (i)) {
			usedReferenceId = *segmentTable->GetReferenceId (i);
			break;
		}
	}

	preferenceReporter.Add ("Deleting a ReferenceId which is used");
	preferenceReporter.Add ("ReferenceId to be deleted", usedReferenceId);
	referenceSet->Modify ([&] (ACAPI::MEP::ReferenceSetBase::Modifier& modifier) {
		ACAPI::Result<void> res = modifier.Delete (usedReferenceId);
		ReportDeletion (res);
	}, "Delete a used value from the Reference Set.");

	// Make the previously found ReferenceId unused, then delete
	preferenceReporter.Add ("Empty every row of the previously found ReferenceId, in order to become unused.");

	for (const UniqueID& uniqueId : segmentContainer->GetPreferenceTables ()) {
		ACAPI::Result<PipeSegmentPreferenceTable> segmentTable = PipeSegmentPreferenceTable::Get (uniqueId);
		segmentTable->Modify ([&] (ACAPI::MEP::PipeSegmentPreferenceTable::Modifier& modifier) {
			modifier.EmptyRowByReferenceId (usedReferenceId);
		}, "Empty row of Segment Table");
	}
	
	ACAPI::Result<PipeElbowPreferenceTableContainer> elbowContainer = GetPipeElbowPreferenceTableContainer ();
	for (const UniqueID& uniqueId : elbowContainer->GetPreferenceTables ()) {
		ACAPI::Result<PipeElbowPreferenceTable> elbowTable = PipeElbowPreferenceTable::Get (uniqueId);
		elbowTable->Modify ([&] (ACAPI::MEP::PipeElbowPreferenceTable::Modifier& modifier) {
			modifier.EmptyRowByReferenceId (usedReferenceId);
		}, "Empty row of Elbow Table");
	}

	ACAPI::Result<PipeBranchPreferenceTableContainer> branchContainer = GetPipeBranchPreferenceTableContainer ();
	for (const UniqueID& uniqueId : branchContainer->GetPreferenceTables ()) {
		ACAPI::Result<PipeBranchPreferenceTable> branchTable = PipeBranchPreferenceTable::Get (uniqueId);
		uint32_t i = 0;
		while (i < branchTable->GetSize ()) {
			ACAPI::Result<uint32_t> mainAxisReferenceId = branchTable->GetMainAxisReferenceId (i);
			ACAPI::Result<uint32_t> branchAxisReferenceId = branchTable->GetBranchAxisReferenceId (i);

			if (*mainAxisReferenceId == usedReferenceId || *branchAxisReferenceId == usedReferenceId) {
				branchTable->Modify ([&] (ACAPI::MEP::PipeBranchPreferenceTable::Modifier& modifier) {
					modifier.DeleteRow (i);
				}, "Delete row of Branch Table");
			} else {
				i++;
			}
		}
	}

	preferenceReporter.Add ("Retry deleting the previous ReferenceId.");

	ACAPI::Result<void> deleteResult3 = referenceSet->Modify ([&] (ACAPI::MEP::ReferenceSetBase::Modifier& modifier) -> GSErrCode {
		ACAPI::Result<void> res = modifier.Delete (usedReferenceId);
		if (res.IsErr ())
			return res.UnwrapErr ().kind;

		return NoError;
	}, "Delete a used value from the Reference Set.");

	ReportDeletion (deleteResult3);

	preferenceReporter.Write ();

	return NoError;
}


} // namespace MEPExample