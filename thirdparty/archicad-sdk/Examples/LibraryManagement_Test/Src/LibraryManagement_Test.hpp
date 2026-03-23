
#include "APIEnvir.h"
#include "ACAPinc.h"


class AddLibraryCommand : public API_AddOnCommand
{
public:

	virtual GS::String							GetName () const override;
	virtual GS::String							GetNamespace () const override;
	virtual GS::Optional<GS::UniString>			GetSchemaDefinitions () const override;
	virtual GS::Optional<GS::UniString>			GetInputParametersSchema () const override;
	virtual GS::Optional<GS::UniString>			GetResponseSchema () const override;

	virtual API_AddOnCommandExecutionPolicy		GetExecutionPolicy () const override { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
	virtual bool								IsProcessWindowVisible () const override { return true; }

	virtual GS::ObjectState						Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;

	virtual void								OnResponseValidationFailed (const GS::ObjectState& response) const override;
};


class AddLibrariesCommand : public API_AddOnCommand
{
public:

	virtual GS::String							GetName () const override;
	virtual GS::String							GetNamespace () const override;
	virtual GS::Optional<GS::UniString>			GetSchemaDefinitions () const override;
	virtual GS::Optional<GS::UniString>			GetInputParametersSchema () const override;
	virtual GS::Optional<GS::UniString>			GetResponseSchema () const override;

	virtual API_AddOnCommandExecutionPolicy		GetExecutionPolicy () const override { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
	virtual bool								IsProcessWindowVisible () const override { return true; }

	virtual GS::ObjectState						Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;

	virtual void								OnResponseValidationFailed (const GS::ObjectState& response) const override;
};


class AddServerLibraryCommand : public API_AddOnCommand
{
public:

	virtual GS::String							GetName () const override;
	virtual GS::String							GetNamespace () const override;
	virtual GS::Optional<GS::UniString>			GetSchemaDefinitions () const override;
	virtual GS::Optional<GS::UniString>			GetInputParametersSchema () const override;
	virtual GS::Optional<GS::UniString>			GetResponseSchema () const override;

	virtual API_AddOnCommandExecutionPolicy		GetExecutionPolicy () const override { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
	virtual bool								IsProcessWindowVisible () const override { return true; }

	virtual GS::ObjectState						Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;

	virtual void								OnResponseValidationFailed (const GS::ObjectState& response) const override;
};


class RemoveAllLibrariesCommand : public API_AddOnCommand
{
public:

	virtual GS::String							GetName () const override;
	virtual GS::String							GetNamespace () const override;
	virtual GS::Optional<GS::UniString>			GetSchemaDefinitions () const override;
	virtual GS::Optional<GS::UniString>			GetInputParametersSchema () const override;
	virtual GS::Optional<GS::UniString>			GetResponseSchema () const override;

	virtual API_AddOnCommandExecutionPolicy		GetExecutionPolicy () const override { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
	virtual bool								IsProcessWindowVisible () const override { return true; }

	virtual GS::ObjectState						Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;

	virtual void								OnResponseValidationFailed (const GS::ObjectState& response) const override;
};


class DumpLibraryListCommand : public API_AddOnCommand
{
public:

	virtual GS::String							GetName () const override;
	virtual GS::String							GetNamespace () const override;
	virtual GS::Optional<GS::UniString>			GetSchemaDefinitions () const override;
	virtual GS::Optional<GS::UniString>			GetInputParametersSchema () const override;
	virtual GS::Optional<GS::UniString>			GetResponseSchema () const override;

	virtual API_AddOnCommandExecutionPolicy		GetExecutionPolicy () const override { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
	virtual bool								IsProcessWindowVisible () const override { return true; }

	virtual GS::ObjectState						Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;

	virtual void								OnResponseValidationFailed (const GS::ObjectState& response) const override;
};


class DumpAllLibPartsCommand : public API_AddOnCommand
{
public:

	virtual GS::String							GetName () const override;
	virtual GS::String							GetNamespace () const override;
	virtual GS::Optional<GS::UniString>			GetSchemaDefinitions () const override;
	virtual GS::Optional<GS::UniString>			GetInputParametersSchema () const override;
	virtual GS::Optional<GS::UniString>			GetResponseSchema () const override;

	virtual API_AddOnCommandExecutionPolicy		GetExecutionPolicy () const override { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
	virtual bool								IsProcessWindowVisible () const override { return true; }

	virtual GS::ObjectState						Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;

	virtual void								OnResponseValidationFailed (const GS::ObjectState& response) const override;
};


class DuplicateSelectionToEmbeddedCommand : public API_AddOnCommand
{
public:

	virtual GS::String							GetName () const override;
	virtual GS::String							GetNamespace () const override;
	virtual GS::Optional<GS::UniString>			GetSchemaDefinitions () const override;
	virtual GS::Optional<GS::UniString>			GetInputParametersSchema () const override;
	virtual GS::Optional<GS::UniString>			GetResponseSchema () const override;

	virtual API_AddOnCommandExecutionPolicy		GetExecutionPolicy () const override { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
	virtual bool								IsProcessWindowVisible () const override { return true; }

	virtual GS::ObjectState						Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;

	virtual void								OnResponseValidationFailed (const GS::ObjectState& response) const override;
};


class CreateNewObjectFromSelectionCommand : public API_AddOnCommand
{
public:

	virtual GS::String							GetName () const override;
	virtual GS::String							GetNamespace () const override;
	virtual GS::Optional<GS::UniString>			GetSchemaDefinitions () const override;
	virtual GS::Optional<GS::UniString>			GetInputParametersSchema () const override;
	virtual GS::Optional<GS::UniString>			GetResponseSchema () const override;

	virtual API_AddOnCommandExecutionPolicy		GetExecutionPolicy () const override { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
	virtual bool								IsProcessWindowVisible () const override { return true; }

	virtual GS::ObjectState						Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;

	virtual void								OnResponseValidationFailed (const GS::ObjectState& response) const override;
};


class UploadLibrariesToServerCommand : public API_AddOnCommand
{
public:

	virtual GS::String							GetName () const override;
	virtual GS::String							GetNamespace () const override;
	virtual GS::Optional<GS::UniString>			GetSchemaDefinitions () const override;
	virtual GS::Optional<GS::UniString>			GetInputParametersSchema () const override;
	virtual GS::Optional<GS::UniString>			GetResponseSchema () const override;

	virtual API_AddOnCommandExecutionPolicy		GetExecutionPolicy () const override { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
	virtual bool								IsProcessWindowVisible () const override { return true; }

	virtual GS::ObjectState						Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;

	virtual void								OnResponseValidationFailed (const GS::ObjectState& response) const override;
};

class RemoveLibrariesFromServerCommand : public API_AddOnCommand {
public:
	virtual GS::String					GetName () const override;
	virtual GS::String					GetNamespace () const override;
	virtual GS::Optional<GS::UniString> GetSchemaDefinitions () const override;
	virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
	virtual GS::Optional<GS::UniString> GetResponseSchema () const override;

	virtual API_AddOnCommandExecutionPolicy GetExecutionPolicy () const override
	{
		return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread;
	}

	virtual bool IsProcessWindowVisible () const override { return true; }

	virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;

	virtual void OnResponseValidationFailed (const GS::ObjectState& response) const override;
};