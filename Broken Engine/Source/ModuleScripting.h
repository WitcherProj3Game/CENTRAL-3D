#ifndef __MODULESCRIPTING_H__
#define __MODULESCRIPTING_H__

#include "Module.h"
#include <vector>

class lua_State;
BE_BEGIN_NAMESPACE

class GameObject;
class ComponentScript;
struct ScriptInstance;
struct ScriptFunc;

enum _AppState;
class BROKEN_API ModuleScripting : public Module {
	friend class ScriptingElements;
	friend class AutoCompleteFileGen;
public:
	ModuleScripting(bool start_enabled = true);
	~ModuleScripting();

public:

	bool DoHotReloading();
	bool JustCompile(std::string absolute_path);
	void CompileScriptTableClass(ScriptInstance* script, bool hotReload = false);
	void SendScriptToModule(ComponentScript* script_component);

	void FillScriptInstanceComponentVars(ScriptInstance* script, bool hotReload = false);
	void FillScriptInstanceComponentFuncs(ScriptInstance* script);

	void DeleteScriptInstanceWithParentComponent(ComponentScript* script_component);
	void NullifyScriptInstanceWithParentComponent(ComponentScript* script_component);
	void NotifyHotReloading();
	bool CheckEverythingCompiles();
	void CallbackScriptFunction(ComponentScript* script_component, const ScriptFunc& function_to_call);
	void CompileDebugging();
	void StopDebugging();
	void CallbackScriptFunctionParam(ComponentScript* script_component, const ScriptFunc& function_to_call, uint id);
	void DeployScriptingGlobals();

	//The purpose of this function is to initialize the scripting vars of an instantiated gameObject on creation
	void EmplaceEditorValues(ScriptInstance* script);
	
	void CleanUpInstances();

	bool Init(json& file) override;
	bool Start();
	bool CleanUp();

	update_status Update(float dt) override;
	update_status GameUpdate(float gameDT);

	bool Stop() override;
	ScriptInstance* GetScriptInstanceFromComponent(ComponentScript* component_script);

	//Load info from the settings json
	void LoadStatus(const json& file) override;

public:
	ScriptInstance* current_script;
	update_status scripting_update = UPDATE_CONTINUE;
	std::string debug_path = "null";

	bool Debug_Build = false;
private:
	// L is our Lua Virtual Machine, it's called L because its the common name it receives, so all programers can understand what this var is
	lua_State* L = nullptr;
	bool start = true;
	bool cannot_start = false; //We cannot start if a compilation error would cause a crash on the engine when we start playing
	bool hot_reloading_waiting = false;

	_AppState previous_AppState = (_AppState)2; // we use the EDITOR value of the script (can't include application.h because it would slow down compilation time)

	ScriptInstance* debug_instance = nullptr;

	std::vector<ScriptInstance*> recompiled_instances;
	std::vector<ScriptInstance*> class_instances;
};
BE_END_NAMESPACE
#endif // !__MODULESCRIPTING_H__
