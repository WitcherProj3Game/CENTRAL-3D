#include "ScriptingScenes.h"
#include "Application.h"
#include "ModuleScripting.h"
#include "ModuleSceneManager.h"
#include "ModuleResourceManager.h"
#include "ModuleAudio.h"
#include "GameObject.h"
#include "ImporterModel.h"
#include "ScriptData.h"
#include "ResourceScene.h"
#include "ResourcePrefab.h"
#include "ResourceModel.h"
#include "ComponentTransform.h"
#include "ModuleTimeManager.h"
#include "ModulePhysics.h"

using namespace Broken;
ScriptingScenes::ScriptingScenes() {}

ScriptingScenes::~ScriptingScenes() {}

void ScriptingScenes::LoadSceneFromScript(uint scene_UUID)
{
	ResourceScene* scene = (ResourceScene*)App->resources->GetResource(scene_UUID, false);
	App->time->Gametime_clock.Stop();
	App->scene_manager->SetActiveScene(scene);
	App->physics->physAccumulatedTime = 0.0f;//Reset Physics
	App->audio->StopAllAudioEvents(); //This will stop all events as a super quick implementation, but if we want something to be continuous between scenes then we will need to develop a small system in the module
	App->time->Gametime_clock.Start();
}

void ScriptingScenes::QuitGame()
{
	if (App->isGame)
		App->scripting->scripting_update = UPDATE_STOP;
	else 
	{
		App->GetAppState() = Broken::AppState::TO_EDITOR;
	}
}

uint ScriptingScenes::Instantiate(uint resource_UUID, float x, float y, float z, float alpha, float beta, float gamma)
{
	uint ret = 0;
	Resource* prefab = App->resources->GetResource(resource_UUID);

	if (prefab) {
		GameObject* go = App->resources->GetImporter<ImporterModel>()->InstanceOnCurrentScene(prefab->GetResourceFile(), nullptr);

		if (go) {
			ComponentTransform* transform = go->GetComponent<ComponentTransform>();

			if (transform) {
				go->GetComponent<ComponentTransform>()->SetPosition(x, y, z);
				go->GetComponent<ComponentTransform>()->SetRotation({ alpha, beta, gamma });
				go->GetComponent<ComponentTransform>()->updateValues = true;
				go->TransformGlobal();
			}
			else
				ENGINE_CONSOLE_LOG("![Script]: (Instantiate) Prefab has no transform");


			//Override the variables of the script with editor values
			ComponentScript* component_script = go->GetComponent<ComponentScript>();
			if (component_script) {
				ScriptInstance* script_inst = App->scripting->GetScriptInstanceFromComponent(component_script);

				if (script_inst)
					App->scripting->EmplaceEditorValues(script_inst);
			}

			ret = go->GetUID();
		}
		else
			ENGINE_CONSOLE_LOG("![Script]: (Instantiate) Failed to instantiate prefab");

	}
	else
		ENGINE_CONSOLE_LOG("![Script]: (Instantiate) No prefab with UID %d", resource_UUID);

	return ret;
}