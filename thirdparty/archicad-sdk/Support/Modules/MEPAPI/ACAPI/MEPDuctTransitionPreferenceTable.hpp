#ifndef GS_ACAPI_MEP_DUCT_TRANSITION_PREFERENCE_TABLE_HPP
#define GS_ACAPI_MEP_DUCT_TRANSITION_PREFERENCE_TABLE_HPP

#include "MEPAPIExport.hpp"

#include "MEPTypes.hpp"

#include "ACAPI/Object.hpp"
#include "ACAPI/Result.hpp"
#include "ACAPI/ModifyScope.hpp"


namespace ACAPI::MEP {


namespace v1 {

/**
* @brief The DuctTransitionPreferenceTable contains cross section and geometry related information for Duct transitions.
* @ingroup MEP
* @since Archicad 29
* @details The table uses the keys defined by the @ref DuctReferenceSet.
*		To consider a table row usable and valid, it should have all its fields filled, beside its description, which is optional.
* @par Example code snippet from Test / New MEP
 *   	Getters @snippet Preferences.cpp DuctTransitionPreferenceTable-Getters-Example
 *   	Modification @snippet Preferences.cpp DuctTransitionPreferenceTable-Modification-Example
*/
class MEPAPI_EXPORT DuctTransitionPreferenceTable final : public Object {
	DuctTransitionPreferenceTable (const UniqueID&);

public:
	class Modifier;
	~DuctTransitionPreferenceTable ();

	/**
	 * @brief Returns a DuctTransitionPreferenceTable.
	 * @param uniqueId The UniqueID of the requested DuctTransitionPreferenceTable.
	 * @return If the uniqueId belongs to a DuctTransitionPreferenceTable, returns the object associated to it, otherwise Error.
	 */
	static Result<DuctTransitionPreferenceTable> Get (const UniqueID& uniqueId);

	/** @return Checks if the object still exists. */
	bool Exists () const;

	/**
	 * @brief Checks whether the table is editable.
	 * @return Returns nothing if the table is editable, otherwise the error code and error message containing information about the reason.
	 */
	Result<void> IsEditable () const;

	/** @return Returns the name of the table. */
	GS::UniString GetName () const;

	/** @return Returns the size of table. */
	uint32_t GetSize () const;

	/**
	* @param index The index of the row. Index starts from 0.
	* @return Returns error if the index is out of range, otherwise returns the begin referenceId of the table's row.
	*/
	Result<uint32_t> GetBeginReferenceId (uint32_t index) const;

	/**
	* @param index The index of the row. Index starts from 0.
	* @return Returns error if the index is out of range, otherwise returns the end referenceId of the table's row.
	*/
	Result<uint32_t> GetEndReferenceId (uint32_t index) const;

	/**
	 * @brief A row is considered valid if both diameters and the transition angle contains non-zero values, description is optional.
	 * @param index The index of the row. Index starts from 0.
	 * @return Returns error if the index is out of range, otherwise returns the validity state of the row.
	 */
	Result<bool> IsRowValid (uint32_t index) const;

	/**
	 * @brief A row is considered valid if both diameters and the transition angle contains non-zero values, description is optional.
	 * @param beginReferenceId The begin referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @return Returns error if the pair of referenceIds does not occur in the table, otherwise returns the validity state of the row.
	 */
	Result<bool> IsRowValidByReferenceIds (uint32_t beginReferenceId, uint32_t endReferenceId) const;

	/**
	 * @param index The index of the row. Index starts from 0.
	 * @return Returns error if the index is out of range, otherwise returns the begin diameter value of the table's row.
	 * If the returned value is 0.0, the row is not valid yet.
	 */
	Result<double> GetBeginDiameter (uint32_t index) const;

	/**
	 * @param beginReferenceId The begin referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @return Returns error if the pair of referenceIds does not occur in the table, otherwise returns the begin diameter value of the table's row.
	 * If the returned value is 0.0, the row is not valid yet.
	 */
	Result<double> GetBeginDiameterByReferenceIds (uint32_t beginReferenceId, uint32_t endReferenceId) const;

	/**
	 * @param index The index of the row. Index starts from 0.
	 * @return Returns error if the index is out of range, otherwise returns the end diameter value of the table's row.
	 * If the returned value is 0.0, the row is not valid yet.
	 */
	Result<double> GetEndDiameter (uint32_t index) const;

	/**
	 * @param beginReferenceId The begin referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @return Returns error if the pair of referenceIds does not occur in the table, otherwise returns the end diameter value of the table's row.
	 * If the returned value is 0.0, the row is not valid yet.
	 */
	Result<double> GetEndDiameterByReferenceIds (uint32_t beginReferenceId, uint32_t endReferenceId) const;

	/**
	 * @param index The index of the row. Index starts from 0.
	 * @return Returns error if the index is out of range, otherwise returns the angle value of the table's row.
	 * If the returned value is 0.0, the row is not valid yet.
	 */
	Result<double> GetAngle (uint32_t index) const;

	/**
	 * @param beginReferenceId The begin referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @return Returns error if the pair of referenceIds does not occur in the table, otherwise returns the angle value of the table's row.
	 * If the returned value is 0.0, the row is not valid yet.
	 */
	Result<double> GetAngleByReferenceIds (uint32_t beginReferenceId, uint32_t endReferenceId) const;

	/**
	 * @param index The index of the row. Index starts from 0.
	 * @return Returns error if the index is out of range, otherwise returns the description of the table's row.
	 */
	Result<GS::UniString> GetDescription (uint32_t index) const;

	/**
	 * @param beginReferenceId The begin referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @return Returns error if the pair of referenceIds does not occur in the table, otherwise returns the description of the table's row.
	 */
	Result<GS::UniString> GetDescriptionByReferenceIds (uint32_t beginReferenceId, uint32_t endReferenceId) const;

	/**
	 * @brief Executes the passed modifier scope in an Undoable command.
	 * @param modifierFunc The modifier scope (lambda, std::function or function pointer) the caller wants to execute.
	 * @param undoString The text which appears in the undo queue of Archicad after executing the command.
	 * @return Returns an error if the process of modification fails for some reason, otherwise nothing.
	 */
	template <typename ModifierFunc>
	Result<void> Modify (const ModifierFunc& modifierFunc, const GS::UniString& undoString);
};


class MEPAPI_EXPORT DuctTransitionPreferenceTable::Modifier : public ACAPI::ModifierBase {
	DuctTransitionPreferenceTable& parent;

protected:
	/**
	 * @brief Constructs a Modifier with the given DuctTransitionPreferenceTable.
	 * @param parent The given DuctTransitionPreferenceTable.
	 */
	Modifier (DuctTransitionPreferenceTable& parent);

public:
	~Modifier ();

	friend DuctTransitionPreferenceTable;

	/**
	* @brief Sets the name of the table.
	* @param tableName The name to be set.
	*/
	void SetName (const GS::UniString& tableName);

	/**
	 * @brief Inserts a new row into the table.
	 * @param beginReferenceId The begin referenceId of the new row. It must be a referenceId that is defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the new row. It must be a referenceId that is defined in the DuctReferenceSet.
	 * @details If endReferenceId is larger than the beginReferenceId, the two values will be swapped before insertion.
	 * @return Returns error if the pair of referenceIds is already present in the table, otherwise the index of the new row.
	 */
	Result<uint32_t> AddNewRow (uint32_t beginReferenceId, uint32_t endReferenceId);

	/**
	 * @brief Deletes a row from the table.
	 * @param index The index of the row. Index starts from 0.
	 * @return Returns error if the index is out of range, otherwise nothing.
	 */
	Result<void> DeleteRow (uint32_t index);

	/**
	 * @brief Deletes a row from the table.
	 * @param beginReferenceId The begin referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @return Returns error if the pair of referenceIds does not occur in the table, otherwise nothing.
	 */
	Result<void> DeleteRowByReferenceIds (uint32_t beginReferenceId, uint32_t endReferenceId);

	/**
	 * @brief Sets the begin diameter value of a table's row.
	 * @param index The index of the row. Index starts from 0.
	 * @param diameter The value to be set. Must be a non-zero positive number.
	 * @return Returns error if the index is out of range, otherwise nothing.
	 */
	Result<void> SetBeginDiameter (uint32_t index, double diameter);

	/**
	 * @brief Sets the begin diameter value of a table's row.
	 * @param beginReferenceId The begin referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param diameter The value to be set. Must be a non-zero positive number.
	 * @details If there isn't any row with the given pair of referenceIds, but both the referenceIds are defined in the DuctReferenceSet, it will insert the row.
	 * @return Returns error if at least one referenceId is not in the DuctReferenceSet, otherwise nothing.
	 */
	Result<void> SetBeginDiameterByReferenceIds (uint32_t beginReferenceId, uint32_t endReferenceId, double diameter);

	/**
	 * @brief Sets the end diameter value of a table's row.
	 * @param index The index of the row. Index starts from 0.
	 * @param diameter The value to be set. Must be a non-zero positive number.
	 * @return Returns error if the index is out of range, otherwise nothing.
	 */
	Result<void> SetEndDiameter (uint32_t index, double diameter);

	/**
	 * @brief Sets the end diameter value of a table's row.
	 * @param beginReferenceId The begin referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param diameter The value to be set. Must be a non-zero positive number.
	 * @details If there isn't any row with the given pair of referenceIds, but both the referenceIds are defined in the DuctReferenceSet, it will insert the row.
	 * @return Returns error if at least one referenceId is not in the DuctReferenceSet, otherwise nothing.
	 */
	Result<void> SetEndDiameterByReferenceIds (uint32_t beginReferenceId, uint32_t endReferenceId, double diameter);

	/**
	 * @brief Sets the angle value of a table's row.
	 * @param index The index of the row. Index starts from 0.
	 * @param angle The value to be set. Needs to be between [1, 89] degrees.
	 * @return Returns an error if the parameter is not in range, otherwise nothing. 
	 */
	Result<void> SetAngle (uint32_t index, double angle);

	/**
	 * @brief Sets the angle value of a table's row.
	 * @param beginReferenceId The begin referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param angle The value to be set. Needs to be between [1, 89] degrees.
	 * @details If there isn't any row with the given pair of referenceIds, but both the referenceIds are defined in the DuctReferenceSet, it will insert the row.
	 * @return Returns an error if the parameter is not in range, otherwise nothing. 
	 */
	Result<void> SetAngleByReferenceIds (uint32_t beginReferenceId, uint32_t endReferenceId, double angle);

	/**
	 * @brief Sets the description of a table's row.
	 * @param index The index of the row. Index starts from 0.
	 * @param description The description to be set.
	 * @return Returns error if the index is out of range, otherwise nothing.
	 */
	Result<void> SetDescription (uint32_t index, const GS::UniString& description);

	/**
	 * @brief Sets the description of a table's row.
	 * @param beginReferenceId The begin referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param endReferenceId The end referenceId of the row. The table only contains the referenceIds that are defined in the DuctReferenceSet.
	 * @param description The description to be set.
	 * @details If there isn't any row with the given pair of referenceIds, but both the referenceIds are defined in the DuctReferenceSet, it will insert the row.
	 * @return Returns error if at least one referenceId is not in the DuctReferenceSet, otherwise nothing.
	 */
	Result<void> SetDescriptionByReferenceIds (uint32_t beginReferenceId, uint32_t endReferenceId, const GS::UniString& description);
};


template <typename ModifierFunc>
Result<void> DuctTransitionPreferenceTable::Modify (const ModifierFunc& modifierFunc, const GS::UniString& undoString)
{
	Result<void> editableResult = IsEditable ();
	if (editableResult.IsErr ())
		return editableResult;

	Modifier modifier (*this);
	return ExecuteUndoableScope (modifierFunc, modifier, *this, undoString);
}


} // v1

using v1::DuctTransitionPreferenceTable;

} // ACAPI::MEP

#endif // GS_ACAPI_MEP_DUCT_TRANSITION_PREFERENCE_TABLE_HPP