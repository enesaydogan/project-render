#include "GraphCalculationInterface.hpp"
#include "Reporter.hpp"

// API
#include "ACAPinc.h"

// ACAPI
#include "ACAPI/Result.hpp"

// MEPAPI
#include "ACAPI/MEPDistributionSystemsGraph.hpp"
#include "ACAPI/MEPDistributionSystem.hpp"
#include "ACAPI/MEPGraphCalculationInterface.hpp"

// STL
#include <vector>
#include <random>


namespace {


class TestColumn1 : public ACAPI::MEP::TypedCalculationResultColumn<GS::UniString>
{
public:
	// Inherited via TypedCalculationResultColumn
	GS::UniString GetId () const override
	{
		return "TST1";
	}

	GS::UniString GetTitle () const override
	{
		return "Test1";
	}

	std::optional<GS::UniString> GetUnitToDisplay (ACAPI::MEP::MeasurementSystem system) const override
	{
		switch (system)
		{
		case ACAPI::MEP::MeasurementSystem::Metric:
			return "kg";
		case ACAPI::MEP::MeasurementSystem::Imperial:
			return "lb";
		default:
			GS::Unreachable ();
		}
	}

	GS::Int32 GetDefaultWidth () const override
	{
		return 80;
	}

	bool IsLess (const std::any&, const std::any&) const override
	{
		return false;
	}

	bool IsEqual (const std::any&, const std::any&) const override
	{
		return false;
	}

	GS::UniString FormatValueToDisplayText (const GS::UniString& value, ACAPI::MEP::MeasurementSystem) const override
	{
		return value;
	}
};


}


namespace MEPExample {

GSErrCode UseCalculationInterface ()
{
	// ! [CreateCalculationInterface_Example]
	ACAPI::Result<ACAPI::MEP::GraphCalculationInterface> calculationInterface = ACAPI::MEP::CreateCalculationInterface ();
	if (calculationInterface.IsErr ())
		return calculationInterface.UnwrapErr ().kind;
	// ! [CreateCalculationInterface_Example]

	
	Reporter reporter;

	// ! [RegisterDoCalculationCallback_Example]
	const auto calculationCallback = [](const GS::Guid& root, const ACAPI::MEP::DistributionSystemsGraph& graph) {
		Reporter reporter;
		reporter.Add (GS::UniString::Printf ("Calculation callback was called on root element %T!", root.ToUniString ().ToPrintf ()));
		reporter.AddNewLine ();
		reporter.Add (GS::UniString::Printf ("In a graph with %zu systems!", graph.GetSystems ().size ()));
		reporter.AddNewLine ();
		reporter.Write ();
	};

	TestColumn1 testCol1;
	std::vector<const ACAPI::MEP::ICalculationResultColumn*> columnsToShowInBrowser;
	columnsToShowInBrowser.emplace_back (&testCol1);

	ACAPI::Result<std::shared_ptr<void>> registrationResult = calculationInterface->RegisterDoCalculationCallback (calculationCallback, columnsToShowInBrowser);
	if (registrationResult.IsErr ()) {
		reporter.Add (GS::UniString (registrationResult.UnwrapErr ().text));
		return Error;
	}
	std::shared_ptr<void> registeredColumnsScope = registrationResult.Unwrap ();
	// ! [RegisterDoCalculationCallback_Example]

	reporter.Add ("DoCalculationCallback registered");
	reporter.AddNewLine ();

	std::vector<ACAPI::MEP::UniqueID> graphElements;
	const auto newGraph = ACAPI::MEP::CreateDistributionSystemsGraph ();
	if (newGraph.IsOk ())
		graphElements = newGraph->GetElements ();

	// ! [InvokeCalculationResults_Example]
	std::vector<std::tuple<GS::Guid, GS::UniString, std::any>> changedElements;

	// Generate some random values to display in the system browser:
	auto genRandomNum = [gen = std::default_random_engine {}, dist = std::uniform_real_distribution<> { 0.1, 100.0 }] () mutable
		{
			return dist (gen);
		};

	for (const ACAPI::MEP::UniqueID& element : graphElements) {
		changedElements.emplace_back (element.GetGuid (), testCol1.GetId (), genRandomNum ());
	}

	calculationInterface->InvokeCalculationResults (changedElements);
	// ! [InvokeCalculationResults_Example]

	reporter.Add ("InvokeCalculationResults called");
	reporter.AddNewLine ();

	reporter.Write ();
	return NoError;
}


} // namespace MEPExample
