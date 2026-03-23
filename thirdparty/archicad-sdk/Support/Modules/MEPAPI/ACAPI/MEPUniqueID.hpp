#ifndef GS_ACAPI_MEP_UNIQUEID_HPP
#define GS_ACAPI_MEP_UNIQUEID_HPP

#include "MEPAPIExport.hpp"

#include "ACAPI/UniqueID.hpp"

#ifdef MEPAPI_USE_V1
/**
 * @brief By defining MEPAPI_USE_V1 in your AddOn you will keep v1 as the default version in case of the classes that got a v2 implementation.
 * @ingroup MEP
 */
	#define MEPAPI_VERSION v1
#else
/**
 * @brief By defining MEPAPI_USE_V1 in your AddOn you will keep v1 as the default version in case of the classes that got a v2 implementation.
 * @ingroup MEP
 */
	#define MEPAPI_VERSION v2
#endif

namespace ACAPI::MEP {


/**
 * @brief The MEP unique identifier.
 * @ingroup MEP
 * @remarks This class is used to identify MEP elements.
 */
class MEPAPI_EXPORT UniqueID : public ACAPI::UniqueID {
public:
	/**
	 * @brief Constructs a UniqueID.
	 * @ingroup MEP
	 */
	UniqueID (const GS::Guid&, const API_Token&);
};

} // ACAPI::MEP


namespace std {

/// @private
template <>
struct hash<ACAPI::MEP::UniqueID> {
	std::size_t operator()(const ACAPI::MEP::UniqueID& uniqueId) const
	{
		return uniqueId.GetGuid ().GetHashValue () ^ uniqueId.GetToken ().GenerateHashValue ();
	}
};

}

#endif // GS_ACAPI_MEP_UNIQUEID_HPP