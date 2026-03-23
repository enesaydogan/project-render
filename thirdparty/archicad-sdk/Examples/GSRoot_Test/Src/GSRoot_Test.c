// *****************************************************************************
// Source code for the GSRoot Test Add-On
// *****************************************************************************

#define	_GSBASE_TEST_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"				// also includes APIdefs.h
#include	"APICommon.h"

#include	"FixArray.hpp"
#include	"Queue.hpp"
#include	"SkipMap.hpp"
#include	"SkipSet.hpp"
#include	"IndexTable.hpp"

// ---------------------------------- Types ------------------------------------


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------


// =============================================================================
//
// Main functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Test the Memory Manager
// -----------------------------------------------------------------------------

static void		MemoryTest (void)
{
	Int32	ret = BMCheckHeap () ;

	WriteReport_Alert ("Heap check returned: %d", ret);

	return;
}		// MemoryTest


// -----------------------------------------------------------------------------
// Test the DateTime Manager
//   - dump the modification date of the layer attribute of the clicked element
// -----------------------------------------------------------------------------

static void		DateTimeTest (void)
{
	API_Element		element = {};
	GSErrCode		err;

	if (!ClickAnElem ("Click an element to get its modification date", API_ZombieElemID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport ("No element was clicked");
		return;
	}

	err = ACAPI_Element_Get (&element);
	if (err == NoError) {
		API_Attribute attribute = {};
		attribute.header.typeID = API_LayerID;
		attribute.header.index  = element.header.layer;
		err = ACAPI_Attribute_Get (&attribute);
		if (err == NoError) {
			WriteReport ("Modification stamp of the layer of this element: %llu", attribute.header.modiTime);
		}
	}

	return;
}		// DateTimeTest


// -----------------------------------------------------------------------------
// Test the Timer Manager
//  - use this manager for optimizing your code
// Launch DebugMonitor, output is written there
// -----------------------------------------------------------------------------

static void		TimerTest (void)
{
	TIReset (0, "Have some fun");
	TIReset (1, "Scan selection");
	TIReset (2, "Create lines");

	TIStart (0);
	
	// Call your function here

	TIStop (0);

	TIPrintTimers ();

	return;
}		// TimerTest

// -----------------------------------------------------------------------------
// Test GS Containers
//   - shows the usage of GS containers
// -----------------------------------------------------------------------------

static void		GSArrayTest (void)
{
	GS::Array<UInt32> gsArray (100);
	gsArray.SetSize (100);
	gsArray.Fill (5);

	GS::Array<UInt32> gsArray2 (gsArray);
	gsArray2.Append (gsArray);

	gsArray.Replace (10, 70);

	DBPrintf ("Size of GSArray: %d\n", gsArray[0]);

	for (auto& var : gsArray) {
		var *= 2;
	}

	for (UInt32 i : InRange (1, 100)) {
		gsArray2.Push (gsArray.GetLastIndex () + gsArray.GetLastIndex () % i);
		gsArray2.Push (gsArray2.GetLastIndex () + gsArray2.GetLastIndex () % i);
	}

	gsArray.EnsureCapacity (100);
	while (!gsArray.IsLast (0)) { // Delete all but the first element
		gsArray.DeleteLast ();
	}

	gsArray.Swap (gsArray2);

	for (auto& var : gsArray.AsConst ()) {
		if (!gsArray2.IsEmpty ())
			gsArray2.Insert (gsArray2.GetLastIndex (), var);
	}

	for (UInt32 i = 0; i < 100; ++i) {
		gsArray2.PushNew (UInt32 (i));
	}

	for (auto& var : gsArray2) {
		if (var == gsArray2.GetFirst () && gsArray2.FindFirst (var) != 0) {
			// Replaces all but the first element which equals to the 1st with their double
			gsArray2.Set (gsArray2.FindFirst (var), var * 2);
		}
	}

	auto predicate = [&](UInt32 other, const GS::Array<UInt32> gsArray) -> bool {
		return other == gsArray.GetFirst ();
	};

	UInt32 last = gsArray2.GetLast ();
	gsArray.FindFirst (predicate(last, gsArray.AsConst ()));

	gsArray.DeleteAll (gsArray.GetFirst());

	gsArray.Count (predicate(2, gsArray.AsConst ()));

}	//ArrayTest

static void		GSArrayFBTest (void)
{
	// ArrayFB behaves like a FixedArray, as long as the # of elements doesn't go over the size of the buffer
	// (no memory and speed overhead)
	GS::ArrayFB<UInt32, /*buffer size: */ 200> gsArray; // allocated on the stack
	gsArray.Fill (5);
	gsArray.SetSize (200);


	GS::ArrayFB<UInt32, 200> gsArray2 (gsArray);
	gsArray2.Append (gsArray);

	GSPtr arrAddr = (GSPtr) &gsArray2[0];
	DBPrintf ("MemarrAddr of ArrayFB on stack: %llx\n", (UInt64)arrAddr);
	gsArray.Fill (10);

	for (auto& var : gsArray) {
		var = var * 2;
	}

	for (UInt32 i : InRange (1, 500)) {
		gsArray2.Push (gsArray2.GetLastIndex () + gsArray2.GetLastIndex () % i);
	}	// from now on gsArray2 behaves like any GS::Array

	arrAddr = (GSPtr) &gsArray2[0];
	DBPrintf ("MemarrAddr of ArrayFB on heap: %llx\n", (UInt64)arrAddr);

	gsArray2.EnsureCapacity (100);

	while (!gsArray.IsLast (0)) { // Delete all but the first element
		gsArray.DeleteLast ();
	}
	// The ArrayFB doesn't go back to stack after deletion
	arrAddr = (GSPtr) &gsArray2[0];
	DBPrintf ("MemarrAddr of ArrayFB on heap after deleting elements: %llx\n", (UInt64)arrAddr);

	gsArray.Swap (gsArray2);

	for (auto& var : gsArray) {
		gsArray2.Insert (gsArray2.GetLastIndex (), var);
	}

	for (UInt32 i = 0; i < 100; ++i) {
		gsArray2.PushNew (UInt32 (i));
	}

	for (auto& var : gsArray2) {
		if (var == gsArray2.GetFirst () && gsArray2.FindFirst (var) != 0) {
			// Replaces all but the first element which equals to the 1st with their double
			gsArray2.Set (gsArray2.FindFirst (var), var * 2);
		}
	}

	auto predicate = [](UInt32 last, const GS::ArrayFB<UInt32, 200>& gsArray) -> bool {
		return last == gsArray.GetFirst ();
	};

	UInt32 last = gsArray2.GetLast ();
	gsArray.FindFirst (predicate (last, gsArray.AsConst ()));

	gsArray.DeleteAll (gsArray.GetFirst ());
}	//GSArrayFBTest

static void		GSFixArrayTest (void)
{
	GS::FixArray<UInt32, 100> gsFixArray_tmp (10);
	GS::FixArray<UInt32, 100> gsFixArray(0);
	gsFixArray.Move (gsFixArray_tmp);

	GS::FixArray<UInt32, 100> gsFixArray2 (0);
	gsFixArray2.Fill (70, 30, 20);

	for (const UInt32 var : gsFixArray2) {
	 	if (var == gsFixArray2.GetFirst () && gsFixArray2.FindFirst (var) != 0) {
	 		// Replaces all but the first element which equals to the 1st with their double
	 		gsFixArray2.Set (gsFixArray2.FindFirst (var), var * 2);
		}
	}
	UInt32 last = gsFixArray2.GetLast ();

	auto predicate = [](UInt32 last, const GS::FixArray<UInt32, 100>& tmpFixArray) -> bool {
		return last == tmpFixArray.GetFirst ();
	};

	gsFixArray.FindFirst (predicate(last, gsFixArray.AsConst ()));

}	// GSFixArrayTest

static void		GSHashSetTest (void)
{
	GS::HashSet<UInt32> hashSet1 (100);

	for (UInt32 i = 0; i < 1000; ++i) {
		hashSet1.Add (i);
	}

	GS::HashSet<UInt32> hashSet2 (100);
	hashSet2.Swap (hashSet1);

	hashSet1 = { 10, 20, 30, 40, 50, 60, 70, 80, 90 };
	hashSet2.Unify (hashSet1);

	GS::HashSet<UInt32> hashSet3 (hashSet1);

	for (UInt32 i = 10; i < 90;  i += 20)
		hashSet3.Delete (i);

	GS::HashSet<UInt32> hashSetUnion = GS::Union (hashSet1, hashSet3);
	DBPrintf ("Size of hashSetUnion: %u\n", hashSetUnion.GetSize ());
} // GSHashMapTest

static void		GSHashTableTest (void)
{
	GS::HashTable<UInt32, UInt32> hashTable1 (100);

	for (UInt32 i = 0; i < 1000; ++i) {
		hashTable1.Add (i, i * 11);
	}

	GS::HashTable<UInt32, UInt32> hashTable2 (100);
	hashTable2.Swap (hashTable1);

	hashTable1 = { {10, 11}, {20, 22}, {30, 33}, {40, 44}, {50, 55}, {60, 66}, {70, 77}, {80, 88}, {90, 99} };

	GS::HashTable<UInt32, bool> hashTable3 (100);

	for (UInt32 i = 10; i < 90; i += 20)
		hashTable3.Delete (i);

	for (auto key : hashTable1.Keys ()) {
		hashTable2.Add (key, hashTable1.Get (key));
	}

	for (UInt32 val : hashTable1.Values ()) {
		hashTable3.Add (val, hashTable2.ContainsValue (val) || hashTable2.ContainsKey (val));
	}

	auto notVals = [](bool hashTableValue) {
		hashTableValue = !hashTableValue;
	};

	hashTable3.EnumerateValues (notVals);

} // GSHashTableTest


static void		GSQueueTest (void)
{
	GS::Queue<UInt32> gsQueue ({1, 2, 3, 4, 5, 6, 7, 8, 42});
	for (UInt32 i = 0; i < 1000; ++i){
		gsQueue.Push (i);
	}

	if (gsQueue.Contains (42)) {
		UInt32 peeked = gsQueue.Peek ();
		gsQueue.Push (peeked);
	}

	for (UInt32 i = 0; i < 500; ++i) {
		gsQueue.Pop();
	}

} // GSQueueTest

static void GSSkipMapTest (void)
{
	// SkipMap is an ordered HashTable-like container
	GS::SkipMap<UInt32, UInt32>
		skipMap1 ({ { 30, 11 },{ 20, 22 },{ 30, 33 },{ 60, 44 },{ 50, 55 },{ 60, 66 },{ 20, 77 },{ 80, 88 },{ 0, 99 } });

	for (UInt32 i = 0; i < 1000; ++i) {
		skipMap1.Add (i, i * 11);	// logn
	}

	GS::SkipMap<UInt32, UInt32> skipMap2 ({ { 0, 0 },{ 10, 2020 },{ 4, 88 },{ 2, 44 },{ 5, 1010 },{ 6, 1212 },{ 1, 22 },{ 80, 88 },{ 0, 99 } });
	skipMap2.Swap (skipMap1);

	GS::SkipMap<UInt32, bool> skipMap3 ({ {0,false} });

	for (UInt32 i = 10; i < 90; i += 20)
		skipMap3.Delete (i);	// logn

	for (auto key : skipMap1.Keys ()) {
		skipMap2.Add (key, skipMap1.Get (key)); // Get() logn + Add() logn
	}

	for (UInt32 val : skipMap1.Values ()) {
		skipMap3.Add (val, skipMap2.ContainsValue (val) || skipMap2.ContainsKey (val));
	}
}	// GSSkipMapTest

static void		GSSkipSetTest (void)
{
	// the SkipSet is an ordered HashSet-like container
	GS::SkipSet<UInt32> skipSet1 ({ 10, 50, 30, 40, 80, 60, 70, 100, 90 });

	for (UInt32 i = 0; i < 1000; ++i) {
		skipSet1.Add (i);
	}

	for (auto& constVal : skipSet1.AsConst ()) {
		// constVal += 10; // can't do this, constVal is const
		UNUSED_VARIABLE(constVal);
	}

	GS::SkipSet<UInt32> skipSet2 ({ 6, 9, 78, 34, 78, 77, 11, 25, 7 });
	skipSet2.Swap (skipSet1);


	GS::SkipSet<UInt32> skipSet3 (skipSet1);

	for (UInt32 i = 10; i < 90; i += 20)
		skipSet3.Delete (i);


} // GSSkipSetTest

static void GSIndexTableTest (void)
{
	GS::UniString a = "aaaa";
	GS::UniString b = "bbbb";
	GS::UniString c = "cccc";
	GS::IndexTable<GS::UniString> indexTable;

	UInt32 aIndex = indexTable.Add (a);
	UInt32 bIndex = indexTable.Add (c);
	UInt32 cIndex = indexTable.Add (a);

	// IndexTable stores the strings only once and refers to them via a UInt32 key
	for (UInt32 i = 0; i < 20; ++i) {
		indexTable.Add (a);
	}

	for (UInt32 i = 0; i < 20; ++i) {
		indexTable.Add (b);
	}

	for (UInt32 i = 0; i < 20; ++i) {
		indexTable.Add (c);
	}

	if (indexTable[aIndex] == a && indexTable[bIndex] == b && indexTable[cIndex] == c){
		indexTable.Clear (); // it will always run
	}


}


static void		ContainerTest (void)
{
	GSArrayTest ();
	GSFixArrayTest ();
	GSHashTableTest ();
	GSHashSetTest ();
	GSQueueTest ();
	GSSkipMapTest ();
	GSSkipSetTest ();
	GSIndexTableTest ();
	GSArrayFBTest ();
}	// GSContainerTest


// -----------------------------------------------------------------------------
// Test DebugManager functions
//   - shows the usage of Debug Manager functions
// Launch DebugMonitor, output is written there
// -----------------------------------------------------------------------------

#if defined (_MSC_VER)
#pragma optimize ("", off)
#endif

static void		Level4 (Int32 what)
{
	switch (what) {
		case 4: {				// Create ACCES_VIOLATION
			char*	p = 0;
			*p = 'X';
			DBPrintf (p);
		}	break;

		case 5: 				// Generate Debug Break (ASSERTION)
			DBBreak (__FILE__, __LINE__, "Test ASSERT from GSRoot_Test.c");
			break;

		case 6:	{				// Generate memory leak
			char*	pChar = new char [256];
			strcpy (pChar, "Leaked memory from GSRoot_Test.c");
		}	break;
	}
}

static void		Level3 (Int32 what)
{
	Level4 (what);
}

static void		Level2 (Int32 what)
{
	Level3 (what);
}

static void		Level1 (Int32 what)
{
	Level2 (what);
}

static void		DebugTest (Int32 what)
{
	GSFlags flags;

	switch (what) {
		case 1: 			// printing formatted text
			DBPrintf ("This is a message is from GSRoot Test %.2f\n", 10.0);
			break;

		case 2: 			// get flags
			flags = DBGetFlags (DBGeneral);

			DBPrintf ("The flags are:\n");
			if ((flags & DBEnableBMTrace) == 0)
				DBPrintf ("DBEnableBMTrace: OFF\n");
			else
				DBPrintf ("DBEnableBMTrace: ON\n");

			if ((flags & DBEnableLogFile) == 0)
				DBPrintf ("DBEnableLogFile: OFF\n");
			else
				DBPrintf ("DBEnableLogFile: ON\n");

			if ((flags & DBEnableTrace) == 0)
				DBPrintf ("DBEnableTrace: OFF\n");
			else
				DBPrintf ("DBEnableTrace: ON\n");

			break;

		case 3:				// invert the BMTrace flag
			flags = DBGetFlags (DBGeneral);
			flags ^= DBEnableBMTrace;
			DBSetFlags (DBGeneral, flags);
			DebugTest (2);
			break;

		case 4:					// Create ACCESS_VIOLATION
		case 5: 				// Generate Debug Break (ASSERTION)
		case 6:					// Generate memory leak
			Level1 (what);
			break;
	}

	return;
}		// DebugTest

#if defined (_MSC_VER)
#pragma optimize ("", on)
#endif

#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Handles menu commands
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case 32500:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:		MemoryTest ();		break;
				case 3:		DateTimeTest ();	break;
				case 4:		TimerTest ();		break;
				case 5:		ContainerTest ();	break;
				// -----
				case 7:		DebugTest (1);		break;  // dump message
				case 8:		DebugTest (2);		break;	// get flags
				case 9:		DebugTest (3);		break;	// toggle flag
				case 10:		DebugTest (4);		break;	// ACCESS_VIOLATION
				case 11:	DebugTest (5);		break;	// DBBreak
				case 12:	DebugTest (6);		break;	// GenerateLeak
			}
			break;
	}

	return NoError;
}		// MenuCommandHandler


// =============================================================================
//
// Required functions
//
// =============================================================================

#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err;

	err = ACAPI_MenuItem_RegisterMenu (32500, 32520, MenuCode_UserDef, MenuFlag_Default);
	if (err != NoError)
		DBPrintf ("DG_Test:: RegisterInterface() ACAPI_MenuItem_RegisterMenu failed\n");

	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
// -----------------------------------------------------------------------------

GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("DG_Test:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

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
